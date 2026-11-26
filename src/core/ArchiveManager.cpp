// QArchiver / 小Q解压
// ArchiveManager implementation: format detection, backend dispatch and retries.
// ArchiveManager 实现：格式检测、后端调度与重试。

#include "core/ArchiveManager.h"

#include "backends/ArchiveBackend.h"

#include "core/ArchiveBackendFactory.h"
#include "core/CompressOutput.h"
#include "core/ExtractionPreflight.h"

#include "utils/Logger.h"
#include "utils/MimeTypeHelper.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTemporaryDir>
#include <QtConcurrent>

#include <algorithm>

ArchiveManager::ArchiveManager()
{
    m_workerThread.setObjectName(QStringLiteral("ArchiveWorker"));
    m_workerThread.start();
}

ArchiveManager::~ArchiveManager()
{
    shutdown();
}

void ArchiveManager::shutdown()
{
    if (m_shutdown) {
        return;
    }
    m_shutdown = true;
    ++m_generation;

    if (m_backend) {
        m_backend->requestCancel();
        disconnect(m_backend, nullptr, this, nullptr);
    }
    if (m_tempBackend) {
        m_tempBackend->requestCancel();
        disconnect(m_tempBackend, nullptr, this, nullptr);
    }
    ArchiveBackend *opening = m_openController.takeOpening();
    if (opening) {
        opening->requestCancel();
        disconnect(opening, nullptr, this, nullptr);
    }

    m_workerThread.quit();
    if (!m_workerThread.wait(15000)) {
        // Never fall back to QThread::terminate(): it can kill the worker while
        // it holds Qt locks or a child-process mutex and corrupt the process.
        // Cancellation has already been requested (requestCancel() kills the
        // helper process), so wait for it to unwind.
        // 不要回退到 QThread::terminate()：它可能在 worker 持有 Qt 锁或子进程
        // 互斥量时杀死线程并破坏进程。取消已经发出（requestCancel() 会结束
        // 辅助进程），这里等待它自行退出。
        LOG_ERROR("ArchiveManager",
                  "Worker thread did not stop within 15s; waiting for it to "
                  "finish instead of terminating it");
        m_workerThread.wait();
    }

    delete m_backend;
    m_backend = nullptr;
    delete m_tempBackend;
    m_tempBackend = nullptr;
    delete opening;
    m_openController.assertInvariants("shutdown");

    cleanupCompressSession();
}

ArchiveManager &ArchiveManager::instance()
{
    static ArchiveManager manager;
    return manager;
}

void ArchiveManager::setSymlinkDecisionCallback(SymlinkDecisionCallback callback)
{
    m_symlinkDecisionCallback = std::move(callback);
}



void ArchiveManager::openArchive(const QString &filePath)
{
    openArchive(filePath, false);
}

void ArchiveManager::openArchive(const QString &filePath, bool forceCli)
{
    if (m_operation.busy) {
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Another operation is already in progress"));
        return;
    }

    const QFileInfo info(filePath);
    if (!info.exists()) {
        emit errorOccurred(ArchiveErrorCode::FileNotFound,
                           tr("File does not exist: %1").arg(filePath));
        return;
    }
    if (!info.isReadable()) {
        emit errorOccurred(ArchiveErrorCode::PermissionDenied,
                           tr("File is not readable: %1").arg(filePath));
        return;
    }

    ++m_generation;
    const quint64 generation = m_generation;

    m_openController.begin(filePath, forceCli);
    m_operation.busy = true;
    m_operation.pending = std::monostate{};
    m_operation.passwordRetries = 0;
    m_operation.preTestPending = false;

    // A write-through reopen (add/delete on a read-only backend) does not
    // change what the user is looking at: the archive stays open and its list
    // stays valid.  Announcing an opening would leave the UI in a half-updated
    // "opening" state, because the replayed write is reported through
    // operationFinished() and the matching archiveOpened() is deliberately
    // suppressed for this reopen.
    // 为执行写操作而重开只读后端时，用户看到的内容并没有变化：压缩包仍处于打开
    // 状态，列表也仍然有效。此时通告“正在打开”会让界面停在一个半更新的打开
    // 状态——重放的写操作通过 operationFinished() 汇报，而这个重开刻意不发出
    // 对应的 archiveOpened()。
    if (std::holds_alternative<std::monostate>(m_operation.writeAfterReopen)) {
        emit archiveOpening(filePath);
    }

    auto *watcher = new QFutureWatcher<ArchiveFormat>(this);
    connect(watcher, &QFutureWatcher<ArchiveFormat>::finished, this,
            [this, watcher, filePath, generation]() {
                ArchiveFormat format = ArchiveFormat::Unknown;
                try {
                    format = watcher->result();
                } catch (...) {
                    format = ArchiveFormat::Unknown;
                }
                watcher->deleteLater();

                if (generation != m_generation) {
                    return;
                }
                m_operation.busy = false;
                finishOpen(filePath, format);
            });

    watcher->setFuture(QtConcurrent::run(
        [filePath]() { return MimeTypeHelper::detect(filePath); }));
}

void ArchiveManager::finishOpen(const QString &filePath, ArchiveFormat format)
{
    if (format == ArchiveFormat::Unknown) {
        m_openController.abort();
        m_openController.assertInvariants("finishOpen/unknown-format");
        m_operation.busy = false;
        clearWriteAfterReopen();
        emit errorOccurred(ArchiveErrorCode::UnsupportedFormat,
                           tr("Unrecognized archive format or file is corrupted: %1")
                               .arg(filePath));
        return;
    }

    QString errorMessage;
    ArchiveBackend *backend =
        m_openController.createOpeningBackend(format, &errorMessage);
    if (!backend) {
        m_openController.assertInvariants("finishOpen/no-backend");
        m_operation.busy = false;
        clearWriteAfterReopen();
        emit errorOccurred(ArchiveErrorCode::UnsupportedFormat,
                           tr("No backend available for format: %1")
                               .arg(formatToString(format)));
        return;
    }

    backend->moveToThread(&m_workerThread);
    m_operation.busy = true;
    connectBackend(backend, m_generation);

    const bool invoked = QMetaObject::invokeMethod(
        backend,
        [backend, filePath]() { backend->openArchive(filePath); },
        Qt::QueuedConnection);

    if (!invoked) {
        ArchiveBackend *failed = m_openController.takeOpening(backend);
        m_openController.assertInvariants("finishOpen/dispatch-failed");
        m_operation.busy = false;
        clearWriteAfterReopen();
        if (failed) {
            destroyBackend(failed);
        }
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Failed to open archive: %1").arg(filePath));
        return;
    }

    m_openController.assertInvariants("finishOpen");
}

QString ArchiveManager::nameEncoding() const
{
    // Cached in the GUI thread by the archiveOpened/fileListReady handlers;
    // never read the backend member directly (the worker thread writes it).
    // 由 archiveOpened/fileListReady 处理函数在 GUI 线程缓存；不直接读取后端
    // 成员（它由工作线程写入）。
    return m_nameEncoding;
}

void ArchiveManager::listContents(const ListOptions &options)
{
    if (!m_backend) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("No archive open"));
        return;
    }

    m_operation.pending = options;
    m_operation.passwordRetries = 0;

    // No encoding hint here on purpose: every backend re-detects the encoding
    // from the archive itself, which is deterministic, while reusing a value
    // from a previous listing could be stale if the archive changed on disk.
    // 此处故意不传编码提示：每个后端都会从压缩包本身重新检测（结果是确定的），
    // 而复用上次列表的值在压缩包被外部修改后可能已过期。
    if (!startOperation(m_backend, [options](ArchiveBackend *backend) {
            backend->listContents(options);
        })) {
        clearPendingState();
    }
}

bool ArchiveManager::validateExtraction(const ExtractOptions &options,
                                        QSet<QString> *skippedSymlinks)
{
    const ExtractionPreflight::Result result = ExtractionPreflight::check(
        options, m_entries, m_symlinkDecisionCallback);
    if (!result.ok) {
        emit errorOccurred(result.errorCode, result.errorMessage);
        return false;
    }
    if (skippedSymlinks) {
        *skippedSymlinks = result.skippedSymlinks;
    }
    return true;
}

void ArchiveManager::extract(const ExtractOptions &options)
{
    if (!m_backend) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("No archive open"));
        return;
    }

    // A user-initiated extraction starts a fresh password budget.  Retries go
    // through startExtractOperation() directly and must keep the counter.
    // 用户发起的解压重新开始计算密码尝试次数；重试直接走
    // startExtractOperation()，必须保留计数器。
    m_operation.passwordRetries = 0;

    ExtractOptions effective = options;
    // Decode member names exactly like the listing did, and let the CLI
    // backends build their listfile in the archive's own code page.
    // 按与列表一致的方式解码成员名，并让命令行后端按压缩包自身的代码页
    // 生成列表文件。
    if (effective.encodingHint.isEmpty()) {
        effective.encodingHint = nameEncoding();
    }
    QSet<QString> rejected;
    if (!validateExtraction(options, &rejected)) {
        return;
    }
    effective.skippedSymlinks.unite(rejected);
    effective.symlinkPreflightDone = true;

    if (!QDir().mkpath(effective.destDir)) {
        emit errorOccurred(ArchiveErrorCode::PermissionDenied,
                           tr("Cannot create destination directory: %1")
                               .arg(effective.destDir));
        return;
    }

    if (effective.testBeforeExtract) {
        m_operation.preTestPending = true;
        m_operation.preTestOptions = effective;
        m_operation.pending = effective;

        TestOptions testOptions;
        testOptions.password = effective.password;
        if (!startOperation(m_backend, [testOptions](ArchiveBackend *backend) {
                backend->testArchive(testOptions);
            })) {
            m_operation.preTestPending = false;
            clearPendingState();
        }
        return;
    }

    startExtractOperation(effective);
}

void ArchiveManager::startExtractOperation(const ExtractOptions &options)
{
    m_operation.pending = options;

    if (!startOperation(m_backend, [options](ArchiveBackend *backend) {
            backend->extract(options);
        })) {
        clearPendingState();
    }
}

void ArchiveManager::compress(const CompressOptions &options, ArchiveFormat format)
{
    if (m_operation.busy) {
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Another operation is already in progress"));
        return;
    }
    if (options.archivePath.trimmed().isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("No archive name specified"));
        return;
    }
    if (options.sourceFiles.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("No source files selected"));
        return;
    }

    cleanupCompressSession();

    ArchiveBackend *backend = ArchiveBackendFactory::create(format);
    if (!backend) {
        emit errorOccurred(ArchiveErrorCode::UnsupportedFormat,
                           tr("No backend available for format: %1")
                               .arg(formatToString(format)));
        return;
    }

    backend->moveToThread(&m_workerThread);
    m_tempBackend = backend;
    connectTempBackend(backend);

    m_compressFinalPath = QFileInfo(options.archivePath).absoluteFilePath();
    m_compressFormat = format;
    const QFileInfo finalInfo(m_compressFinalPath);
    if (!QDir().mkpath(finalInfo.absolutePath())) {
        emit errorOccurred(ArchiveErrorCode::PermissionDenied,
                           tr("Cannot create target directory: %1")
                               .arg(finalInfo.absolutePath()));
        cleanupTempBackend();
        cleanupCompressSession();
        return;
    }

    m_compressTargetExisted = CompressOutput::outputExists(finalInfo, format);
    CompressOptions effectiveOptions = options;
    effectiveOptions.archivePath = m_compressFinalPath;

    if (m_compressTargetExisted) {
        m_compressTempDir = std::make_unique<QTemporaryDir>(
            QDir(finalInfo.absolutePath())
                .filePath(QStringLiteral(".qarchiver_compress_XXXXXX")));
        if (!m_compressTempDir->isValid()) {
            emit errorOccurred(ArchiveErrorCode::PermissionDenied,
                               tr("Cannot create a temporary directory next to the target"));
            cleanupTempBackend();
            cleanupCompressSession();
            return;
        }
        effectiveOptions.archivePath =
            QDir(m_compressTempDir->path()).filePath(finalInfo.fileName());
    }

    m_operation.busy = true;
    backend->clearCancel();

    const bool invoked = QMetaObject::invokeMethod(
        backend,
        [backend, effectiveOptions]() { backend->compress(effectiveOptions); },
        Qt::QueuedConnection);

    if (!invoked) {
        m_operation.busy = false;
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Failed to dispatch the compression operation"));
        cleanupTempBackend();
        cleanupCompressSession();
    }
}

void ArchiveManager::addFiles(const AddFilesOptions &options)
{
    if (!m_backend) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("No archive open"));
        return;
    }
    if (!m_backend->supportsAppend()) {
        if (canReopenForWrite()) {
            LOG_INFO("ArchiveManager",
                     "Add files requested on a read-only backend; reopening %s with the CLI backend",
                     m_archivePath.toUtf8().constData());
            m_operation.writeAfterReopen = options;
            openArchive(m_archivePath, true);
            return;
        }
        emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                           tr("This archive does not support adding files"));
        return;
    }

    m_operation.pending = options;
    m_operation.passwordRetries = 0;

    if (!startOperation(m_backend, [options](ArchiveBackend *backend) {
            backend->addFiles(options);
        })) {
        clearPendingState();
    }
}

void ArchiveManager::deleteFiles(const DeleteFilesOptions &options)
{
    if (!m_backend) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("No archive open"));
        return;
    }
    if (!m_backend->supportsAppend()) {
        if (canReopenForWrite()) {
            LOG_INFO("ArchiveManager",
                     "Delete files requested on a read-only backend; reopening %s with the CLI backend",
                     m_archivePath.toUtf8().constData());
            m_operation.writeAfterReopen = options;
            openArchive(m_archivePath, true);
            return;
        }
        emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                           tr("This archive does not support deleting files"));
        return;
    }

    m_operation.pending = options;
    m_operation.passwordRetries = 0;

    if (!startOperation(m_backend, [options](ArchiveBackend *backend) {
            backend->deleteFiles(options);
        })) {
        clearPendingState();
    }
}

void ArchiveManager::testArchive(const TestOptions &options)
{
    if (!m_backend) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("No archive open"));
        return;
    }

    m_operation.pending = options;
    m_operation.passwordRetries = 0;

    if (!startOperation(m_backend, [options](ArchiveBackend *backend) {
            backend->testArchive(options);
        })) {
        clearPendingState();
    }
}

void ArchiveManager::retryWithPassword(const QString &password)
{
    if (!m_backend) {
        return;
    }

    if (m_operation.passwordRetries >= kMaxPasswordRetries) {
        clearPendingState();
        m_operation.preTestPending = false;
        m_operation.preTestOptions = ExtractOptions{};
        emit errorOccurred(ArchiveErrorCode::WrongPassword,
                           tr("Too many failed password attempts."));
        return;
    }

    ++m_operation.passwordRetries;

    if (auto *options = std::get_if<ListOptions>(&m_operation.pending)) {
        options->password = password;
        const ListOptions opts = *options;
        if (!startOperation(m_backend, [opts](ArchiveBackend *backend) {
                backend->listContents(opts);
            })) {
            clearPendingState();
        }
        return;
    }
    if (auto *options = std::get_if<ExtractOptions>(&m_operation.pending)) {
        options->password = password;
        // Copy before startExtractOperation() reassigns m_operation.pending, which
        // would invalidate @p options.
        // 在 startExtractOperation() 重新赋值 m_operation.pending 前先拷贝，否则
        // @p options 会失效。
        const ExtractOptions opts = *options;
        if (m_operation.preTestPending) {
            // The pre-extraction test is what failed; replay it with the new
            // password instead of skipping straight to extraction.
            // 失败的是解压前测试；用新密码重放测试，而不是直接解压。
            m_operation.preTestOptions = opts;
            TestOptions testOptions;
            testOptions.password = password;
            if (!startOperation(m_backend, [testOptions](ArchiveBackend *backend) {
                    backend->testArchive(testOptions);
                })) {
                clearPendingState();
            }
            return;
        }
        startExtractOperation(opts);
        return;
    }
    if (auto *options = std::get_if<TestOptions>(&m_operation.pending)) {
        options->password = password;
        const TestOptions opts = *options;
        if (!startOperation(m_backend, [opts](ArchiveBackend *backend) {
                backend->testArchive(opts);
            })) {
            clearPendingState();
        }
        return;
    }
    if (auto *options = std::get_if<AddFilesOptions>(&m_operation.pending)) {
        options->password = password;
        const AddFilesOptions opts = *options;
        if (!startOperation(m_backend, [opts](ArchiveBackend *backend) {
                backend->addFiles(opts);
            })) {
            clearPendingState();
        }
        return;
    }
    if (auto *options = std::get_if<DeleteFilesOptions>(&m_operation.pending)) {
        options->password = password;
        const DeleteFilesOptions opts = *options;
        if (!startOperation(m_backend, [opts](ArchiveBackend *backend) {
                backend->deleteFiles(opts);
            })) {
            clearPendingState();
        }
        return;
    }
}

void ArchiveManager::cancelCurrentOperation()
{
    if (m_backend) {
        m_backend->requestCancel();
    }
    if (m_openController.isOpening()) {
        ArchiveBackend *opening = m_openController.takeOpening();
        if (opening) {
            opening->requestCancel();
        }
    }
    if (m_tempBackend) {
        m_tempBackend->requestCancel();
    }
}

void ArchiveManager::cancelPendingOperation()
{
    clearPendingState();
    m_operation.preTestPending = false;
    m_operation.preTestOptions = ExtractOptions{};
    m_operation.busy = false;
    emit operationFinished(false, tr("Cancelled"));
}

bool ArchiveManager::canModifyCurrentArchive() const
{
    return m_backend
        && (m_backend->supportsAppend() || canReopenForWrite());
}

bool ArchiveManager::canReopenForWrite() const
{
    if (m_openController.usingLibArchive() == false || m_archivePath.isEmpty()) {
        return false;
    }
    if (MimeTypeHelper::isSplitVolume(m_archivePath)) {
        return false;
    }
    return m_format == ArchiveFormat::Zip
        || m_format == ArchiveFormat::SevenZip
        || m_format == ArchiveFormat::Tar;
}

void ArchiveManager::clearWriteAfterReopen()
{
    m_operation.writeAfterReopen = std::monostate{};
}


void ArchiveManager::closeArchive()
{
    ++m_generation;
    m_operation.busy = false;
    m_nameEncoding.clear();
    clearPendingState();
    m_operation.preTestPending = false;
    m_operation.preTestOptions = ExtractOptions{};
    m_archivePath.clear();
    m_format = ArchiveFormat::Unknown;
    clearWriteAfterReopen();

    ArchiveBackend *opening = m_openController.reset();
    m_openController.assertInvariants("closeArchive");
    destroyBackend(opening);

    ArchiveBackend *current = m_backend;
    m_backend = nullptr;
    m_entries.clear();
    destroyBackend(current);

    QTemporaryDir *tempDir = m_compressTempDir.release();
    ArchiveBackend *tempBackend = m_tempBackend;
    m_tempBackend = nullptr;

    if (tempBackend) {
        tempBackend->requestCancel();
        disconnect(tempBackend, nullptr, this, nullptr);
        if (m_workerThread.isRunning()) {
            QMetaObject::invokeMethod(
                tempBackend,
                [tempBackend, tempDir]() {
                    tempBackend->closeArchive();
                    delete tempBackend;
                    delete tempDir;
                },
                Qt::QueuedConnection);
        } else {
            tempBackend->closeArchive();
            delete tempBackend;
            delete tempDir;
        }
    } else {
        delete tempDir;
    }

    m_compressFinalPath.clear();
    m_compressTargetExisted = false;
}

void ArchiveManager::destroyBackend(ArchiveBackend *backend)
{
    if (!backend) {
        return;
    }
    backend->requestCancel();
    disconnect(backend, nullptr, this, nullptr);
    if (m_workerThread.isRunning()) {
        QMetaObject::invokeMethod(
            backend,
            [backend]() {
                backend->closeArchive();
                delete backend;
            },
            Qt::QueuedConnection);
    } else {
        backend->closeArchive();
        delete backend;
    }
}

void ArchiveManager::clearPendingState()
{
    m_operation.pending = std::monostate{};
    m_operation.passwordRetries = 0;
}

void ArchiveManager::cleanupTempBackend()
{
    ArchiveBackend *backend = m_tempBackend;
    m_tempBackend = nullptr;
    if (!backend) {
        return;
    }

    disconnect(backend, nullptr, this, nullptr);
    if (m_workerThread.isRunning()) {
        QMetaObject::invokeMethod(
            backend,
            [backend]() {
                backend->closeArchive();
                delete backend;
            },
            Qt::QueuedConnection);
    } else {
        backend->closeArchive();
        delete backend;
    }
}

bool ArchiveManager::promoteCompressedArchive()
{
    if (!m_compressTempDir) {
        return true;
    }
    return CompressOutput::promote(QFileInfo(m_compressFinalPath),
                                   m_compressFormat,
                                   m_compressTempDir->path(),
                                   m_compressTargetExisted);
}

void ArchiveManager::removeFailedCompressOutput()
{
    if (m_compressTempDir || m_compressFinalPath.isEmpty()) {
        return;
    }
    CompressOutput::removeOutput(QFileInfo(m_compressFinalPath), m_compressFormat);
}

void ArchiveManager::cleanupCompressSession()
{
    m_compressTempDir.reset();
    m_compressFinalPath.clear();
    m_compressFormat = ArchiveFormat::Unknown;
    m_compressTargetExisted = false;
}
