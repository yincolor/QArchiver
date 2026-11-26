// QArchiver / 小Q解压
// MainWindow actions: QAction setup, menus, tool button style, state updates and
// the menu/toolbar command handlers.
// MainWindow 动作：QAction 构建、菜单、工具栏按钮样式、状态更新以及
// 菜单/工具栏命令处理。

#include "ui/MainWindow.h"

#include "ui/MainWindowHelpers.h"
#include "ui/icons/IconUtils.h"

#include "ui/models/FileListModel.h"
#include "ui/models/FilterProxyModel.h"
#include "ui/models/FileListViewDelegate.h"
#include "ui/services/PreviewManager.h"
#include "ui/services/PreviewUtils.h"
#include "ui/services/DragOutManager.h"
#include "ui/services/UpdateChecker.h"
#include "ui/widgets/ArchiveTreeView.h"
#include "ui/widgets/ArchiveGridView.h"
#include "ui/widgets/BackgroundWidget.h"
#include "ui/widgets/SidebarWidget.h"
#include "ui/widgets/ModernToolBar.h"
#include "ui/widgets/ImmersiveTitleBar.h"
#include "ui/widgets/ModernNavBar.h"
#include "ui/widgets/ArchiveFolderPane.h"
#include "ui/widgets/WelcomePage.h"
#include "ui/dialogs/ExtractDialog.h"
#include "ui/dialogs/CompressDialog.h"
#include "ui/dialogs/PasswordDialog.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/PreferencesDialog.h"
#include "ui/dialogs/BenchmarkDialog.h"
#include "ui/dialogs/FileAssociationDialog.h"
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
#include <QUrl>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <limits>

void MainWindow::openPreferences()
{
    onPreferences();
}

void MainWindow::openBenchmark()
{
    onBenchmark();
}

void MainWindow::checkSevenZipFallback()
{
    static bool started = false;
    if (started) {
        return;
    }
    started = true;

    // Probe the fallback CLI off the GUI thread so --help/codec detection
    // cannot delay the first paint.
    // 在 GUI 线程之外探测兜底 CLI，避免 --help/编解码器检测阻塞首帧。
    auto *watcher = new QFutureWatcher<SevenZipToolInfo>(this);
    connect(watcher, &QFutureWatcher<SevenZipToolInfo>::finished, this,
            [this, watcher]() {
                const SevenZipToolInfo info = watcher->result();
                watcher->deleteLater();
                showSevenZipFallbackInfo(info);
            });
    watcher->setFuture(QtConcurrent::run([]() { return SevenZipTool::detect(); }));
}

void MainWindow::showSevenZipFallbackInfo(const SevenZipToolInfo &info)
{
    if (info.source == SevenZipToolInfo::Source::Bundled
        || info.source == SevenZipToolInfo::Source::Override) {
        return;
    }

    if (info.source == SevenZipToolInfo::Source::System) {
        const QString rarStatus = info.supportsRar
            ? tr("RAR extraction: supported by this system 7-Zip.")
            : tr("RAR extraction: NOT supported by this system 7-Zip. "
                 "On Debian/Ubuntu install the '7zip-rar' package "
                 "(older releases: 'p7zip-rar') to enable it.");

        QStringList dirs = info.bundledDirectories;
        if (dirs.isEmpty()) {
            dirs << QDir(QCoreApplication::applicationDirPath())
                        .filePath(QStringLiteral("7zip"));
        }

        QMessageBox::warning(
            this, tr("System 7-Zip Fallback"),
            tr("The bundled 7-Zip command-line tool was not found in the "
               "program directories:\n%1\n\n"
               "QArchiver is using the system 7-Zip instead:\n%2\n\n"
               "%3\n\n"
               "For a self-contained installation, download the official "
               "7-Zip command-line tool and place it in the '7zip' directory "
               "next to QArchiver.")
                .arg(dirs.join(QLatin1Char('\n')), info.path, rarStatus));
        return;
    }

    QString targetDir = info.bundledDirectories.value(0);
    if (targetDir.isEmpty()) {
        targetDir = QDir(QCoreApplication::applicationDirPath())
                        .filePath(QStringLiteral("7zip"));
    }

#ifdef Q_OS_WIN
    const QString hint = tr(
        "Download the official 7-Zip command-line tool from "
        "https://www.7-zip.org/download.html and place 7z.exe together with "
        "the matching 7z.dll in:\n%1\n\nThen restart QArchiver.").arg(targetDir);
#else
    const QString hint = tr(
        "Fix this in one of these ways:\n\n"
        "1. Download the official 7-Zip command-line tool from "
        "https://www.7-zip.org/download.html and place the executable in:\n"
        "%1\n\n"
        "2. On Debian/Ubuntu, install the system packages:\n"
        "   sudo apt update\n"
        "   sudo apt install 7zip\n"
        "   sudo apt install 7zip-rar\n"
        "   (older releases: sudo apt install p7zip-full p7zip-rar)\n\n"
        "Then restart QArchiver.").arg(targetDir);
#endif
    QMessageBox::critical(
        this, tr("No 7-Zip Fallback"),
        tr("No usable 7-Zip command-line tool was found in the program "
           "directories or in PATH.\n\n"
           "QArchiver uses 7-Zip as the fallback backend for archive formats "
           "and operations the built-in backend cannot handle. Without it, "
           "some archives cannot be opened, extracted or created.\n\n%1")
            .arg(hint));
}

void MainWindow::setupActions()
{
    // Classic-layout navigation.  The modern layout uses ModernNavBar's own
    // buttons; these actions are added to the classic filter row only.
    // 经典布局导航。现代布局使用 ModernNavBar 自带的按钮；
    // 这三个动作只加入经典布局的筛选行。
    m_backAct = new QAction(tr("Back"), this);
    m_backAct->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    connect(m_backAct, &QAction::triggered, this, &MainWindow::onNavBack);

    m_forwardAct = new QAction(tr("Forward"), this);
    m_forwardAct->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
    connect(m_forwardAct, &QAction::triggered, this, &MainWindow::onNavForward);

    m_upAct = new QAction(tr("Up"), this);
    m_upAct->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    connect(m_upAct, &QAction::triggered, this, &MainWindow::onNavUp);

    m_openAct = new QAction(tr("Open"), this);
    m_openAct->setShortcut(QKeySequence::Open);
    connect(m_openAct, &QAction::triggered, this, &MainWindow::onOpen);

    m_newAct = new QAction(tr("Compress"), this);
    m_newAct->setShortcut(QKeySequence::New);
    m_newAct->setToolTip(tr("Create a new archive (Ctrl+N)"));
    connect(m_newAct, &QAction::triggered, this, &MainWindow::onNewArchive);

    m_extractAct = new QAction(tr("Extract To..."), this);
    m_extractAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(m_extractAct, &QAction::triggered, this, &MainWindow::onExtractAll);

    m_oneClickAct = new QAction(tr("One-click Extract"), this);
    m_oneClickAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    connect(m_oneClickAct, &QAction::triggered,
            this, &MainWindow::onOneClickExtract);

    m_testAct = new QAction(tr("Test"), this);
    m_testAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(m_testAct, &QAction::triggered, this, &MainWindow::onTestArchive);

    m_addAct = new QAction(tr("Add"), this);
    m_addAct->setToolTip(tr("Add files to the current archive"));
    connect(m_addAct, &QAction::triggered, this, &MainWindow::onAddFiles);

    m_deleteAct = new QAction(tr("Delete"), this);
    m_deleteAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Delete));
    connect(m_deleteAct, &QAction::triggered,
            this, &MainWindow::onDeleteSelected);

    m_closeAct = new QAction(tr("Close"), this);
    m_closeAct->setShortcut(QKeySequence::Close);
    m_closeAct->setToolTip(tr("Close the current archive"));
    connect(m_closeAct, &QAction::triggered, this, &MainWindow::onCloseArchive);

    m_prefsAct = new QAction(tr("Preferences"), this);
    connect(m_prefsAct, &QAction::triggered, this, &MainWindow::onPreferences);

    m_aboutAct = new QAction(tr("About"), this);
    connect(m_aboutAct, &QAction::triggered, this, &MainWindow::onAbout);

    m_checkUpdatesAct = new QAction(tr("Check for Updates..."), this);
    connect(m_checkUpdatesAct, &QAction::triggered,
            this, &MainWindow::onCheckUpdates);

    m_quitAct = new QAction(tr("Quit"), this);
    m_quitAct->setShortcut(QKeySequence::Quit);
    connect(m_quitAct, &QAction::triggered, qApp, &QApplication::quit);

    applyActionIcons();
}

void MainWindow::applyActionIcons()
{
    const auto classic = [](const QString &name) {
        return Icons::aero(name);
    };
    const auto modern = [](const QString &name) {
        return Icons::modern(name);
    };

    if (m_modernUi) {
        m_backAct->setIcon(modern(QStringLiteral("back.png")));
        m_forwardAct->setIcon(modern(QStringLiteral("forward.png")));
        m_upAct->setIcon(modern(QStringLiteral("up.png")));
        m_openAct->setIcon(modern(QStringLiteral("open.png")));
        m_newAct->setIcon(modern(QStringLiteral("compress.png")));
        m_extractAct->setIcon(modern(QStringLiteral("extract.png")));
        m_oneClickAct->setIcon(modern(QStringLiteral("oneclick.png")));
        m_testAct->setIcon(modern(QStringLiteral("test.png")));
        m_addAct->setIcon(modern(QStringLiteral("add.png")));
        m_deleteAct->setIcon(modern(QStringLiteral("delete.png")));
        m_closeAct->setIcon(modern(QStringLiteral("close.png")));
        m_prefsAct->setIcon(modern(QStringLiteral("settings.png")));
        m_checkUpdatesAct->setIcon(modern(QStringLiteral("refresh.png")));
        m_aboutAct->setIcon(modern(QStringLiteral("help.png")));
        m_quitAct->setIcon(modern(QStringLiteral("close.png")));
    } else {
        m_backAct->setIcon(classic(QStringLiteral("left.ico")));
        m_forwardAct->setIcon(classic(QStringLiteral("right.ico")));
        m_upAct->setIcon(classic(QStringLiteral("up.ico")));
        m_openAct->setIcon(classic(QStringLiteral("Open.png")));
        m_newAct->setIcon(classic(QStringLiteral("New.png")));
        m_extractAct->setIcon(classic(QStringLiteral("Extract.png")));
        m_oneClickAct->setIcon(classic(QStringLiteral("Extract.png")));
        m_testAct->setIcon(classic(QStringLiteral("test.png")));
        m_addAct->setIcon(classic(QStringLiteral("add.png")));
        m_deleteAct->setIcon(classic(QStringLiteral("Close.png")));
        m_closeAct->setIcon(classic(QStringLiteral("Close.png")));
        m_prefsAct->setIcon(classic(QStringLiteral("Preferences.png")));
        m_checkUpdatesAct->setIcon(classic(QStringLiteral("Open.png")));
        m_aboutAct->setIcon(classic(QStringLiteral("About.png")));
        m_quitAct->setIcon(classic(QStringLiteral("Quit.png")));
    }

    for (QAction *action : {m_backAct, m_forwardAct, m_upAct,
                            m_openAct, m_newAct, m_extractAct, m_oneClickAct,
                            m_testAct, m_addAct, m_deleteAct, m_closeAct,
                            m_prefsAct, m_checkUpdatesAct, m_aboutAct, m_quitAct}) {
        if (!action) {
            continue;
        }
        QString tip = action->text();
        if (action->shortcut().isEmpty()) {
            action->setToolTip(tip);
        } else {
            tip += QStringLiteral(" (%1)")
                       .arg(action->shortcut().toString(QKeySequence::NativeText));
            action->setToolTip(tip);
        }
    }
}

void MainWindow::setupMenuBar()
{
    // The immersive title bar always hosts the menus; never fall back to
    // QMainWindow::menuBar(), which would replace the title bar in the
    // menu-widget slot with a lazily created QMenuBar.
    // 菜单始终由沉浸式标题栏承载；不能回退到 QMainWindow::menuBar()，
    // 否则它会用一个新建的 QMenuBar 顶掉 menuWidget 槽位里的标题栏。
    Q_ASSERT(m_titleBar);
    if (!m_titleBar) {
        return;
    }
    QMenuBar *bar = m_titleBar->menuBarWidget();
    bar->clear();
    bar->setNativeMenuBar(false);

    auto *fileMenu = bar->addMenu(tr("&File"));
    fileMenu->addAction(m_openAct);
    fileMenu->addAction(m_newAct);
    fileMenu->addSeparator();

    auto *startPageAction = fileMenu->addAction(tr("Start Page"));
    connect(startPageAction, &QAction::triggered, this, [this]() {
        // Showing the start page must not leave an archive loaded behind it;
        // close the archive first so the folder pane and window title match
        // the visible page.
        // 显示起始页时不能把压缩包留在后台：先关闭压缩包，使侧栏和标题栏与
        // 可见页面一致。
        if (ArchiveManager::instance().isBusy()) {
            return;
        }
        if (ArchiveManager::instance().backend()) {
            onCloseArchive();
            if (ArchiveManager::instance().backend()) {
                return;
            }
        }
        showStartPage();
    });

    auto *browseLocalAction = fileMenu->addAction(tr("Browse Local Files..."));
    connect(browseLocalAction, &QAction::triggered, this, [this]() {
        const QString startDir = m_localBrowse
            ? m_localBrowseDir
            : AppSettings::instance().localBrowsePath();
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Browse Local Files"), startDir,
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!dir.isEmpty()) {
            loadLocalDirectory(dir, false);
        }
    });

    fileMenu->addSeparator();
    fileMenu->addAction(m_closeAct);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAct);

    auto *actionMenu = bar->addMenu(tr("&Actions"));
    actionMenu->addAction(m_extractAct);
    actionMenu->addAction(m_oneClickAct);
    actionMenu->addSeparator();
    actionMenu->addAction(m_addAct);
    actionMenu->addAction(m_deleteAct);
    actionMenu->addSeparator();
    actionMenu->addAction(m_testAct);

    auto *toolsMenu = bar->addMenu(tr("&Tools"));
    auto *benchmarkAction = toolsMenu->addAction(
        Icons::modern(QStringLiteral("toolbox.png")), tr("7-Zip Benchmark..."));
    connect(benchmarkAction, &QAction::triggered,
            this, &MainWindow::onBenchmark);
    auto *associationsAction = toolsMenu->addAction(
        Icons::modern(QStringLiteral("settings.png")),
        tr("File Associations..."));
    connect(associationsAction, &QAction::triggered, this, [this]() {
        FileAssociationDialog dialog(this);
        dialog.exec();
    });
    toolsMenu->addAction(m_prefsAct);

    auto *helpMenu = bar->addMenu(tr("&Help"));
    helpMenu->addAction(m_checkUpdatesAct);
    helpMenu->addSeparator();
    helpMenu->addAction(m_aboutAct);

    bar->show();
}

void MainWindow::applyToolButtonStyle()
{
    if (!m_mainToolBar) {
        return;
    }
    const QString style = AppSettings::instance().toolButtonStyle();
    Qt::ToolButtonStyle buttonStyle = Qt::ToolButtonTextUnderIcon;
    if (style == QStringLiteral("textBesideIcon")) {
        buttonStyle = Qt::ToolButtonTextBesideIcon;
    } else if (style == QStringLiteral("textOnly")) {
        buttonStyle = Qt::ToolButtonTextOnly;
    } else if (style == QStringLiteral("iconOnly")) {
        buttonStyle = Qt::ToolButtonIconOnly;
    }
    m_mainToolBar->setToolButtonStyle(buttonStyle);
}

void MainWindow::applySidebarVisibility()
{
    if (m_sidebarDock) {
        m_sidebarDock->setVisible(AppSettings::instance().showRecentFiles());
    }
}

void MainWindow::updateActionsState()
{
    ArchiveBackend *backend = ArchiveManager::instance().backend();
    const bool busy = ArchiveManager::instance().isBusy();
    const bool hasArchive = (backend != nullptr);
    const bool localBrowse = m_localBrowse && !hasArchive;
    const bool canModify = hasArchive
        && ArchiveManager::instance().canModifyCurrentArchive();
    const bool hasSelection = !selectedEntryPaths().isEmpty();

    m_openAct->setEnabled(!busy);
    m_newAct->setEnabled(!busy);
    m_extractAct->setEnabled(hasArchive && !busy);
    m_oneClickAct->setEnabled(!busy);
    m_testAct->setEnabled(hasArchive && !busy);
    m_addAct->setEnabled(canModify && !busy);
    m_deleteAct->setEnabled(canModify && hasSelection && !busy);
    m_closeAct->setEnabled(hasArchive && !busy);
    m_prefsAct->setEnabled(!busy);
    m_aboutAct->setEnabled(true);
    m_quitAct->setEnabled(true);

    // The classic toolbar shows the same actions; only the compress tooltip
    // changes between "create archive" and "compress the current selection".
    // 经典工具栏共用这些动作；只有压缩按钮的提示在“新建压缩包”与
    // “压缩当前选择”之间切换。
    if (m_newAct) {
        m_newAct->setToolTip(
            localBrowse && hasSelection
                ? tr("Compress the selected items (Ctrl+N)")
                : tr("Create a new archive (Ctrl+N)"));
    }

    updateNavigationState();
}

void MainWindow::onOpen()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Archive"), QString(), MainWindowHelpers::archiveDialogFilter());
    if (!path.isEmpty()) {
        openFile(path);
    }
}

void MainWindow::onNewArchive()
{
    compressPathsWithDialog({});
}

void MainWindow::onTestArchive()
{
    if (!ArchiveManager::instance().backend()) {
        return;
    }
    TestOptions options;
    options.password = m_currentPassword;
    m_currentOp = UiOp::Test;
    showProgressDialog(tr("Testing Archive..."));
    ArchiveManager::instance().testArchive(options);
}

void MainWindow::onAddFiles()
{
    if (!ArchiveManager::instance().backend()) {
        return;
    }
    const QStringList items = chooseSourceItems(tr("Select Files to Add"));
    if (items.isEmpty()) {
        return;
    }
    addFilesToCurrentArchive(items);
}

void MainWindow::onDeleteSelected()
{
    ArchiveBackend *backend = ArchiveManager::instance().backend();
    const QStringList paths = selectedEntryPaths();
    if (!backend || paths.isEmpty()) {
        return;
    }
    if (!ArchiveManager::instance().canModifyCurrentArchive()) {
        QMessageBox::information(this, tr("Delete Files"),
                                 tr("This archive does not support deleting files."));
        return;
    }

    const auto answer = QMessageBox::question(
        this, tr("Delete Selected Entries"),
        tr("Delete %1 selected entr%2 from the archive?\n\nThis cannot be undone.")
            .arg(paths.size())
            .arg(paths.size() == 1 ? QStringLiteral("y") : QStringLiteral("ies")),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    DeleteFilesOptions options;
    options.archivePath = ArchiveManager::instance().currentPath();
    options.targetEntries = paths;
    options.password = m_currentPassword;

    m_currentOp = UiOp::Delete;
    showProgressDialog(tr("Deleting Entries..."));
    ArchiveManager::instance().deleteFiles(options);
}

void MainWindow::onPreferences()
{
    const QString oldLayout = AppSettings::instance().uiLayoutMode();
    PreferencesDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const QString newLayout = AppSettings::instance().uiLayoutMode();
    bool layoutChanged = newLayout != oldLayout;
    if (layoutChanged && ArchiveManager::instance().isBusy()) {
        // Rebuilding the views while an operation is running would drop the
        // progress dialog's view of the archive.  Keep the old layout.
        // 操作进行中重建视图会丢失进度对话框的上下文，因此保留旧布局。
        AppSettings::instance().setUiLayoutMode(oldLayout);
        ThemeManager::applyTheme(*qApp);
        MessageBoxUtils::showWarning(
            this, tr("Preferences"),
            tr("The layout can only be switched while no archive operation is "
               "running. The previous layout was kept."));
        layoutChanged = false;
    }

    if (!AppSettings::instance().rememberPasswords()) {
        m_sessionPassword.clear();
    }

    if (layoutChanged) {
        teardownUi();
        buildUi();
    } else {
        applyContentViewMode();
        applyToolButtonStyle();
        applySidebarVisibility();
        updateSearchIcon();
        updateStatusSummary();
        updateSelectionDetails();
        updateNavLocation();
        updateNavigationState();
    }
    updateActionsState();
}

void MainWindow::onBenchmark()
{
    BenchmarkDialog dlg(this);
    dlg.exec();
}

void MainWindow::onCloseArchive()
{
    if (ArchiveManager::instance().isBusy()) {
        return;
    }
    const QString archivePath = ArchiveManager::instance().currentPath();

    ArchiveManager::instance().closeArchive();
    // Staged drag data belongs to the archive that is being closed.
    DragOutManager::instance().clearCache();
    m_model->setEntries({});
    m_model->setArchiveContext(QString(), ArchiveFormat::Unknown, QString());
    m_listReady = false;
    m_oneClickPending = false;
    m_oneClickExtractActive = false;
    m_oneClickCloseAfter = false;
    setSearchText(QString());

    m_currentPassword.clear();
    if (!AppSettings::instance().rememberPasswords()) {
        m_sessionPassword.clear();
    }

    setBusyIndicatorVisible(false);
    // Closing an archive returns to the folder it came from; with no archive
    // the window browses local directories instead of showing a blank page.
    // 关闭压缩包后回到它所在的文件夹；没有压缩包时窗口浏览本地目录，
    // 而不是显示空白页。
    QString directory = archivePath.isEmpty()
        ? AppSettings::instance().localBrowsePath()
        : QFileInfo(archivePath).absolutePath();
    if (!QFileInfo(directory).isDir()) {
        directory = AppSettings::instance().localBrowsePath();
    }
    loadLocalDirectory(directory, false);
    statusBar()->showMessage(tr("Archive closed"), 3000);
    updateStatusSummary();
    updateActionsState();
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About QArchiver"),
        tr("<h3>QArchiver (小Q解压)</h3>"
           "<p>A lightweight Qt-based archive manager for Linux and Windows.</p>"
           "<p>The modern interface follows HaoZip: flat toolbar, folder and "
           "details panes, one-click extraction and the classic layout in "
           "Preferences. No ads, no bundled security scanners.</p>"
           "<p>Supports: tar, tar.gz, tar.xz, tar.bz2, zip, 7z, rar, gz, bz2, xz</p>"
           "<p>Version: %1</p>")
            .arg(QCoreApplication::applicationVersion()));
}

void MainWindow::onCheckUpdates()
{
    UpdateChecker &checker = UpdateChecker::instance();
    disconnect(&checker, nullptr, this, nullptr);
    connect(&checker, &UpdateChecker::updateAvailable, this,
            [this](const QString &version, const QString &releaseUrl,
                   const QString &notes) {
        QMessageBox box(this);
        box.setWindowTitle(tr("Update Available"));
        box.setText(tr("QArchiver %1 is available. You are using %2.")
                        .arg(version, QCoreApplication::applicationVersion()));
        box.setTextFormat(Qt::PlainText);
        if (!notes.isEmpty()) {
            box.setInformativeText(notes);
        }
        QPushButton *openButton = box.addButton(
            tr("Open Download Page"), QMessageBox::AcceptRole);
        box.addButton(tr("Later"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == openButton) {
            QDesktopServices::openUrl(QUrl(releaseUrl));
        }
    });
    connect(&checker, &UpdateChecker::upToDate, this,
            [this](const QString &current) {
        QMessageBox::information(
            this, tr("Check for Updates"),
            tr("You are already using the latest version (%1).").arg(current));
    });
    connect(&checker, &UpdateChecker::checkFailed, this,
            [this](const QString &message) {
        QMessageBox::warning(this, tr("Check for Updates"),
                             tr("Unable to check for updates:\n%1").arg(message));
    });
    checker.check(false);
}
