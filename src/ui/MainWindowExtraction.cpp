// QArchiver / 小Q解压
// MainWindow extraction: extract/add flows, one-click extraction UI orchestration
// and the dialogs that drive them.  Destination planning lives in
// OneClickExtractController.
// MainWindow 解压：解压/添加流程、一键解压界面编排以及驱动它们的对话框。
// 目标目录规划位于 OneClickExtractController。

#include "common/FormatRegistry.h"

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
#include "ui/services/ExternalEditManager.h"
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



void MainWindow::openArchivePath(const QString &path)
{
    if (!path.isEmpty()) {
        openFile(path);
    }
}

void MainWindow::extractOneClickPath(const QString &path)
{
    m_oneClick.request();
    openFile(path);
}

QStringList MainWindow::chooseSourceItems(const QString &title)
{
    // Native file dialogs cannot select files and directories in one dialog.
    // Ask which kind to pick, then use the system dialog for that kind.  This
    // keeps the common path native (Windows Explorer / GTK / KDE) instead of
    // forcing the Qt widget dialog on every desktop.
    // 原生文件对话框无法同时选择文件与目录；先询问类型，再调用系统对话框，
    // 避免为了混合选择而在所有桌面强制使用 Qt 控件对话框。
    QMessageBox chooser(this);
    chooser.setWindowTitle(title);
    chooser.setIcon(QMessageBox::Question);
    chooser.setText(tr("What would you like to select?"));
    chooser.setInformativeText(
        tr("Files can be selected together; folders are added one at a time."));
    QPushButton *filesButton = chooser.addButton(
        tr("Select Files..."), QMessageBox::AcceptRole);
    QPushButton *folderButton = chooser.addButton(
        tr("Select Folder..."), QMessageBox::ActionRole);
    chooser.addButton(QMessageBox::Cancel);
    chooser.setDefaultButton(filesButton);
    chooser.exec();

    if (chooser.clickedButton() == filesButton) {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, title, QDir::homePath(), tr("All Files (*)"));
        return files;
    }
    if (chooser.clickedButton() == folderButton) {
        const QString folder = QFileDialog::getExistingDirectory(
            this, title, QDir::homePath(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        return folder.isEmpty() ? QStringList() : QStringList{folder};
    }
    return {};
}

void MainWindow::openFile(const QString &path)
{
    // Keep the current archive (and its password) until the new one opens
    // successfully.  onArchiveOpened() switches to the session password.
    // 在新压缩包成功打开前保留当前压缩包及其密码。
    ArchiveManager::instance().openArchive(path);
    updateActionsState();
}

void MainWindow::startExtract(const ExtractOptions &opts)
{
    ExtractOptions options = opts;
    if (!m_currentPassword.isEmpty()) {
        options.password = m_currentPassword;
    }

    if (options.overwrite == OverwritePolicy::Ask) {
        const QDir dest(options.destDir);
        const bool hasExisting = dest.exists()
            && !dest.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty();
        if (!hasExisting) {
            options.overwrite = OverwritePolicy::Always;
        } else {
            const auto answer = QMessageBox::question(
                this, tr("Overwrite Files"),
                tr("The destination directory is not empty. Overwrite existing files?"),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                QMessageBox::No);
            if (answer == QMessageBox::Cancel) {
                return;
            }
            options.overwrite = (answer == QMessageBox::Yes)
                ? OverwritePolicy::Always
                : OverwritePolicy::Never;
        }
    }

    m_openDestAfterExtract = options.openDestAfterFinish;
    m_lastExtractDest = options.destDir;
    m_currentOp = UiOp::Extract;

    ArchiveManager::instance().extract(options);
    if (ArchiveManager::instance().isBusy()) {
        showProgressDialog(tr("Extracting..."));
    }
}

void MainWindow::addFilesToCurrentArchive(const QStringList &files)
{
    ArchiveBackend *backend = ArchiveManager::instance().backend();
    if (!backend || files.isEmpty()) {
        return;
    }
    if (!ArchiveManager::instance().canModifyCurrentArchive()) {
        QMessageBox::information(this, tr("Add Files"),
                                 tr("This archive does not support adding files."));
        return;
    }

    AddFilesOptions options;
    options.archivePath = ArchiveManager::instance().currentPath();
    options.sourceFiles = files;
    options.password = m_currentPassword;
    options.compressionLevel = AppSettings::instance().defaultCompressionLevel();
    options.followSymlinks = AppSettings::instance().followSymlinks();

    m_currentOp = UiOp::Add;
    showProgressDialog(tr("Adding Files..."));
    ArchiveManager::instance().addFiles(options);
}

void MainWindow::setupExternalEditHandlers()
{
    ExternalEditManager &edits = ExternalEditManager::instance();

    edits.setPromptHandler([this](const ExternalEditManager::Job &job) {
        // The editor may be full screen; flash the task bar so the question is
        // noticed even when QArchiver is in the background.
        // 编辑器可能全屏；让任务栏闪烁，即使 QArchiver 在后台也能注意到提问。
        QApplication::alert(this);

        QMessageBox box(this);
        box.setWindowTitle(tr("Save Changes to Archive"));
        box.setIconPixmap(
            Icons::aero(QStringLiteral("warning.ico")).pixmap(32, 32));
        box.setText(tr("\"%1\" was changed in the external application.")
                        .arg(QFileInfo(job.entryPath).fileName()));
        box.setInformativeText(
            tr("Save the changes to the archive \"%1\"?\n\n"
               "The archive is updated in place. Choosing \"Don't Save\" "
               "leaves it unchanged.")
                .arg(QFileInfo(job.archivePath).fileName()));
        QPushButton *saveButton =
            box.addButton(tr("Save to Archive"), QMessageBox::AcceptRole);
        box.addButton(tr("Don't Save"), QMessageBox::RejectRole);
        box.setDefaultButton(saveButton);
        box.exec();
        return box.clickedButton() == saveButton
            ? ExternalEditManager::Decision::Save
            : ExternalEditManager::Decision::Discard;
    });

    edits.setSaveHandler(
        [this](const ExternalEditManager::Job &job,
               const AddFilesOptions &options) {
            m_externalSaveRetriesLeft = 60;
            startExternalEditSave(options, job.localPath);
        });
}

void MainWindow::startExternalEditSave(const AddFilesOptions &options,
                                       const QString &localPath)
{
    ArchiveManager &manager = ArchiveManager::instance();

    if (manager.isBusy()) {
        // Another archive operation is still running: retry shortly instead of
        // doing nothing, so the user's confirmation is not lost.
        // 另一个压缩包操作仍在运行：稍后重试，避免丢失用户的确认。
        if (m_externalSaveRetriesLeft-- <= 0) {
            ExternalEditManager::instance().reportSaveFailed(localPath);
            MessageBoxUtils::showWarning(
                this, tr("Save Changes to Archive"),
                tr("The archive is busy. Open the file again and save it once "
                   "the current operation has finished."));
            return;
        }
        QTimer::singleShot(500, this, [this, options, localPath]() {
            startExternalEditSave(options, localPath);
        });
        return;
    }

    if (!manager.backend() || manager.currentPath() != options.archivePath) {
        // The archive was closed or replaced: reopen it and continue from
        // onFileListReady().
        // 压缩包已关闭或被替换：重新打开，并在 onFileListReady() 中继续。
        m_queuedExternalSave = options;
        m_queuedExternalSaveLocalPath = localPath;
        openArchivePath(options.archivePath);
        return;
    }

    m_externalSaveLocalPath = localPath;
    m_currentOp = UiOp::Add;
    showProgressDialog(tr("Saving Changes to Archive..."));
    manager.addFiles(options);
    updateActionsState();
}

void MainWindow::dispatchQueuedExternalSave()
{
    if (m_queuedExternalSave.archivePath.isEmpty()) {
        return;
    }
    const AddFilesOptions options = m_queuedExternalSave;
    const QString localPath = m_queuedExternalSaveLocalPath;
    m_queuedExternalSave = AddFilesOptions{};
    m_queuedExternalSaveLocalPath.clear();
    startExternalEditSave(options, localPath);
}

void MainWindow::finishExternalSave(bool success)
{
    if (m_externalSaveLocalPath.isEmpty()) {
        return;
    }
    const QString localPath = m_externalSaveLocalPath;
    m_externalSaveLocalPath.clear();

    if (success) {
        ExternalEditManager::instance().confirmSaved(localPath);
        statusBar()->showMessage(tr("Changes saved to the archive"), 5000);
        return;
    }
    ExternalEditManager::instance().reportSaveFailed(localPath);
}

void MainWindow::onExtractAll()
{
    if (!ArchiveManager::instance().backend()) {
        return;
    }

    ExtractDialog dlg(ArchiveManager::instance().currentPath(), this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    startExtract(dlg.options());
}

void MainWindow::onExtractSelected()
{
    const QStringList paths = selectedEntryPaths();
    if (paths.isEmpty() || !ArchiveManager::instance().backend()) {
        return;
    }

    ExtractDialog dlg(ArchiveManager::instance().currentPath(), this);
    dlg.setSelectedFiles(paths);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    startExtract(dlg.options());
}

bool MainWindow::askOneClickClosePreference()
{
    // Shell integration never keeps a hidden window around: close as soon as
    // the extraction finishes.
    // 外壳集成模式不会保留隐藏窗口：解压完成后立即退出。
    if (m_dialogOnly) {
        return true;
    }
    AppSettings &settings = AppSettings::instance();
    if (settings.oneClickCloseAsked()) {
        return settings.oneClickCloseAfterExtract();
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("One-click Extract"));
    box.setIconPixmap(Icons::modern(QStringLiteral("oneclick.png")).pixmap(48, 48));
    box.setText(tr("Close QArchiver after one-click extraction finishes?"));
    box.setInformativeText(
        tr("The archive is extracted to its own folder. You can change this "
           "behaviour later in Preferences → One-click Extract."));
    QPushButton *closeButton = box.addButton(tr("Extract and Close"),
                                             QMessageBox::AcceptRole);
    box.addButton(tr("Keep Window Open"), QMessageBox::RejectRole);
    // Emphasise the recommended "extract and close" choice with the accent
    // colour; QMessageBox styles the default button as primary.
    // 用强调色突出推荐的“解压后关闭”；QMessageBox 会把默认按钮画成主按钮。
    box.setDefaultButton(closeButton);
    // QMessageBox overrides its own minimumWidth from the layout; widen the
    // two labels instead so the informative text does not wrap into a column.
    // QMessageBox 会用布局覆盖自身 minimumWidth；改为加宽文本标签，
    // 避免说明文字被挤成窄列。
    box.setStyleSheet(
        "QLabel#qt_msgbox_label, QLabel#qt_msgbox_informativelabel "
        "{ min-width: 460px; }");
    box.exec();

    const bool closeAfter = box.clickedButton() == closeButton;
    settings.setOneClickCloseAfterExtract(closeAfter);
    settings.setOneClickCloseAsked(true);
    return closeAfter;
}

void MainWindow::beginOneClickExtract()
{
    auto &manager = ArchiveManager::instance();
    if (!manager.backend()) {
        // A shell --oneclick invocation owns a hidden window; never leave it
        // running when there is no archive backend to extract with.
        // 外壳 --oneclick 使用隐藏窗口；没有可用后端时必须退出，不能留下进程。
        if (m_dialogOnly) {
            finishDialogOnly();
        }
        return;
    }
    if (manager.isBusy()) {
        return;
    }
    if (!m_listReady) {
        m_oneClick.request();
        statusBar()->showMessage(tr("Reading archive contents..."), 4000);
        return;
    }

    m_oneClick.clearPending();
    const bool closeAfter = askOneClickClosePreference();

    const auto failPreflight = [this](const QString &message) {
        MessageBoxUtils::showWarning(this, tr("One-click Extract"), message);
        if (m_dialogOnly) {
            finishDialogOnly();
        }
    };

    // One-click extraction applies to the current selection.  With no
    // selection the whole archive is extracted.
    // 一键解压处理当前选择；没有选择时解压整个压缩包。
    const OneClickExtractController::Plan plan = m_oneClick.plan(
        manager.currentPath(), selectedEntryPaths(), m_currentPassword,
        closeAfter);
    if (!plan.ok) {
        failPreflight(plan.errorMessage);
        return;
    }

    m_oneClick.start(plan.closeAfter);
    m_openDestAfterExtract = plan.openDestAfterFinish;
    m_lastExtractDest = plan.destination;
    m_currentOp = UiOp::Extract;

    statusBar()->showMessage(
        tr("One-click extracting to %1").arg(plan.destination), 8000);
    manager.extract(plan.options);
    if (manager.isBusy()) {
        showProgressDialog(tr("One-click Extract..."));
    }
    updateActionsState();
}

void MainWindow::onOneClickExtract()
{
    if (ArchiveManager::instance().isBusy()) {
        return;
    }

    if (ArchiveManager::instance().backend()) {
        m_oneClick.request();
        if (m_listReady) {
            beginOneClickExtract();
        } else {
            statusBar()->showMessage(tr("Reading archive contents..."), 4000);
        }
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, tr("One-click Extract"), QString(), MainWindowHelpers::archiveDialogFilter());
    if (path.isEmpty()) {
        return;
    }
    m_oneClick.request();
    openFile(path);
}
