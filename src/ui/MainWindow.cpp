// QArchiver / 小Q解压
// MainWindow: construction, page switching, backend signal handling and input
// events.  Layout building, actions, navigation, extraction and drag-out live
// in the sibling MainWindow*.cpp files.
// MainWindow：构造、页面切换、后端信号处理与输入事件。布局构建、动作、
// 导航、解压与拖出位于同级 MainWindow*.cpp 文件中。

#include "common/FormatRegistry.h"

#include "ui/MainWindow.h"

#include "ui/MainWindowHelpers.h"
#include "ui/icons/IconUtils.h"

#include "ui/models/FileListModel.h"
#include "ui/models/FilterProxyModel.h"
#include "ui/models/FileListViewDelegate.h"
#include "ui/services/PreviewManager.h"
#include "ui/services/ThumbnailManager.h"
#include "ui/services/PreviewUtils.h"
#include "ui/services/DragOutManager.h"
#include "ui/services/ExternalEditManager.h"
#include "ui/widgets/ArchiveTreeView.h"
#include "ui/widgets/ArchiveGridView.h"
#include "ui/widgets/BackgroundWidget.h"
#include "ui/widgets/SidebarWidget.h"
#include "ui/widgets/ModernToolBar.h"
#include "ui/widgets/ImmersiveTitleBar.h"
#include "ui/widgets/WindowResizeBorders.h"
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
#include <QScreen>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QShowEvent>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <limits>

MainWindow::MainWindow(QWidget *parent, bool startupContentPending)
    : QMainWindow(parent)
    , m_startupContentPending(startupContentPending)
{
    setWindowTitle(tr("QArchiver"));
    setWindowIcon(Icons::modern(QStringLiteral("app-icon.png")));
    setWindowFlag(Qt::FramelessWindowHint, true);
    resize(1180, 760);
    setMinimumSize(880, 560);

    // The menu widget sits above toolbars and central content, giving the
    // modern/classic layouts one shared immersive title bar.
    // 菜单控件位于工具栏与内容上方，让现代/经典布局共用同一个沉浸式标题栏。
    m_titleBar = new ImmersiveTitleBar(this);
    setMenuWidget(m_titleBar);
    connect(m_titleBar, &ImmersiveTitleBar::minimizeRequested,
            this, &QWidget::showMinimized);
    connect(m_titleBar, &ImmersiveTitleBar::maximizeRequested, this, [this]() {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
        m_titleBar->updateMaximizeState();
    });
    connect(m_titleBar, &ImmersiveTitleBar::closeRequested,
            this, &QWidget::close);

    m_model = new FileListModel(this);
    m_proxyModel = new FilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);

    // Slider ticks and Ctrl+wheel events arrive in bursts.  Update the view
    // immediately but persist the size only after the interaction settles.
    // 滑块拖动与 Ctrl+滚轮会连续触发事件；界面立即更新，停止操作后再持久化。
    m_itemSizeSaveTimer = new QTimer(this);
    m_itemSizeSaveTimer->setSingleShot(true);
    m_itemSizeSaveTimer->setInterval(400);
    connect(m_itemSizeSaveTimer, &QTimer::timeout, this, [this]() {
        AppSettings::instance().setItemSize(m_itemSize);
    });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        if (m_itemSizeSaveTimer && m_itemSizeSaveTimer->isActive()) {
            m_itemSizeSaveTimer->stop();
            AppSettings::instance().setItemSize(m_itemSize);
        }
    });

    setupActions();
    m_modernUi = AppSettings::instance().uiLayoutMode() == QStringLiteral("modern");
    m_sortColumn = AppSettings::instance().sortColumn();
    m_sortOrder = AppSettings::instance().sortOrder();
    m_itemSize = AppSettings::instance().itemSize();
    buildUi();
    m_resizeBorders = new WindowResizeBorders(this);

    // The background image is a presentation option, independent of the
    // archive data.  It is only used by the classic layout.
    // 背景图片属于展示选项，与压缩包数据无关。仅经典布局使用。
    connect(&AppSettings::instance(), &AppSettings::backgroundImageChanged,
            this, &MainWindow::updatePlaceholderBackground);

    auto &manager = ArchiveManager::instance();
    connect(&manager, &ArchiveManager::archiveOpening,
            this, &MainWindow::onArchiveOpening);
    connect(&manager, &ArchiveManager::archiveOpened,
            this, &MainWindow::onArchiveOpened);
    connect(&manager, &ArchiveManager::fileListReady,
            this, &MainWindow::onFileListReady);
    connect(&manager, &ArchiveManager::progressChanged,
            this, &MainWindow::onProgressChanged);
    connect(&manager, &ArchiveManager::outputReady,
            this, &MainWindow::onOutputReady);
    connect(&manager, &ArchiveManager::passwordRequested,
            this, &MainWindow::onPasswordRequested);
    connect(&manager, &ArchiveManager::errorOccurred,
            this, &MainWindow::onErrorOccurred);
    connect(&manager, &ArchiveManager::operationFinished,
            this, &MainWindow::onOperationFinished);

    connect(&ThumbnailManager::instance(),
            &ThumbnailManager::thumbnailReady, this,
            [this](const QString &archivePath, const QString &entryPath) {
        if (archivePath == ArchiveManager::instance().currentPath()) {
            m_model->refreshThumbnail(entryPath);
        }
    });
    connect(&ThumbnailManager::instance(),
            &ThumbnailManager::localThumbnailReady, this,
            [this](const QString &filePath) {
        if (!m_localBrowse) {
            return;
        }
        const QFileInfo info(filePath);
        if (QDir::cleanPath(info.absolutePath())
            != QDir::cleanPath(m_localBrowseDir)) {
            return;
        }
        m_model->refreshThumbnail(info.fileName());
    });

    manager.setSymlinkDecisionCallback(
        [this](const QString &path, const QString &target) {
            QMessageBox box(this);
            box.setWindowTitle(tr("Unsafe Symbolic Link"));
            box.setIconPixmap(Icons::aero(QStringLiteral("warning.ico"))
                                  .pixmap(32, 32));
            box.setText(tr("The archive contains a symbolic link that points "
                           "outside the extraction directory."));
            box.setInformativeText(
                tr("Link: %1\nTarget: %2\n\n"
                   "Skipping the link prevents it and its contents from being "
                   "extracted. Aborting removes the temporary extraction.")
                    .arg(path, target.isEmpty() ? tr("(empty)") : target));
            auto *applyAll = new QCheckBox(
                tr("Apply to the same items in this task"), &box);
            box.setCheckBox(applyAll);
            QPushButton *skipButton =
                box.addButton(tr("Skip Link"), QMessageBox::DestructiveRole);
            QPushButton *abortButton =
                box.addButton(tr("Abort Extraction"), QMessageBox::RejectRole);
            box.setDefaultButton(abortButton);
            box.exec();
            if (box.clickedButton() == skipButton) {
                return applyAll->isChecked()
                    ? ArchiveManager::SymlinkDecision::SkipAll
                    : ArchiveManager::SymlinkDecision::Skip;
            }
            return ArchiveManager::SymlinkDecision::Abort;
        });

    setupExternalEditHandlers();

    // Ctrl+F focuses the search field of whichever layout is active.
    auto *findAction = new QAction(this);
    findAction->setShortcut(QKeySequence::Find);
    addAction(findAction);
    connect(findAction, &QAction::triggered, this, [this]() {
        if (m_modernUi && m_modernNavBar) {
            m_modernNavBar->focusSearch();
        } else if (m_searchEdit) {
            m_searchEdit->setFocus(Qt::ShortcutFocusReason);
            m_searchEdit->selectAll();
        }
    });

    PreviewManager::instance().cleanupStale();
    DragOutManager::instance().cleanupStale();
    updateActionsState();

    // Probe the fallback CLI after the window is visible, so the warning is
    // shown once without delaying application startup.
    // 窗口显示后再检测兜底 CLI，只提示一次且不阻塞启动。
    QTimer::singleShot(0, this, &MainWindow::checkSevenZipFallback);
}

MainWindow::~MainWindow()
{
    ArchiveManager::instance().setSymlinkDecisionCallback(nullptr);
    PreviewManager::instance().cleanupOwn();
    DragOutManager::instance().cleanupOwn();
}

void MainWindow::buildUi()
{
    // The immersive title bar lives in the menu-widget slot.  Re-attach it in
    // case anything (e.g. an accidental QMainWindow::menuBar() call) replaced
    // it; QMainWindow::menuBar() lazily creates a QMenuBar and takes that slot.
    // 标题栏占用 menuWidget 槽位。若它被意外替换（如误调用 menuBar()，
    // 该方法会懒创建 QMenuBar 并占用该槽位），此处重新挂回并显示。
    if (m_titleBar && menuWidget() != m_titleBar) {
        setMenuWidget(m_titleBar);
        m_titleBar->show();
    }

    m_modernUi = AppSettings::instance().uiLayoutMode() == QStringLiteral("modern");
    applyActionIcons();

    if (m_modernUi) {
        setupModernUi();
    } else {
        setupClassicUi();
    }
    setupStatusBar();
    applyItemSizeInternal(m_itemSize, false);
    applyContentViewMode();

    if (ArchiveManager::instance().backend()) {
        showArchivePage();
        if (m_modernUi) {
            m_folderPane->setEntries(m_model->sourceEntries());
            m_folderPane->setCurrentDirectory(m_navCurrentDir);
        }
        updateStatusSummary();
    } else {
        // Modern starts on the welcome page; classic uses the local browser as
        // its start page.  If the command line is about to open an archive,
        // showStartPage() keeps the placeholder instead of scanning disk.
        // 现代布局显示欢迎页；经典布局以本地文件浏览作为起始页。如果命令行
        // 马上要打开压缩包，showStartPage() 会保留占位页而不扫描磁盘。
        showStartPage();
    }
    m_startupContentPending = false;

    updateNavLocation();
    updateNavigationState();
    updateSelectionDetails();
    updateActionsState();
    if (!m_searchText.isEmpty()) {
        setSearchText(m_searchText);
    }
    if (m_resizeBorders) {
        m_resizeBorders->raiseBorders();
    }
    QTimer::singleShot(0, this, &MainWindow::updateSearchIcon);
}

void MainWindow::teardownUi()
{
    if (m_progressDialog) {
        m_progressDialog->close();
        m_progressDialog = nullptr;
    }

    if (m_sidebarDock) {
        removeDockWidget(m_sidebarDock);
        delete m_sidebarDock;
        m_sidebarDock = nullptr;
    }

    const QList<QToolBar *> toolbars = findChildren<QToolBar *>();
    for (QToolBar *toolBar : toolbars) {
        removeToolBar(toolBar);
        delete toolBar;
    }
    m_mainToolBar = nullptr;

    if (auto *bar = m_titleBar ? m_titleBar->menuBarWidget() : nullptr) {
        bar->clear();
    }

    QWidget *oldCentral = takeCentralWidget();
    if (oldCentral) {
        oldCentral->hide();
        delete oldCentral;
    }

    if (m_busyWidget) {
        statusBar()->removeWidget(m_busyWidget);
        delete m_busyWidget;
        m_busyWidget = nullptr;
        m_busyLabel = nullptr;
        m_busyBar = nullptr;
    }
    if (m_statusSummaryLeft) {
        statusBar()->removeWidget(m_statusSummaryLeft);
        delete m_statusSummaryLeft;
        m_statusSummaryLeft = nullptr;
    }
    if (m_statusSummaryRight) {
        statusBar()->removeWidget(m_statusSummaryRight);
        delete m_statusSummaryRight;
        m_statusSummaryRight = nullptr;
    }
    if (m_sizeSlider) {
        statusBar()->removeWidget(m_sizeSlider);
        delete m_sizeSlider;
        m_sizeSlider = nullptr;
    }
    statusBar()->clearMessage();

    m_treeView = nullptr;
    m_gridView = nullptr;
    m_viewStack = nullptr;
    m_centralStack = nullptr;
    m_searchEdit = nullptr;
    m_classicBreadcrumb = nullptr;
    m_placeholderLabel = nullptr;
    m_searchAction = nullptr;
    m_backgroundWidget = nullptr;
    m_modernToolBar = nullptr;
    m_modernNavBar = nullptr;
    m_folderPane = nullptr;
    m_welcomePage = nullptr;
    m_bodySplitter = nullptr;
}

void MainWindow::updatePlaceholderBackground()
{
    if (!m_backgroundWidget || !m_centralStack) {
        return;
    }

    if (m_centralStack->currentIndex() == 0) {
        m_backgroundWidget->reloadFromSettings();
    } else {
        // Archive page: ArchiveTreeView paints the image inside its viewport.
        // 归档页：由 ArchiveTreeView 在其 viewport 内绘制图片。
        m_backgroundWidget->setBackgroundImage(BackgroundImageOptions{});
    }
}

void MainWindow::showPlaceholder(const QString &message)
{
    if (m_modernUi) {
        showWelcomePage();
        return;
    }
    if (m_placeholderLabel) {
        m_placeholderLabel->setText(message);
    }
    if (m_centralStack) {
        m_centralStack->setCurrentIndex(0);
    }
    updatePlaceholderBackground();
}

void MainWindow::showWelcomePage()
{
    if (!m_centralStack) {
        return;
    }
    m_centralStack->setCurrentIndex(0);
    if (m_welcomePage) {
        m_welcomePage->refreshRecent();
    }
    setNavPlaceholder(tr("No archive open"));
    updateNavigationState();
}

void MainWindow::showStartPage()
{
    if (!m_modernUi) {
        // The classic layout has no welcome page or File Manager button, so
        // its start page is the local browser.  Keep the placeholder only for
        // the first build when the command line is about to open an archive.
        // 经典布局没有欢迎页与“文件管理”按钮，其起始页就是本地文件浏览。
        // 仅在首次构建且命令行即将打开压缩包时保留占位页。
        if (m_startupContentPending) {
            showPlaceholder(
                tr("No archive open.\nUse the Open button or drag a file here."));
            return;
        }
        browseLocalDirectory(m_localBrowse
                                 ? m_localBrowseDir
                                 : AppSettings::instance().localBrowsePath());
        return;
    }

    if (m_localBrowse) {
        m_localBrowse = false;
        m_localBrowseDir.clear();
        m_navHistory.clear();
        m_navIndex = -1;
    }
    m_model->setLocalContext(QString());
    showWelcomePage();
    // Keep the folder pane visible on the welcome page so its content uses
    // the same centred content area as the archive/file-manager views.
    // Returning from the local file manager must restore the archive sidebar.
    // 欢迎页保留文件夹侧栏；从本地文件管理返回时也恢复归档侧栏。
    // Only reset the pane when no archive is open: clearing it while an archive
    // is still loaded would drop the archive folder tree even though the
    // archive remains open.
    // 仅在没有打开压缩包时重置侧栏；压缩包仍打开时清空会丢失归档文件夹树。
    if (m_folderPane) {
        m_folderPane->setVisible(true);
        if (!ArchiveManager::instance().backend()) {
            m_folderPane->setLocalMode(false);
            m_folderPane->clear();
        }
    }
    setWindowTitle(tr("QArchiver"));
    updateStatusSummary();
    updateActionsState();
}

void MainWindow::showArchivePage()
{
    if (m_centralStack) {
        m_centralStack->setCurrentIndex(1);
    }
}

void MainWindow::showProgressDialog(const QString &title)
{
    if (m_progressDialog) {
        m_progressDialog->close();
        m_progressDialog = nullptr;
    }
    // In shell-integration mode the main window is hidden; make the progress
    // dialog a top-level window so it is visible and gets focus.
    // 外壳集成模式下主窗口是隐藏的；把进度对话框设为顶层窗口，确保可见并
    // 获得焦点。
    auto *dlg = new ProgressDialog(m_dialogOnly ? nullptr : this);
    dlg->setOperation(title);
    connect(dlg, &ProgressDialog::cancelled, this, []() {
        ArchiveManager::instance().cancelCurrentOperation();
    });
    m_progressDialog = dlg;
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
    QTimer::singleShot(0, this, &MainWindow::updateActionsState);
}

void MainWindow::finishDialogOnly()
{
    if (!m_dialogOnly) {
        return;
    }
    m_dialogOnly = false;
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void MainWindow::setBusyIndicatorVisible(bool visible, const QString &text)
{
    if (!m_busyWidget) {
        return;
    }
    m_busyLabel->setText(text);
    m_busyWidget->setVisible(visible);
}

void MainWindow::onArchiveOpening(const QString &filePath)
{
    // A normal open invalidates the current list.  A write-through reopen
    // (add/delete on a read-only libarchive backend) reopens the same archive,
    // so keep the current list usable if the write is cancelled or fails.
    // 普通打开会使当前列表失效；只读 libarchive 后端的添加/删除写回退会重新
    // 打开同一个压缩包，因此保留当前列表，写操作取消/失败时仍可使用。
    if (m_currentOp != UiOp::Add && m_currentOp != UiOp::Delete) {
        m_listReady = false;
    }
    setBusyIndicatorVisible(true,
        tr("Detecting format: %1").arg(QFileInfo(filePath).fileName()));
    statusBar()->showMessage(tr("Detecting archive format..."));
    setNavPlaceholder(
        tr("Opening %1...").arg(QFileInfo(filePath).fileName()));
    updateActionsState();
}

void MainWindow::onArchiveOpened(const QString &filePath)
{
    m_currentPassword = m_sessionPassword;
    m_listReady = false;
    m_localBrowse = false;
    m_localBrowseDir.clear();
    ThumbnailManager::instance().clearFailures();
    m_model->setArchiveContext(
        filePath, ArchiveManager::instance().currentFormat(), m_currentPassword);

    // A new archive means new content: drop staging prepared for the previous
    // one.  (The staging directories themselves are removed on exit.)
    DragOutManager::instance().clearCache();
    m_model->setEntries({});
    setSearchText(QString());
    setWindowTitle(tr("QArchiver - %1").arg(QFileInfo(filePath).fileName()));
    if (m_folderPane) {
        m_folderPane->setVisible(true);
        m_folderPane->setLocalMode(false);
        m_folderPane->clear();
    }
    applyContentViewMode();
    showArchivePage();
    resetNavigationHistory();
    statusBar()->showMessage(tr("Opened: %1").arg(filePath), 3000);
    setBusyIndicatorVisible(true,
        tr("Reading archive %1").arg(QFileInfo(filePath).fileName()));

    ListOptions opts;
    if (!m_currentPassword.isEmpty()) {
        opts.password = m_currentPassword;
    }
    m_currentOp = UiOp::List;
    ArchiveManager::instance().listContents(opts);
    updateActionsState();
}

void MainWindow::onFileListReady(const QList<ArchiveEntry> &entries)
{
    if (m_currentOp == UiOp::List) {
        m_currentOp = UiOp::None;
    }
    m_listReady = true;
    m_model->setArchiveContext(
        ArchiveManager::instance().currentPath(),
        ArchiveManager::instance().currentFormat(), m_currentPassword);
    m_model->setEntries(entries);
    if (!m_model->isFlatMode()) {
        m_treeView->expandToDepth(0);
    } else {
        m_navCurrentDir = m_model->currentDirectory();
    }
    if (m_modernUi) {
        updateFolderPaneState();
    }
    // Both layouts render the same location state (modern nav bar vs. classic
    // breadcrumb), so refresh it whenever a list arrives.  Only refreshing it
    // for the modern layout left the classic breadcrumb on whatever placeholder
    // an internal reopen had set ("Opening <archive>...").
    // 两种布局呈现同一份路径状态（现代导航栏与经典面包屑），因此收到列表时都
    // 需要刷新。此前只刷新现代布局，重开压缩包留下的占位文本
    //（“正在打开 <压缩包>...”）就会一直留在经典面包屑上。
    if (m_navHistory.isEmpty()) {
        resetNavigationHistory(m_navCurrentDir);
    } else {
        updateNavLocation();
        updateNavigationState();
    }
    setBusyIndicatorVisible(false);
    statusBar()->showMessage(tr("%1 entries").arg(entries.size()), 3000);
    updateStatusSummary();
    updateSelectionDetails();
    updateActionsState();

    if (m_oneClickPending) {
        beginOneClickExtract();
    }

    // A write-back that had to reopen its archive continues here.  Deferred to
    // the event loop because ArchiveManager still owns the just-finished
    // listing when this slot runs.
    // 需要重新打开压缩包的写回在这里继续。推迟到事件循环，因为本槽执行时
    // ArchiveManager 仍持有刚结束的列表操作。
    if (!m_queuedExternalSave.archivePath.isEmpty()
        && ArchiveManager::instance().currentPath()
            == m_queuedExternalSave.archivePath) {
        QTimer::singleShot(0, this, &MainWindow::dispatchQueuedExternalSave);
    }
}

void MainWindow::onProgressChanged(int percent, int total, int current, int etaSec)
{
    if (m_progressDialog) {
        m_progressDialog->updateProgress(percent, total, current, etaSec);
    }
}

void MainWindow::onOutputReady(const QString &line)
{
    if (m_progressDialog) {
        m_progressDialog->appendOutput(line);
    }
}

void MainWindow::onPasswordRequested()
{
    setBusyIndicatorVisible(false);

    PasswordDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        ArchiveManager::instance().cancelPendingOperation();
        updateActionsState();
        return;
    }

    m_currentPassword = dlg.password();
    if (dlg.rememberPassword()) {
        m_sessionPassword = m_currentPassword;
    }
    ThumbnailManager::instance().clearFailures();
    m_model->setArchiveContext(
        ArchiveManager::instance().currentPath(),
        ArchiveManager::instance().currentFormat(), m_currentPassword);
    setBusyIndicatorVisible(true, tr("Processing..."));
    ArchiveManager::instance().retryWithPassword(m_currentPassword);
}

void MainWindow::onErrorOccurred(ArchiveErrorCode code, const QString &msg)
{
    // A failed write-back (or its archive reopen) must release the file again.
    // 写回失败（或其重新打开压缩包失败）时需要释放该文件。
    finishExternalSave(false);
    if (!m_queuedExternalSave.archivePath.isEmpty()) {
        if (ArchiveManager::instance().currentPath()
            == m_queuedExternalSave.archivePath) {
            // The reopen itself worked (e.g. only the listing was cancelled):
            // continue with the write-back instead of dropping the answer.
            // 重新打开本身已成功（例如只取消了列表）：继续写回而不是丢弃回答。
            QTimer::singleShot(0, this, &MainWindow::dispatchQueuedExternalSave);
        } else {
            // The reopen failed (the previous archive stays open) or another
            // archive was opened instead: release the file again.
            // 重新打开失败（旧压缩包仍然打开）或改开了别的压缩包：释放该文件。
            const QString localPath = m_queuedExternalSaveLocalPath;
            m_queuedExternalSave = AddFilesOptions{};
            m_queuedExternalSaveLocalPath.clear();
            ExternalEditManager::instance().reportSaveFailed(localPath);
        }
    }

    if (m_progressDialog) {
        m_progressDialog->close();
        m_progressDialog = nullptr;
    }
    m_currentOp = UiOp::None;
    m_oneClickExtractActive = false;
    m_oneClickCloseAfter = false;
    m_oneClickPending = false;
    m_compressCloseAfter = false;
    m_lastCompressTarget.clear();
    if (!ArchiveManager::instance().backend()) {
        m_listReady = false;
    }
    setBusyIndicatorVisible(false);

    if (code == ArchiveErrorCode::WrongPassword) {
        m_currentPassword.clear();
    }

    if (code == ArchiveErrorCode::Cancelled) {
        statusBar()->showMessage(msg.isEmpty() ? tr("Cancelled") : msg, 5000);
        if (ArchiveManager::instance().backend()) {
            m_listReady = true;
        }
        updateNavLocation();
        updateNavigationState();
        updateActionsState();
        finishDialogOnly();
        return;
    }

    if (code == ArchiveErrorCode::PartialExtraction
        || code == ArchiveErrorCode::HardLinkUnsupported) {
        MessageBoxUtils::showWarning(
            this,
            code == ArchiveErrorCode::PartialExtraction
                ? tr("Extraction Incomplete")
                : tr("Hard Link Not Supported"),
            msg);
        updateActionsState();
        updateStatusSummary();
        finishDialogOnly();
        return;
    }

    QString title = tr("Error");
    switch (code) {
    case ArchiveErrorCode::UnsupportedFormat:   title = tr("Unsupported Format"); break;
    case ArchiveErrorCode::WrongPassword:       title = tr("Wrong Password"); break;
    case ArchiveErrorCode::CorruptedArchive:    title = tr("Corrupted Archive"); break;
    case ArchiveErrorCode::ToolNotFound:        title = tr("Required Tool Not Found"); break;
    case ArchiveErrorCode::OperationNotSupported: title = tr("Operation Not Supported"); break;
    case ArchiveErrorCode::PasswordRequired:    title = tr("Password Required"); break;
    case ArchiveErrorCode::InsufficientDiskSpace: title = tr("Insufficient Disk Space"); break;
    case ArchiveErrorCode::PathTraversalBlocked: title = tr("Security: Path Traversal Blocked"); break;
    case ArchiveErrorCode::VolumeMissing:       title = tr("Volume Missing"); break;
    case ArchiveErrorCode::FileNotFound:        title = tr("File Not Found"); break;
    case ArchiveErrorCode::PermissionDenied:    title = tr("Permission Denied"); break;
    case ArchiveErrorCode::PathTooLong:         title = tr("Path Too Long"); break;
    case ArchiveErrorCode::EncodingError:       title = tr("Encoding Error"); break;
    case ArchiveErrorCode::PartialExtraction:   title = tr("Extraction Incomplete"); break;
    case ArchiveErrorCode::HardLinkUnsupported: title = tr("Hard Link Not Supported"); break;
    case ArchiveErrorCode::Unknown:             break;
    case ArchiveErrorCode::Cancelled:           break;
    }

    QMessageBox::critical(this, title, msg);

    if (!ArchiveManager::instance().backend()) {
        if (m_localBrowse) {
            // A failed open keeps the local listing usable.
            // 打开失败时保留本地列表。
            loadLocalDirectory(m_localBrowseDir, false);
        } else {
            showPlaceholder(tr("Unable to open archive.\n%1").arg(msg));
        }
    } else {
        // A failed attempt to open/reopen another archive leaves the previous
        // archive usable; its list is still valid.
        // 打开/重开另一个压缩包失败时，旧压缩包仍可用，其列表仍然有效。
        m_listReady = true;
    }
    updateNavLocation();
    updateNavigationState();
    updateActionsState();
    updateStatusSummary();
    finishDialogOnly();
}

void MainWindow::onOperationFinished(bool success, const QString &msg)
{
    if (m_progressDialog) {
        m_progressDialog->close();
        m_progressDialog = nullptr;
    }
    setBusyIndicatorVisible(false);

    const UiOp finishedOp = m_currentOp;
    m_currentOp = UiOp::None;

    if (!success) {
        // An external-edit write-back that failed keeps watching the file but
        // refreshes its baseline, so the user is not asked again immediately.
        // 失败的外部编辑写回仍然继续监视该文件，但会刷新基线，
        // 避免立刻重复询问用户。
        finishExternalSave(false);
        m_oneClickExtractActive = false;
        m_oneClickCloseAfter = false;
        m_compressCloseAfter = false;
        statusBar()->showMessage(msg.isEmpty() ? tr("Operation failed") : msg, 5000);
        updateNavLocation();
        updateNavigationState();
        updateActionsState();
        finishDialogOnly();
        return;
    }

    statusBar()->showMessage(msg, 5000);

    if (finishedOp == UiOp::Compress) {
        // The local listing may now contain the new archive.
        // 本地列表可能新增了刚创建的压缩包。
        if (m_localBrowse) {
            refreshLocalDirectory();
        }
        if (!m_lastCompressTarget.isEmpty()) {
            statusBar()->showMessage(
                tr("Created %1").arg(QDir::toNativeSeparators(m_lastCompressTarget)),
                8000);
        }
        if (m_openAfterCompress && !m_lastCompressTarget.isEmpty()) {
            // Explorer "add to ZIP"/"compress to" should reveal the result in
            // QArchiver after the operation succeeds.
            // 资源管理器“添加到 ZIP/压缩到”成功后在 QArchiver 中打开结果。
            const QString target = m_lastCompressTarget;
            m_openAfterCompress = false;
            m_compressCloseAfter = false;
            m_dialogOnly = false;
            m_lastCompressTarget.clear();
            // Defer to the event loop: this slot runs inside ArchiveManager's
            // operationFinished emission, and ArchiveManager still has to
            // clean up the finished backend/session after it returns.  Opening
            // the new archive synchronously would re-enter the manager while
            // it is still cleaning up the previous operation.
            // 推迟到事件循环：本槽在 ArchiveManager 的 operationFinished 发射
            // 过程中执行，而 ArchiveManager 返回后还要清理已完成的后端/会话。
            // 同步打开新压缩包会在它还没清理完时就重入管理器。
            QTimer::singleShot(0, this, [this, target]() {
                show();
                openArchivePath(target);
                updateStatusSummary();
                updateActionsState();
            });
            return;
        }
        if (m_compressCloseAfter) {
            m_compressCloseAfter = false;
            // Explorer menu / command-line compression is a one-shot task.
            // 资源管理器菜单/命令行压缩是一次性任务。
            if (m_dialogOnly) {
                finishDialogOnly();
            } else {
                QTimer::singleShot(500, this, [this]() { close(); });
            }
        }
        m_lastCompressTarget.clear();
    }

    if (finishedOp == UiOp::Extract && m_oneClickExtractActive) {
        const bool closeAfter = m_oneClickCloseAfter;
        m_oneClickExtractActive = false;
        m_oneClickCloseAfter = false;
        statusBar()->showMessage(
            tr("Extracted to %1").arg(m_lastExtractDest), 8000);

        if (closeAfter) {
            // Let the progress dialog disappear before closing the window.
            // 先关闭进度对话框，再关闭主窗口。
            if (m_dialogOnly) {
                finishDialogOnly();
            } else {
                QTimer::singleShot(650, this, [this]() { close(); });
            }
            updateActionsState();
            return;
        }
    }

    if (finishedOp == UiOp::Extract && m_openDestAfterExtract
        && !m_lastExtractDest.isEmpty()) {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastExtractDest))) {
            MessageBoxUtils::showOpenFailure(this, m_lastExtractDest);
        }
    }

    if (finishedOp == UiOp::Test) {
        QMessageBox::information(
            this, tr("Test Archive"),
            tr("The archive test completed successfully. No errors were found."));
    }

    // The write-back of an externally edited file succeeded.
    // 外部编辑文件的写回成功。
    finishExternalSave(true);

    if (finishedOp == UiOp::Add || finishedOp == UiOp::Delete) {
        ListOptions opts;
        if (!m_currentPassword.isEmpty()) {
            opts.password = m_currentPassword;
        }
        m_currentOp = UiOp::List;
        ArchiveManager::instance().listContents(opts);
    }

    updateStatusSummary();
    updateActionsState();
    finishDialogOnly();
}

void MainWindow::onSelectionChanged()
{
    updateActionsState();
    updateSelectionDetails();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // Esc aborts a drag preparation that is still running (the mouse button is
    // held down at that point, so a dialog button cannot be used).
    // Esc 中止仍在进行的拖拽准备（此时鼠标按键被按住，无法使用对话框按钮）。
    if (event->key() == Qt::Key_Escape && m_dragPrepareInProgress) {
        DragOutManager::instance().cancel();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && !m_searchText.isEmpty()) {
        setSearchText(QString());
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(
            QStringLiteral("application/x-qarchiver-entries"))) {
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        return;
    }
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasFormat(
            QStringLiteral("application/x-qarchiver-entries"))) {
        m_selfDropAttempted = true;
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        return;
    }
    if (!event->mimeData()->hasUrls()) {
        return;
    }

    const QList<QUrl> urls = event->mimeData()->urls();
    QStringList localFiles;
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            localFiles << url.toLocalFile();
        }
    }
    if (localFiles.isEmpty()) {
        return;
    }

    if (localFiles.size() == 1 && FormatRegistry::isArchivePath(localFiles.first())) {
        openFile(localFiles.first());
        event->acceptProposedAction();
        return;
    }

    if (ArchiveManager::instance().backend()) {
        addFilesToCurrentArchive(localFiles);
    } else {
        compressPathsWithDialog(localFiles);
    }
    event->acceptProposedAction();
}

void MainWindow::showEvent(QShowEvent *event)
{
    if (!m_initialPlacementDone) {
        m_initialPlacementDone = true;
        // Frameless windows carry no position of their own, so some window
        // managers (and Windows' default placement) drop them at an arbitrary
        // edge, often bottom-right.  Center the first show explicitly on the
        // primary screen's available area.  Later shows (restore, layout
        // switch) keep whatever position the user chose.
        // 无边框窗口自身没有位置，部分窗口管理器（以及 Windows 的默认放置）
        // 会把它丢到任意边缘，常见是右下角。首次显示时显式居中到主屏可用
        // 区域；之后的恢复/切换布局不再覆盖用户手动调整的位置。
        if (!isMaximized() && !isFullScreen()) {
            if (QScreen *screen = QGuiApplication::primaryScreen()) {
                const QRect available = screen->availableGeometry();
                const QSize windowSize = size();
                const int x = windowSize.width() >= available.width()
                    ? available.left()
                    : available.left()
                          + (available.width() - windowSize.width()) / 2;
                const int y = windowSize.height() >= available.height()
                    ? available.top()
                    : available.top()
                          + (available.height() - windowSize.height()) / 2;
                move(x, y);
            }
        }
    }
    QMainWindow::showEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_itemSizeSaveTimer && m_itemSizeSaveTimer->isActive()) {
        m_itemSizeSaveTimer->stop();
        AppSettings::instance().setItemSize(m_itemSize);
    }
    if (m_progressDialog) {
        m_progressDialog->close();
        m_progressDialog = nullptr;
    }
    ArchiveManager::instance().closeArchive();
    PreviewManager::instance().cleanupOwn();
    DragOutManager::instance().cleanupOwn();
    event->accept();
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::StyleChange) {
        updateSearchIcon();
    }
    if (event->type() == QEvent::WindowStateChange && m_titleBar) {
        m_titleBar->updateMaximizeState();
    }
    QMainWindow::changeEvent(event);
}
