// QArchiver / 小Q解压
// MainWindow local browser: directory listing shown when no archive is open,
// plus compression without the compress dialog (Explorer menu / command line).
// MainWindow 本地浏览：未打开压缩包时显示的目录列表，以及不经过压缩对话框
// 的压缩流程（资源管理器右键菜单/命令行）。

#include "common/FormatRegistry.h"

#include "ui/MainWindow.h"

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

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <limits>

void MainWindow::browseLocalDirectory(const QString &path)
{
    loadLocalDirectory(path, false);
}

void MainWindow::loadLocalDirectory(const QString &path, bool addHistory)
{
    if (ArchiveManager::instance().isBusy()) {
        return;
    }

    QString target = path.trimmed();
    if (target.isEmpty()) {
        target = AppSettings::instance().localBrowsePath();
    }
    QFileInfo info(target);
    if (!info.exists() || !info.isDir()) {
        target = QDir::homePath();
        info = QFileInfo(target);
    }
    QDir dir(info.absoluteFilePath());
    if (!dir.exists()) {
        dir.setPath(QDir::homePath());
    }
    const QString directory = QDir::cleanPath(dir.absolutePath());

    // A local listing replaces the archive view; release the archive first so
    // the manager and the local browser never claim the same content.
    // 本地列表会取代压缩包视图；先释放压缩包，避免管理器与本地浏览器同时
    // 持有同一份内容。
    if (ArchiveManager::instance().backend()) {
        ArchiveManager::instance().closeArchive();
        DragOutManager::instance().clearCache();
        m_currentPassword.clear();
        m_listReady = false;
    }

    const bool wasLocal = m_localBrowse;
    m_localBrowse = true;
    m_localBrowseDir = directory;
    AppSettings::instance().setLocalBrowsePath(directory);

    QList<ArchiveEntry> entries;
    const QFileInfoList infos = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot,
        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    entries.reserve(infos.size());
    for (const QFileInfo &fileInfo : infos) {
        ArchiveEntry entry;
        // Local entries use the bare file name as their model path; the
        // absolute path is rebuilt from m_localBrowseDir when needed.
        // 本地条目以纯文件名作为模型路径；需要时用 m_localBrowseDir 还原
        // 绝对路径。
        entry.path = fileInfo.fileName();
        entry.isDir = fileInfo.isDir();
        entry.isSymLink = fileInfo.isSymLink();
        if (!entry.isDir) {
            entry.size = fileInfo.size();
        }
        entry.mtime = fileInfo.lastModified();
        entries.append(entry);
    }

    m_model->setArchiveContext(QString(), ArchiveFormat::Unknown, QString());
    m_model->setLocalContext(directory);
    m_model->setEntries(entries);
    m_model->setFlatMode(true);
    m_navCurrentDir.clear();
    setSearchText(QString());

    if (!wasLocal) {
        m_navHistory.clear();
        m_navHistory << directory;
        m_navIndex = 0;
    } else if (addHistory) {
        while (m_navHistory.size() > m_navIndex + 1) {
            m_navHistory.removeLast();
        }
        if (m_navHistory.isEmpty() || m_navHistory.constLast() != directory) {
            m_navHistory << directory;
        }
        m_navIndex = m_navHistory.size() - 1;
    } else if (m_navHistory.isEmpty()) {
        m_navHistory << directory;
        m_navIndex = 0;
    }

    showArchivePage();
    if (m_treeView) {
        m_treeView->setFlatMode(true);
        m_treeView->expandToDepth(0);
    }
    // Only rebuild the sidebar when entering local browsing; keeping it while
    // navigating preserves the system places list.
    // 仅从非本地模式进入时清理侧栏；本地目录间导航保留系统位置列表。
    if (m_folderPane && !wasLocal) {
        m_folderPane->clear();
    }
    if (m_modernNavBar) {
        m_modernNavBar->setViewMode(contentViewMode());
    }
    applyContentViewMode();
    updateLocalModeUi();
    setWindowTitle(tr("QArchiver - %1").arg(directory));
    setBusyIndicatorVisible(false);
    statusBar()->showMessage(tr("%1 items").arg(entries.size()), 3000);
}

void MainWindow::refreshLocalDirectory()
{
    if (m_localBrowse) {
        loadLocalDirectory(m_localBrowseDir, false);
    }
}

QString MainWindow::localPathForEntry(const ArchiveEntry &entry) const
{
    if (entry.path.isEmpty()) {
        return QString();
    }
    return QDir(m_localBrowseDir).filePath(entry.path);
}

QStringList MainWindow::selectedLocalPaths() const
{
    QStringList paths;
    for (const ArchiveEntry &entry : selectedEntries()) {
        const QString path = localPathForEntry(entry);
        if (!path.isEmpty()) {
            paths << path;
        }
    }
    return paths;
}

void MainWindow::openLocalEntry(const QString &name)
{
    if (name.isEmpty()) {
        return;
    }
    const QString fullPath = QDir(m_localBrowseDir).filePath(name);
    const QFileInfo info(fullPath);
    if (!info.exists()) {
        refreshLocalDirectory();
        return;
    }
    if (info.isDir()) {
        loadLocalDirectory(fullPath, true);
        return;
    }
    // Archives stay inside QArchiver; everything else opens in the desktop's
    // default application.
    // 压缩包继续由 QArchiver 打开；其他文件交给系统默认程序。
    if (FormatRegistry::isArchivePath(fullPath)) {
        openFile(fullPath);
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(fullPath))) {
        MessageBoxUtils::showOpenFailure(this, fullPath);
    }
}

void MainWindow::updateLocalModeUi()
{
    if (m_folderPane) {
        m_folderPane->setVisible(true);
        m_folderPane->setLocalMode(m_localBrowse);
        if (m_localBrowse) {
            m_folderPane->setCurrentLocalPath(m_localBrowseDir);
        }
    }
    updateNavLocation();
    updateNavigationState();
    updateStatusSummary();
    updateSelectionDetails();
    updateActionsState();
}

QString MainWindow::suggestedArchivePath(const QStringList &sources,
                                         ArchiveFormat format) const
{
    if (sources.isEmpty()) {
        return QString();
    }
    const QString extension = defaultExtension(format);
    const QFileInfo first(sources.first());
    const QString directory = first.absolutePath();

    QString base;
    if (sources.size() == 1) {
        base = first.isDir() ? first.fileName() : first.completeBaseName();
        if (base.isEmpty()) {
            base = first.fileName();
        }
    } else {
        // Explorer passes multiple files one at a time, but grouping by the
        // containing folder keeps a direct CLI invocation predictable.
        // 资源管理器通常逐个调用；直接传多个文件时以所在文件夹命名。
        base = QFileInfo(directory).fileName();
        if (base.isEmpty()) {
            base = QStringLiteral("archive");
        }
    }
    if (base.isEmpty()) {
        base = QStringLiteral("archive");
    }
    return QDir(directory).filePath(base + extension);
}

QMenu *MainWindow::createCompressToMenu(const QStringList &sources, QWidget *parent)
{
    auto *menu = new QMenu(tr("Compress to"), parent);
    const auto addFormat = [this, menu, sources](ArchiveFormat format,
                                                 const QString &label) {
        QAction *action = menu->addAction(label);
        connect(action, &QAction::triggered, this,
                [this, sources, format]() {
            compressPaths(sources, format, QString(), false);
        });
    };

    addFormat(ArchiveFormat::SevenZip, tr("7z archive (.7z)"));
    addFormat(ArchiveFormat::Zip, tr("ZIP archive (.zip)"));
    addFormat(ArchiveFormat::TarGz, tr("Gzip tar (.tar.gz)"));
    addFormat(ArchiveFormat::TarXz, tr("Xz tar (.tar.xz)"));
    addFormat(ArchiveFormat::TarBz2, tr("Bzip2 tar (.tar.bz2)"));
    addFormat(ArchiveFormat::Tar, tr("Tar (.tar)"));

    menu->addSeparator();
    QAction *custom = menu->addAction(tr("Custom options..."));
    connect(custom, &QAction::triggered, this, [this, sources]() {
        compressPathsWithDialog(sources);
    });
    return menu;
}

void MainWindow::compressPaths(const QStringList &sources, ArchiveFormat format,
                               const QString &targetPath, bool closeAfterFinish)
{
    if (sources.isEmpty()) {
        return;
    }
    if (format == ArchiveFormat::Unknown) {
        QMessageBox::warning(this, tr("Create Archive"),
                             tr("The requested archive format is not supported."));
        return;
    }

    QStringList absoluteSources;
    for (const QString &source : sources) {
        const QFileInfo info(source);
        if (info.exists()) {
            absoluteSources << info.absoluteFilePath();
        }
    }
    if (absoluteSources.isEmpty()) {
        QMessageBox::warning(this, tr("Create Archive"),
                             tr("None of the selected items exist."));
        return;
    }

    if (isStreamFamily(format)
        && (absoluteSources.size() != 1
            || QFileInfo(absoluteSources.first()).isDir())) {
        QMessageBox::warning(
            this, tr("Create Archive"),
            tr("A single-file compression stream can contain exactly one "
               "regular file."));
        return;
    }

    QString target = targetPath.isEmpty()
        ? suggestedArchivePath(absoluteSources, format)
        : QFileInfo(targetPath).absoluteFilePath();
    if (target.isEmpty()) {
        return;
    }

    bool targetIsSource = false;
    for (const QString &source : absoluteSources) {
        if (QFileInfo(source) == QFileInfo(target)) {
            targetIsSource = true;
            break;
        }
    }
    if (targetIsSource) {
        // Compressing an archive into itself would corrupt it; pick a
        // non-existing sibling name instead.
        // 把压缩包压进自身会损坏文件；改用不存在的同级名称。
        target = FileUtils::uniqueDestinationPath(target);
    } else if (QFileInfo::exists(target)) {
        const auto answer = QMessageBox::question(
            this, tr("Overwrite Archive"),
            tr("The archive already exists:\n%1\n\nOverwrite it?")
                .arg(QDir::toNativeSeparators(target)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    startImmediateCompress(absoluteSources, format, target, closeAfterFinish);
}

void MainWindow::startImmediateCompress(const QStringList &sources,
                                        ArchiveFormat format,
                                        const QString &targetPath,
                                        bool closeAfterFinish)
{
    CompressOptions options;
    options.archivePath = targetPath;
    options.sourceFiles = sources;
    options.compressionLevel = AppSettings::instance().defaultCompressionLevel();
    options.followSymlinks = AppSettings::instance().followSymlinks();

    m_compressCloseAfter = closeAfterFinish;
    m_lastCompressTarget = targetPath;
    m_currentOp = UiOp::Compress;
    showProgressDialog(tr("Creating Archive..."));
    ArchiveManager::instance().compress(options, format);
    updateActionsState();
}

void MainWindow::compressPathsWithDialog(const QStringList &sources)
{
    QStringList items = sources;
    if (items.isEmpty() && m_localBrowse) {
        items = selectedLocalPaths();
    }
    if (items.isEmpty()) {
        items = chooseSourceItems(tr("Select Files to Compress"));
    }
    if (items.isEmpty()) {
        finishDialogOnly();
        return;
    }

    CompressDialog dlg(items, this);
    if (dlg.exec() != QDialog::Accepted) {
        finishDialogOnly();
        return;
    }
    const CompressOptions options = dlg.options();
    m_compressCloseAfter = false;
    m_lastCompressTarget = options.archivePath;
    m_currentOp = UiOp::Compress;
    showProgressDialog(tr("Creating Archive..."));
    ArchiveManager::instance().compress(options, dlg.selectedFormat());
    updateActionsState();
}

void MainWindow::compressFromCommandLine(const QStringList &sources,
                                         const QString &formatName,
                                         const QString &targetPath)
{
    const ArchiveFormat format = FormatRegistry::formatFromName(formatName);
    if (format == ArchiveFormat::Unknown) {
        QMessageBox::critical(
            this, tr("Unsupported Format"),
            tr("The archive format \"%1\" is not supported.").arg(formatName));
        finishDialogOnly();
        return;
    }

    QStringList existing;
    for (const QString &source : sources) {
        const QFileInfo info(source);
        if (info.exists()) {
            existing << info.absoluteFilePath();
        }
    }
    if (existing.isEmpty()) {
        QMessageBox::critical(
            this, tr("Create Archive"),
            tr("No existing files or folders were passed to QArchiver."));
        finishDialogOnly();
        return;
    }

    compressPaths(existing, format, targetPath, true);
    // If the request stopped before an operation started (for example the
    // overwrite prompt was declined), close the hidden shell window.
    // 若请求在启动操作前停止（例如用户拒绝覆盖），关闭隐藏的外壳窗口。
    if (!ArchiveManager::instance().isBusy() && m_currentOp != UiOp::Compress) {
        finishDialogOnly();
    }
}
