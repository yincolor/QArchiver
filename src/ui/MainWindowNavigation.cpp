// QArchiver / 小Q解压
// MainWindow navigation: directory history, location bar, selection details,
// search and the folder pane.
// MainWindow 导航：目录历史、位置栏、选中详情、搜索与文件夹面板。

#include "ui/MainWindow.h"
#include "ui/MainWindow_p.h"

#include "ui/MainWindowHelpers.h"
#include "ui/icons/IconUtils.h"

#include "ui/models/FileListModel.h"
#include "ui/models/FilterProxyModel.h"
#include "ui/models/FileListViewDelegate.h"
#include "ui/services/PreviewManager.h"
#include "ui/services/PreviewUtils.h"
#include "ui/services/DragOutManager.h"
#include "ui/widgets/ArchiveTreeView.h"
#include "ui/widgets/ArchiveGridView.h"
#include "ui/widgets/BackgroundWidget.h"
#include "ui/widgets/SidebarWidget.h"
#include "ui/widgets/ModernToolBar.h"
#include "ui/widgets/ModernNavBar.h"
#include "ui/widgets/BreadcrumbPathBar.h"
#include "ui/widgets/ArchiveFolderPane.h"
#include "ui/widgets/WelcomePage.h"
#include "ui/dialogs/ExtractDialog.h"
#include "ui/dialogs/CompressDialog.h"
#include "ui/dialogs/PasswordDialog.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/PreferencesDialog.h"
#include "ui/dialogs/BenchmarkDialog.h"
#include "utils/Logger.h"
#include "utils/MimeTypeHelper.h"
#include "utils/SevenZipTool.h"
#include "ui/icons/MessageBoxUtils.h"
#include "utils/FileUtils.h"
#include "ui/icons/FileTypeHelper.h"
#include "ui/theme/ThemeManager.h"

#include "core/ArchiveManager.h"
#include "common/ArchiveError.h"
#include "common/ArchiveOptions.h"
#include "common/ArchiveConstants.h"
#include "core/AppSettings.h"

#include "backends/ArchiveBackend.h"

#include <QProcess>


QString MainWindow::contentViewMode() const
{
    // The content-view preference applies to both layouts. Classic has no
    // toolbar switcher, but the same grid/list/tree choice is available from
    // Preferences and must take effect there as well.
    // 内容视图设置对两种布局都生效。经典布局没有工具栏切换按钮，但偏好设置
    // 中仍可选择网格/列表/树，并且必须实际生效。
    const QString mode = AppSettings::instance().contentViewMode();
    if (mode == QStringLiteral("list") || mode == QStringLiteral("tree")) {
        return mode;
    }
    return QStringLiteral("grid");
}

QAbstractItemView *MainWindow::currentContentView() const
{
    if (contentViewMode() == QStringLiteral("grid") && m_gridView) {
        return m_gridView;
    }
    return m_treeView;
}

QList<ArchiveEntry> MainWindow::selectedEntries() const
{
    QList<ArchiveEntry> entries;
    QAbstractItemView *view = currentContentView();
    if (!view || !view->selectionModel() || !m_model || !m_proxyModel) {
        return entries;
    }
    const QModelIndexList rows = view->selectionModel()->selectedRows();
    for (const QModelIndex &proxyIndex : rows) {
        if (proxyIndex.data(FileListModel::IsParentRole).toBool()) {
            continue;
        }
        const QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
        if (const ArchiveEntry *entry = m_model->entryAt(sourceIndex)) {
            entries.append(*entry);
        }
    }
    return entries;
}

void MainWindow::updateSelectionDetails()
{
    if (m_localBrowse) {
        // Local browsing hides the archive details pane and has no
        // archive-internal directory to track; the system-places sidebar is
        // maintained by loadLocalDirectory()/updateLocalModeUi().
        // 本地浏览隐藏压缩包详情面板，也没有压缩包内部目录需要跟踪；系统位置
        // 侧栏由 loadLocalDirectory()/updateLocalModeUi() 维护。
        return;
    }

    const QList<ArchiveEntry> entries = selectedEntries();
    if (m_folderPane) {
        m_folderPane->setSelection(entries);
    }

    if (!m_model || !m_model->isFlatMode()) {
        QString dir = m_navCurrentDir;
        if (entries.size() == 1) {
            QString path = FileUtils::normalizeEntryPath(entries.first().path);
            while (path.endsWith(QLatin1Char('/'))) {
                path.chop(1);
            }
            if (entries.first().isDir) {
                dir = path;
            } else {
                const int slash = path.lastIndexOf(QLatin1Char('/'));
                dir = slash < 0 ? QString() : path.left(slash);
            }
        }
        if (dir != m_navCurrentDir) {
            m_navCurrentDir = dir;
            if (m_folderPane) {
                m_folderPane->setCurrentDirectory(dir);
            }
            updateNavLocation();
            updateNavigationState();
        }
    }
}

void MainWindow::updateStatusSummary()
{
    if (!m_statusSummaryLeft || !m_statusSummaryRight) {
        return;
    }

    if (m_localBrowse) {
        const QList<ArchiveEntry> entries = m_model->sourceEntries();
        qint64 totalBytes = 0;
        int files = 0;
        int folders = 0;
        for (const ArchiveEntry &entry : entries) {
            if (entry.isDir) {
                ++folders;
                continue;
            }
            ++files;
            totalBytes += entry.size;
        }
        m_statusSummaryLeft->setText(
            tr("Local folder · %1").arg(QDir::toNativeSeparators(m_localBrowseDir)));
        m_statusSummaryRight->setText(
            tr("%1 files, %2 folders (%3)")
                .arg(files)
                .arg(folders)
                .arg(MainWindowHelpers::formatFileSize(totalBytes)));
        return;
    }

    ArchiveBackend *backend = ArchiveManager::instance().backend();
    if (!backend) {
        m_statusSummaryLeft->setText(tr("No archive open"));
        m_statusSummaryRight->clear();
        return;
    }

    const QList<ArchiveEntry> entries = m_model->sourceEntries();
    qint64 unpacked = 0;
    qint64 packed = 0;
    int files = 0;
    int folders = 0;
    for (const ArchiveEntry &entry : entries) {
        if (entry.isDir) {
            ++folders;
            continue;
        }
        ++files;
        if (entry.size > 0
            && unpacked <= std::numeric_limits<qint64>::max() - entry.size) {
            unpacked += entry.size;
        }
        if (entry.packedSize > 0
            && packed <= std::numeric_limits<qint64>::max() - entry.packedSize) {
            packed += entry.packedSize;
        }
    }

    const QString format = FileTypeHelper::formatLabel(
        ArchiveManager::instance().currentFormat());
    if (entries.isEmpty()) {
        m_statusSummaryLeft->setText(
            tr("%1 · Reading archive...").arg(format));
    } else if (packed > 0 && unpacked > 0) {
        const double ratio =
            (1.0 - static_cast<double>(packed) / static_cast<double>(unpacked))
            * 100.0;
        m_statusSummaryLeft->setText(
            tr("%1 · Unpacked %2 · Ratio %3%")
                .arg(format, MainWindowHelpers::formatFileSize(unpacked))
                .arg(ratio, 0, 'f', 1));
    } else {
        // libarchive does not always expose packed sizes; do not claim 100%.
        // libarchive 未必提供压缩后大小；不要因此显示 100%。
        m_statusSummaryLeft->setText(
            tr("%1 · Unpacked %2").arg(format, MainWindowHelpers::formatFileSize(unpacked)));
    }
    m_statusSummaryRight->setText(
        tr("%1 files, %2 folders (%3)")
            .arg(files)
            .arg(folders)
            .arg(MainWindowHelpers::formatFileSize(unpacked)));
}

void MainWindow::setNavLocationText(const QString &archiveName,
                                    const QString &internalDir)
{
    if (m_modernNavBar) {
        m_modernNavBar->setArchiveLocation(archiveName, internalDir);
    }
    if (m_classicBreadcrumb) {
        m_classicBreadcrumb->setArchiveLocation(archiveName, internalDir);
    }
}

void MainWindow::setNavLocalLocation(const QString &directory)
{
    if (m_modernNavBar) {
        m_modernNavBar->setLocalLocation(directory);
    }
    if (m_classicBreadcrumb) {
        m_classicBreadcrumb->setLocalLocation(directory);
    }
}

void MainWindow::setNavPlaceholder(const QString &text)
{
    if (m_modernNavBar) {
        m_modernNavBar->setPlaceholder(text);
    }
    if (m_classicBreadcrumb) {
        m_classicBreadcrumb->setPlaceholder(text);
    }
}

void MainWindow::updateNavLocation()
{
    if (m_localBrowse) {
        setNavLocalLocation(m_localBrowseDir);
        return;
    }
    const QString archivePath = ArchiveManager::instance().currentPath();
    if (archivePath.isEmpty()) {
        setNavPlaceholder(tr("No archive open"));
        return;
    }
    setNavLocationText(QFileInfo(archivePath).fileName(), m_navCurrentDir);
}

void MainWindow::updateNavigationState()
{
    const bool canBack = m_navIndex > 0;
    const bool canForward =
        m_navIndex >= 0 && m_navIndex + 1 < m_navHistory.size();
    bool canUp = false;

    if (m_localBrowse) {
        QDir dir(m_localBrowseDir);
        canUp = !dir.isRoot() && dir.cdUp();
    } else if (ArchiveManager::instance().backend() != nullptr) {
        if (m_model && m_model->isFlatMode()) {
            canUp = !m_model->currentDirectory().isEmpty();
        } else {
            canUp = !m_navCurrentDir.isEmpty();
        }
    }

    // Both layouts share the same history and directory state; only the
    // presentation differs (modern nav bar vs. classic filter-row buttons).
    // 两种布局共用同一份历史记录与目录状态，只是呈现方式不同：
    // 现代导航栏与经典筛选行按钮。
    if (m_modernNavBar) {
        m_modernNavBar->setNavigationState(canBack, canForward, canUp);
    }
    if (m_backAct) {
        m_backAct->setEnabled(canBack);
    }
    if (m_forwardAct) {
        m_forwardAct->setEnabled(canForward);
    }
    if (m_upAct) {
        m_upAct->setEnabled(canUp);
    }
}

void MainWindow::resetNavigationHistory(const QString &directory)
{
    m_navHistory.clear();
    m_navHistory << directory;
    m_navIndex = 0;
    m_navCurrentDir = directory;
    updateNavLocation();
    updateNavigationState();
}

void MainWindow::navigateHistory(int delta)
{
    const int target = m_navIndex + delta;
    if (target < 0 || target >= m_navHistory.size()) {
        return;
    }
    const QString directory = m_navHistory.at(target);
    if (m_localBrowse) {
        loadLocalDirectory(directory, false);
        if (QDir::cleanPath(m_localBrowseDir) == QDir::cleanPath(directory)) {
            m_navIndex = target;
            updateNavigationState();
        }
        return;
    }
    navigateToDirectory(directory, false);
    if (m_navCurrentDir == directory) {
        m_navIndex = target;
        updateNavigationState();
    }
}

void MainWindow::navigateToDirectory(const QString &path, bool addHistory)
{
    if (!m_model || !m_treeView) {
        return;
    }
    if (m_localBrowse) {
        QString target = path;
        if (QFileInfo(target).isRelative()) {
            target = QDir(m_localBrowseDir).filePath(target);
        }
        loadLocalDirectory(target, addHistory);
        return;
    }

    QString directory = FileUtils::normalizeEntryPath(path);
    if (directory == QStringLiteral(".")) {
        directory.clear();
    }

    if (m_model->isFlatMode()) {
        if (!m_model->enterDirectory(directory)) {
            if (directory != m_model->currentDirectory()) {
                return;
            }
        }
        m_navCurrentDir = m_model->currentDirectory();
        if (QAbstractItemView *view = currentContentView()) {
            view->scrollToTop();
        }
    } else {
        if (directory.isEmpty()) {
            m_treeView->clearSelection();
            m_treeView->setCurrentIndex(QModelIndex());
            m_treeView->scrollToTop();
            m_navCurrentDir.clear();
        } else {
            const QModelIndex index = m_model->indexForPath(directory);
            if (!index.isValid()) {
                return;
            }
            for (QModelIndex parent = index.parent(); parent.isValid();
                 parent = parent.parent()) {
                m_treeView->expand(parent);
            }
            m_treeView->setCurrentIndex(index);
            if (m_treeView->selectionModel()) {
                m_treeView->selectionModel()->select(
                    index, QItemSelectionModel::ClearAndSelect
                               | QItemSelectionModel::Rows);
            }
            m_treeView->scrollTo(index);
            m_navCurrentDir = directory;
        }
    }

    if (addHistory) {
        if (m_navHistory.isEmpty()) {
            m_navHistory << m_navCurrentDir;
            m_navIndex = 0;
        } else if (m_navHistory.value(m_navIndex) != m_navCurrentDir) {
            while (m_navHistory.size() > m_navIndex + 1) {
                m_navHistory.removeLast();
            }
            m_navHistory << m_navCurrentDir;
            m_navIndex = m_navHistory.size() - 1;
        }
    }

    setSearchText(QString());
    if (m_folderPane) {
        m_folderPane->setCurrentDirectory(m_navCurrentDir);
    }
    updateNavLocation();
    updateNavigationState();
    updateSelectionDetails();
}

void MainWindow::setSearchText(const QString &text)
{
    m_searchText = text;
    if (m_modernNavBar) {
        m_modernNavBar->setSearchText(text);
    }
    if (m_searchEdit) {
        const bool blocked = m_searchEdit->blockSignals(true);
        m_searchEdit->setText(text);
        m_searchEdit->blockSignals(blocked);
    }
    if (m_proxyModel) {
        m_proxyModel->setFilterWildcard(text.trimmed());
    }
}

QStringList MainWindow::selectedEntryPaths() const
{
    if (auto *grid = qobject_cast<ArchiveGridView *>(currentContentView())) {
        return grid->selectedEntryPaths();
    }
    return m_treeView ? m_treeView->selectedEntryPaths() : QStringList();
}

void MainWindow::updateFolderPaneState()
{
    if (!m_folderPane) {
        return;
    }
    m_folderPane->setEntries(m_model->sourceEntries());
    m_folderPane->setCurrentDirectory(m_navCurrentDir);
    m_folderPane->setSelection(selectedEntries());
}

void MainWindow::onEntryDoubleClicked(const QString &path)
{
    if (m_localBrowse) {
        openLocalEntry(path);
        return;
    }
    if (m_model->isFlatMode()) {
        if (path == QStringLiteral("..")) {
            const QString current = m_model->currentDirectory();
            const int slash = current.lastIndexOf(QLatin1Char('/'));
            navigateToDirectory(slash < 0 ? QString() : current.left(slash),
                                true);
            return;
        }

        const QModelIndex index = m_model->indexForPath(path);
        const ArchiveEntry *entry = m_model->entryAt(index);
        if (entry && entry->isDir) {
            navigateToDirectory(entry->path, true);
            return;
        }
    }

    const QModelIndex index = m_model->indexForPath(path);
    const ArchiveEntry *entry = m_model->entryAt(index);
    if (!entry || entry->isDir) {
        return;
    }

    const QStringList imagePaths =
        PreviewUtils::imageSiblings(m_model->sourceEntries(), entry->path);
    PreviewManager::instance().preview(
        ArchiveManager::instance().currentPath(),
        ArchiveManager::instance().currentFormat(),
        entry->path, entry->size, imagePaths, m_currentPassword);
}

void MainWindow::onNavBack()
{
    navigateHistory(-1);
}

void MainWindow::onNavForward()
{
    navigateHistory(1);
}

void MainWindow::onNavUp()
{
    if (!m_model) {
        return;
    }
    if (m_localBrowse) {
        QDir dir(m_localBrowseDir);
        if (dir.isRoot() || !dir.cdUp()) {
            return;
        }
        loadLocalDirectory(dir.absolutePath(), true);
        return;
    }
    if (m_model->isFlatMode()) {
        const QString current = m_model->currentDirectory();
        if (current.isEmpty()) {
            return;
        }
        const int slash = current.lastIndexOf(QLatin1Char('/'));
        navigateToDirectory(slash < 0 ? QString() : current.left(slash), true);
        return;
    }
    if (m_navCurrentDir.isEmpty()) {
        return;
    }
    const int slash = m_navCurrentDir.lastIndexOf(QLatin1Char('/'));
    navigateToDirectory(slash < 0 ? QString() : m_navCurrentDir.left(slash),
                        true);
}

void MainWindow::onFolderDirectoryActivated(const QString &path)
{
    if (m_localBrowse) {
        loadLocalDirectory(path, true);
        return;
    }
    navigateToDirectory(path, true);
}

void MainWindow::openLocalWith(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        return;
    }
#ifdef Q_OS_WIN
    if (!QProcess::startDetached(
            QStringLiteral("rundll32.exe"),
            {QStringLiteral("shell32.dll,OpenAs_RunDLL"),
             QDir::toNativeSeparators(info.absoluteFilePath())})) {
        MessageBoxUtils::showOpenFailure(this, info.absoluteFilePath());
    }
#else
    // No portable "Open with" picker exists on this platform; the menu item
    // is hidden below, so this is only a defensive fallback.
    // 该平台没有可移植的“打开方式”选择器；下方会隐藏菜单项，
    // 这里仅作防御性兜底。
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()))) {
        MessageBoxUtils::showOpenFailure(this, info.absoluteFilePath());
    }
#endif
}

void MainWindow::openEntryWith(const QString &path)
{
    if (m_localBrowse) {
        openLocalWith(QDir(m_localBrowseDir).filePath(path));
        return;
    }
    if (!m_model) {
        return;
    }
    const QModelIndex index = m_model->indexForPath(path);
    const ArchiveEntry *entry = m_model->entryAt(index);
    if (!entry || entry->isDir) {
        return;
    }
    PreviewManager::instance().preview(
        ArchiveManager::instance().currentPath(),
        ArchiveManager::instance().currentFormat(),
        entry->path, entry->size, QStringList(), m_currentPassword, true);
}

void MainWindow::onTreeContextMenu(const QPoint &pos)
{
    auto *view = qobject_cast<QAbstractItemView *>(sender());
    if (!view) {
        view = currentContentView();
    }
    if (!view) {
        return;
    }

    const QModelIndex index = view->indexAt(pos);
    if (index.isValid() && view->selectionModel()) {
        view->selectionModel()->select(
            index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        view->setCurrentIndex(index);
    } else if (view->selectionModel()) {
        // Right-click on empty space: keep only the sort/navigation commands.
        // 在空白处右击：只显示排序与导航命令。
        view->selectionModel()->clearSelection();
    }

    if (m_localBrowse) {
        const QStringList localPaths = selectedLocalPaths();
        QMenu menu(this);
        QAction *openAction = menu.addAction(
            Icons::modern(QStringLiteral("open.png")), tr("Open"));
        openAction->setEnabled(localPaths.size() == 1);
        connect(openAction, &QAction::triggered, this, [this, localPaths]() {
            if (!localPaths.isEmpty()) {
                openLocalEntry(QFileInfo(localPaths.first()).fileName());
            }
        });

#ifdef Q_OS_WIN
        QAction *openWithAction = menu.addAction(
            Icons::modern(QStringLiteral("open.png")), tr("Open With..."));
        openWithAction->setEnabled(localPaths.size() == 1
                                   && QFileInfo(localPaths.first()).isFile());
        connect(openWithAction, &QAction::triggered, this, [this, localPaths]() {
            if (localPaths.size() == 1) {
                openLocalWith(localPaths.first());
            }
        });
#endif

        menu.addSeparator();

        QAction *addToZip = menu.addAction(
            Icons::modern(QStringLiteral("compress.png")),
            localPaths.isEmpty()
                ? tr("Add to ZIP archive")
                : tr("Add to \"%1\"")
                      .arg(QFileInfo(suggestedArchivePath(localPaths,
                                                          ArchiveFormat::Zip))
                               .fileName()));
        addToZip->setEnabled(!localPaths.isEmpty());
        connect(addToZip, &QAction::triggered, this, [this, localPaths]() {
            compressPaths(localPaths, ArchiveFormat::Zip, QString(), false);
        });

        QMenu *compressTo = createCompressToMenu(localPaths, &menu);
        compressTo->setEnabled(!localPaths.isEmpty());
        menu.addMenu(compressTo);

        menu.addSeparator();
        menu.addMenu(createSortMenu(&menu));
        menu.addSeparator();

        QAction *openFolder = menu.addAction(
            Icons::fileType(QStringLiteral("folder.png")),
            tr("Open Containing Folder"));
        connect(openFolder, &QAction::triggered, this, [this, localPaths]() {
            const QString directory = localPaths.size() == 1
                ? QFileInfo(localPaths.first()).absolutePath()
                : m_localBrowseDir;
            if (!QDesktopServices::openUrl(
                    QUrl::fromLocalFile(directory))) {
                MessageBoxUtils::showOpenFailure(this, directory);
            }
        });

        QAction *refresh = menu.addAction(
            Icons::modern(QStringLiteral("refresh.png")), tr("Refresh"));
        connect(refresh, &QAction::triggered,
                this, &MainWindow::refreshLocalDirectory);

        menu.exec(view->viewport()->mapToGlobal(pos));
        updateActionsState();
        return;
    }

    ArchiveBackend *backend = ArchiveManager::instance().backend();
    const bool busy = ArchiveManager::instance().isBusy();
    const QStringList selected = selectedEntryPaths();
    const bool singleSelected = selected.size() == 1;
#ifdef Q_OS_WIN
    const ArchiveEntry *selectedEntry = nullptr;
    if (singleSelected && m_model) {
        selectedEntry = m_model->entryAt(m_model->indexForPath(selected.first()));
    }
#endif

    QMenu menu(this);
    QAction *openEntry = menu.addAction(
        Icons::modern(QStringLiteral("open.png")), tr("Open"));
    openEntry->setEnabled(backend && singleSelected && !busy);
    connect(openEntry, &QAction::triggered, this, [this, selected]() {
        if (!selected.isEmpty()) {
            onEntryDoubleClicked(selected.first());
        }
    });

#ifdef Q_OS_WIN
    QAction *openWithEntryAction = menu.addAction(
        Icons::modern(QStringLiteral("open.png")), tr("Open With..."));
    openWithEntryAction->setEnabled(backend && singleSelected && selectedEntry
                                    && !selectedEntry->isDir && !busy);
    connect(openWithEntryAction, &QAction::triggered, this, [this, selected]() {
        if (!selected.isEmpty()) {
            openEntryWith(selected.first());
        }
    });
#endif

    menu.addSeparator();

    QAction *extractSelected = menu.addAction(
        Icons::modern(QStringLiteral("extract.png")), tr("Extract Selected..."));
    extractSelected->setEnabled(backend && !selected.isEmpty() && !busy);
    connect(extractSelected, &QAction::triggered, this, &MainWindow::onExtractSelected);

    QAction *oneClick = menu.addAction(
        Icons::modern(QStringLiteral("oneclick.png")), tr("One-click Extract"));
    oneClick->setEnabled(backend && !busy);
    connect(oneClick, &QAction::triggered, this, &MainWindow::onOneClickExtract);

    menu.addSeparator();

    QAction *addFiles = menu.addAction(
        Icons::modern(QStringLiteral("add.png")), tr("Add Files..."));
    addFiles->setEnabled(backend && ArchiveManager::instance().canModifyCurrentArchive()
                         && !busy);
    connect(addFiles, &QAction::triggered, this, &MainWindow::onAddFiles);

    QAction *deleteSelected = menu.addAction(
        Icons::modern(QStringLiteral("delete.png")), tr("Delete Selected"));
    deleteSelected->setEnabled(backend
                               && ArchiveManager::instance().canModifyCurrentArchive()
                               && !selected.isEmpty() && !busy);
    connect(deleteSelected, &QAction::triggered, this, &MainWindow::onDeleteSelected);

    menu.addSeparator();

    QAction *testArchive = menu.addAction(
        Icons::modern(QStringLiteral("test.png")), tr("Test Archive"));
    testArchive->setEnabled(backend && !busy);
    connect(testArchive, &QAction::triggered, this, &MainWindow::onTestArchive);

    menu.addSeparator();
    menu.addMenu(createSortMenu(&menu));

    menu.exec(view->viewport()->mapToGlobal(pos));
    updateActionsState();
}

void MainWindow::onSearchChanged(const QString &text)
{
    m_searchText = text;
    m_proxyModel->setFilterWildcard(text.trimmed());
}

void MainWindow::onSidebarFileOpened(const QString &path)
{
    if (QFileInfo::exists(path)) {
        openFile(path);
    }
}
