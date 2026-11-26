// QArchiver / 小Q解压
// Integration tests for tar, zip, 7z, RAR, stream and encoding backends.
// tar、zip、7z、RAR、流与编码后端的集成测试。

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>


#include "backends/iso/IsoBackend.h"
#ifdef QARCHIVER_HAS_LIBARCHIVE
#include "backends/libarchive/LibArchiveBackend.h"
#endif
#include "backends/sevenzip/SevenZipBackend.h"
#include "backends/rar/RarBackend.h"
#include "backends/stream/StreamBackend.h"
#include "backends/tar/TarBackend.h"
#include "backends/sevenzip/ZipBackend.h"
#include "backends/sevenzip/SevenZipUtils.h"
#include "common/ArchiveOptions.h"
#include "common/FormatRegistry.h"
#include "utils/EncodingDetector.h"
#include "utils/MimeTypeHelper.h"

#include "TestArchiveBuilder.h"

namespace {

using TestArchiveBuilder::makeStoredZip;

} // namespace

class TestBackends : public QObject {
    Q_OBJECT

private slots:
    void tarRoundTrip();
    void tarCompressedRoundTrip_data();
    void tarCompressedRoundTrip();
    void tarCompressedCacheInvalidation();
    void tarAddDelete();
    void tarStreamDetection();
    void tarDotPrefixAndNonAscii();
    void tarUnsafeSymlinkCleansTemporaryTree();
    void tarSkippedSymlinkIsExcluded();
    void rarBackendCapabilities();
    void rarCreateArchive();
    void rarCreateSplitVolumes();
    void rarSplitVolumes_data();
    void rarSplitVolumes();
    void rarSplitMissingVolume();
    void splitVolumeDetection_data();
    void splitVolumeDetection();
    void zipRoundTrip();
    void sevenZipRoundTrip();
    void sevenZipSpecialNames();
    void sevenZipExcludePatterns();
#ifdef Q_OS_LINUX
    void legacyGbkZipSelectedExtract();
#endif
    void legacyNameSampleIgnoresHeader();
    void gzipStream();
    void streamCompressRoundTrip();
    void zstdStreams();
    void isoImage();
    void isoWriteRejected();

private:
    struct OperationResult {
        bool finished = false;
        bool success = false;
        QString message;
    };

    static OperationResult compress(ArchiveBackend &backend,
                                    const CompressOptions &options)
    {
        OperationResult result;
        const auto finishedConnection = QObject::connect(
            &backend, &ArchiveBackend::operationFinished,
            &backend, [&result](bool ok, const QString &msg) {
                result.finished = true;
                result.success = ok;
                result.message = msg;
            });
        const auto errorConnection = QObject::connect(
            &backend, &ArchiveBackend::errorOccurred,
            &backend, [&result](ArchiveErrorCode, const QString &msg) {
                result.finished = true;
                result.success = false;
                result.message = msg;
            });
        backend.compress(options);
        QObject::disconnect(finishedConnection);
        QObject::disconnect(errorConnection);
        return result;
    }

    static OperationResult extract(ArchiveBackend &backend,
                                   const ExtractOptions &options)
    {
        OperationResult result;
        const auto finishedConnection = QObject::connect(
            &backend, &ArchiveBackend::operationFinished,
            &backend, [&result](bool ok, const QString &msg) {
                result.finished = true;
                result.success = ok;
                result.message = msg;
            });
        const auto errorConnection = QObject::connect(
            &backend, &ArchiveBackend::errorOccurred,
            &backend, [&result](ArchiveErrorCode, const QString &msg) {
                result.finished = true;
                result.success = false;
                result.message = msg;
            });
        backend.extract(options);
        QObject::disconnect(finishedConnection);
        QObject::disconnect(errorConnection);
        return result;
    }

    static OperationResult addFiles(ArchiveBackend &backend,
                                    const AddFilesOptions &options)
    {
        OperationResult result;
        const auto finishedConnection = QObject::connect(
            &backend, &ArchiveBackend::operationFinished,
            &backend, [&result](bool ok, const QString &msg) {
                result.finished = true;
                result.success = ok;
                result.message = msg;
            });
        const auto errorConnection = QObject::connect(
            &backend, &ArchiveBackend::errorOccurred,
            &backend, [&result](ArchiveErrorCode, const QString &msg) {
                result.finished = true;
                result.success = false;
                result.message = msg;
            });
        backend.addFiles(options);
        QObject::disconnect(finishedConnection);
        QObject::disconnect(errorConnection);
        return result;
    }

    static OperationResult removeEntries(ArchiveBackend &backend,
                                         const DeleteFilesOptions &options)
    {
        OperationResult result;
        const auto finishedConnection = QObject::connect(
            &backend, &ArchiveBackend::operationFinished,
            &backend, [&result](bool ok, const QString &msg) {
                result.finished = true;
                result.success = ok;
                result.message = msg;
            });
        const auto errorConnection = QObject::connect(
            &backend, &ArchiveBackend::errorOccurred,
            &backend, [&result](ArchiveErrorCode, const QString &msg) {
                result.finished = true;
                result.success = false;
                result.message = msg;
            });
        backend.deleteFiles(options);
        QObject::disconnect(finishedConnection);
        QObject::disconnect(errorConnection);
        return result;
    }

    static QList<ArchiveEntry> list(ArchiveBackend &backend, bool *ok)
    {
        QList<ArchiveEntry> entries;
        bool finished = false;
        *ok = false;
        const auto listConnection = QObject::connect(
            &backend, &ArchiveBackend::fileListReady,
            &backend, [&entries, &finished](const QList<ArchiveEntry> &list) {
                entries = list;
                finished = true;
            });
        const auto errorConnection = QObject::connect(
            &backend, &ArchiveBackend::errorOccurred,
            &backend, [&finished](ArchiveErrorCode, const QString &) {
                finished = true;
            });
        backend.listContents();
        QObject::disconnect(listConnection);
        QObject::disconnect(errorConnection);
        *ok = finished && !entries.isEmpty();
        return entries;
    }

    static QString readFile(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return QString();
        }
        return QString::fromUtf8(file.readAll());
    }

    static QByteArray readBytes(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        return file.readAll();
    }

    static bool writeFile(const QString &path, const QByteArray &data)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        return file.write(data) == data.size();
    }
};

// Create, list and extract a plain tar archive.
// 创建、列出并解压普通 tar 压缩包。
void TestBackends::tarRoundTrip()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString a = temp.filePath(QStringLiteral("a.txt"));
    const QString b = temp.filePath(QStringLiteral("b.txt"));
    QVERIFY(writeFile(a, QByteArrayLiteral("hello")));
    QVERIFY(writeFile(b, QByteArrayLiteral("world")));

    const QString archive = temp.filePath(QStringLiteral("test.tar"));
    TarBackend backend(ArchiveFormat::Tar);

    CompressOptions compressOptions;
    compressOptions.archivePath = archive;
    compressOptions.sourceFiles = {a, b};
    const auto compressed = compress(backend, compressOptions);
    QVERIFY2(compressed.finished && compressed.success,
             qPrintable(compressed.message));

    TarBackend reader(ArchiveFormat::Tar);
    reader.openArchive(archive);
    bool listed = false;
    const auto entries = list(reader, &listed);
    QVERIFY2(listed, "tar list failed");
    QCOMPARE(entries.size(), 2);

    const QString dest = temp.filePath(QStringLiteral("out"));
    ExtractOptions extractOptions;
    extractOptions.destDir = dest;
    extractOptions.overwrite = OverwritePolicy::Always;
    const auto extracted = extract(reader, extractOptions);
    QVERIFY2(extracted.finished && extracted.success,
             qPrintable(extracted.message));
    QCOMPARE(readFile(dest + QStringLiteral("/a.txt")), QStringLiteral("hello"));
    QCOMPARE(readFile(dest + QStringLiteral("/b.txt")), QStringLiteral("world"));
}

// Compression variants for the tar round-trip test.
// tar 往返测试的压缩格式变体。
void TestBackends::tarCompressedRoundTrip_data()
{
    QTest::addColumn<int>("format");
    QTest::addColumn<QString>("suffix");

    QTest::newRow("gzip") << int(ArchiveFormat::TarGz)
                          << QStringLiteral(".tar.gz");
    QTest::newRow("xz") << int(ArchiveFormat::TarXz)
                        << QStringLiteral(".tar.xz");
    QTest::newRow("bzip2") << int(ArchiveFormat::TarBz2)
                           << QStringLiteral(".tar.bz2");
}

// Two-stage tar.gz/tar.xz/tar.bz2 creation and extraction.
// 两阶段创建与解压 tar.gz/tar.xz/tar.bz2。
void TestBackends::tarCompressedRoundTrip()
{
    QFETCH(int, format);
    QFETCH(QString, suffix);

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString a = temp.filePath(QStringLiteral("a.txt"));
    QVERIFY(writeFile(a, QByteArrayLiteral("compressed")));

    const QString archive = temp.filePath(QStringLiteral("test") + suffix);
    TarBackend backend(static_cast<ArchiveFormat>(format));
    CompressOptions options;
    options.archivePath = archive;
    options.sourceFiles = {a};
    options.compressionLevel = 6;
    const auto compressed = compress(backend, options);
    QVERIFY2(compressed.finished && compressed.success,
             qPrintable(compressed.message));
    QVERIFY(QFile::exists(archive));

    TarBackend reader(static_cast<ArchiveFormat>(format));
    reader.openArchive(archive);
    QVERIFY(!reader.supportsAppend());

    bool listed = false;
    const auto entries = list(reader, &listed);
    QVERIFY2(listed, "compressed tar list failed");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().path, QStringLiteral("a.txt"));
    QVERIFY(!entries.first().isDir);

    const QString dest = temp.filePath(QStringLiteral("out"));
    ExtractOptions extractOptions;
    extractOptions.destDir = dest;
    extractOptions.overwrite = OverwritePolicy::Always;
    const auto extracted = extract(reader, extractOptions);
    QVERIFY2(extracted.finished && extracted.success,
             qPrintable(extracted.message));
    QCOMPARE(readFile(dest + QStringLiteral("/a.txt")),
             QStringLiteral("compressed"));
}

// Reusing the cached inner tar must be invalidated when the outer archive is
// rewritten (size/mtime change), otherwise stale entries would be listed.
// 外层压缩包被重写（大小/修改时间变化）时必须让缓存的内层 tar 失效，
// 否则会列出过期条目。
void TestBackends::tarCompressedCacheInvalidation()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString archive = temp.filePath(QStringLiteral("cache.tar.gz"));

    const auto createArchive = [&](const QString &name, const QString &content) {
        const QString source = temp.filePath(name);
        if (!writeFile(source, content.toUtf8())) {
            return false;
        }
        TarBackend writer(ArchiveFormat::TarGz);
        CompressOptions options;
        options.archivePath = archive;
        options.sourceFiles = {source};
        options.compressionLevel = 1;
        return compress(writer, options).success;
    };

    QVERIFY(createArchive(QStringLiteral("first.txt"), QStringLiteral("first")));

    TarBackend reader(ArchiveFormat::TarGz);
    reader.openArchive(archive);
    bool listed = false;
    auto entries = list(reader, &listed);
    QVERIFY2(listed, "first compressed tar list failed");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().path, QStringLiteral("first.txt"));

    // Replace the archive behind the open backend's back.
    // 在已打开的后端背后替换压缩包。
    QVERIFY(createArchive(QStringLiteral("second.txt"), QStringLiteral("second")));

    listed = false;
    entries = list(reader, &listed);
    QVERIFY2(listed, "second compressed tar list failed");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().path, QStringLiteral("second.txt"));

    const QString dest = temp.filePath(QStringLiteral("out"));
    ExtractOptions extractOptions;
    extractOptions.destDir = dest;
    extractOptions.overwrite = OverwritePolicy::Always;
    const auto extracted = extract(reader, extractOptions);
    QVERIFY2(extracted.finished && extracted.success,
             qPrintable(extracted.message));
    QCOMPARE(readFile(dest + QStringLiteral("/second.txt")),
             QStringLiteral("second"));
}

// Append and delete entries in an uncompressed tar.
// 在未压缩 tar 中追加与删除条目。
void TestBackends::tarAddDelete()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString a = temp.filePath(QStringLiteral("a.txt"));
    const QString b = temp.filePath(QStringLiteral("b.txt"));
    QVERIFY(writeFile(a, QByteArrayLiteral("first")));
    QVERIFY(writeFile(b, QByteArrayLiteral("second")));

    const QString archive = temp.filePath(QStringLiteral("test.tar"));
    TarBackend creator(ArchiveFormat::Tar);
    CompressOptions compressOptions;
    compressOptions.archivePath = archive;
    compressOptions.sourceFiles = {a};
    const auto compressed = compress(creator, compressOptions);
    QVERIFY2(compressed.finished && compressed.success,
             qPrintable(compressed.message));

    TarBackend writer(ArchiveFormat::Tar);
    writer.openArchive(archive);
    QVERIFY(writer.supportsAppend());

    AddFilesOptions addOptions;
    addOptions.archivePath = archive;
    addOptions.sourceFiles = {b};
    addOptions.followSymlinks = true;
    const auto added = addFiles(writer, addOptions);
    QVERIFY2(added.finished && added.success, qPrintable(added.message));

    DeleteFilesOptions deleteOptions;
    deleteOptions.archivePath = archive;
    deleteOptions.targetEntries = {QStringLiteral("a.txt")};
    const auto deleted = removeEntries(writer, deleteOptions);
    QVERIFY2(deleted.finished && deleted.success, qPrintable(deleted.message));
    writer.closeArchive();

    TarBackend reader(ArchiveFormat::Tar);
    reader.openArchive(archive);
    bool listed = false;
    const auto entries = list(reader, &listed);
    QVERIFY2(listed, "tar list after add/delete failed");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().path, QStringLiteral("b.txt"));

    const QString dest = temp.filePath(QStringLiteral("out"));
    ExtractOptions extractOptions;
    extractOptions.destDir = dest;
    extractOptions.overwrite = OverwritePolicy::Always;
    const auto extracted = extract(reader, extractOptions);
    QVERIFY2(extracted.finished && extracted.success,
             qPrintable(extracted.message));
    QCOMPARE(readFile(dest + QStringLiteral("/b.txt")),
             QStringLiteral("second"));
}

// Detect a renamed tar.gz stream and distinguish plain gzip.
// 检测重命名后的 tar.gz 流，并区分普通 gzip。
void TestBackends::tarStreamDetection()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString source = temp.filePath(QStringLiteral("payload.txt"));
    QVERIFY(writeFile(source, QByteArrayLiteral("payload")));

    const QString innerTar = temp.filePath(QStringLiteral("inner.tar"));
    TarBackend tarWriter(ArchiveFormat::Tar);
    CompressOptions tarOptions;
    tarOptions.archivePath = innerTar;
    tarOptions.sourceFiles = {source};
    const auto tarCompressed = compress(tarWriter, tarOptions);
    QVERIFY2(tarCompressed.finished && tarCompressed.success,
             qPrintable(tarCompressed.message));

    // A gzip stream whose decompressed payload is a tar, but with a plain
    // .gz suffix so detection has to probe the stream.
    const QString tarGz = temp.filePath(QStringLiteral("payload.tar.gz"));
    TarBackend gzWriter(ArchiveFormat::TarGz);
    CompressOptions gzOptions;
    gzOptions.archivePath = tarGz;
    gzOptions.sourceFiles = {innerTar};
    const auto gzCompressed = compress(gzWriter, gzOptions);
    QVERIFY2(gzCompressed.finished && gzCompressed.success,
             qPrintable(gzCompressed.message));

    const QString disguised = temp.filePath(QStringLiteral("payload.gz"));
    QVERIFY(QFile::rename(tarGz, disguised));
    QCOMPARE(MimeTypeHelper::detect(disguised), ArchiveFormat::TarGz);

    const QString gzipProgram =
        QStandardPaths::findExecutable(QStringLiteral("gzip"));
    if (gzipProgram.isEmpty()) {
        QSKIP("system gzip is not available for the compatibility fixture");
    }
    const QString plainGz = temp.filePath(QStringLiteral("plain.gz"));
    QProcess gzip;
    gzip.start(gzipProgram, {QStringLiteral("-c"), source});
    QVERIFY(gzip.waitForFinished(10000));
    QCOMPARE(gzip.exitCode(), 0);
    QVERIFY(writeFile(plainGz, gzip.readAllStandardOutput()));
    QCOMPARE(MimeTypeHelper::detect(plainGz), ArchiveFormat::Gzip);
}

// Read GNU tar entries with "./" prefixes and non-ASCII names.
// 读取带 "./" 前缀及非 ASCII 名称的 GNU tar 条目。
void TestBackends::tarDotPrefixAndNonAscii()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString sourceDir = temp.filePath(QStringLiteral("source"));
    QVERIFY(QDir().mkpath(sourceDir));
    const QString fileName = QStringLiteral("动画片_15.json");
    const QByteArray payload = QByteArrayLiteral("unicode-name");
    QVERIFY(writeFile(QDir(sourceDir).filePath(fileName), payload));

    // Create with "." so members are stored as "./动画片_15.json".  Combined
    // with LC_ALL=C this used to make tar octal-escape the UTF-8 name.
    const QString tarProgram = QStandardPaths::findExecutable(QStringLiteral("tar"));
    if (tarProgram.isEmpty()) {
        QSKIP("system tar is not available for the compatibility fixture");
    }

    const QString archive = temp.filePath(QStringLiteral("dot-prefix.tar.gz"));
    QProcess tar;
    tar.start(tarProgram,
              {QStringLiteral("-czf"), archive, QStringLiteral("-C"),
               sourceDir, QStringLiteral(".")});
    QVERIFY(tar.waitForFinished(10000));
    QCOMPARE(tar.exitCode(), 0);

    TarBackend reader(ArchiveFormat::TarGz);
    reader.openArchive(archive);
    bool listed = false;
    const auto entries = list(reader, &listed);
    QVERIFY2(listed, "tar list failed");

    ArchiveEntry target;
    for (const ArchiveEntry &entry : entries) {
        if (entry.path.contains(fileName)) {
            target = entry;
            break;
        }
    }
    QVERIFY2(!target.path.isEmpty(), "non-ASCII entry not found");
    QVERIFY2(!target.path.contains(QLatin1Char('\\')),
             qPrintable(QStringLiteral("entry was escaped: %1").arg(target.path)));

    const QString dest = temp.filePath(QStringLiteral("out"));
    ExtractOptions options;
    options.destDir = dest;
    options.selectedFiles = QStringList{target.path};
    options.overwrite = OverwritePolicy::Always;
    const auto extracted = extract(reader, options);
    QVERIFY2(extracted.finished && extracted.success,
             qPrintable(extracted.message));
    QCOMPARE(readFile(QDir(dest).filePath(fileName)), QString::fromUtf8(payload));
}

// A symlink explicitly skipped by the user must not be extracted even when
// the rest of the archive is selected.  The archive-root "." entry must not
// be passed to 7-Zip, otherwise it expands the whole tree and re-adds the link.
// 用户明确跳过的符号链接在解压其余条目时也不能被解出。归档根目录 "." 条目
// 不能传给 7-Zip，否则它会展开整棵树并重新加入该链接。
void TestBackends::tarSkippedSymlinkIsExcluded()
{
#ifdef Q_OS_WIN
    QSKIP("Unix symlink archives are not represented by QFile::link on Windows");
#endif
    const QString tar = QStandardPaths::findExecutable(QStringLiteral("tar"));
    if (tar.isEmpty()) {
        QSKIP("the tar tool is required for this test");
    }

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString src = temp.filePath(QStringLiteral("src"));
    QVERIFY(QDir().mkpath(src));
    QVERIFY(writeFile(QDir(src).filePath(QStringLiteral("safe.txt")),
                      QByteArrayLiteral("safe")));
    QVERIFY(QFile::link(QStringLiteral("/etc/passwd"),
                        QDir(src).filePath(QStringLiteral("escape"))));

    const QString archive = temp.filePath(QStringLiteral("skip-link.tar"));
    {
        QProcess process;
        process.start(tar, {QStringLiteral("-cf"), archive,
                            QStringLiteral("-C"), src, QStringLiteral(".")});
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitCode(), 0);
    }

    TarBackend backend(ArchiveFormat::Tar);
    backend.openArchive(archive);
    bool listed = false;
    const QList<ArchiveEntry> entries = list(backend, &listed);
    QVERIFY2(listed, "tar list failed");

    QString linkPath;
    for (const ArchiveEntry &entry : entries) {
        if (entry.isSymLink) {
            linkPath = entry.path;
            break;
        }
    }
    QVERIFY2(!linkPath.isEmpty(), "tar did not expose the symlink entry");

    const QString dest = temp.filePath(QStringLiteral("out"));
    ExtractOptions options;
    options.destDir = dest;
    options.overwrite = OverwritePolicy::Always;
    options.skippedSymlinks = {linkPath};
    options.symlinkPreflightDone = true;
    const OperationResult result = extract(backend, options);
    QVERIFY2(result.finished && result.success,
             qPrintable(result.message));
    QVERIFY(QFileInfo::exists(QDir(dest).filePath(QStringLiteral("safe.txt"))));
    QVERIFY(!QFileInfo::exists(QDir(dest).filePath(QStringLiteral("escape"))));
}

// RAR is read-only, supports RAR4/RAR5 and rejects writes.
// RAR 为只读，支持 RAR4/RAR5，并拒绝写操作。
// The backend-level move-time check rejects an escaping symlink and removes
// the temporary extraction tree.
// 后端在移动前拒绝逃逸符号链接，并删除临时解压目录。
void TestBackends::tarUnsafeSymlinkCleansTemporaryTree()
{
#ifdef Q_OS_WIN
    QSKIP("Unix symlink archives are not represented by QFile::link on Windows");
#endif
    const QString tar = QStandardPaths::findExecutable(QStringLiteral("tar"));
    if (tar.isEmpty()) {
        QSKIP("the tar tool is required for this test");
    }

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString src = temp.filePath(QStringLiteral("src"));
    QVERIFY(QDir().mkpath(src));
    QVERIFY(writeFile(QDir(src).filePath(QStringLiteral("safe.txt")),
                      QByteArrayLiteral("safe")));
    QVERIFY(QFile::link(QStringLiteral("../outside.txt"),
                        QDir(src).filePath(QStringLiteral("escape"))));

    const QString archive = temp.filePath(QStringLiteral("unsafe.tar"));
    {
        QProcess process;
        process.start(tar, {QStringLiteral("-cf"), archive,
                            QStringLiteral("-C"), src, QStringLiteral(".")});
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitCode(), 0);
    }

    TarBackend backend(ArchiveFormat::Tar);
    backend.openArchive(archive);
    const QString dest = temp.filePath(QStringLiteral("out"));
    ExtractOptions options;
    options.destDir = dest;
    options.overwrite = OverwritePolicy::Always;
    const OperationResult result = extract(backend, options);
    QVERIFY(result.finished);
    QVERIFY(!result.success);
    QVERIFY(!QFileInfo::exists(QDir(dest).filePath(QStringLiteral("escape"))));
    const QStringList temporary = QDir(dest).entryList(
        {QStringLiteral(".tmp_extract_*")}, QDir::Dirs | QDir::Hidden);
    QVERIFY2(temporary.isEmpty(), qPrintable(temporary.join(QStringLiteral(", "))));
}

void TestBackends::rarBackendCapabilities()
{
    RarBackend rar4(ArchiveFormat::Rar4);
    QCOMPARE(rar4.format(), ArchiveFormat::Rar4);
    QVERIFY(rar4.isReadOnly());
    QVERIFY(!rar4.supportsAppend());
    QVERIFY(rar4.supportsEncryption());
    QVERIFY(rar4.supportsVolume());

    RarBackend rar5(ArchiveFormat::Rar5);
    QCOMPARE(rar5.format(), ArchiveFormat::Rar5);

    if (!RarBackend::isCreateAvailable()) {
        CompressOptions options;
        options.archivePath = QStringLiteral("/tmp/qarchiver-unused.rar");
        options.sourceFiles = {QStringLiteral("/tmp/qarchiver-unused.txt")};
        const auto result = compress(rar5, options);
        QVERIFY(result.finished);
        QVERIFY(!result.success);
    }
}

// Creating RAR archives needs the system rar command; skip when absent.
// 创建 RAR 压缩包需要系统 rar 命令；未安装时跳过。
void TestBackends::rarCreateArchive()
{
    if (!RarBackend::isCreateAvailable()) {
        QSKIP("the rar command is not installed");
    }

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("created 中文.txt"));
    const QByteArray payload = QByteArrayLiteral("created by the rar command");
    QVERIFY(writeFile(source, payload));

    const QString archive = temp.filePath(QStringLiteral("created.rar"));
    RarBackend writer(ArchiveFormat::Rar5);
    CompressOptions options;
    options.archivePath = archive;
    options.sourceFiles = {source};
    options.compressionLevel = 6;
    const auto compressed = compress(writer, options);
    QVERIFY2(compressed.finished && compressed.success,
             qPrintable(compressed.message));
    QVERIFY(QFileInfo::exists(archive));
    QCOMPARE(MimeTypeHelper::detect(archive), ArchiveFormat::Rar5);
    QVERIFY(!MimeTypeHelper::isSplitVolume(archive));

    RarBackend reader(ArchiveFormat::Rar5);
    reader.openArchive(archive);
    bool listed = false;
    const auto entries = list(reader, &listed);
    QVERIFY2(listed, "created RAR list failed");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().path, QStringLiteral("created 中文.txt"));

    const QString dest = temp.filePath(QStringLiteral("out"));
    ExtractOptions extractOptions;
    extractOptions.destDir = dest;
    extractOptions.overwrite = OverwritePolicy::Always;
    const auto extracted = extract(reader, extractOptions);
    QVERIFY2(extracted.finished && extracted.success,
             qPrintable(extracted.message));
    QCOMPARE(readBytes(QDir(dest).filePath(QStringLiteral("created 中文.txt"))),
             payload);
}

// RAR volumes must use the part1.rar / part2.rar naming and stay readable.
// RAR 分卷必须使用 part1.rar / part2.rar 命名，并且可正常读取。
void TestBackends::rarCreateSplitVolumes()
{
    if (!RarBackend::isCreateAvailable()) {
        QSKIP("the rar command is not installed");
    }

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("volume-payload.bin"));
    QByteArray payload;
    payload.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        payload.append(static_cast<char>((i * 29 + 7) & 0xff));
    }
    QVERIFY(writeFile(source, payload));

    const QString archive = temp.filePath(QStringLiteral("split.rar"));
    RarBackend writer(ArchiveFormat::Rar5);
    CompressOptions options;
    options.archivePath = archive;
    options.sourceFiles = {source};
    options.compressionLevel = 0;
    options.volumeSize = 3000;
    const auto compressed = compress(writer, options);
    QVERIFY2(compressed.finished && compressed.success,
             qPrintable(compressed.message));

    const QString first = temp.filePath(QStringLiteral("split.part1.rar"));
    const QString second = temp.filePath(QStringLiteral("split.part2.rar"));
    QVERIFY(QFileInfo::exists(first));
    QVERIFY(QFileInfo::exists(second));
    QVERIFY(!QFileInfo::exists(archive));
    QVERIFY(MimeTypeHelper::isSplitVolume(second));
    QCOMPARE(QFileInfo(MimeTypeHelper::resolveFirstVolume(second)).canonicalFilePath(),
             QFileInfo(first).canonicalFilePath());

    RarBackend reader(ArchiveFormat::Rar5);
    reader.openArchive(second);
    bool listed = false;
    const auto entries = list(reader, &listed);
    QVERIFY2(listed, "created split RAR list failed");
    QCOMPARE(entries.size(), 1);

    const QString dest = temp.filePath(QStringLiteral("out"));
    ExtractOptions extractOptions;
    extractOptions.destDir = dest;
    extractOptions.overwrite = OverwritePolicy::Always;
    const auto extracted = extract(reader, extractOptions);
    QVERIFY2(extracted.finished && extracted.success,
             qPrintable(extracted.message));
    QCOMPARE(readBytes(QDir(dest).filePath(QStringLiteral("volume-payload.bin"))),
             payload);
}

void TestBackends::rarSplitVolumes_data()
{
    QTest::addColumn<QString>("volume");
    QTest::addColumn<QString>("firstVolume");
    QTest::addColumn<ArchiveFormat>("format");

    QTest::newRow("rar5-part") << QStringLiteral("rar5_split/data.part2.rar")
                                << QStringLiteral("rar5_split/data.part1.rar")
                                << ArchiveFormat::Rar5;
    QTest::newRow("rar4-r00") << QStringLiteral("rar4_split/data.r01")
                               << QStringLiteral("rar4_split/data.rar")
                               << ArchiveFormat::Rar4;
}

// Split RAR volumes: opening a non-first volume resolves the first one and
// then lists/extracts through the bundled 7-Zip helper.
// RAR 分卷：从非首卷打开时解析首卷，再经内置 7-Zip 列出/解压。
void TestBackends::rarSplitVolumes()
{
    QFETCH(QString, volume);
    QFETCH(QString, firstVolume);
    QFETCH(ArchiveFormat, format);

#ifndef QARCHIVER_TEST_DATA_DIR
#define QARCHIVER_TEST_DATA_DIR "test_files"
#endif
    const QDir dataDir(QStringLiteral(QARCHIVER_TEST_DATA_DIR));
    const QString volumePath = dataDir.filePath(volume);
    const QString firstPath = dataDir.filePath(firstVolume);
    QVERIFY2(QFileInfo::exists(volumePath), qPrintable(volumePath));
    QVERIFY2(QFileInfo::exists(firstPath), qPrintable(firstPath));

    QCOMPARE(MimeTypeHelper::detect(volumePath), format);
    QVERIFY(MimeTypeHelper::isSplitVolume(volumePath));
    QCOMPARE(QFileInfo(MimeTypeHelper::resolveFirstVolume(volumePath)).canonicalFilePath(),
             QFileInfo(firstPath).canonicalFilePath());

    RarBackend reader(format);
    reader.openArchive(volumePath);
    bool listed = false;
    const auto entries = list(reader, &listed);
    QVERIFY2(listed, "split RAR list failed");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().path, QStringLiteral("payload.bin"));

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    ExtractOptions options;
    options.destDir = temp.filePath(QStringLiteral("out"));
    options.overwrite = OverwritePolicy::Always;
    const auto extracted = extract(reader, options);
    QVERIFY2(extracted.finished && extracted.success,
             qPrintable(extracted.message));

    QByteArray expected;
    expected.reserve(8000);
    for (int i = 0; i < 8000; ++i) {
        expected.append(static_cast<char>((i * 37 + 11) & 0xff));
    }
    QCOMPARE(readBytes(QDir(options.destDir).filePath(QStringLiteral("payload.bin"))),
             expected);
}

// 7z/zip split volumes only embed the format header in the .001 volume;
// opening .002 must still resolve the first volume and detect the format.
// 7z/zip 分卷只有 .001 带格式头；打开 .002 时仍必须解析首卷并识别格式。
void TestBackends::splitVolumeDetection_data()
{
    QTest::addColumn<int>("format");
    QTest::addColumn<QString>("suffix");

    QTest::newRow("7z") << int(ArchiveFormat::SevenZip)
                        << QStringLiteral(".7z");
    QTest::newRow("zip") << int(ArchiveFormat::Zip)
                         << QStringLiteral(".zip");
}

void TestBackends::splitVolumeDetection()
{
    QFETCH(int, format);
    QFETCH(QString, suffix);

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    QByteArray payload;
    payload.reserve(20000);
    for (int i = 0; i < 20000; ++i) {
        payload.append(static_cast<char>((i * 31 + 5) & 0xff));
    }
    const QString source = temp.filePath(QStringLiteral("payload.bin"));
    QVERIFY(writeFile(source, payload));

    const QString archive = temp.filePath(QStringLiteral("split") + suffix);
    SevenZipBackend writer(static_cast<ArchiveFormat>(format));
    CompressOptions options;
    options.archivePath = archive;
    options.sourceFiles = {source};
    options.compressionLevel = 0;
    options.volumeSize = 4096;
    const auto compressed = compress(writer, options);
    QVERIFY2(compressed.finished && compressed.success,
             qPrintable(compressed.message));

    const QString first = archive + QStringLiteral(".001");
    const QString second = archive + QStringLiteral(".002");
    QVERIFY(QFileInfo::exists(first));
    QVERIFY(QFileInfo::exists(second));
    QCOMPARE(MimeTypeHelper::detect(first), static_cast<ArchiveFormat>(format));
    QCOMPARE(MimeTypeHelper::detect(second), static_cast<ArchiveFormat>(format));
    QVERIFY(MimeTypeHelper::isSplitVolume(second));
    QCOMPARE(QFileInfo(MimeTypeHelper::resolveFirstVolume(second)).canonicalFilePath(),
             QFileInfo(first).canonicalFilePath());

    SevenZipBackend reader(static_cast<ArchiveFormat>(format));
    reader.openArchive(second);
    bool listed = false;
    const auto entries = list(reader, &listed);
    QVERIFY2(listed, "split volume list failed");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().path, QStringLiteral("payload.bin"));

    const QString dest = temp.filePath(QStringLiteral("out"));
    ExtractOptions extractOptions;
    extractOptions.destDir = dest;
    extractOptions.overwrite = OverwritePolicy::Always;
    const auto extracted = extract(reader, extractOptions);
    QVERIFY2(extracted.finished && extracted.success,
             qPrintable(extracted.message));
    QCOMPARE(readBytes(QDir(dest).filePath(QStringLiteral("payload.bin"))),
             payload);
}

// A missing middle volume must surface as VolumeMissing, not Unknown.
// 缺失中间分卷时必须报告 VolumeMissing，而不是 Unknown。
void TestBackends::rarSplitMissingVolume()
{
#ifndef QARCHIVER_TEST_DATA_DIR
#define QARCHIVER_TEST_DATA_DIR "test_files"
#endif
    const QDir dataDir(QStringLiteral(QARCHIVER_TEST_DATA_DIR) +
                       QStringLiteral("/rar5_split"));
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    for (const QString &name : {QStringLiteral("data.part1.rar"),
                                QStringLiteral("data.part3.rar")}) {
        QVERIFY(QFile::copy(dataDir.filePath(name), temp.filePath(name)));
    }

    RarBackend reader(ArchiveFormat::Rar5);
    reader.openArchive(temp.filePath(QStringLiteral("data.part1.rar")));

    bool finished = false;
    ArchiveErrorCode code = ArchiveErrorCode::Unknown;
    connect(&reader, &ArchiveBackend::fileListReady, &reader,
            [&finished](const QList<ArchiveEntry> &) { finished = true; });
    connect(&reader, &ArchiveBackend::errorOccurred, &reader,
            [&finished, &code](ArchiveErrorCode errorCode, const QString &) {
                finished = true;
                code = errorCode;
            });
    reader.listContents();

    QVERIFY(finished);
    QCOMPARE(code, ArchiveErrorCode::VolumeMissing);
}

// Create, list, extract and test a zip archive.
// 创建、列出、解压并测试 zip 压缩包。
void TestBackends::zipRoundTrip()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString a = temp.filePath(QStringLiteral("a.txt"));
    QVERIFY(writeFile(a, QByteArrayLiteral("zip")));

    const QString archive = temp.filePath(QStringLiteral("test.zip"));
    ZipBackend backend;
    CompressOptions options;
    options.archivePath = archive;
    options.sourceFiles = {a};
    options.compressionLevel = 5;
    const auto compressed = compress(backend, options);
    QVERIFY2(compressed.finished && compressed.success,
             qPrintable(compressed.message));

    ZipBackend reader;
    reader.openArchive(archive);
    bool listed = false;
    const auto entries = list(reader, &listed);
    QVERIFY2(listed, "zip list failed");
    QCOMPARE(entries.size(), 1);

    const QString dest = temp.filePath(QStringLiteral("out"));
    ExtractOptions extractOptions;
    extractOptions.destDir = dest;
    extractOptions.overwrite = OverwritePolicy::Always;
    const auto extracted = extract(reader, extractOptions);
    QVERIFY2(extracted.finished && extracted.success,
             qPrintable(extracted.message));
    QCOMPARE(readFile(dest + QStringLiteral("/a.txt")), QStringLiteral("zip"));
}

// Create, list, extract and test a 7z archive.
// 创建、列出、解压并测试 7z 压缩包。
void TestBackends::sevenZipRoundTrip()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString a = temp.filePath(QStringLiteral("a.txt"));
    QVERIFY(writeFile(a, QByteArrayLiteral("sevenzip")));

    const QString archive = temp.filePath(QStringLiteral("test.7z"));
    SevenZipBackend backend;
    CompressOptions options;
    options.archivePath = archive;
    options.sourceFiles = {a};
    options.compressionLevel = 7;
    const auto compressed = compress(backend, options);
    QVERIFY2(compressed.finished && compressed.success,
             qPrintable(compressed.message));

    SevenZipBackend reader;
    reader.openArchive(archive);
    bool listed = false;
    list(reader, &listed);
    QVERIFY(listed);

    const QString dest = temp.filePath(QStringLiteral("out"));
    ExtractOptions extractOptions;
    extractOptions.destDir = dest;
    extractOptions.overwrite = OverwritePolicy::Always;
    const auto extracted = extract(reader, extractOptions);
    QVERIFY2(extracted.finished && extracted.success,
             qPrintable(extracted.message));
    QCOMPARE(readFile(dest + QStringLiteral("/a.txt")), QStringLiteral("sevenzip"));
}

// Handle wildcard, dash, @ and non-ASCII member names safely.
// 安全处理通配符、短横线、@ 及非 ASCII 成员名。
void TestBackends::sevenZipSpecialNames()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString sourceDir = temp.filePath(QStringLiteral("special"));
    QVERIFY(QDir().mkpath(sourceDir));

    QList<QPair<QString, QByteArray>> files = {
        {QStringLiteral("-dash.txt"), QByteArrayLiteral("dash")},
        {QStringLiteral("at@name.txt"), QByteArrayLiteral("at")},
        {QStringLiteral("中文 空格.txt"), QByteArrayLiteral("chinese")},
    };
#ifndef Q_OS_WIN
    // '*' and '?' cannot be part of a Windows file name.
    // Windows 文件名中不能包含 '*' 与 '?'。
    files.append({QStringLiteral("star*name.txt"), QByteArrayLiteral("star")});
    files.append({QStringLiteral("quest?name.txt"), QByteArrayLiteral("quest")});
#endif
    for (const auto &file : files) {
        QVERIFY2(writeFile(QDir(sourceDir).filePath(file.first), file.second),
                 qPrintable(file.first));
    }

    const QString archive = temp.filePath(QStringLiteral("special.7z"));
    SevenZipBackend writer;
    CompressOptions compressOptions;
    compressOptions.archivePath = archive;
    for (const auto &file : files) {
        compressOptions.sourceFiles << QDir(sourceDir).filePath(file.first);
    }
    const auto compressed = compress(writer, compressOptions);
    QVERIFY2(compressed.finished && compressed.success,
             qPrintable(compressed.message));

    SevenZipBackend reader;
    reader.openArchive(archive);
    bool listed = false;
    const auto entries = list(reader, &listed);
    QVERIFY2(listed, "special-name 7z list failed");
    QCOMPARE(entries.size(), files.size());

    QStringList names;
    for (const ArchiveEntry &entry : entries) {
        names << entry.path;
    }
    for (const auto &file : files) {
        QVERIFY2(names.contains(file.first),
                 qPrintable(QStringLiteral("missing entry: %1").arg(file.first)));
    }

    const QString dest = temp.filePath(QStringLiteral("out"));
    ExtractOptions extractOptions;
    extractOptions.destDir = dest;
    extractOptions.selectedFiles = {
        QStringLiteral("-dash.txt"),
        QStringLiteral("at@name.txt"),
        QStringLiteral("中文 空格.txt"),
    };
#ifndef Q_OS_WIN
    extractOptions.selectedFiles << QStringLiteral("star*name.txt");
#endif
    extractOptions.overwrite = OverwritePolicy::Always;
    const auto extracted = extract(reader, extractOptions);
    QVERIFY2(extracted.finished && extracted.success,
             qPrintable(extracted.message));
#ifndef Q_OS_WIN
    QCOMPARE(readFile(QDir(dest).filePath(QStringLiteral("star*name.txt"))),
             QStringLiteral("star"));
#endif
    QCOMPARE(readFile(QDir(dest).filePath(QStringLiteral("-dash.txt"))),
             QStringLiteral("dash"));
    QCOMPARE(readFile(QDir(dest).filePath(QStringLiteral("at@name.txt"))),
             QStringLiteral("at"));
    QCOMPARE(readFile(QDir(dest).filePath(QStringLiteral("中文 空格.txt"))),
             QStringLiteral("chinese"));

    // Exercise the add-files path with a literal '*' in the source name.
    // Windows 文件名不允许 '*'，改用普通名称。
#ifdef Q_OS_WIN
    const QString extra = QDir(sourceDir).filePath(QStringLiteral("added-star.txt"));
#else
    const QString extra = QDir(sourceDir).filePath(QStringLiteral("added*star.txt"));
#endif
    QVERIFY(writeFile(extra, QByteArrayLiteral("added")));
    SevenZipBackend adder;
    adder.openArchive(archive);
    AddFilesOptions addOptions;
    addOptions.archivePath = archive;
    addOptions.sourceFiles = {extra};
    const auto added = addFiles(adder, addOptions);
    QVERIFY2(added.finished && added.success, qPrintable(added.message));

    DeleteFilesOptions deleteOptions;
    deleteOptions.archivePath = archive;
    deleteOptions.targetEntries = {
        QStringLiteral("-dash.txt"),
    };
#ifndef Q_OS_WIN
    deleteOptions.targetEntries << QStringLiteral("star*name.txt");
#endif
    const auto removed = removeEntries(reader, deleteOptions);
    QVERIFY2(removed.finished && removed.success,
             qPrintable(removed.message));

    SevenZipBackend afterDelete;
    afterDelete.openArchive(archive);
    bool listedAfterDelete = false;
    const auto remaining = list(afterDelete, &listedAfterDelete);
    QVERIFY2(listedAfterDelete, "special-name 7z list after delete failed");
    QCOMPARE(remaining.size(),
             files.size() + 1 - deleteOptions.targetEntries.size());
    bool foundAdded = false;
    for (const ArchiveEntry &entry : remaining) {
        QVERIFY2(entry.path != QStringLiteral("-dash.txt")
                     && entry.path != QStringLiteral("star*name.txt"),
                 qPrintable(QStringLiteral("entry was not deleted: %1").arg(entry.path)));
        foundAdded = foundAdded || entry.path == QFileInfo(extra).fileName();
    }
    QVERIFY(foundAdded);
}

// Exclude patterns must still work without -spd.
// 排除规则在未使用 -spd 时仍须生效。
void TestBackends::sevenZipExcludePatterns()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString keep = temp.filePath(QStringLiteral("keep.txt"));
    const QString skip = temp.filePath(QStringLiteral("skip.tmp"));
    QVERIFY(writeFile(keep, QByteArrayLiteral("keep")));
    QVERIFY(writeFile(skip, QByteArrayLiteral("skip")));

    const QString archive = temp.filePath(QStringLiteral("exclude.7z"));
    SevenZipBackend writer;
    CompressOptions options;
    options.archivePath = archive;
    options.sourceFiles = {keep, skip};
    options.excludePatterns = {QStringLiteral("*.tmp")};
    const auto compressed = compress(writer, options);
    QVERIFY2(compressed.finished && compressed.success,
             qPrintable(compressed.message));

    SevenZipBackend reader;
    reader.openArchive(archive);
    bool listed = false;
    const auto entries = list(reader, &listed);
    QVERIFY2(listed, "exclude-pattern 7z list failed");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().path, QStringLiteral("keep.txt"));
}

#ifdef Q_OS_LINUX
// Decode GBK names and extract selected members via a listfile.
// 解码 GBK 名称并通过列表文件解压选定成员。
void TestBackends::legacyGbkZipSelectedExtract()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    // "中文测试.txt" encoded as GBK, without the ZIP UTF-8 flag.
    const QByteArray gbkName =
        QByteArray::fromHex("d6d0cec4b2e2cad42e747874");
    const QString archive = temp.filePath(QStringLiteral("legacy-gbk.zip"));
    QVERIFY(writeFile(archive, makeStoredZip(gbkName, QByteArrayLiteral("legacy"))));

    SevenZipBackend reader(ArchiveFormat::Zip);
    reader.openArchive(archive);
    bool listed = false;
    const auto entries = list(reader, &listed);
    QVERIFY2(listed, "legacy GBK zip list failed");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().path, QStringLiteral("中文测试.txt"));
    // The detected encoding has to be exposed so the UI can hand it back to
    // extraction and drag-out.  7-Zip converts legacy names to UTF-8 when the
    // system locale uses a matching code page and passes the raw bytes through
    // otherwise, so the exact value is locale dependent: what matters is that
    // re-encoding the listed name with it reproduces the same characters.
    // 检测到的编码必须可以导出，以便界面回传给解压与拖出。7-Zip 在系统 locale
    // 使用匹配代码页时会把旧式名称转成 UTF-8，否则原样输出原始字节，
    // 因此具体值取决于 locale：关键是按该编码重编码后能还原同样的字符。
    const QString encoding = reader.nameEncoding();
    QVERIFY(!encoding.isEmpty());
    QCOMPARE(EncodingDetector::decode(
                 EncodingDetector::encode(entries.first().path, encoding), encoding),
             entries.first().path);

    const QString dest = temp.filePath(QStringLiteral("out"));
    ExtractOptions options;
    options.destDir = dest;
    options.selectedFiles = {entries.first().path};
    options.overwrite = OverwritePolicy::Always;
    const auto extracted = extract(reader, options);
    QVERIFY2(extracted.finished && extracted.success,
             qPrintable(extracted.message));
    QCOMPARE(readFile(QDir(dest).filePath(QStringLiteral("中文测试.txt"))),
             QStringLiteral("legacy"));
}
#endif

// The encoding vote must look at member-name lines only: the 7-Zip banner and
// the archive's own (always UTF-8) path would otherwise outvote a legacy
// archive that has just a few non-UTF-8 members.
// 编码投票必须只看成员名行：否则 7-Zip 横幅与压缩包自身（始终为 UTF-8）的
// 路径会把只含少量非 UTF-8 成员的旧式压缩包的判断带偏。
void TestBackends::legacyNameSampleIgnoresHeader()
{
    const QByteArray gbkName = QByteArray::fromHex("d6d0cec4b2e2cad42e747874");
    const QByteArray archivePathUtf8 = QStringLiteral("/tmp/测试.zip").toUtf8();

    QByteArray output = QByteArrayLiteral(
        "7-Zip 23.01 (x64) : Copyright (c) 1999-2023 Igor Pavlov : 2023-06-20\n"
        "\n"
        "Scanning the drive for archives:\n"
        "1 file, 123 bytes (1 KiB)\n"
        "\n"
        "Listing archive: ");
    output.append(archivePathUtf8);
    output.append(QByteArrayLiteral("\n\n--\nPath = "));
    output.append(archivePathUtf8);
    output.append(QByteArrayLiteral("\nType = zip\nPhysical Size = 123\n\n----------\nPath = "));
    output.append(gbkName);
    output.append(QByteArrayLiteral("\nSize = 6\nModified = 2024-01-01\n"));

    const QByteArray sample = SevenZipUtils::sampleNameBytes(output);
    QVERIFY(!sample.contains(QByteArrayLiteral("7-Zip")));
    QVERIFY(!sample.contains(QByteArrayLiteral("Type = zip")));
    QVERIFY(!sample.contains(archivePathUtf8));
    QCOMPARE(sample, gbkName + '\n');
    QCOMPARE(EncodingDetector::detect(sample), QStringLiteral("GB18030"));

    // Without the "----------" separator the helper falls back to every
    // "Path = " value: better a slightly noisier sample than none at all.
    // 没有 "----------" 分隔行时，辅助函数回退到所有 "Path = " 值：
    // 宁可样本略噪，也不要完全取不到样本。
    QCOMPARE(SevenZipUtils::sampleNameBytes(QByteArrayLiteral("Path = a.zip\n")),
             QByteArrayLiteral("a.zip\n"));
}

// Create pure gzip/bzip2/xz streams through StreamBackend and read them back.
// 通过 StreamBackend 创建纯 gzip/bzip2/xz 流并重新读取。
void TestBackends::streamCompressRoundTrip()
{
    const struct {
        ArchiveFormat format;
        QString suffix;
    } cases[] = {
        {ArchiveFormat::Gzip, QStringLiteral(".gz")},
        {ArchiveFormat::Bzip2, QStringLiteral(".bz2")},
        {ArchiveFormat::Xz, QStringLiteral(".xz")},
    };

    for (const auto &item : cases) {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath(QStringLiteral("stream.txt"));
        const QByteArray payload = QByteArrayLiteral("stream-compress-data");
        QVERIFY(writeFile(source, payload));

        const QString archive = temp.filePath(
            QStringLiteral("payload") + item.suffix);
        StreamBackend writer(item.format);
        CompressOptions options;
        options.archivePath = archive;
        options.sourceFiles = {source};
        options.compressionLevel = 6;
        const OperationResult compressed = compress(writer, options);
        QVERIFY2(compressed.finished && compressed.success,
                 qPrintable(compressed.message));
        QVERIFY(QFileInfo::exists(archive));

        StreamBackend reader(item.format);
        reader.openArchive(archive);
        bool listed = false;
        const QList<ArchiveEntry> entries = list(reader, &listed);
        QVERIFY2(listed, "stream list failed");
        QCOMPARE(entries.size(), 1);

        const QString dest = temp.filePath(QStringLiteral("out"));
        ExtractOptions extractOptions;
        extractOptions.destDir = dest;
        extractOptions.overwrite = OverwritePolicy::Always;
        const OperationResult extracted = extract(reader, extractOptions);
        QVERIFY2(extracted.finished && extracted.success,
                 qPrintable(extracted.message));

        const QStringList files = QDir(dest).entryList(
            QDir::Files | QDir::NoDotAndDotDot);
        QCOMPARE(files.size(), 1);
        QCOMPARE(readFile(QDir(dest).filePath(files.first())), QString::fromUtf8(payload));
    }
}

// Read tar.zst and pure .zst streams through the bundled 7-Zip backend.
// The test skips when the system zstd CLI is unavailable (test fixture only).
// 通过内置 7-Zip 读取 tar.zst 与纯 .zst；系统没有 zstd 命令时跳过
// （仅用于生成测试输入）。
void TestBackends::zstdStreams()
{
    const QString zstd = QStandardPaths::findExecutable(QStringLiteral("zstd"));
    if (zstd.isEmpty()) {
        QSKIP("system zstd is not available for the compatibility fixture");
    }

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("zstd-source.txt"));
    const QByteArray payload = QByteArrayLiteral("zstd backend payload");
    QVERIFY(writeFile(source, payload));

    auto compressWithZstd = [&zstd](const QString &input,
                                    const QString &output) {
        QProcess process;
        process.start(zstd, {QStringLiteral("-q"), QStringLiteral("-f"),
                             input, QStringLiteral("-o"), output});
        if (!process.waitForFinished(10000)) {
            return false;
        }
        return process.exitStatus() == QProcess::NormalExit
            && process.exitCode() == 0;
    };

    // Pure .zst stream: one member, named after the source file.
    const QString pureZst = temp.filePath(QStringLiteral("zstd-source.txt.zst"));
    QVERIFY(compressWithZstd(source, pureZst));
    QCOMPARE(MimeTypeHelper::detect(pureZst), ArchiveFormat::Zstd);

    StreamBackend streamReader(ArchiveFormat::Zstd);
    streamReader.openArchive(pureZst);
    bool listed = false;
    const QList<ArchiveEntry> streamEntries = list(streamReader, &listed);
    QVERIFY2(listed, "zstd stream list failed");
    QCOMPARE(streamEntries.size(), 1);

    const QString streamDest = temp.filePath(QStringLiteral("zstd-out"));
    ExtractOptions streamOptions;
    streamOptions.destDir = streamDest;
    streamOptions.overwrite = OverwritePolicy::Always;
    const OperationResult streamExtracted = extract(streamReader, streamOptions);
    QVERIFY2(streamExtracted.finished && streamExtracted.success,
             qPrintable(streamExtracted.message));
    QCOMPARE(streamEntries.first().path, QStringLiteral("zstd-source.txt"));
    QCOMPARE(readFile(QDir(streamDest).filePath("zstd-source.txt")),
             QString::fromUtf8(payload));

    // Compressed tar: detect the compound suffix without probing, then list
    // and extract the inner tar through TarBackend.
    const QString innerTar = temp.filePath(QStringLiteral("zstd.tar"));
    TarBackend tarWriter(ArchiveFormat::Tar);
    CompressOptions tarOptions;
    tarOptions.archivePath = innerTar;
    tarOptions.sourceFiles = {source};
    const OperationResult tarCreated = compress(tarWriter, tarOptions);
    QVERIFY2(tarCreated.finished && tarCreated.success,
             qPrintable(tarCreated.message));

    const QString tarZst = temp.filePath(QStringLiteral("zstd.tar.zst"));
    QVERIFY(compressWithZstd(innerTar, tarZst));
    QCOMPARE(MimeTypeHelper::detect(tarZst), ArchiveFormat::TarZst);

    TarBackend tarReader(ArchiveFormat::TarZst);
    QVERIFY(tarReader.isReadOnly());
    tarReader.openArchive(tarZst);
    listed = false;
    const QList<ArchiveEntry> tarEntries = list(tarReader, &listed);
    QVERIFY2(listed, "tar.zst list failed");
    QCOMPARE(tarEntries.size(), 1);
    QCOMPARE(tarEntries.first().path, QStringLiteral("zstd-source.txt"));

    const QString tarDest = temp.filePath(QStringLiteral("tar-zstd-out"));
    ExtractOptions tarExtractOptions;
    tarExtractOptions.destDir = tarDest;
    tarExtractOptions.overwrite = OverwritePolicy::Always;
    const OperationResult tarExtracted = extract(tarReader, tarExtractOptions);
    QVERIFY2(tarExtracted.finished && tarExtracted.success,
             qPrintable(tarExtracted.message));
    QCOMPARE(readFile(QDir(tarDest).filePath("zstd-source.txt")),
             QString::fromUtf8(payload));
}

// Detect, list and extract an ISO9660/Joliet disc image (test_files/test.iso).
// 识别、列出并解压 ISO9660/Joliet 光盘镜像（test_files/test.iso）。
void TestBackends::isoImage()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QDir dataDir(QStringLiteral(QARCHIVER_TEST_DATA_DIR));
    const QString fixture = dataDir.filePath(QStringLiteral("test.iso"));
    QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));
    const QString isoPath = temp.filePath(QStringLiteral("disc.iso"));
    QVERIFY(QFile::copy(fixture, isoPath));

    QCOMPARE(MimeTypeHelper::detect(isoPath), ArchiveFormat::Iso);
    QCOMPARE(FormatRegistry::formatFromSuffix(isoPath), ArchiveFormat::Iso);
    QCOMPARE(FormatRegistry::formatFromName(QStringLiteral("iso")),
             ArchiveFormat::Iso);
    QCOMPARE(defaultExtension(ArchiveFormat::Iso),
             QStringLiteral(".iso"));
    QVERIFY(FormatRegistry::isArchivePath(isoPath));
    QVERIFY(!FormatRegistry::isSplitVolume(isoPath));

    // The magic bytes drive detection, not the file name.
    // 格式识别依赖魔数而不是文件名。
    const QString renamed = temp.filePath(QStringLiteral("disc.bin"));
    QVERIFY(QFile::rename(isoPath, renamed));
    QCOMPARE(MimeTypeHelper::detect(renamed), ArchiveFormat::Iso);

    IsoBackend reader;
    reader.openArchive(renamed);
    bool listed = false;
    const QList<ArchiveEntry> entries = list(reader, &listed);
    QVERIFY2(listed, "iso list failed");
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries.at(0).path, QStringLiteral("hello.txt"));
    QCOMPARE(entries.at(1).path, QStringLiteral("sub"));
    QVERIFY(entries.at(1).isDir);
    QCOMPARE(entries.at(2).path, QStringLiteral("sub/nested.txt"));

    const QString dest = temp.filePath(QStringLiteral("iso-out"));
    ExtractOptions extractOptions;
    extractOptions.destDir = dest;
    extractOptions.overwrite = OverwritePolicy::Always;
    const OperationResult extracted = extract(reader, extractOptions);
    QVERIFY2(extracted.finished && extracted.success,
             qPrintable(extracted.message));
    QCOMPARE(readFile(QDir(dest).filePath(QStringLiteral("hello.txt"))),
             QStringLiteral("hello iso\n"));
    QCOMPARE(readFile(QDir(dest).filePath(QStringLiteral("sub/nested.txt"))),
             QStringLiteral("nested\n"));
    reader.closeArchive();

#ifdef QARCHIVER_HAS_LIBARCHIVE
    // The default read path uses the in-process backend; UDF images that it
    // cannot open are retried on the CLI backend by ArchiveManager.
    // 默认读取路径使用进程内后端；它无法打开的 UDF 镜像会由 ArchiveManager
    // 改用命令行后端重试。
    LibArchiveBackend inProcess(ArchiveFormat::Iso);
    inProcess.openArchive(renamed);
    bool inProcessListed = false;
    const QList<ArchiveEntry> inProcessEntries = list(inProcess, &inProcessListed);
    QVERIFY2(inProcessListed, "libarchive iso list failed");
    QCOMPARE(inProcessEntries.size(), 3);
    // libarchive returns the members in image order and marks directories with
    // a trailing slash, so compare as a set.
    // libarchive 按镜像中的顺序返回成员，目录路径带尾斜杠，因此按集合比较。
    QStringList inProcessPaths;
    for (const ArchiveEntry &entry : inProcessEntries) {
        inProcessPaths << entry.path;
    }
    QVERIFY2(inProcessPaths.contains(QStringLiteral("hello.txt")),
             qPrintable(inProcessPaths.join(QLatin1Char(','))));
    QVERIFY2(inProcessPaths.contains(QStringLiteral("sub/")),
             qPrintable(inProcessPaths.join(QLatin1Char(','))));
    QVERIFY2(inProcessPaths.contains(QStringLiteral("sub/nested.txt")),
             qPrintable(inProcessPaths.join(QLatin1Char(','))));

    const QString inProcessDest = temp.filePath(QStringLiteral("iso-out-lib"));
    ExtractOptions inProcessOptions;
    inProcessOptions.destDir = inProcessDest;
    inProcessOptions.overwrite = OverwritePolicy::Always;
    const OperationResult inProcessExtracted =
        extract(inProcess, inProcessOptions);
    QVERIFY2(inProcessExtracted.finished && inProcessExtracted.success,
             qPrintable(inProcessExtracted.message));
    QCOMPARE(readFile(QDir(inProcessDest).filePath(QStringLiteral("sub/nested.txt"))),
             QStringLiteral("nested\n"));
    inProcess.closeArchive();
#endif
}

// Disc images are read-only: every write path is rejected.
// 光盘镜像只读：所有写入路径都被拒绝。
void TestBackends::isoWriteRejected()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QDir dataDir(QStringLiteral(QARCHIVER_TEST_DATA_DIR));
    const QString fixture = dataDir.filePath(QStringLiteral("test.iso"));
    QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));
    const QString isoPath = temp.filePath(QStringLiteral("disc.iso"));
    QVERIFY(QFile::copy(fixture, isoPath));

    IsoBackend backend;
    backend.openArchive(isoPath);
    QVERIFY(backend.isReadOnly());
    QVERIFY(!backend.supportsAppend());
    QVERIFY(!backend.supportsVolume());
    QVERIFY(!backend.supportsEncryption());
    QCOMPARE(backend.format(), ArchiveFormat::Iso);

    AddFilesOptions addOptions;
    addOptions.archivePath = isoPath;
    addOptions.sourceFiles = {isoPath};
    const OperationResult added = addFiles(backend, addOptions);
    QVERIFY(added.finished);
    QVERIFY(!added.success);

    DeleteFilesOptions deleteOptions;
    deleteOptions.archivePath = isoPath;
    deleteOptions.targetEntries = {QStringLiteral("hello.txt")};
    const OperationResult deleted = removeEntries(backend, deleteOptions);
    QVERIFY(deleted.finished);
    QVERIFY(!deleted.success);

    CompressOptions compressOptions;
    compressOptions.archivePath = temp.filePath(QStringLiteral("new.iso"));
    compressOptions.sourceFiles = {isoPath};
    const OperationResult created = compress(backend, compressOptions);
    QVERIFY(created.finished);
    QVERIFY(!created.success);
    QVERIFY(!QFileInfo::exists(compressOptions.archivePath));
    backend.closeArchive();
}

// Read a pure gzip stream as a single entry.
// 将纯 gzip 流作为单个条目读取。
void TestBackends::gzipStream()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("stream.txt"));
    QVERIFY(writeFile(source, QByteArrayLiteral("stream-data")));

    const QString gz = temp.filePath(QStringLiteral("stream.txt.gz"));
    const QString gzipProgram =
        QStandardPaths::findExecutable(QStringLiteral("gzip"));
    if (gzipProgram.isEmpty()) {
        QSKIP("system gzip is not available for the compatibility fixture");
    }
    QProcess gzip;
    gzip.start(gzipProgram, {QStringLiteral("-c"), source});
    QVERIFY(gzip.waitForFinished(10000));
    QCOMPARE(gzip.exitCode(), 0);
    QVERIFY(writeFile(gz, gzip.readAllStandardOutput()));

    StreamBackend backend(ArchiveFormat::Gzip);
    backend.openArchive(gz);
    bool listed = false;
    const auto entries = list(backend, &listed);
    QVERIFY2(listed, "gzip stream list failed");
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().path, QStringLiteral("stream.txt"));

    const QString dest = temp.filePath(QStringLiteral("out"));
    ExtractOptions options;
    options.destDir = dest;
    options.overwrite = OverwritePolicy::Always;
    const auto extracted = extract(backend, options);
    QVERIFY2(extracted.finished && extracted.success,
             qPrintable(extracted.message));
    QCOMPARE(readFile(dest + QStringLiteral("/stream.txt")),
             QStringLiteral("stream-data"));
}

QTEST_GUILESS_MAIN(TestBackends)

#include "tst_backends.moc"
