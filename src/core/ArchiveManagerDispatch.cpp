// QArchiver / 小Q解压
// ArchiveManager worker-thread dispatch: operation start, signal wiring,
// terminal handling and error/fallback routing.
// ArchiveManager 工作线程调度：操作启动、信号连接、终止处理与错误/回退分发。

#include "core/ArchiveManager.h"

#include "backends/ArchiveBackend.h"
#include "common/FormatRegistry.h"

#include "utils/FileUtils.h"
#include "utils/Logger.h"
#include "utils/MimeTypeHelper.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QtConcurrent>

#include "core/ArchiveBackendFactory.h"
#include "core/RecentFilesManager.h"

namespace {

// Variant visitor helper for the pending-operation dispatch.
template <typename... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <typename... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

} // namespace

bool ArchiveManager::startOperation(ArchiveBackend *backend,
                                    std::function<void(ArchiveBackend *)> operation)
{
    if (!backend) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("No archive open"));
        return false;
    }
    if (m_operation.busy) {
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Another operation is already in progress"));
        return false;
    }

    m_operation.busy = true;
    backend->clearCancel();

    const bool invoked = QMetaObject::invokeMethod(
        backend,
        [backend, operation = std::move(operation)]() { operation(backend); },
        Qt::QueuedConnection);

    if (!invoked) {
        m_operation.busy = false;
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Failed to dispatch the archive operation"));
    }
    return invoked;
}

void ArchiveManager::connectBackend(ArchiveBackend *backend, quint64 generation)
{
    const auto isCurrent = [this, generation]() { return generation == m_generation; };

    connect(backend, &ArchiveBackend::archiveOpened, this,
            [this, backend, isCurrent](const QString &filePath) {
                if (!isCurrent()) {
                    return;
                }
                // Cache the encoding here, in the GUI thread, instead of
                // reading the backend member from the worker thread later.
                // 在 GUI 线程缓存编码，避免之后从工作线程读取后端成员。
                m_nameEncoding = backend->nameEncoding();
                OpenController::Opened opened;
                if (m_openController.accept(backend, &opened)) {
                    m_openController.assertInvariants("archiveOpened/accepted");
                    // The new archive opened successfully; only now replace
                    // the previously open archive, so a failure leaves it
                    // untouched.
                    // 新压缩包成功打开后才替换旧压缩包；失败时旧包保持不变。
                    ArchiveBackend *old = m_backend;
                    m_backend = opened.backend;
                    m_archivePath = opened.path;
                    m_format = opened.format;
                    m_operation.busy = false;
                    m_entries.clear();
                    m_operation.pending = std::monostate{};
                    m_operation.passwordRetries = 0;
                    if (old && old != backend) {
                        destroyBackend(old);
                    }

                    // A read-only libarchive backend was reopened with the CLI
                    // to replay add/delete.  Dispatch that write now; the UI is
                    // still showing its progress dialog and will reload the list
                    // when operationFinished arrives.
                    // 为执行添加/删除而用命令行后端重新打开了只读的 libarchive
                    // 后端。现在派发该写操作；界面仍显示进度对话框，并在
                    // operationFinished 后重新列出内容。
                    if (!std::holds_alternative<std::monostate>(
                            m_operation.writeAfterReopen)) {
                        auto write = std::move(m_operation.writeAfterReopen);
                        m_operation.writeAfterReopen = std::monostate{};
                        m_operation.busy = true;
                        backend->clearCancel();
                        const bool invoked = std::visit(
                            Overloaded{
                                [this, backend](const AddFilesOptions &options) {
                                    m_operation.pending = options;
                                    m_operation.passwordRetries = 0;
                                    return QMetaObject::invokeMethod(
                                        backend,
                                        [backend, options]() {
                                            backend->addFiles(options);
                                        },
                                        Qt::QueuedConnection);
                                },
                                [this, backend](const DeleteFilesOptions &options) {
                                    m_operation.pending = options;
                                    m_operation.passwordRetries = 0;
                                    return QMetaObject::invokeMethod(
                                        backend,
                                        [backend, options]() {
                                            backend->deleteFiles(options);
                                        },
                                        Qt::QueuedConnection);
                                },
                                [](const std::monostate &) { return false; },
                            },
                            write);
                        if (!invoked) {
                            m_operation.busy = false;
                            clearPendingState();
                            emit errorOccurred(
                                ArchiveErrorCode::Unknown,
                                tr("Failed to dispatch the archive operation"));
                        }
                        return;
                    }

                    RecentFilesManager::instance().add(filePath);
                    emit archiveOpened(filePath);
                    return;
                }

                if (m_openController.fallbackInProgress()) {
                    // Re-opening on the CLI backend after a fallback: the UI
                    // already knows this archive, do not re-announce it.
                    m_openController.finishFallback();
                    m_openController.assertInvariants("archiveOpened/fallback");
                    return;
                }
                RecentFilesManager::instance().add(filePath);
                emit archiveOpened(filePath);
            });

    connect(backend, &ArchiveBackend::fileListReady, this,
            [this, backend, isCurrent](const QList<ArchiveEntry> &entries) {
                if (isCurrent()) {
                    m_entries = entries;
                    m_nameEncoding = backend->nameEncoding();
                    m_operation.busy = false;
                    m_operation.pending = std::monostate{};
                    m_operation.passwordRetries = 0;
                    LOG_DEBUG("ArchiveManager", "Listed %d entries from %s in %lld ms",
                              static_cast<int>(entries.size()),
                              m_archivePath.toUtf8().constData(),
                              static_cast<long long>(m_operation.timer.elapsed()));
                    emit fileListReady(entries);
                }
            });

    connect(backend, &ArchiveBackend::progressChanged, this,
            [this, isCurrent](int percent, int total, int current, int etaSec) {
                if (isCurrent()) {
                    emit progressChanged(percent, total, current, etaSec);
                }
            });

    connect(backend, &ArchiveBackend::outputReady, this,
            [this, isCurrent](const QString &line) {
                if (isCurrent()) {
                    emit outputReady(line);
                }
            });

    connect(backend, &ArchiveBackend::passwordRequested, this,
            [this, isCurrent]() {
                if (!isCurrent()) {
                    return;
                }
                m_operation.busy = false;
                emit passwordRequested(false);
            });

    connect(backend, &ArchiveBackend::operationFinished, this,
            [this, backend, isCurrent](bool success, const QString &msg) {
                if (isCurrent()) {
                    handleTerminal(backend, success, msg);
                }
            });

    connect(backend, &ArchiveBackend::errorOccurred, this,
            [this, backend, isCurrent](ArchiveErrorCode code, const QString &msg) {
                if (isCurrent()) {
                    handleError(backend, code, msg);
                }
            });
}

void ArchiveManager::connectTempBackend(ArchiveBackend *backend)
{
    connect(backend, &ArchiveBackend::progressChanged, this,
            [this, backend](int percent, int total, int current, int etaSec) {
                if (backend == m_tempBackend) {
                    emit progressChanged(percent, total, current, etaSec);
                }
            });

    connect(backend, &ArchiveBackend::outputReady, this,
            [this, backend](const QString &line) {
                if (backend == m_tempBackend) {
                    emit outputReady(line);
                }
            });

    connect(backend, &ArchiveBackend::operationFinished, this,
            [this, backend](bool success, const QString &msg) {
                if (backend == m_tempBackend) {
                    handleTerminal(backend, success, msg);
                }
            });

    connect(backend, &ArchiveBackend::errorOccurred, this,
            [this, backend](ArchiveErrorCode code, const QString &msg) {
                if (backend == m_tempBackend) {
                    handleError(backend, code, msg);
                }
            });
}

void ArchiveManager::handleTerminal(ArchiveBackend *backend, bool success,
                                    const QString &msg)
{
    if (success) {
        LOG_DEBUG("ArchiveManager", "%s reported success in %lld ms: %s",
                  backend ? backend->metaObject()->className() : "(null backend)",
                  static_cast<long long>(m_operation.timer.elapsed()),
                  msg.toUtf8().constData());
    } else {
        // Failures reported through operationFinished() bypass handleError(),
        // so they must be logged here to stay visible at the default level.
        // 通过 operationFinished() 报告的失败不经过 handleError()，
        // 因此需要在此记录，才能在默认级别下可见。
        LOG_WARN("ArchiveManager", "%s reported failure in %lld ms: %s",
                 backend ? backend->metaObject()->className() : "(null backend)",
                 static_cast<long long>(m_operation.timer.elapsed()),
                 msg.toUtf8().constData());
    }

    m_operation.busy = false;

    if (backend == m_tempBackend) {
        bool promoted = true;
        if (success) {
            promoted = promoteCompressedArchive();
        } else {
            removeFailedCompressOutput();
        }

        const bool finalSuccess = success && promoted;
        emit operationFinished(finalSuccess,
                               finalSuccess ? msg
                                            : tr("Failed to finalize the compressed archive"));
        cleanupTempBackend();
        cleanupCompressSession();
        return;
    }

    if (m_operation.preTestPending) {
        m_operation.preTestPending = false;
        if (!success) {
            clearPendingState();
            m_operation.preTestOptions = ExtractOptions{};
            emit operationFinished(false, msg);
            return;
        }

        ExtractOptions options = m_operation.preTestOptions;
        options.testBeforeExtract = false;
        // The pre-test proved the password correct: a fresh budget applies to
        // the extraction that follows.
        // 预测试已证明密码正确：随后的解压重新计算尝试次数。
        m_operation.passwordRetries = 0;
        startExtractOperation(options);
        return;
    }

    m_operation.pending = std::monostate{};
    m_operation.passwordRetries = 0;
    emit operationFinished(success, msg);
}

bool ArchiveManager::tryLibArchiveFallback(ArchiveBackend *backend,
                                           ArchiveErrorCode code, const QString &msg)
{
    if (backend != m_backend || !m_openController.canFallback()) {
        return false;
    }

    switch (code) {
    case ArchiveErrorCode::Cancelled:
    case ArchiveErrorCode::OperationNotSupported:
    case ArchiveErrorCode::PathTraversalBlocked:
    case ArchiveErrorCode::InsufficientDiskSpace:
    case ArchiveErrorCode::ToolNotFound:
    case ArchiveErrorCode::FileNotFound:
    case ArchiveErrorCode::PermissionDenied:
    case ArchiveErrorCode::PartialExtraction:
    case ArchiveErrorCode::HardLinkUnsupported:
    case ArchiveErrorCode::WrongPassword:
        return false;
    default:
        break;
    }

    const bool retryable = std::holds_alternative<ListOptions>(m_operation.pending)
        || std::holds_alternative<ExtractOptions>(m_operation.pending)
        || std::holds_alternative<TestOptions>(m_operation.pending);
    if (!retryable) {
        return false;
    }

    m_openController.beginFallback();
    m_openController.assertInvariants("fallback/begin");

    LOG_INFO("ArchiveManager",
             "libarchive could not handle %s (%s); retrying with the CLI backend",
             m_archivePath.toUtf8().constData(), msg.toUtf8().constData());
    emit outputReady(
        tr("libarchive cannot handle this archive, switching to the 7z command line."));

    ArchiveBackend *old = m_backend;
    m_backend = nullptr;
    old->requestCancel();
    disconnect(old, nullptr, this, nullptr);
    QMetaObject::invokeMethod(
        old, [old]() { old->closeArchive(); delete old; }, Qt::QueuedConnection);

    ArchiveBackend *cli = ArchiveBackendFactory::create(m_format);
    if (!cli) {
        m_openController.finishFallback();
        m_openController.assertInvariants("fallback/no-backend");
        m_operation.busy = false;
        clearPendingState();
        emit errorOccurred(code, msg);
        return true;
    }

    cli->moveToThread(&m_workerThread);
    m_backend = cli;
    m_operation.busy = true;

    const quint64 generation = m_generation;
    connectBackend(cli, generation);

    const QString path = m_archivePath;
    const auto pending = m_operation.pending;
    // When the failing operation was the "test before extract" pre-test, that
    // pre-test must be replayed, not the extraction itself.
    const bool preTest = m_operation.preTestPending;
    const QString preTestPassword = m_operation.preTestOptions.password;

    QMetaObject::invokeMethod(
        cli,
        [cli, path, pending, generation, preTest, preTestPassword, this]() {
            if (generation != m_generation) {
                return;
            }
            cli->openArchive(path);

            if (preTest) {
                TestOptions testOptions;
                testOptions.password = preTestPassword;
                cli->testArchive(testOptions);
                return;
            }

            std::visit(Overloaded{
                           [cli](const ListOptions &options) {
                               cli->listContents(options);
                           },
                           [cli](const ExtractOptions &options) {
                               cli->extract(options);
                           },
                           [cli](const TestOptions &options) {
                               cli->testArchive(options);
                           },
                           [](const auto &) {},
                       },
                       pending);
        },
        Qt::QueuedConnection);
    return true;
}

void ArchiveManager::handleError(ArchiveBackend *backend, ArchiveErrorCode code,
                                 const QString &msg)
{
    LOG_WARN("ArchiveManager", "%s failed on %s (%s, %lld ms): %s",
             m_archivePath.toUtf8().constData(),
             backend ? backend->metaObject()->className() : "(null backend)",
             errorCodeName(code),
             static_cast<long long>(m_operation.timer.elapsed()),
             msg.toUtf8().constData());

    if (tryLibArchiveFallback(backend, code, msg)) {
        return;
    }

    m_operation.busy = false;

    if (ArchiveBackend *failed = m_openController.takeOpening(backend)) {
        m_openController.assertInvariants("handleError/open-failed");
        clearWriteAfterReopen();
        disconnect(failed, nullptr, this, nullptr);
        destroyBackend(failed);
        emit errorOccurred(code, msg);
        return;
    }

    if (backend == m_tempBackend) {
        removeFailedCompressOutput();
        emit errorOccurred(code, msg);
        cleanupTempBackend();
        cleanupCompressSession();
        return;
    }

    // A wrong password is recoverable: keep the pending operation and ask
    // again while attempts remain instead of dropping the request.
    // 密码错误是可恢复的：只要还有尝试次数，就保留待处理操作并再次询问，
    // 而不是直接丢弃请求。
    if (code == ArchiveErrorCode::WrongPassword && !m_openController.isOpening()
        && hasPendingOperation()
        && m_operation.passwordRetries < kMaxPasswordRetries) {
        emit passwordRequested(true);
        return;
    }

    if (m_operation.preTestPending) {
        m_operation.preTestPending = false;
        m_operation.preTestOptions = ExtractOptions{};
        clearPendingState();
    }

    if (!m_openController.isOpening()) {
        clearPendingState();
    }

    m_openController.assertInvariants("handleError");
    emit errorOccurred(code, msg);
}
