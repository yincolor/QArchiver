// QArchiver / 小Q解压
// MainWindow action handlers: command implementations and action state.
// QAction/menu construction lives in MainWindowMenu.cpp.
// MainWindow 动作处理：命令实现与动作状态。QAction/菜单构建位于
// MainWindowMenu.cpp。

#include "ui/MainWindow.h"
#include "ui/MainWindow_p.h"

#include "ui/MainWindowHelpers.h"
#include "ui/icons/IconUtils.h"

#include "ui/models/FileListModel.h"
#include "ui/models/FilterProxyModel.h"
#include "ui/models/FileListViewDelegate.h"
#include "ui/services/PreviewManager.h"
#include "ui/services/ThumbnailManager.h"
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

    if (!archivePath.isEmpty()) {
        PreviewManager::instance().clearArchiveCache(archivePath);
    }
    m_previewArchivePath.clear();
    ArchiveManager::instance().closeArchive();
    // Thumbnail and staged drag data belong to the archive being closed.
    // 缩略图与拖出暂存数据都属于正在关闭的压缩包。
    ThumbnailManager::instance().clearArchiveCache();
    DragOutManager::instance().clearCache();
    m_model->setEntries({});
    m_model->setArchiveContext(QString(), ArchiveFormat::Unknown, QString());
    m_listReady = false;
    m_oneClick.reset();
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
