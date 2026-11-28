// QArchiver / 小Q解压
// 7-Zip command construction, listing, extraction, writing and splits.
// 7-Zip 命令构建、列表、解压、写入及分卷处理。

#include "backends/sevenzip/SevenZipBackend.h"

#include "backends/ExtractionUtils.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

#include <QSet>

#include "backends/sevenzip/SevenZipListParser.h"
#include "backends/sevenzip/SevenZipUtils.h"
#include "utils/EncodingDetector.h"
#include "utils/FileUtils.h"
#include "utils/Logger.h"
#include "utils/MimeTypeHelper.h"
#include "utils/ProcessRunner.h"

namespace {

// 7-Zip reports missing split volumes on stdout (stderr stays empty for
// `l -slt`), so inspect both channels before falling back to a generic error.
// 7-Zip 会把缺失分卷报告到 stdout（`l -slt` 的 stderr 为空），
// 因此两个通道都要检查后再回退到通用错误。
bool isMissingVolumeError(const ProcessResult &result)
{
    return (result.stdOut + QLatin1Char('\n') + result.stdErr)
        .contains(QStringLiteral("Missing volume"), Qt::CaseInsensitive);
}

// Parse 7-Zip progress lines such as " 42% 3 + file" (stdout with -bsp1).
// 解析 7-Zip 的进度行，例如“ 42% 3 + file”（配合 -bsp1 输出到 stdout）。
int sevenZipPercent(const QString &line)
{
    const QString trimmed = line.trimmed();
    int digits = 0;
    while (digits < trimmed.size() && trimmed.at(digits).isDigit()) {
        ++digits;
    }
    if (digits == 0 || digits >= trimmed.size()
        || trimmed.at(digits) != QLatin1Char('%')) {
        return -1;
    }
    bool ok = false;
    const int value = trimmed.left(digits).toInt(&ok);
    return ok ? qBound(0, value, 100) : -1;
}

} // namespace

SevenZipBackend::SevenZipBackend(ArchiveFormat fmt, QObject *parent)
    : ArchiveBackend(parent)
    , m_format(fmt)
{
}

QString SevenZipBackend::sevenZipPath() const
{
    return ProcessRunner::resolveToolPath(QStringLiteral("7z"));
}

QString SevenZipBackend::formatSwitch() const
{
    return m_format == ArchiveFormat::Zip ? QStringLiteral("-tzip") : QString();
}

bool SevenZipBackend::isSplitArchive() const
{
    return MimeTypeHelper::isSplitVolume(m_archivePath);
}

QString SevenZipBackend::targetPath() const
{
    return isSplitArchive() ? MimeTypeHelper::resolveFirstVolume(m_archivePath)
                            : m_archivePath;
}

void SevenZipBackend::appendPassword(QStringList &args, const QString &password) const
{
    // Always pass -p, even when the password is empty.  Without it, 7-Zip
    // prompts for a password on stdin when the archive has encrypted headers
    // and the child process would block forever.
    // 即使密码为空也始终传递 -p。否则当压缩包加密了文件头时，
    // 7-Zip 会从 stdin 提示输入密码，导致子进程永久阻塞。
    args << QStringLiteral("-p") + password;
}

int SevenZipBackend::validateLevel(int level) const
{
    return qBound(0, level, 9);
}

void SevenZipBackend::openArchive(const QString &filePath)
{
    const QFileInfo info(filePath);
    if (!info.exists()) {
        emit errorOccurred(ArchiveErrorCode::FileNotFound,
                           tr("File not found: %1").arg(filePath));
        return;
    }
    if (!info.isReadable()) {
        emit errorOccurred(ArchiveErrorCode::PermissionDenied,
                           tr("File is not readable: %1").arg(filePath));
        return;
    }
    m_archivePath = filePath;
    m_entries.clear();
    m_encrypted = false;
    m_nameEncoding = QStringLiteral("UTF-8");
    m_isOpen = true;
    emit archiveOpened(filePath);
}

void SevenZipBackend::listContents(const ListOptions &options)
{
    if (!m_isOpen) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("Archive not opened"));
        return;
    }

    const QString sevenZip = sevenZipPath();
    if (sevenZip.isEmpty()) {
        LOG_ERROR("SevenZipBackend", "Cannot list %s: 7z tool not found",
                  m_archivePath.toUtf8().constData());
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("7z tool not found"));
        return;
    }

    QElapsedTimer timer;
    timer.start();
    LOG_DEBUG("SevenZipBackend", "Listing %s with %s (format %s, password=%d)",
              m_archivePath.toUtf8().constData(),
              sevenZip.toUtf8().constData(),
              formatToString(format()).toUtf8().constData(),
              options.password.isEmpty() ? 0 : 1);

    QStringList args;
    args << QStringLiteral("l") << QStringLiteral("-slt")
         << QStringLiteral("-sccUTF-8")
         << QStringLiteral("-bsp1");
    appendPassword(args, options.password);
    args << QStringLiteral("--") << targetPath();

    SevenZipListParser parser;
    ProcessOutputOptions outputOptions;
    outputOptions.streamStdOut = true;
    outputOptions.maxStdOutBytes = 256 * 1024;
    outputOptions.maxRawSampleBytes = 64 * 1024;

    const ProcessResult result = ProcessRunner::run(
        sevenZip, args, QString(), QProcessEnvironment(),
        QString(), 300000, &m_cancelRequested,
        [&parser](const QString &line, bool isError) {
            if (!isError) {
                parser.feedLine(line);
            }
        },
        outputOptions);

    if (result.cancelled) {
        emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
        return;
    }
    if (!result.started) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("Failed to start 7z"));
        return;
    }

    if (!result.success()) {
        if (isMissingVolumeError(result)) {
            emit errorOccurred(ArchiveErrorCode::VolumeMissing,
                               tr("A required archive volume is missing"));
            return;
        }

        const QString errorText = result.stdErr;
        // 7-Zip reports encrypted headers as "Cannot open encrypted
        // archive. Wrong password?"; a plain "Can not open" is usually a
        // corrupted/missing archive, so do not turn every open failure into a
        // password prompt.
        // 7-Zip 对加密文件头会报告 "Cannot open encrypted archive. Wrong
        // password?"；普通的 "Can not open" 通常是压缩包损坏或缺失，
        // 因此不能把所有打开失败都当成密码请求。
        const bool looksLikePassword = errorText.contains(QStringLiteral("password"), Qt::CaseInsensitive)
            || errorText.contains(QStringLiteral("encrypted archive"), Qt::CaseInsensitive);

        if (looksLikePassword) {
            m_encrypted = true;
            if (options.password.isEmpty()) {
                LOG_INFO("SevenZipBackend", "%s needs a password to list",
                         m_archivePath.toUtf8().constData());
                emit passwordRequested();
                return;
            }
            LOG_WARN("SevenZipBackend", "Wrong password for %s",
                     m_archivePath.toUtf8().constData());
            emit errorOccurred(ArchiveErrorCode::WrongPassword, tr("Wrong password"));
            return;
        }

        LOG_DEBUG("SevenZipBackend", "Listing %s failed (exit %d): %s",
                 m_archivePath.toUtf8().constData(), result.exitCode,
                 result.stdErr.trimmed().toUtf8().constData());
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("List failed: %1").arg(errorText.trimmed()));
        return;
    }

    m_entries = parser.takeEntries();
    // Vote on the member-name lines only: the raw stdout also carries the
    // 7-Zip banner, sizes and timestamps, which would bias the detection.
    // 仅对成员名行投票：原始 stdout 还包含 7-Zip 横幅、大小与时间戳，
    // 会干扰检测。
    const QByteArray nameSample = SevenZipUtils::sampleNameBytes(result.stdOutRaw);
    m_nameEncoding = nameSample.isEmpty() ? QStringLiteral("UTF-8")
                                          : EncodingDetector::detect(nameSample);
    LOG_INFO("SevenZipBackend", "Listed %d entries from %s (name encoding %s, %lld ms)",
             static_cast<int>(m_entries.size()),
             m_archivePath.toUtf8().constData(),
             m_nameEncoding.toUtf8().constData(),
             static_cast<long long>(timer.elapsed()));
    if (!nameSample.isEmpty()
        && m_nameEncoding.compare(QStringLiteral("UTF-8"), Qt::CaseInsensitive) != 0) {
        // Legacy code page: report it explicitly, since mojibake reports hinge
        // on whether the archive or the decoding was wrong.
        // 旧式代码页：显式记录，乱码问题需区分压缩包本身还是解码造成的。
        LOG_WARN("SevenZipBackend",
                 "%s uses non-UTF-8 member names; decoded as %s",
                 m_archivePath.toUtf8().constData(),
                 m_nameEncoding.toUtf8().constData());
    }
    m_encrypted = std::any_of(m_entries.cbegin(), m_entries.cend(),
                              [](const ArchiveEntry &entry) { return entry.isEncrypted; });
    emit fileListReady(m_entries);
}

void SevenZipBackend::extract(const ExtractOptions &options)
{
    if (!m_isOpen) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("Archive not opened"));
        return;
    }
    if (!options.encodingHint.isEmpty()) {
        // An explicit user choice overrides the detected encoding so the
        // listfile is written in the code page the archive really uses.
        // 用户显式选择会覆盖检测结果，使列表文件按压缩包实际使用的
        // 代码页写出。
        m_nameEncoding = options.encodingHint;
    }

    const QString sevenZip = sevenZipPath();
    if (sevenZip.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("7z tool not found"));
        return;
    }

    for (const QString &selected : options.selectedFiles) {
        if (FileUtils::isPathTraversal(selected, options.destDir)) {
            emit errorOccurred(ArchiveErrorCode::PathTraversalBlocked,
                               tr("Blocked unsafe path: %1").arg(selected));
            return;
        }
    }

    QDir().mkpath(options.destDir);
    const QString tempDir = FileUtils::createTempExtractDir(options.destDir);

    QStringList args;
    args << QStringLiteral("x")
         << QStringLiteral("-o") + tempDir
         << QStringLiteral("-y")
         << QStringLiteral("-spd")
         << QStringLiteral("-sccUTF-8")
         << QStringLiteral("-bsp1");
    appendPassword(args, options.password);

    // Pass selected members through a listfile instead of the command line:
    // 7-Zip interprets positional names as wildcards, treats leading '-' as a
    // switch and leading '@' as a nested listfile.
    // 通过列表文件而不是命令行传递选定成员：7-Zip 会把位置参数当作通配符，
    // 把前导 '-' 当作开关，把前导 '@' 当作嵌套列表文件。
    SevenZipUtils::ListFile membersFile(QStringLiteral("7z-members"));
    const QStringList selectedPaths =
        ExtractionUtils::buildExtractionSelection(m_entries, options);
    if (!selectedPaths.isEmpty()) {
        if (!SevenZipUtils::appendListFileArgs(membersFile, selectedPaths,
                                               m_nameEncoding, args)) {
            FileUtils::removeTemporaryDirectory(tempDir);
            emit errorOccurred(ArchiveErrorCode::Unknown,
                               tr("Could not prepare the selected files for extraction"));
            return;
        }
    } else if (!options.skippedSymlinks.isEmpty()) {
        // Every requested entry was an unsafe symlink the user chose to skip.
        // 所有请求的条目都是用户选择跳过的危险符号链接。
        FileUtils::removeTemporaryDirectory(tempDir);
        emit operationFinished(true, tr("No files were extracted"));
        return;
    }

    args << QStringLiteral("--") << targetPath();

    LOG_DEBUG("SevenZipBackend", "Extract: %s",
              ProcessRunner::maskedCommand(sevenZip, args).toUtf8().constData());

    const int total = options.selectedFiles.isEmpty()
        ? m_entries.size()
        : options.selectedFiles.size();
    int processed = 0;

    ProcessOutputOptions outputOptions;
    outputOptions.maxStdOutBytes = 1024 * 1024;
    outputOptions.maxRawSampleBytes = 0;

    const ProcessResult result = ProcessRunner::run(
        sevenZip, args, QString(), QProcessEnvironment(),
        QString(), 0, &m_cancelRequested,
        [this, total, &processed](const QString &line, bool isError) {
            if (isError || line.isEmpty()) {
                return;
            }
            const int percent = sevenZipPercent(line);
            if (percent >= 0) {
                const int current =
                    total > 0 ? qBound(0, total * percent / 100, total) : 0;
                emit progressChanged(percent, total, current, -1);
                return;
            }
            emit outputReady(line);
            if (line.startsWith(QLatin1String("- "))
                || line.startsWith(QLatin1String("+ "))
                || line.startsWith(QLatin1String("U "))) {
                ++processed;
                const int progress = total > 0
                    ? qBound(0, processed * 100 / total, 100)
                    : 0;
                emit progressChanged(progress, total, processed, -1);
            }
        },
        outputOptions);

    if (result.cancelled) {
        FileUtils::removeTemporaryDirectory(tempDir);
        emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
        return;
    }
    if (!result.started) {
        FileUtils::removeTemporaryDirectory(tempDir);
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("Failed to start 7z"));
        return;
    }
    if (!result.success()) {
        FileUtils::removeTemporaryDirectory(tempDir);
        if (isMissingVolumeError(result)) {
            emit errorOccurred(ArchiveErrorCode::VolumeMissing,
                               tr("A required archive volume is missing"));
            return;
        }
        const QString errorText = result.stdErr;
        const bool looksLikePassword =
            errorText.contains(QStringLiteral("Wrong password"), Qt::CaseInsensitive)
            || errorText.contains(QStringLiteral("password"), Qt::CaseInsensitive)
            || (m_encrypted
                && errorText.contains(QStringLiteral("Data Error"),
                                      Qt::CaseInsensitive));
        if (looksLikePassword && options.password.isEmpty()) {
            emit passwordRequested();
        } else if (looksLikePassword) {
            emit errorOccurred(ArchiveErrorCode::WrongPassword, tr("Wrong password"));
        } else {
            emit errorOccurred(ArchiveErrorCode::Unknown,
                               tr("Extraction failed: %1").arg(errorText.trimmed()));
        }
        return;
    }

#ifndef Q_OS_WIN
    // Convert legacy-codepage filenames written by 7-Zip into UTF-8 before the
    // temporary tree is moved to its final destination.
    // 在临时目录移动到最终位置前，将 7-Zip 写出的旧代码页文件名转换为 UTF-8。
    if (m_nameEncoding.compare(QStringLiteral("UTF-8"), Qt::CaseInsensitive) != 0) {
        SevenZipUtils::renameLegacyExtractedTree(QFile::encodeName(tempDir), m_nameEncoding);
    }
#endif

    if (!FileUtils::unsafeExtractedSymlinks(tempDir).isEmpty()) {
        FileUtils::removeTemporaryDirectory(tempDir);
        emit errorOccurred(ArchiveErrorCode::PathTraversalBlocked,
                           tr("Blocked unsafe symbolic link during extraction"));
        return;
    }

    const int failedMoves = FileUtils::moveExtractedContents(
        tempDir, options.destDir, options.overwrite, options.preservePaths);
    if (failedMoves > 0) {
        FileUtils::removeTemporaryDirectory(tempDir);
        emit errorOccurred(ArchiveErrorCode::PartialExtraction,
            tr("Extraction completed, but %1 entr%2 could not be moved. "
               "Files already moved to the destination were not rolled back.")
                .arg(failedMoves)
                .arg(failedMoves == 1 ? QStringLiteral("y") : QStringLiteral("ies")));
        return;
    }

    emit progressChanged(100, total, total, -1);
    emit operationFinished(true, tr("Extraction completed"));
}

void SevenZipBackend::compress(const CompressOptions &options)
{
    if (isReadOnly()) {
        emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                           tr("This archive format is read-only"));
        return;
    }

    const QString sevenZip = sevenZipPath();
    if (sevenZip.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("7z tool not found"));
        return;
    }

    QStringList args;
    args << QStringLiteral("a");
    if (!formatSwitch().isEmpty()) {
        args << formatSwitch();
    }
    args << QStringLiteral("-mx=%1").arg(validateLevel(options.compressionLevel))
         << QStringLiteral("-mmt=on")
         << QStringLiteral("-sccUTF-8")
         << QStringLiteral("-bsp1");

    if (m_format == ArchiveFormat::SevenZip && options.solidMode) {
        args << QStringLiteral("-ms=on");
    }
    if (!options.password.isEmpty()) {
        args << QStringLiteral("-p") + options.password;
        if (m_format == ArchiveFormat::SevenZip && options.encryptHeaders) {
            args << QStringLiteral("-mhe=on");
        }
    }
    if (options.volumeSize > 0) {
        args << QStringLiteral("-v%1b").arg(options.volumeSize);
    }
    // 7-Zip follows symlinks by default; -snl stores them as links.
    // 7-Zip 默认跟随符号链接；-snl 将符号链接按链接本身保存。
    if (!options.followSymlinks) {
        args << QStringLiteral("-snl");
    }
    if (options.storeFullPath) {
        args << QStringLiteral("-spf");
    }
    for (const QString &pattern : options.excludePatterns) {
        if (!pattern.trimmed().isEmpty()) {
            args << QStringLiteral("-x!") + pattern;
        }
    }

    SevenZipUtils::ListFile sourceListFile(QStringLiteral("7z-sources"));
    if (!SevenZipUtils::appendListFileArgs(sourceListFile, options.sourceFiles,
                            QStringLiteral("UTF-8"), args)) {
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Could not prepare the source files for compression"));
        return;
    }

    // -spd disables wildcard matching for both filenames and -x patterns, so
    // only enable it when no exclude patterns are in use.
    // -spd 会同时禁用文件名与 -x 模式的通配符匹配，
    // 因此仅在没有排除规则时启用。
    if (options.excludePatterns.isEmpty()) {
        args << QStringLiteral("-spd");
    }

    args << QStringLiteral("--") << options.archivePath;

    LOG_DEBUG("SevenZipBackend", "Compress: %s",
              ProcessRunner::maskedCommand(sevenZip, args).toUtf8().constData());

    emit outputReady(tr("Creating %1").arg(options.archivePath));

    const int total = FileUtils::countFiles(options.sourceFiles);
    int processed = 0;

    ProcessOutputOptions outputOptions;
    outputOptions.maxStdOutBytes = 1024 * 1024;
    outputOptions.maxRawSampleBytes = 0;

    const ProcessResult result = ProcessRunner::run(
        sevenZip, args, QString(), QProcessEnvironment(),
        QString(), 0, &m_cancelRequested,
        [this, total, &processed](const QString &line, bool isError) {
            if (isError || line.isEmpty()) {
                return;
            }
            const int percent = sevenZipPercent(line);
            if (percent >= 0) {
                // Percentages go through progressChanged only: the dialog shows
                // them in its progress bar and stats line.  Routing them
                // through outputReady used to overwrite the "current file"
                // label with "Compressing... 40%".
                // 百分比只走 progressChanged：对话框用它更新进度条与统计行。
                // 之前经 outputReady 会把握手文件名的标签覆盖成
                // “正在压缩... 40%”。
                const int current =
                    total > 0 ? qBound(0, total * percent / 100, total) : 0;
                emit progressChanged(percent, total, current, -1);
                return;
            }
            emit outputReady(line);
            if (line.startsWith(QLatin1String("+ "))) {
                ++processed;
                const int progress = total > 0
                    ? qBound(0, processed * 100 / total, 100)
                    : 0;
                emit progressChanged(progress, total, processed, -1);
            }
        },
        outputOptions);

    if (result.cancelled) {
        QFile::remove(options.archivePath);
        emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
        return;
    }
    if (!result.started) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("Failed to start 7z"));
        return;
    }
    if (!result.success()) {
        QFile::remove(options.archivePath);
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Compression failed: %1").arg(result.stdErr.trimmed()));
        return;
    }

    emit progressChanged(100, total, processed, -1);
    emit operationFinished(true, tr("Archive created: %1").arg(options.archivePath));
}

void SevenZipBackend::addFiles(const AddFilesOptions &options)
{
    if (isReadOnly()) {
        emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                           tr("This archive format is read-only"));
        return;
    }
    if (!supportsAppend()) {
        emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                           tr("Cannot add files to a split archive"));
        return;
    }

    const QString sevenZip = sevenZipPath();
    if (sevenZip.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("7z tool not found"));
        return;
    }

    QStringList args;
    args << QStringLiteral("a");
    if (!formatSwitch().isEmpty()) {
        args << formatSwitch();
    }
    args << QStringLiteral("-mx=%1").arg(validateLevel(options.compressionLevel))
         << QStringLiteral("-sccUTF-8")
         << QStringLiteral("-bsp1");
    if (m_encrypted && options.password.isEmpty()) {
        emit passwordRequested();
        return;
    }
    if (!options.password.isEmpty()) {
        args << QStringLiteral("-p") + options.password;
    }
    // 7-Zip follows symlinks by default; -snl stores them as links.
    if (!options.followSymlinks) {
        args << QStringLiteral("-snl");
    }
    if (options.storeFullPath) {
        args << QStringLiteral("-spf");
    }

    // External-edit write-back switches 7-Zip into "run inside the staged
    // archive root" mode so the edited file replaces the member it came from.
    // 外部编辑写回会让 7-Zip 在暂存的压缩包根目录中运行，
    // 使被编辑的文件替换它原来的成员。
    const bool writeBack = !options.sourceRoot.isEmpty();
    QStringList sourceFiles;
    QString workingDir;
    QString sourceEncoding = QStringLiteral("UTF-8");
    if (writeBack) {
        sourceEncoding = options.nameEncoding.isEmpty()
            ? QStringLiteral("UTF-8")
            : options.nameEncoding;
        workingDir = options.sourceRoot;
        for (const QString &relative : options.sourceFiles) {
            const QString normalized = FileUtils::normalizeEntryPath(relative);
            if (normalized.isEmpty()
                || normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts)
                       .contains(QStringLiteral(".."))) {
                emit errorOccurred(ArchiveErrorCode::PathTraversalBlocked,
                                   tr("Blocked unsafe path: %1").arg(relative));
                return;
            }
            sourceFiles << relative;
        }
    } else {
        sourceFiles = options.sourceFiles;
    }
    if (sourceFiles.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("No source files selected"));
        return;
    }

    SevenZipUtils::ListFile sourceListFile(QStringLiteral("7z-sources"));
    if (!SevenZipUtils::appendListFileArgs(sourceListFile, sourceFiles,
                            sourceEncoding, args)) {
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Could not prepare the source files for adding"));
        return;
    }
    args << QStringLiteral("-spd")
         << QStringLiteral("--") << options.archivePath;

    const ProcessResult result = ProcessRunner::run(
        sevenZip, args, QString(), QProcessEnvironment(),
        workingDir, 300000, &m_cancelRequested);

    if (result.cancelled) {
        emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
        return;
    }
    if (!result.success()) {
        if (options.password.isEmpty()
            && result.stdErr.contains(QStringLiteral("password"), Qt::CaseInsensitive)) {
            emit passwordRequested();
            return;
        }
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Add files failed: %1").arg(result.stdErr.trimmed()));
        return;
    }
    emit operationFinished(true, writeBack ? tr("Changes saved to the archive")
                                           : tr("Files added to archive"));
}

void SevenZipBackend::deleteFiles(const DeleteFilesOptions &options)
{
    if (isReadOnly()) {
        emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                           tr("This archive format is read-only"));
        return;
    }
    if (!supportsAppend()) {
        emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                           tr("Cannot delete files from a split archive"));
        return;
    }

    const QString sevenZip = sevenZipPath();
    if (sevenZip.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("7z tool not found"));
        return;
    }

    QStringList args;
    args << QStringLiteral("d");
    if (!formatSwitch().isEmpty()) {
        args << formatSwitch();
    }
    args << QStringLiteral("-spd") << QStringLiteral("-sccUTF-8")
         << QStringLiteral("-bsp1");
    if (m_encrypted && options.password.isEmpty()) {
        emit passwordRequested();
        return;
    }
    if (!options.password.isEmpty()) {
        args << QStringLiteral("-p") + options.password;
    }

    QStringList targetEntries;
    targetEntries.reserve(options.targetEntries.size());
    for (const QString &entry : options.targetEntries) {
        const QString normalized = FileUtils::normalizeEntryPath(entry);
        if (normalized.isEmpty()) {
            continue;
        }
        if (normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts)
                .contains(QStringLiteral(".."))) {
            emit errorOccurred(ArchiveErrorCode::PathTraversalBlocked,
                               tr("Blocked unsafe path: %1").arg(entry));
            return;
        }
        targetEntries << entry;
    }

    SevenZipUtils::ListFile membersFile(QStringLiteral("7z-members"));
    if (targetEntries.isEmpty()
        || !SevenZipUtils::appendListFileArgs(membersFile, targetEntries,
                               m_nameEncoding, args)) {
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Could not prepare the selected files for deletion"));
        return;
    }

    args << QStringLiteral("--") << options.archivePath;

    const ProcessResult result = ProcessRunner::run(
        sevenZip, args, QString(), QProcessEnvironment(),
        QString(), 300000, &m_cancelRequested);

    if (result.cancelled) {
        emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
        return;
    }
    if (!result.success()) {
        if (options.password.isEmpty()
            && result.stdErr.contains(QStringLiteral("password"), Qt::CaseInsensitive)) {
            emit passwordRequested();
            return;
        }
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Delete files failed: %1").arg(result.stdErr.trimmed()));
        return;
    }
    emit operationFinished(true, tr("Files deleted from archive"));
}

void SevenZipBackend::testArchive(const TestOptions &options)
{
    const QString sevenZip = sevenZipPath();
    if (sevenZip.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("7z tool not found"));
        return;
    }

    QStringList args;
    args << QStringLiteral("t") << QStringLiteral("-sccUTF-8")
         << QStringLiteral("-bsp1");
    appendPassword(args, options.password);
    args << QStringLiteral("--") << targetPath();

    const ProcessResult result = ProcessRunner::run(
        sevenZip, args, QString(), QProcessEnvironment(),
        QString(), 300000, &m_cancelRequested);

    if (result.cancelled) {
        emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
        return;
    }
    if (!result.success()) {
        if (isMissingVolumeError(result)) {
            emit errorOccurred(ArchiveErrorCode::VolumeMissing,
                               tr("A required archive volume is missing"));
            return;
        }
        const bool looksLikePassword =
            result.stdErr.contains(QStringLiteral("password"), Qt::CaseInsensitive)
            || (m_encrypted
                && result.stdErr.contains(QStringLiteral("Data Error"),
                                          Qt::CaseInsensitive));
        if (looksLikePassword && options.password.isEmpty()) {
            emit passwordRequested();
        } else if (looksLikePassword) {
            emit errorOccurred(ArchiveErrorCode::WrongPassword, tr("Wrong password"));
        } else {
            emit errorOccurred(ArchiveErrorCode::CorruptedArchive,
                               tr("Archive test failed"));
        }
        return;
    }
    emit operationFinished(true, tr("Archive integrity verified"));
}

void SevenZipBackend::closeArchive()
{
    m_isOpen = false;
    m_archivePath.clear();
    m_entries.clear();
    m_encrypted = false;
    m_nameEncoding = QStringLiteral("UTF-8");
}

bool SevenZipBackend::supportsAppend() const
{
    return m_isOpen && !isSplitArchive();
}
