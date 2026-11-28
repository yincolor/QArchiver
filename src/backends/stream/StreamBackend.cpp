// QArchiver / 小Q解压
// StreamBackend compression, extraction and single-member listing.
// StreamBackend 的压缩、解压与单条目列表。

#include "backends/stream/StreamBackend.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include "utils/FileUtils.h"
#include "utils/Logger.h"
#include "utils/ProcessRunner.h"

StreamBackend::StreamBackend(ArchiveFormat fmt, QObject *parent)
    : ArchiveBackend(parent)
    , m_format(fmt)
{
}

QString StreamBackend::sevenZipPath() const
{
    return ProcessRunner::resolveToolPath(QStringLiteral("7z"));
}

QString StreamBackend::inferInternalName() const
{
    QFileInfo info(m_archivePath);
    QString baseName = info.fileName();
    if (baseName.endsWith(QStringLiteral(".gz"), Qt::CaseInsensitive)) {
        baseName.chop(3);
    } else if (baseName.endsWith(QStringLiteral(".bz2"), Qt::CaseInsensitive)) {
        baseName.chop(4);
    } else if (baseName.endsWith(QStringLiteral(".xz"), Qt::CaseInsensitive)) {
        baseName.chop(3);
    } else if (baseName.endsWith(QStringLiteral(".zst"), Qt::CaseInsensitive)) {
        baseName.chop(4);
    }
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("output");
    }
    return baseName;
}

qint64 StreamBackend::queryUncompressedSize() const
{
    const QString sevenZip = sevenZipPath();
    if (sevenZip.isEmpty()) {
        return -1;
    }

    const ProcessResult result = ProcessRunner::run(
        sevenZip,
        {QStringLiteral("l"), QStringLiteral("-slt"),
         QStringLiteral("-sccUTF-8"), QStringLiteral("--"), m_archivePath},
        QString(), QProcessEnvironment(), QString(), 30000);

    if (!result.success()) {
        return -1;
    }

    // The entry block contains "Size = <uncompressed>" and "Packed Size =".
    // 条目块包含 "Size = <解压后大小>" 与 "Packed Size ="。
    static const QRegularExpression sizeRe(
        QStringLiteral("^Size\\s*=\\s*(\\d+)$"));
    static const QRegularExpression packedRe(
        QStringLiteral("^Packed Size\\s*=\\s*(\\d+)$"));

    qint64 size = -1;
    const QStringList lines = result.stdOut.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const auto packed = packedRe.match(line.trimmed());
        if (packed.hasMatch()) {
            // We are inside an entry block; the preceding "Size" is the
            // uncompressed size.
            // 当前位于条目块内，前面的 "Size" 即解压后大小。
            break;
        }
        const auto match = sizeRe.match(line.trimmed());
        if (match.hasMatch()) {
            size = match.captured(1).toLongLong();
        }
    }
    return size;
}

void StreamBackend::openArchive(const QString &filePath)
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
    m_isOpen = true;
    m_entry.path = inferInternalName();
    m_entry.size = info.size();
    emit archiveOpened(filePath);
}

void StreamBackend::listContents(const ListOptions &)
{
    if (!m_isOpen) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("Archive not opened"));
        return;
    }

    QElapsedTimer timer;
    timer.start();
    m_entry.path = inferInternalName();
    const qint64 size = queryUncompressedSize();
    if (size >= 0) {
        m_entry.size = size;
    }
    LOG_INFO("StreamBackend",
             "Listed %s from %s (stored size %lld, %lld ms)",
             m_entry.path.toUtf8().constData(),
             m_archivePath.toUtf8().constData(),
             static_cast<long long>(m_entry.size),
             static_cast<long long>(timer.elapsed()));
    emit fileListReady({m_entry});
}

void StreamBackend::extract(const ExtractOptions &options)
{
    if (!m_isOpen) {
        emit errorOccurred(ArchiveErrorCode::Unknown, tr("Archive not opened"));
        return;
    }

    const QString sevenZip = sevenZipPath();
    if (sevenZip.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("7z tool not found"));
        return;
    }

    QDir().mkpath(options.destDir);
    const QString tempDir = FileUtils::createTempExtractDir(options.destDir);

    QStringList args;
    args << QStringLiteral("x")
         << QStringLiteral("-o") + tempDir
         << QStringLiteral("-y")
         << QStringLiteral("-sccUTF-8")
         << QStringLiteral("--") << m_archivePath;

    LOG_DEBUG("StreamBackend", "Extract: %s",
              ProcessRunner::maskedCommand(sevenZip, args).toUtf8().constData());

    const ProcessResult result = ProcessRunner::run(
        sevenZip, args, QString(), QProcessEnvironment(),
        QString(), 0, &m_cancelRequested,
        [this](const QString &line, bool isError) {
            if (!isError && !line.isEmpty()) {
                emit outputReady(line);
                emit progressChanged(50, 1, 0, -1);
            }
        });

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
        emit errorOccurred(ArchiveErrorCode::CorruptedArchive,
                           tr("Decompression failed (corrupted stream)"));
        return;
    }

    QStringList extractedFiles;
    QDirIterator it(tempDir, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        extractedFiles << it.next();
    }

    if (extractedFiles.isEmpty()) {
        FileUtils::removeTemporaryDirectory(tempDir);
        emit errorOccurred(ArchiveErrorCode::CorruptedArchive,
                           tr("The stream did not contain any file"));
        return;
    }

    const QString inferredName = inferInternalName();
    int index = 0;
    for (const QString &source : extractedFiles) {
        QString destName = (index == 0 && !inferredName.isEmpty())
            ? inferredName
            : QFileInfo(source).fileName();
        ++index;

        QString destPath = QDir(options.destDir).filePath(destName);
        if (QFileInfo::exists(destPath) || QFileInfo(destPath).isSymLink()) {
            switch (options.overwrite) {
            case OverwritePolicy::Never:
                continue;
            case OverwritePolicy::Rename:
                destPath = FileUtils::uniqueDestinationPath(destPath);
                break;
            case OverwritePolicy::Ask:
            case OverwritePolicy::Always:
                QFile::remove(destPath);
                break;
            }
        }

        if (!FileUtils::moveFileAtomically(source, destPath)) {
            FileUtils::removeTemporaryDirectory(tempDir);
            emit errorOccurred(ArchiveErrorCode::Unknown,
                               tr("Failed to move extracted file to %1").arg(destPath));
            return;
        }
    }
    FileUtils::removeTemporaryDirectory(tempDir);

    emit progressChanged(100, 1, 1, -1);
    emit operationFinished(true, tr("Decompression completed"));
}

void StreamBackend::compress(const CompressOptions &options)
{
    QString typeFlag;
    switch (m_format) {
    case ArchiveFormat::Gzip:  typeFlag = QStringLiteral("-tgzip"); break;
    case ArchiveFormat::Bzip2: typeFlag = QStringLiteral("-tbzip2"); break;
    case ArchiveFormat::Xz:    typeFlag = QStringLiteral("-txz"); break;
    default:
        emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                           tr("This format cannot be created here"));
        return;
    }

    if (options.sourceFiles.size() != 1) {
        emit errorOccurred(
            ArchiveErrorCode::OperationNotSupported,
            tr("A pure compression stream can contain exactly one file."));
        return;
    }
    const QFileInfo sourceInfo(options.sourceFiles.first());
    if (!sourceInfo.exists() || sourceInfo.isDir()) {
        emit errorOccurred(
            ArchiveErrorCode::OperationNotSupported,
            tr("Select exactly one regular file for a single-file compression stream."));
        return;
    }
    if (options.archivePath.trimmed().isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("No archive name specified"));
        return;
    }

    const QString sevenZip = sevenZipPath();
    if (sevenZip.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound,
                           tr("7z tool not found"));
        return;
    }

    QStringList args;
    args << QStringLiteral("a") << typeFlag << QStringLiteral("-y")
         << QStringLiteral("-sccUTF-8");
    const int level = qBound(0, options.compressionLevel, 9);
    args << QStringLiteral("-mx=%1").arg(level);
    args << QStringLiteral("--") << options.archivePath
         << sourceInfo.absoluteFilePath();

    LOG_DEBUG("StreamBackend", "Compress: %s",
              ProcessRunner::maskedCommand(sevenZip, args).toUtf8().constData());

    emit progressChanged(0, 100, 0, -1);
    const ProcessResult result = ProcessRunner::run(
        sevenZip, args, QString(), QProcessEnvironment(),
        QString(), 0, &m_cancelRequested,
        [this](const QString &line, bool isError) {
            if (!isError && !line.isEmpty()) {
                emit outputReady(line);
                emit progressChanged(50, 100, 50, -1);
            }
        });

    if (result.cancelled) {
        emit errorOccurred(ArchiveErrorCode::Cancelled,
                           tr("Operation cancelled"));
        return;
    }
    if (!result.started) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound,
                           tr("Failed to start 7z"));
        return;
    }
    if (!result.success()) {
        emit errorOccurred(
            ArchiveErrorCode::Unknown,
            tr("Compression failed: %1").arg(result.stdErr.trimmed()));
        return;
    }
    if (!QFileInfo::exists(options.archivePath)) {
        emit errorOccurred(ArchiveErrorCode::Unknown,
                           tr("Compression did not produce an output file"));
        return;
    }

    emit progressChanged(100, 100, 100, -1);
    emit operationFinished(true, tr("Compression completed"));
}

void StreamBackend::addFiles(const AddFilesOptions &)
{
    emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                       tr("Cannot add files to a pure compression stream"));
}

void StreamBackend::deleteFiles(const DeleteFilesOptions &)
{
    emit errorOccurred(ArchiveErrorCode::OperationNotSupported,
                       tr("Cannot delete files from a pure compression stream"));
}

void StreamBackend::testArchive(const TestOptions &)
{
    const QString sevenZip = sevenZipPath();
    if (sevenZip.isEmpty()) {
        emit errorOccurred(ArchiveErrorCode::ToolNotFound, tr("7z tool not found"));
        return;
    }

    const ProcessResult result = ProcessRunner::run(
        sevenZip,
        {QStringLiteral("t"), QStringLiteral("-sccUTF-8"),
         QStringLiteral("--"), m_archivePath},
        QString(), QProcessEnvironment(), QString(),
        300000, &m_cancelRequested);

    if (result.cancelled) {
        emit errorOccurred(ArchiveErrorCode::Cancelled, tr("Operation cancelled"));
        return;
    }
    if (!result.success()) {
        emit errorOccurred(ArchiveErrorCode::CorruptedArchive,
                           tr("Stream test failed"));
        return;
    }
    emit operationFinished(true, tr("Stream integrity verified"));
}

void StreamBackend::closeArchive()
{
    m_isOpen = false;
    m_archivePath.clear();
    m_entry = ArchiveEntry();
}
