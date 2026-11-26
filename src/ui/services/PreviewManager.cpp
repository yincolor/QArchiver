// QArchiver / 小Q解压
// PreviewManager worker lifecycle and continuous-preview preparation.
// PreviewManager 工作线程生命周期与连续看图准备。

#include "core/ArchiveBackendFactory.h"
#include "ui/services/PreviewManager.h"

#include "backends/ArchiveBackend.h"
#include "core/ArchiveManager.h"
#include "common/ArchiveOptions.h"
#include "core/AppSettings.h"
#include "common/ArchiveError.h"
#include "utils/FileUtils.h"
#include "utils/Logger.h"
#include "ui/services/ExternalEditManager.h"
#include "ui/services/PreviewUtils.h"
#include "ui/icons/MessageBoxUtils.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>

#include <atomic>
#include <memory>
#include <utility>

#include "ui/dialogs/ProgressDialog.h"

namespace {

// Open an extracted preview target, optionally through the system
// "Open with" picker on Windows.  Returns false when no application could be
// started, so the caller can skip the change tracking.
// 打开解压出来的预览目标，Windows 上可选用系统“打开方式”选择器。
// 无法启动任何程序时返回 false，以便调用方跳过修改监视。
bool openExtractedPath(const QString &path, bool openWith)
{
#ifdef Q_OS_WIN
    if (openWith) {
        if (!QProcess::startDetached(
                QStringLiteral("rundll32.exe"),
                {QStringLiteral("shell32.dll,OpenAs_RunDLL"),
                 QDir::toNativeSeparators(path)})) {
            MessageBoxUtils::showOpenFailure(nullptr, path);
            return false;
        }
        return true;
    }
#else
    Q_UNUSED(openWith);
#endif
    // On non-Windows there is no portable "Open with" picker; callers hide
    // the action and this fallback opens the system default program.
    // 非 Windows 没有可移植的“打开方式”选择器；调用方会隐藏该动作，
    // 这里仅作为默认程序打开的兜底。
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        MessageBoxUtils::showOpenFailure(nullptr, path);
        return false;
    }
    return true;
}

} // namespace

struct PreviewJob {
    QString archivePath;
    QString previewDir;
    QString destPath;
    QString password;
    ArchiveFormat format = ArchiveFormat::Unknown;
    QPointer<ProgressDialog> dialog;
    std::shared_ptr<std::atomic_bool> cancel =
        std::make_shared<std::atomic_bool>(false);
    std::atomic<ArchiveBackend *> backend{nullptr};
    bool finished = false;
    bool openWith = false;
    bool cancelled = false;
};

PreviewManager::PreviewManager()
{
}

PreviewManager &PreviewManager::instance()
{
    static PreviewManager manager;
    return manager;
}

QString PreviewManager::previewRoot() const
{
    const QString cacheDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return QDir(cacheDir).filePath(QStringLiteral("preview"));
}

QString PreviewManager::ownPreviewDir() const
{
    return QDir(previewRoot()).filePath(
        QString::number(QCoreApplication::applicationPid()));
}

QString PreviewManager::archivePreviewDir(const QString &archivePath) const
{
    // Include size and modification time in the cache identity: an archive
    // that was rewritten must not reuse files extracted from the old content.
    // 缓存标识包含大小与修改时间：被重写的压缩包不会复用旧内容解压出的文件。
    const QFileInfo archiveInfo(archivePath);
    const QString archiveIdentity =
        archivePath + QLatin1Char('|') + QString::number(archiveInfo.size())
        + QLatin1Char('|')
        + QString::number(archiveInfo.lastModified().toMSecsSinceEpoch());
    const QString archiveHash = QCryptographicHash::hash(
        archiveIdentity.toUtf8(), QCryptographicHash::Md5).toHex();
    return QDir(ownPreviewDir()).filePath(archiveHash);
}

void PreviewManager::preview(const QString &archivePath, ArchiveFormat format,
                             const QString &entryPath, qint64 entrySize,
                             const QStringList &imagePaths,
                             const QString &password, bool openWith)
{
    if (archivePath.isEmpty() || entryPath.isEmpty()) {
        return;
    }
    if (!openWith && entrySize > AppSettings::instance().maxPreviewSize()) {
        MessageBoxUtils::showWarning(
            nullptr, tr("Preview"),
            tr("This file is too large to preview (%1).")
                .arg(QLocale().formattedDataSize(entrySize)));
        return;
    }

    const QString previewDir = archivePreviewDir(archivePath);
    const QString normalized = FileUtils::normalizeEntryPath(entryPath);

    if (FileUtils::isPathTraversal(normalized, previewDir)) {
        LOG_WARN("PreviewManager", "Blocked unsafe preview path: %s",
                 entryPath.toUtf8().constData());
        return;
    }

    const QString destPath = QDir(previewDir).filePath(normalized);

    // Extract the clicked image together with its same-directory image
    // siblings.  Drag-out/extraction already preserves paths, so the system
    // viewer can browse the containing folder and switch images.
    // 将点击的图片与同目录图片一起解压。后端保留目录结构，
    // 系统查看器即可浏览所在目录并连续切换图片。
    QStringList selectedPaths;
    QSet<QString> selectedNormalized;
    auto addSelected = [&](const QString &rawPath) {
        const QString normalizedPath = FileUtils::normalizeEntryPath(rawPath);
        if (normalizedPath.isEmpty()
            || selectedNormalized.contains(normalizedPath)) {
            return;
        }
        if (FileUtils::isPathTraversal(normalizedPath, previewDir)) {
            LOG_WARN("PreviewManager", "Blocked unsafe preview path: %s",
                     rawPath.toUtf8().constData());
            return;
        }
        selectedNormalized.insert(normalizedPath);
        selectedPaths << rawPath;
    };

    for (const QString &imagePath : imagePaths) {
        addSelected(imagePath);
    }
    if (!selectedNormalized.contains(normalized)) {
        addSelected(entryPath);
    }
    if (!selectedNormalized.contains(normalized)) {
        return;
    }

    // Reuse the cache per file.  Overwriting cached images is not an option on
    // Windows: the system image viewer keeps the file open, so re-extracting
    // the whole folder fails with a sharing violation ("the file is in use").
    // 逐文件复用缓存。Windows 上不能覆盖缓存图片：系统图片查看器会一直占用
    // 已打开的文件，重新解压整个目录会因共享冲突（“文件被占用”）而失败。
    QStringList missingPaths;
    for (const QString &path : selectedPaths) {
        const QString normalizedPath = FileUtils::normalizeEntryPath(path);
        if (!QFileInfo::exists(QDir(previewDir).filePath(normalizedPath))) {
            missingPaths << path;
        }
    }
    if (missingPaths.isEmpty()) {
        openExtractedPath(destPath, openWith);
        return;
    }

    if (m_jobs.contains(previewDir)) {
        // A batch for this folder is already running.  Open the most recent
        // target after it finishes instead of extracting everything twice.
        // Remember its open-mode too: the worker was started with the first
        // request's mode and must not override this newer click.
        // 该目录的批量解压正在进行；等它结束后打开最新点击的目标，
        // 避免重复解压整个目录。同时记录最新的打开模式，不能让先启动的
        // worker 覆盖后一次“Open With”请求。
        if (PreviewJob *pendingJob = m_jobs.value(previewDir, nullptr)) {
            pendingJob->openWith = openWith;
        }
        m_pendingTargets.insert(previewDir, destPath);
        return;
    }

    QDir().mkpath(QFileInfo(destPath).absolutePath());

    auto *job = new PreviewJob;
    job->archivePath = archivePath;
    job->previewDir = previewDir;
    job->destPath = destPath;
    job->password = password;
    job->format = format;
    job->openWith = openWith;
    m_jobs.insert(previewDir, job);

    const int threshold =
        AppSettings::instance().continuousImageDialogThreshold();
    const bool needsDialog = PreviewUtils::shouldShowPreparationDialog(
        missingPaths.size(), threshold, false);
    if (needsDialog) {
        auto *dialog = new ProgressDialog(nullptr);
        dialog->setOperation(tr("Preparing Continuous Preview..."));
        dialog->setStatusText(tr("Preparing continuous preview..."));
        dialog->setIndeterminate(true);
        dialog->setOutputVisible(false);
        job->dialog = dialog;

        connect(dialog, &ProgressDialog::cancelled, this,
                [this, previewDir]() { cancelPreview(previewDir); });
        connect(dialog, &QDialog::rejected, this,
                [this, previewDir]() { cancelPreview(previewDir); });

        // Delay showing to avoid a flash for fast extractions.
        // 延迟显示，避免快速解压时对话框一闪而过。
        QTimer::singleShot(250, this, [this, previewDir]() {
            PreviewJob *pending = m_jobs.value(previewDir, nullptr);
            if (!pending || pending->finished || !pending->dialog) {
                return;
            }
            if (!pending->cancelled) {
                pending->dialog->show();
            }
        });
    }

    LOG_DEBUG("PreviewManager", "Preparing %lld of %lld preview file(s) in %s",
              static_cast<long long>(missingPaths.size()),
              static_cast<long long>(selectedPaths.size()),
              previewDir.toUtf8().constData());

    // Do not call the main backend from the GUI thread.  A private backend is
    // created, used and destroyed entirely inside the worker thread.
    // 不要在 GUI 线程调用主 backend。私有 backend 完全在工作线程内创建、使用和销毁。
    (void)QtConcurrent::run([archivePath, format, missingPaths, previewDir,
                              destPath, password, job]() {
        auto cancel = job->cancel;
        std::unique_ptr<ArchiveBackend> backend(
            ArchiveBackendFactory::createForRead(format, archivePath));

        bool success = false;
        bool cancelled = false;
        QString errorMessage;

        if (cancel->load(std::memory_order_relaxed)) {
            cancelled = true;
            errorMessage = QObject::tr("Cancelled");
        } else if (backend) {
            job->backend.store(backend.get(), std::memory_order_relaxed);
            if (cancel->load(std::memory_order_relaxed)) {
                backend->requestCancel();
            }

            QObject::connect(
                backend.get(), &ArchiveBackend::operationFinished,
                backend.get(),
                [&success, &errorMessage](bool ok, const QString &msg) {
                    success = ok;
                    errorMessage = msg;
                },
                Qt::DirectConnection);
            QObject::connect(
                backend.get(), &ArchiveBackend::errorOccurred,
                backend.get(),
                [&success, &cancelled, &errorMessage](ArchiveErrorCode code,
                                                      const QString &msg) {
                    success = false;
                    cancelled = (code == ArchiveErrorCode::Cancelled);
                    errorMessage = msg;
                },
                Qt::DirectConnection);
            QObject::connect(
                backend.get(), &ArchiveBackend::progressChanged,
                backend.get(),
                [previewDir](int percent, int total, int current, int etaSec) {
                    QMetaObject::invokeMethod(
                        &PreviewManager::instance(),
                        [previewDir, percent, total, current, etaSec]() {
                            PreviewManager::instance().updatePreviewProgress(
                                previewDir, percent, total, current, etaSec);
                        },
                        Qt::QueuedConnection);
                },
                Qt::DirectConnection);

            backend->openArchive(archivePath);

            ExtractOptions options;
            options.destDir = previewDir;
            options.selectedFiles = missingPaths;
            options.overwrite = OverwritePolicy::Always;
            options.preservePaths = true;
            options.password = password;

            backend->extract(options);
            backend->closeArchive();
            job->backend.store(nullptr, std::memory_order_relaxed);
        } else {
            errorMessage = QObject::tr("No backend available for preview");
        }

        cancelled = cancelled || cancel->load(std::memory_order_relaxed);
        QMetaObject::invokeMethod(
            &PreviewManager::instance(),
            [previewDir, destPath, success, cancelled, errorMessage]() {
                PreviewManager::instance().finishPreview(
                    previewDir, destPath, success, cancelled, errorMessage);
            },
            Qt::QueuedConnection);
    });
}

void PreviewManager::updatePreviewProgress(const QString &previewDir,
                                           int percent, int total,
                                           int current, int etaSec)
{
    PreviewJob *job = m_jobs.value(previewDir, nullptr);
    if (!job || !job->dialog) {
        return;
    }
    job->dialog->setIndeterminate(false);
    job->dialog->updateProgress(percent, total, current, etaSec);
    job->dialog->setStatusText(
        tr("Preparing images... %1 / %2").arg(current).arg(total));
}

void PreviewManager::cancelPreview(const QString &previewDir)
{
    PreviewJob *job = m_jobs.value(previewDir, nullptr);
    if (!job || job->finished) {
        return;
    }
    job->cancelled = true;
    job->cancel->store(true, std::memory_order_relaxed);
    if (ArchiveBackend *backend =
            job->backend.load(std::memory_order_relaxed)) {
        backend->requestCancel();
    }
    if (job->dialog) {
        job->dialog->setStatusText(tr("Cancelling..."));
    }
}

void PreviewManager::finishPreview(const QString &previewDir,
                                   const QString &destPath, bool success,
                                   bool cancelled,
                                   const QString &errorMessage)
{
    PreviewJob *job = m_jobs.take(previewDir);
    if (!job) {
        return;
    }
    job->finished = true;
    job->cancelled = job->cancelled || cancelled;
    const bool openWith = job->openWith;
    const QString archivePath = job->archivePath;
    const QString password = job->password;
    const ArchiveFormat format = job->format;

    QString targetPath = destPath;
    const QString pending = m_pendingTargets.take(previewDir);
    if (!pending.isEmpty()) {
        targetPath = pending;
    }

    // Close the preparation dialog before opening the system viewer.
    // 在打开系统查看器前关闭准备对话框。
    if (job->dialog) {
        disconnect(job->dialog, nullptr, this, nullptr);
        job->dialog->close();
    }

    const bool wasCancelled = job->cancelled;
    delete job;

    const bool fileExists = QFileInfo::exists(targetPath);
    if (fileExists) {
        if (!success) {
            LOG_WARN("PreviewManager",
                     "Preview extraction reported failure but target exists: %s",
                     targetPath.toUtf8().constData());
        }
        if (openExtractedPath(targetPath, openWith)) {
            trackExternalEdit(previewDir, targetPath, archivePath, format,
                              password);
        }
        return;
    }

    if (wasCancelled) {
        return;
    }

    LOG_WARN("PreviewManager", "Preview failed: %s",
             errorMessage.toUtf8().constData());
    MessageBoxUtils::showWarning(
        nullptr, tr("Preview"),
        (success || errorMessage.isEmpty())
            ? tr("Unable to extract this file for preview.")
            : errorMessage);
}

void PreviewManager::trackExternalEdit(const QString &previewDir,
                                       const QString &targetPath,
                                       const QString &archivePath,
                                       ArchiveFormat format,
                                       const QString &password)
{
    // The write-back only makes sense while the archive itself is open and
    // writable; for read-only formats (iso/rar/split volumes/compressed tar)
    // the file is opened for viewing only.
    // 只有压缩包本身已打开且可写时写回才有意义；只读格式
    //（iso/rar/分卷/压缩 tar）仅用于查看。
    ArchiveManager &manager = ArchiveManager::instance();
    if (manager.currentPath() != archivePath
        || !manager.canModifyCurrentArchive()) {
        return;
    }

    // The preview cache mirrors the archive tree, so the path relative to the
    // cache root is the member path inside the archive.
    // 预览缓存镜像了压缩包目录结构，因此相对缓存根目录的路径就是压缩包内路径。
    const QString entryPath = QDir(previewDir).relativeFilePath(targetPath);
    if (entryPath.isEmpty() || entryPath.startsWith(QStringLiteral(".."))) {
        return;
    }

    ExternalEditManager::instance().track(
        targetPath, previewDir, entryPath, archivePath, format, password,
        manager.nameEncoding());
}

void PreviewManager::clearArchiveCache(const QString &archivePath)
{
    if (archivePath.isEmpty()) {
        return;
    }
    const QString previewDir = archivePreviewDir(archivePath);
    if (m_jobs.contains(previewDir)) {
        cancelPreview(previewDir);
    }
    m_pendingTargets.remove(previewDir);
    if (!QDir(previewDir).removeRecursively() && QFileInfo::exists(previewDir)) {
        // Windows keeps previewed images open in the system viewer.  Leaving
        // the directory in place is intentional: cleanupStale() retries on the
        // next start instead of failing the close path.
        // Windows 上系统图片查看器会占用预览文件。这里保留目录，由下次启动的
        // cleanupStale() 重试，而不是让关闭流程失败。
        LOG_DEBUG("PreviewManager",
                  "Could not remove preview cache %s; it will be retried on next start",
                  previewDir.toUtf8().constData());
    }
}

void PreviewManager::cleanupStale()
{
    QDir root(previewRoot());
    if (!root.exists()) {
        return;
    }

    const QStringList entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool ok = false;
        const qint64 pid = entry.toLongLong(&ok);
        if (ok && pid > 0 && !FileUtils::isProcessRunning(pid)) {
            QDir(root.absoluteFilePath(entry)).removeRecursively();
            LOG_DEBUG("PreviewManager", "Removed stale preview dir for PID %lld", pid);
        }
    }
}

void PreviewManager::cleanupOwn()
{
    for (PreviewJob *job : std::as_const(m_jobs)) {
        if (!job) {
            continue;
        }
        job->cancel->store(true, std::memory_order_relaxed);
        if (ArchiveBackend *backend =
                job->backend.load(std::memory_order_relaxed)) {
            backend->requestCancel();
        }
        if (job->dialog) {
            disconnect(job->dialog, nullptr, this, nullptr);
            job->dialog->close();
            job->dialog = nullptr;
        }
        // Do not delete the job: a worker may still hold this pointer while
        // the process is shutting down.  The process itself is about to exit.
        // 不要删除 job：进程关闭期间工作线程可能仍持有该指针。
    }
    m_jobs.clear();
    m_pendingTargets.clear();

#ifndef Q_OS_WIN
    // On Windows the system image viewer keeps the preview file open, so
    // deleting the cache on exit fails (and can break continuous viewing).
    // cleanupStale() removes the directory on the next start instead.
    // Windows 上系统图片查看器会占用预览文件，退出时删除缓存会失败
    //（并影响连续看图）。改由下次启动时的 cleanupStale() 清理。
    QDir dir(ownPreviewDir());
    if (dir.exists()) {
        dir.removeRecursively();
    }
#endif
}
