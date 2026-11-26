// QArchiver / 小Q解压
// Two-stage tar/tar.gz/tar.xz/tar.bz2 creation, listing and extraction.
// tar/tar.gz/tar.xz/tar.bz2 的两阶段创建、列表与解压。

#include "backends/tar/TarBackend.h"

#include "backends/ExtractionUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <memory>

#include <QSet>

#include "backends/sevenzip/SevenZipListParser.h"
#include "backends/sevenzip/SevenZipUtils.h"
#include "utils/EncodingDetector.h"
#include "utils/FileUtils.h"
#include "utils/Logger.h"
#include "utils/ProcessRunner.h"

namespace {

QString commonParentDirectory(const QStringList &files)
{
    if (files.isEmpty()) {
        return QString();
    }

    auto isUnder = [](const QString &path, const QString &base) {
        if (base == QStringLiteral("/")) {
            return path.startsWith(QLatin1Char('/'));
        }
        return path == base || path.startsWith(base + QLatin1Char('/'));
    };

    QString common = QFileInfo(files.first()).absolutePath();
    for (const QString &file : files) {
        QString dir = QFileInfo(file).absolutePath();
        while (!isUnder(dir, common)) {
            const QString parent = QFileInfo(common).absolutePath();
            if (parent == common) {
                break;
            }
            common = parent;
        }
    }
    return common;
}

QString compressionSwitch(ArchiveFormat fmt)
{
    switch (fmt) {
    case ArchiveFormat::TarGz:  return QStringLiteral("-tgzip");
    case ArchiveFormat::TarBz2: return QStringLiteral("-tbzip2");
    case ArchiveFormat::TarXz:  return QStringLiteral("-txz");
    default:                    return QString();
    }
}

} // namespace

TarBackend::TarBackend(ArchiveFormat fmt, QObject *parent)
    : ArchiveBackend(parent)
    , m_format(fmt)
{
}

bool TarBackend::isCompressed() const
{
    return m_format != ArchiveFormat::Tar;
}

QString TarBackend::compressionSwitch() const
{
    return ::compressionSwitch(m_format);
}

int TarBackend::validateCompressionLevel(int level) const
{
    if (m_format == ArchiveFormat::Tar) {
        return -1;
    }
    int minLevel = 0;
    if (m_format == ArchiveFormat::TarGz || m_format == ArchiveFormat::TarBz2) {
        minLevel = 1;
    }
    if (level < minLevel || level > 9) {
        return -1;
    }
    return level;
}

void TarBackend::openArchive(const QString &filePath)
{
    m_innerTarPath.clear();
    m_innerTarDir.reset();
    m_innerTarSize = -1;
    m_innerTarMtime = 0;

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
    m_archivePath = filePath;
    m_entries.clear();
    m_nameEncoding = QStringLiteral("UTF-8");
    m_isOpen = true;
    emit archiveOpened(filePath);
}

TarBackend::InnerTarResult TarBackend::extractInnerTar(const QString &sevenZip,
                                                       const QString &outerDir,
                                                       QString &innerTarPath,
                                                       QString &errorText)
{
    QDir().mkpath(outerDir);

    QStringList args;
    args << QStringLiteral("x")
         << QStringLiteral("-o") + outerDir
         << QStringLiteral("-y")
         << QStringLiteral("-sccUTF-8")
         << QStringLiteral("--") << m_archivePath;

    LOG_DEBUG("TarBackend", "Extract outer stream: %s",
              ProcessRunner::maskedCommand(sevenZip, args).toUtf8().constData());

    const ProcessResult result = ProcessRunner::run(
        sevenZip, args, QString(), QProcessEnvironment(),
        QString(), 0, &m_cancelRequested);

    if (result.cancelled) {
        return InnerTarResult::Cancelled;
    }
    if (!result.started) {
        errorText = tr("Failed to start 7z");
        return InnerTarResult::Failed;
    }
    if (!result.success()) {
        errorText = result.stdErr.trimmed();
        return InnerTarResult::Failed;
    }

    const QFileInfoList files = QDir(outerDir).entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot);
    if (files.isEmpty()) {
        errorText = tr("The compressed stream does not contain a tar archive");
        return InnerTarResult::Failed;
    }

    // gzip usually preserves the inner name; xz/bzip2 streams are unnamed and
    // 7-Zip chooses a name derived from the outer archive.  Either way there
    // must be exactly one member.  If there are several, prefer *.tar.
    // gzip 通常会保留内部名称；xz/bzip2 流没有名称，
    // 7-Zip 会根据外层压缩包生成名称。无论如何只能有一个成员；
    // 若出现多个，优先选择 *.tar。
    QFileInfo inner = files.first();
    if (files.size() > 1) {
        bool found = false;
        for (const QFileInfo &file : files) {
            if (file.suffix().compare(QStringLiteral("tar"), Qt::CaseInsensitive) == 0) {
                inner = file;
                found = true;
                break;
            }
        }
        if (!found) {
            errorText = tr("Unexpected contents in the compressed stream");
            return InnerTarResult::Failed;
        }
    }

    innerTarPath = inner.absoluteFilePath();
    return InnerTarResult::Ok;
}

TarBackend::InnerTarResult TarBackend::resolveInnerTar(const QString &sevenZip,
                                                       QString &innerTarPath,
                                                       QString &errorText)
{
    if (!isCompressed()) {
        innerTarPath = m_archivePath;
        return InnerTarResult::Ok;
    }

    const QFileInfo archiveInfo(m_archivePath);
    const qint64 size = archiveInfo.size();
    const qint64 mtime = archiveInfo.lastModified().toMSecsSinceEpoch();
    if (!m_innerTarPath.isEmpty() && QFileInfo::exists(m_innerTarPath)
        && m_innerTarSize == size && m_innerTarMtime == mtime) {
        innerTarPath = m_innerTarPath;
        return InnerTarResult::Ok;
    }

    // The archive changed (or this is the first request): drop the old stage
    // and unpack the outer stream once for this session.
    // 压缩包发生变化（或首次请求）：丢弃旧缓存，为本次会话解压一次外层流。
    m_innerTarPath.clear();
    m_innerTarDir = std::make_unique<QTemporaryDir>(
        QDir::tempPath() + QStringLiteral("/qarchiver-tar-cache-XXXXXX"));
    if (!m_innerTarDir->isValid()) {
        errorText = tr("Cannot create a temporary directory");
        return InnerTarResult::Failed;
    }

    const InnerTarResult result = extractInnerTar(
        sevenZip, m_innerTarDir->path(), innerTarPath, errorText);
    if (result != InnerTarResult::Ok) {
        m_innerTarDir.reset();
        innerTarPath.clear();
        return result;
    }
    m_innerTarPath = innerTarPath;
    m_innerTarSize = size;
    m_innerTarMtime = mtime;
    return result;
}

void TarBackend::listContents(const ListOptions &options)
{
    Q_UNUSED(options);

    if (!m_isOpen) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("Archive not opened"));
        return;
    }

    const QString sevenZip = SevenZipUtils::resolveSevenZip();
    if (sevenZip.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("7z tool not found"));
        return;
    }

    QString target = m_archivePath;
    if (isCompressed()) {
        QString errorText;
        const InnerTarResult innerResult =
            resolveInnerTar(sevenZip, target, errorText);
        if (innerResult == InnerTarResult::Cancelled) {
            emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
            return;
        }
        if (innerResult == InnerTarResult::Failed) {
            emit errorOccurred(ExtractionUtils::looksLikeCorruption(errorText)
                                   ? ArchiveErrorCode::CorruptedArchive
                                   : ArchiveErrorCode::Unknown,
                               errorText.isEmpty()
                                   ? tr("Failed to open the compressed tar stream")
                                   : errorText);
            return;
        }
    }

    QStringList args;
    args << QStringLiteral("l") << QStringLiteral("-slt")
         << QStringLiteral("-sccUTF-8")
         << QStringLiteral("--") << target;

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
        const QString errorText = result.stdErr.trimmed();
        emit errorOccurred(ExtractionUtils::looksLikeCorruption(errorText)
                               ? ArchiveErrorCode::CorruptedArchive
                               : ArchiveErrorCode::Unknown,
                           errorText.isEmpty() ? tr("Failed to list archive") : errorText);
        return;
    }

    m_entries = parser.takeEntries();
    // Vote on the member-name lines only (see SevenZipUtils::sampleNameBytes).
    // 仅对成员名行投票（见 SevenZipUtils::sampleNameBytes）。
    const QByteArray nameSample = SevenZipUtils::sampleNameBytes(result.stdOutRaw);
    m_nameEncoding = nameSample.isEmpty() ? QStringLiteral("UTF-8")
                                          : EncodingDetector::detect(nameSample);
    emit fileListReady(m_entries);
}

void TarBackend::extract(const ExtractOptions &options)
{
    if (!m_isOpen) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("Archive not opened"));
        return;
    }
    if (!options.encodingHint.isEmpty()) {
        // Honour an explicit user choice over the detected encoding.
        // 用户显式选择优先于检测结果。
        m_nameEncoding = options.encodingHint;
    }

    const QString sevenZip = SevenZipUtils::resolveSevenZip();
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
    QString target = m_archivePath;

    if (isCompressed()) {
        QString errorText;
        const InnerTarResult innerResult =
            resolveInnerTar(sevenZip, target, errorText);
        if (innerResult == InnerTarResult::Cancelled) {
            FileUtils::removeTemporaryDirectory(tempDir);
            emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
            return;
        }
        if (innerResult == InnerTarResult::Failed) {
            FileUtils::removeTemporaryDirectory(tempDir);
            emit errorOccurred(ExtractionUtils::looksLikeCorruption(errorText)
                                   ? ArchiveErrorCode::CorruptedArchive
                                   : ArchiveErrorCode::Unknown,
                               errorText.isEmpty()
                                   ? tr("Failed to open the compressed tar stream")
                                   : errorText);
            return;
        }
    }

    QStringList args;
    args << QStringLiteral("x")
         << QStringLiteral("-o") + tempDir
         << QStringLiteral("-y")
         << QStringLiteral("-spd")
         << QStringLiteral("-sccUTF-8");

    SevenZipUtils::ListFile membersFile(QStringLiteral("tar-members"));
    const QStringList selectedPaths =
        ExtractionUtils::buildExtractionSelection(m_entries, options);
    if (!selectedPaths.isEmpty()) {
        if (!SevenZipUtils::appendListFileArgs(
                membersFile, selectedPaths, m_nameEncoding, args)) {
            FileUtils::removeTemporaryDirectory(tempDir);
            emit errorOccurred(ArchiveErrorCode::Unknown,
                               tr("Could not prepare the selected files for extraction"));
            return;
        }
    } else if (!options.skippedSymlinks.isEmpty()) {
        FileUtils::removeTemporaryDirectory(tempDir);
        emit operationFinished(true, tr("No files were extracted"));
        return;
    }

    args << QStringLiteral("--") << target;

    LOG_DEBUG("TarBackend", "Extract: %s",
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
            emit outputReady(line);
            if (line.startsWith(QLatin1String("- "))
                || line.startsWith(QLatin1String("+ "))
                || line.startsWith(QLatin1String("U "))) {
                ++processed;
                const int percent = total > 0
                    ? qBound(0, processed * 100 / total, 100)
                    : 0;
                emit progressChanged(percent, total, processed, -1);
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
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Extraction failed: %1").arg(result.stdErr.trimmed()));
        return;
    }

    if (m_nameEncoding.compare(QStringLiteral("UTF-8"), Qt::CaseInsensitive) != 0) {
        SevenZipUtils::renameLegacyExtractedTree(
            QFile::encodeName(tempDir), m_nameEncoding);
    }

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

void TarBackend::compress(const CompressOptions &options)
{
    if (m_format == ArchiveFormat::TarZst) {
        emit errorOccurred(
            ArchiveErrorCode::OperationNotSupported,
            tr("The bundled 7-Zip can read .tar.zst but cannot create it. "
               "Use tar.gz/tar.xz/tar.bz2 for creation."));
        return;
    }

    const QString sevenZip = SevenZipUtils::resolveSevenZip();
    if (sevenZip.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("7z tool not found"));
        return;
    }

    if (!options.password.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                           tr("tar does not support encryption; use 7z or zip instead"));
        return;
    }
    if (options.sourceFiles.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("No source files selected"));
        return;
    }

    const int level = validateCompressionLevel(options.compressionLevel);

    QStringList sourceFiles;
    sourceFiles.reserve(options.sourceFiles.size());
    for (const QString &file : options.sourceFiles) {
        sourceFiles << QFileInfo(file).absoluteFilePath();
    }

    QString baseDir;
    QStringList entries;
    QStringList args;
    args << QStringLiteral("a") << QStringLiteral("-ttar")
         << QStringLiteral("-sccUTF-8");

    if (options.storeFullPath) {
        args << QStringLiteral("-spf");
        entries = sourceFiles;
    } else {
        baseDir = commonParentDirectory(sourceFiles);
        for (const QString &file : sourceFiles) {
            entries << QDir(baseDir).relativeFilePath(file);
        }
    }

    // 7-Zip follows symlinks by default; -snl stores them as links.
    // 7-Zip 默认跟随符号链接；-snl 将符号链接按链接本身保存。
    if (!options.followSymlinks) {
        args << QStringLiteral("-snl");
    }
    for (const QString &pattern : options.excludePatterns) {
        if (!pattern.trimmed().isEmpty()) {
            args << QStringLiteral("-x!") + pattern;
        }
    }
    // -spd disables wildcard matching for both filenames and -x patterns, so
    // only enable it when no exclude patterns are in use.
    // -spd 会同时禁用文件名与 -x 模式的通配符匹配，
    // 因此仅在没有排除规则时启用。
    if (options.excludePatterns.isEmpty()) {
        args << QStringLiteral("-spd");
    }

    SevenZipUtils::ListFile sourceListFile(QStringLiteral("tar-sources"));
    if (!SevenZipUtils::appendListFileArgs(
            sourceListFile, entries, QStringLiteral("UTF-8"), args)) {
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Could not prepare the source files for compression"));
        return;
    }

    const int total = FileUtils::countFiles(sourceFiles);
    int processed = 0;

    if (isCompressed()) {
        QTemporaryDir tarTemp(
            QDir::tempPath() + QStringLiteral("/qarchiver-tar-create-XXXXXX"));
        if (!tarTemp.isValid()) {
            emit errorOccurred(ArchiveErrorCode::PermissionDenied,
                               tr("Cannot create a temporary directory"));
            return;
        }

        const QString innerTar = tarTemp.filePath(QStringLiteral("inner.tar"));
        QStringList tarArgs = args;
        tarArgs << QStringLiteral("--") << innerTar;

        LOG_DEBUG("TarBackend", "Create inner tar: %s",
                  ProcessRunner::maskedCommand(sevenZip, tarArgs).toUtf8().constData());

        ProcessOutputOptions outputOptions;
        outputOptions.maxStdOutBytes = 1024 * 1024;
        outputOptions.maxRawSampleBytes = 0;

        const ProcessResult tarResult = ProcessRunner::run(
            sevenZip, tarArgs, QString(), QProcessEnvironment(),
            baseDir, 0, &m_cancelRequested,
            [this, total, &processed](const QString &line, bool isError) {
                if (isError || line.isEmpty()) {
                    return;
                }
                emit outputReady(line);
                if (line.startsWith(QLatin1String("+ "))) {
                    ++processed;
                    const int percent = total > 0
                        ? qBound(0, processed * 90 / total, 90)
                        : 0;
                    emit progressChanged(percent, total, processed, -1);
                }
            },
            outputOptions);

        if (tarResult.cancelled) {
            emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
            return;
        }
        if (!tarResult.started) {
            emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("Failed to start 7z"));
            return;
        }
        if (!tarResult.success()) {
            emit errorOccurred(ArchiveErrorCode::Unknown,
                               tr("Compression failed: %1").arg(tarResult.stdErr.trimmed()));
            return;
        }

        QFile::remove(options.archivePath);

        QStringList compressArgs;
        compressArgs << QStringLiteral("a")
                     << compressionSwitch()
                     << QStringLiteral("-mmt=on")
                     << QStringLiteral("-sccUTF-8");
        if (level >= 0) {
            compressArgs << QStringLiteral("-mx=%1").arg(level);
        }
        compressArgs << QStringLiteral("--")
                     << options.archivePath << innerTar;

        LOG_DEBUG("TarBackend", "Compress: %s",
                  ProcessRunner::maskedCommand(sevenZip, compressArgs).toUtf8().constData());

        const ProcessResult compressResult = ProcessRunner::run(
            sevenZip, compressArgs, QString(), QProcessEnvironment(),
            QString(), 0, &m_cancelRequested,
            [this](const QString &line, bool isError) {
                if (!isError && !line.isEmpty()) {
                    emit outputReady(line);
                }
            },
            outputOptions);

        if (compressResult.cancelled || !compressResult.success()) {
            QFile::remove(options.archivePath);
            if (compressResult.cancelled) {
                emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
            } else if (!compressResult.started) {
                emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("Failed to start 7z"));
            } else {
                emit errorOccurred(ArchiveErrorCode::Unknown,
                                   tr("Compression failed: %1")
                                       .arg(compressResult.stdErr.trimmed()));
            }
            return;
        }
    } else {
        QFile::remove(options.archivePath);
        args << QStringLiteral("--") << options.archivePath;

        LOG_DEBUG("TarBackend", "Compress: %s",
                  ProcessRunner::maskedCommand(sevenZip, args).toUtf8().constData());

        ProcessOutputOptions outputOptions;
        outputOptions.maxStdOutBytes = 1024 * 1024;
        outputOptions.maxRawSampleBytes = 0;

        const ProcessResult result = ProcessRunner::run(
            sevenZip, args, QString(), QProcessEnvironment(),
            baseDir, 0, &m_cancelRequested,
            [this, total, &processed](const QString &line, bool isError) {
                if (isError || line.isEmpty()) {
                    return;
                }
                emit outputReady(line);
                if (line.startsWith(QLatin1String("+ "))) {
                    ++processed;
                    const int percent = total > 0
                        ? qBound(0, processed * 100 / total, 100)
                        : 0;
                    emit progressChanged(percent, total, processed, -1);
                }
            },
            outputOptions);

        if (result.cancelled) {
            QFile::remove(options.archivePath);
            emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
            return;
        }
        if (!result.started) {
            QFile::remove(options.archivePath);
            emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("Failed to start 7z"));
            return;
        }
        if (!result.success()) {
            QFile::remove(options.archivePath);
            emit errorOccurred(ArchiveErrorCode::Unknown,
                               tr("Compression failed: %1").arg(result.stdErr.trimmed()));
            return;
        }
    }

    emit progressChanged(100, total, processed, -1);
    emit operationFinished(true, tr("Archive created: %1").arg(options.archivePath));
}

void TarBackend::addFiles(const AddFilesOptions &options)
{
    if (!supportsAppend()) {
        emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                           tr("Cannot add files to compressed tar archives"));
        return;
    }

    const QString sevenZip = SevenZipUtils::resolveSevenZip();
    if (sevenZip.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("7z tool not found"));
        return;
    }

    // External-edit write-back runs 7-Zip inside the staged archive root and
    // reproduces the member's relative path; ordinary adds keep absolute paths.
    // 外部编辑写回会在暂存的压缩包根目录中运行 7-Zip 并复现成员相对路径；
    // 普通追加仍使用绝对路径。
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
        sourceFiles.reserve(options.sourceFiles.size());
        for (const QString &file : options.sourceFiles) {
            sourceFiles << QFileInfo(file).absoluteFilePath();
        }
    }
    if (sourceFiles.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("No source files selected"));
        return;
    }

    QStringList args;
    args << QStringLiteral("a") << QStringLiteral("-ttar")
         << QStringLiteral("-sccUTF-8")
         << QStringLiteral("-spd");
    if (!options.followSymlinks) {
        args << QStringLiteral("-snl");
    }
    if (options.storeFullPath) {
        args << QStringLiteral("-spf");
    }

    SevenZipUtils::ListFile sourceListFile(QStringLiteral("tar-add"));
    if (!SevenZipUtils::appendListFileArgs(
            sourceListFile, sourceFiles, sourceEncoding, args)) {
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Could not prepare the source files for adding"));
        return;
    }

    args << QStringLiteral("--") << options.archivePath;

    const ProcessResult result = ProcessRunner::run(
        sevenZip, args, QString(), QProcessEnvironment(),
        workingDir, 300000, &m_cancelRequested);

    if (result.cancelled) {
        emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
        return;
    }
    if (!result.started) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("Failed to start 7z"));
        return;
    }
    if (!result.success()) {
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Add files failed: %1").arg(result.stdErr.trimmed()));
        return;
    }
    emit operationFinished(true, writeBack ? tr("Changes saved to the archive")
                                           : tr("Files added to archive"));
}

void TarBackend::deleteFiles(const DeleteFilesOptions &options)
{
    if (!supportsAppend()) {
        emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                           tr("Cannot delete files from compressed tar archives"));
        return;
    }

    const QString sevenZip = SevenZipUtils::resolveSevenZip();
    if (sevenZip.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("7z tool not found"));
        return;
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
    if (targetEntries.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("No entries selected"));
        return;
    }

    QStringList args;
    args << QStringLiteral("d") << QStringLiteral("-ttar")
         << QStringLiteral("-spd")
         << QStringLiteral("-sccUTF-8");

    SevenZipUtils::ListFile membersFile(QStringLiteral("tar-members"));
    if (!SevenZipUtils::appendListFileArgs(
            membersFile, targetEntries, m_nameEncoding, args)) {
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
    if (!result.started) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("Failed to start 7z"));
        return;
    }
    if (!result.success()) {
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Delete files failed: %1").arg(result.stdErr.trimmed()));
        return;
    }
    emit operationFinished(true, tr("Files deleted from archive"));
}

void TarBackend::testArchive(const TestOptions &options)
{
    Q_UNUSED(options);

    const QString sevenZip = SevenZipUtils::resolveSevenZip();
    if (sevenZip.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("7z tool not found"));
        return;
    }

    QString target = m_archivePath;
    if (isCompressed()) {
        QString errorText;
        const InnerTarResult innerResult =
            resolveInnerTar(sevenZip, target, errorText);
        if (innerResult == InnerTarResult::Cancelled) {
            emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
            return;
        }
        if (innerResult == InnerTarResult::Failed) {
            emit errorOccurred(ArchiveErrorCode::CorruptedArchive,
                               errorText.isEmpty()
                                   ? tr("Archive test failed (corrupted)")
                                   : errorText);
            return;
        }
    }

    QStringList args;
    args << QStringLiteral("t")
         << QStringLiteral("-sccUTF-8")
         << QStringLiteral("--") << target;

    const ProcessResult result = ProcessRunner::run(
        sevenZip, args, QString(), QProcessEnvironment(),
        QString(), 300000, &m_cancelRequested);

    if (result.cancelled) {
        emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
        return;
    }
    if (!result.started) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("Failed to start 7z"));
        return;
    }
    if (!result.success()) {
        emit errorOccurred(ArchiveErrorCode::CorruptedArchive,
                           tr("Archive test failed (corrupted)"));
        return;
    }
    emit operationFinished(true, tr("Archive integrity verified"));
}

void TarBackend::closeArchive()
{
    m_isOpen = false;
    m_archivePath.clear();
    m_entries.clear();
    m_nameEncoding = QStringLiteral("UTF-8");
    m_innerTarPath.clear();
    m_innerTarDir.reset();
    m_innerTarSize = -1;
    m_innerTarMtime = 0;
}

bool TarBackend::supportsAppend() const
{
    return m_isOpen && m_format == ArchiveFormat::Tar;
}
