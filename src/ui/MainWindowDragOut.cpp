// QArchiver / 小Q解压
// MainWindow drag-out: stage selected entries as files and start a standard
// file URL drag.
// MainWindow 拖出：把选中条目暂存为文件并启动标准文件 URL 拖拽。

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




// Drag out: stage the selected entries as real files, then start a standard
// file URL drag.  The drop target (usually a file manager) performs the copy.
// 拖出：把选中条目暂存为真实文件，然后启动标准文件 URL 拖拽。
// 复制动作由放置目标（通常是文件管理器）执行。
namespace {

// Drag payload that reports who reads it: this is the only way to tell a
// rejected drop (the target never asks for data) from a broken URL list.
// 可报告谁读取了数据的拖拽负载：这是区分“放置被拒绝”（目标从未请求数据）
// 与“URL 列表损坏”的唯一方式。
class LoggingMimeData : public QMimeData {
public:
    explicit LoggingMimeData(const QStringList &entryPaths)
        : m_entryPaths(entryPaths) {}

protected:
    QVariant retrieveData(const QString &mimeType, QMetaType type) const override
    {
        if (m_reporting) {
            return QMimeData::retrieveData(mimeType, type);
        }
        m_reporting = true;
        const QVariant value = QMimeData::retrieveData(mimeType, type);
        m_reporting = false;
        LOG_INFO("DragOut", "drop target requested mime '%s' (entries=%lld, %s)",
                 mimeType.toUtf8().constData(),
                 static_cast<long long>(m_entryPaths.size()),
                 value.isValid() ? "served" : "empty");
        return value;
    }

private:
    QStringList m_entryPaths;
    mutable bool m_reporting = false;
};

} // namespace

void MainWindow::onEntriesDragOut(const QStringList &paths)
{
    auto &manager = ArchiveManager::instance();
    if (m_dragPrepareInProgress) {
        LOG_WARN("DragOut", "another drag preparation is still running, ignored");
        return;
    }
    if (paths.isEmpty()) {
        LOG_WARN("DragOut", "drag requested without selected entries");
        return;
    }
    if (!manager.backend()) {
        LOG_WARN("DragOut", "drag requested without an open archive");
        return;
    }
    if (manager.isBusy()) {
        LOG_WARN("DragOut", "drag requested while the archive manager is busy");
        return;
    }

    const QString archivePath = manager.currentPath();
    const ArchiveFormat format = manager.currentFormat();
    auto &dragOut = DragOutManager::instance();

    LOG_INFO("DragOut", "drag out: %lld entr%s from %s",
             static_cast<long long>(paths.size()),
             paths.size() == 1 ? "y" : "ies",
             archivePath.toUtf8().constData());

    DragOutPayload payload;
    if (dragOut.hasPreparedFor(archivePath, format, paths, m_currentPassword)) {
        LOG_INFO("DragOut", "reusing cached staging for this selection");
        payload = dragOut.takePreparedFor(archivePath, format, paths,
                                          m_currentPassword);
    } else {
        m_dragPrepareInProgress = true;

        const QList<ArchiveEntry> entries = m_model->sourceEntries();
        QString password = m_currentPassword;

        QApplication::setOverrideCursor(Qt::BusyCursor);
        setBusyIndicatorVisible(true, tr("Preparing files for drag..."));
        const QMetaObject::Connection progressConnection = connect(
            &dragOut, &DragOutManager::progressChanged, this,
            [this](int percent, int total, int, int) {
                if (total > 0) {
                    setBusyIndicatorVisible(
                        true, tr("Preparing files for drag... %1%").arg(percent));
                }
            });
        const QMetaObject::Connection messageConnection = connect(
            &dragOut, &DragOutManager::stageMessage, this,
            [this](const QString &message) {
                setBusyIndicatorVisible(true, message);
            });

        for (int attempt = 0;; ++attempt) {
            payload = dragOut.extractForDrag(archivePath, format, entries, paths,
                                             password, manager.nameEncoding());
            if (payload.success || payload.cancelled) {
                break;
            }
            const bool passwordIssue =
                payload.errorCode == ArchiveErrorCode::PasswordRequired
                || payload.errorCode == ArchiveErrorCode::WrongPassword;
            if (!passwordIssue) {
                break;
            }
            if (attempt >= 2) {
                payload.errorMessage = tr("Too many failed password attempts.");
                break;
            }

            QApplication::restoreOverrideCursor();
            PasswordDialog pw(this);
            const bool accepted = pw.exec() == QDialog::Accepted;
            QApplication::setOverrideCursor(Qt::BusyCursor);
            if (!accepted) {
                dragOut.cancel();
                payload.cancelled = true;
                payload.success = false;
                break;
            }
            password = pw.password();
            m_currentPassword = password;
            if (pw.rememberPassword()) {
                m_sessionPassword = password;
            }
        }

        disconnect(progressConnection);
        disconnect(messageConnection);
        QApplication::restoreOverrideCursor();
        setBusyIndicatorVisible(false);
        m_dragPrepareInProgress = false;
    }

    LOG_INFO("DragOut", "preparation finished: success=%d cancelled=%d staging=%s files=%lld",
             payload.success ? 1 : 0, payload.cancelled ? 1 : 0,
             payload.stagingRoot.toUtf8().constData(),
             static_cast<long long>(payload.localPaths.size()));

    if (!payload.success) {
        LOG_WARN("DragOut", "preparation failed: %s",
                 payload.errorMessage.toUtf8().constData());
        if (!payload.cancelled && !payload.errorMessage.isEmpty()) {
            QMessageBox::warning(this, tr("Drag Out"), payload.errorMessage);
        }
        updateActionsState();
        return;
    }

    const Qt::MouseButtons buttons = QGuiApplication::mouseButtons();
    LOG_INFO("DragOut", "mouse buttons at drag start: 0x%x",
             static_cast<unsigned>(buttons));
    if (!(buttons & Qt::LeftButton)) {
        LOG_WARN("DragOut", "left button already released, drag not started");
        dragOut.retire(payload);
        statusBar()->showMessage(
            tr("Files are ready. Drag the selection again to copy them out."),
            8000);
        updateActionsState();
        return;
    }

    QList<QUrl> urls;
    for (const QString &local : payload.localPaths) {
        urls << QUrl::fromLocalFile(local);
    }

    auto *mimeData = new LoggingMimeData(paths);
    mimeData->setUrls(urls);
    mimeData->setData(QStringLiteral("application/x-qarchiver-entries"),
                      paths.join(QLatin1Char('\n')).toUtf8());

    for (const QString &local : payload.localPaths) {
        if (!QFileInfo::exists(local)) {
            LOG_WARN("DragOut", "staged file is missing: %s",
                     local.toUtf8().constData());
        }
    }

    m_selfDropAttempted = false;
    auto *drag = new QDrag(this);
    drag->setMimeData(mimeData);
    QElapsedTimer dragTimer;
    dragTimer.start();
    LOG_INFO("DragOut",
             "payload: hasUrls=%d formats=[%s] uri-list=%lld bytes",
             mimeData->hasUrls() ? 1 : 0,
             mimeData->formats().join(QStringLiteral(", ")).toUtf8().constData(),
             static_cast<long long>(
                 mimeData->data(QStringLiteral("text/uri-list")).size()));

    QObject::connect(drag, &QDrag::targetChanged, this,
                     [](QObject *target) {
                         LOG_INFO("DragOut", "drag target changed: %s",
                                  target ? target->metaObject()->className()
                                         : "none");
                     });
    LOG_INFO("DragOut", "starting QDrag with %lld urls, first=%s",
             static_cast<long long>(urls.size()),
             urls.isEmpty() ? "-" : urls.first().toLocalFile().toUtf8().constData());
    const bool skipNativeDrag =
        qEnvironmentVariableIsSet("QARCHIVER_DRAG_DEBUG_NO_EXEC");
    Qt::DropAction action = Qt::IgnoreAction;
    if (skipNativeDrag) {
        delete drag;
    } else {
        action = drag->exec(Qt::CopyAction, Qt::CopyAction);
    }
    const qint64 dragMillis = dragTimer.elapsed();
    LOG_INFO("DragOut", "QDrag finished: action=%d elapsed=%lldms selfDrop=%d",
             static_cast<int>(action), static_cast<long long>(dragMillis),
             m_selfDropAttempted ? 1 : 0);

    dragOut.retire(payload);
    updateActionsState();

    if (m_selfDropAttempted) {
        m_selfDropAttempted = false;
        QMessageBox::warning(
            this, tr("Drag Out"),
            tr("Cannot drop entries onto QArchiver itself.\n"
               "Please drop them onto a file manager, the desktop, or another "
               "application."));
    } else if (action == Qt::IgnoreAction) {
        statusBar()->showMessage(
            dragMillis < 150
                ? tr("Files are ready. Drag the selection again to copy them out.")
                : tr("The target application does not accept file drops."),
            6000);
    }
}
