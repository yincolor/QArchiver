// QArchiver / 小Q解压
// Tests for ArchiveManager routing, retries and error handling.
// ArchiveManager 路由、重试与错误处理测试。

#include <QtTest>

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

// The factory is used by both build configurations (libarchive on/off), so its
// include must stay outside the feature guard.
// 工厂在开启/关闭 libarchive 的构建中都会用到，因此其 include 不能放进
// 功能开关内部。
#include "core/ArchiveBackendFactory.h"

#ifdef QARCHIVER_HAS_LIBARCHIVE
#include "backends/libarchive/LibArchiveBackend.h"
#endif
#include "backends/rar/RarBackend.h"
#include "backends/sevenzip/SevenZipBackend.h"
#include "backends/tar/TarBackend.h"
#include "backends/sevenzip/ZipBackend.h"
#include "core/AppSettings.h"
#include "core/ArchiveManager.h"
#include "common/ArchiveOptions.h"
#include "ui/services/PreviewManager.h"
#include "ui/services/PreviewUtils.h"
#include "utils/FileUtils.h"
#include "utils/ProcessRunner.h"

class PreviewUrlCapture : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;

public slots:
    void handleUrl(const QUrl &url)
    {
        urls << url;
    }
};

namespace {

// Create a 7z archive with encrypted headers and payload (password
// "classified").  Returns an empty string when the 7z tool is unavailable.
// 创建文件头与内容都加密（密码 "classified"）的 7z 压缩包；没有 7z 时返回空。
QString createEncrypted7z(const QString &dir)
{
    const QString sevenZip = ProcessRunner::resolveToolPath(QStringLiteral("7z"));
    if (sevenZip.isEmpty()) {
        return QString();
    }
    const QString payload = QDir(dir).filePath(QStringLiteral("secret.txt"));
    {
        QFile file(payload);
        if (!file.open(QIODevice::WriteOnly)
            || file.write(QByteArrayLiteral("classified")) != 10) {
            return QString();
        }
    }
    const QString archive = QDir(dir).filePath(QStringLiteral("secret.7z"));
    QProcess process;
    process.setWorkingDirectory(dir);
    process.start(sevenZip,
                  {QStringLiteral("a"), QStringLiteral("-bso0"),
                   QStringLiteral("-bsp0"), QStringLiteral("-pclassified"),
                   QStringLiteral("-mhe=on"), archive, payload});
    if (!process.waitForFinished(60000) || process.exitCode() != 0) {
        return QString();
    }
    return archive;
}

} // namespace

class TestManager : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void openListExtract();
    void compressOverwritesExistingTarget();
    void encrypted7zFallsBackToCli();
    void passwordRetrySucceeds();
    void passwordRetryLimit();
    void rarSplitCompressionReplacesVolumes();
    void continuousPreviewExtractsSiblingImages();
    void openFailurePreservesCurrentArchive();
    void unsafeSymlinkRequiresDecision();
    void backendSelectionHonoursPreference();
    void addDeleteReopensReadOnlyBackend();
    void externalEditWriteBackThroughReopen();
    void isoImageIsReadOnly();
    void workerShutdownIsCooperative();
};

// Prepare the manager test environment.
// 准备管理器测试环境。
void TestManager::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("QArchiverTest"));
    QCoreApplication::setApplicationName(QStringLiteral("QArchiverTest"));
}

// Open, list and extract through ArchiveManager.
// 通过 ArchiveManager 打开、列出并解压。
void TestManager::openListExtract()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString source = temp.filePath(QStringLiteral("payload.txt"));
    {
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray payload = QByteArrayLiteral("manager-data");
        QCOMPARE(file.write(payload), qint64(payload.size()));
    }

    const QString archive = temp.filePath(QStringLiteral("manager.tar"));
    {
        TarBackend backend(ArchiveFormat::Tar);
        CompressOptions options;
        options.archivePath = archive;
        options.sourceFiles = {source};
        bool finished = false;
        bool success = false;
        connect(&backend, &ArchiveBackend::operationFinished, &backend,
                [&finished, &success](bool ok, const QString &) {
                    finished = true;
                    success = ok;
                });
        backend.compress(options);
        QVERIFY(finished);
        QVERIFY(success);
    }

    ArchiveManager &manager = ArchiveManager::instance();
    QSignalSpy openedSpy(&manager, &ArchiveManager::archiveOpened);
    QSignalSpy listSpy(&manager, &ArchiveManager::fileListReady);

    manager.openArchive(archive);
    QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 10000);
    QCOMPARE(manager.currentPath(), archive);
    QCOMPARE(manager.currentFormat(), ArchiveFormat::Tar);

    manager.listContents();
    QTRY_COMPARE_WITH_TIMEOUT(listSpy.count(), 1, 10000);
    QVERIFY(!manager.entries().isEmpty());
    // The manager caches the encoding reported by the backend, so extraction
    // does not have to read the worker-owned member.
    // 管理器缓存后端上报的编码，解压时无需读取工作线程持有的成员。
    QCOMPARE(manager.nameEncoding(), QStringLiteral("UTF-8"));

    const QString dest = temp.filePath(QStringLiteral("out"));
    QSignalSpy finishSpy(&manager, &ArchiveManager::operationFinished);
    ExtractOptions extractOptions;
    extractOptions.destDir = dest;
    extractOptions.overwrite = OverwritePolicy::Always;
    manager.extract(extractOptions);
    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 10000);
    QVERIFY(finishSpy.first().at(0).toBool());

    QFile extracted(dest + QStringLiteral("/payload.txt"));
    QVERIFY(extracted.open(QIODevice::ReadOnly));
    QCOMPARE(extracted.readAll(), QByteArrayLiteral("manager-data"));

    manager.closeArchive();
    QCOMPARE(manager.nameEncoding(), QString());
}

// A disc image is opened through the normal read path and stays read-only.
// 光盘镜像通过普通读取路径打开，并保持只读。
void TestManager::isoImageIsReadOnly()
{
    const QString fixture =
        QStringLiteral(QARCHIVER_TEST_DATA_DIR "/test.iso");
    QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString isoPath = temp.filePath(QStringLiteral("disc.iso"));
    QVERIFY(QFile::copy(fixture, isoPath));

    ArchiveManager &manager = ArchiveManager::instance();
    QSignalSpy openedSpy(&manager, &ArchiveManager::archiveOpened);
    QSignalSpy listSpy(&manager, &ArchiveManager::fileListReady);

    manager.openArchive(isoPath);
    QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 10000);
    QCOMPARE(manager.currentFormat(), ArchiveFormat::Iso);

    manager.listContents();
    QTRY_COMPARE_WITH_TIMEOUT(listSpy.count(), 1, 10000);
    QStringList paths;
    for (const ArchiveEntry &entry : manager.entries()) {
        paths << entry.path;
    }
    QVERIFY2(paths.contains(QStringLiteral("hello.txt")),
             qPrintable(paths.join(QLatin1Char(','))));
    QVERIFY2(paths.contains(QStringLiteral("sub/nested.txt")),
             qPrintable(paths.join(QLatin1Char(','))));

    // No add/delete for disc images, so the UI keeps those actions disabled.
    // 光盘镜像不能添加/删除，界面因此保持这些动作不可用。
    QVERIFY(!manager.canModifyCurrentArchive());

    const QString dest = temp.filePath(QStringLiteral("out"));
    QSignalSpy finishSpy(&manager, &ArchiveManager::operationFinished);
    ExtractOptions extractOptions;
    extractOptions.destDir = dest;
    extractOptions.overwrite = OverwritePolicy::Always;
    manager.extract(extractOptions);
    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 10000);
    QVERIFY(finishSpy.first().at(0).toBool());

    QFile extracted(dest + QStringLiteral("/sub/nested.txt"));
    QVERIFY(extracted.open(QIODevice::ReadOnly));
    QCOMPARE(extracted.readAll(), QByteArrayLiteral("nested\n"));

    manager.closeArchive();
}

// Compression replaces an existing target safely.
// 压缩操作安全替换已有目标文件。
void TestManager::compressOverwritesExistingTarget()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString source = temp.filePath(QStringLiteral("source.txt"));
    const QByteArray payload = QByteArrayLiteral("replacement-content");
    {
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(payload), qint64(payload.size()));
    }

    const QString target = temp.filePath(QStringLiteral("target.tar"));
    {
        QFile file(target);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArrayLiteral("old-content")), qint64(11));
    }

    ArchiveManager &manager = ArchiveManager::instance();
    QSignalSpy finishSpy(&manager, &ArchiveManager::operationFinished);
    CompressOptions options;
    options.archivePath = target;
    options.sourceFiles = {source};
    manager.compress(options, ArchiveFormat::Tar);
    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 10000);
    QVERIFY2(finishSpy.first().at(0).toBool(),
             qPrintable(finishSpy.first().at(1).toString()));

    QSignalSpy openedSpy(&manager, &ArchiveManager::archiveOpened);
    QSignalSpy listSpy(&manager, &ArchiveManager::fileListReady);
    manager.openArchive(target);
    QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 10000);
    manager.listContents();
    QTRY_COMPARE_WITH_TIMEOUT(listSpy.count(), 1, 10000);

    const QString dest = temp.filePath(QStringLiteral("out"));
    QSignalSpy extractSpy(&manager, &ArchiveManager::operationFinished);
    ExtractOptions extractOptions;
    extractOptions.destDir = dest;
    extractOptions.overwrite = OverwritePolicy::Always;
    manager.extract(extractOptions);
    QTRY_COMPARE_WITH_TIMEOUT(extractSpy.count(), 1, 10000);
    QVERIFY(extractSpy.first().at(0).toBool());

    QFile extracted(dest + QStringLiteral("/source.txt"));
    QVERIFY(extracted.open(QIODevice::ReadOnly));
    QCOMPARE(extracted.readAll(), payload);
    manager.closeArchive();
}

// An encrypted 7z cannot be read by libarchive. The manager must notice the
// UnsupportedFormat error, rebuild the backend on the 7z command line and
// replay the pending operation (including the password prompt).
// Encrypted 7z falls back from libarchive to CLI.
// 加密 7z 从 libarchive 回退到 CLI。
void TestManager::encrypted7zFallsBackToCli()
{
#ifdef QARCHIVER_HAS_LIBARCHIVE
    const QString sevenZip = ProcessRunner::resolveToolPath(QStringLiteral("7z"));
    if (sevenZip.isEmpty()) {
        QSKIP("the 7z tool is required for this test");
    }

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString payloadPath = temp.filePath(QStringLiteral("secret.txt"));
    {
        QFile file(payloadPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArrayLiteral("classified")), qint64(10));
    }

    const QString archive = temp.filePath(QStringLiteral("secret.7z"));
    {
        QProcess process;
        process.setWorkingDirectory(temp.path());
        process.start(sevenZip,
                      {QStringLiteral("a"), QStringLiteral("-bso0"),
                       QStringLiteral("-bsp0"), QStringLiteral("-pclassified"),
                       archive, payloadPath});
        QVERIFY(process.waitForFinished(60000));
        QCOMPARE(process.exitCode(), 0);
    }

    ArchiveManager &manager = ArchiveManager::instance();
    QSignalSpy openedSpy(&manager, &ArchiveManager::archiveOpened);
    QSignalSpy listSpy(&manager, &ArchiveManager::fileListReady);

    manager.openArchive(archive);
    QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 10000);

    // Listing works: 7z does not encrypt the entry names by default, so both
    // libarchive and the CLI can list the archive.
    manager.listContents();
    QTRY_COMPARE_WITH_TIMEOUT(listSpy.count(), 1, 10000);

    // Extraction is where libarchive gives up; the manager must rebuild the
    // backend on the 7z command line and replay the extraction.
    const QString dest = temp.filePath(QStringLiteral("out"));
    QSignalSpy finishSpy(&manager, &ArchiveManager::operationFinished);
    ExtractOptions options;
    options.destDir = dest;
    options.overwrite = OverwritePolicy::Always;
    options.password = QStringLiteral("classified");
    manager.extract(options);

    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 20000);
    QVERIFY2(finishSpy.first().at(0).toBool(),
             qPrintable(finishSpy.first().at(1).toString()));
    QVERIFY(qobject_cast<SevenZipBackend *>(manager.backend()) != nullptr);

    QFile extracted(
        dest + QStringLiteral("/") + QFileInfo(payloadPath).fileName());
    QVERIFY(extracted.open(QIODevice::ReadOnly));
    QCOMPARE(extracted.readAll(), QByteArrayLiteral("classified"));

    manager.closeArchive();
#else
    QSKIP("built without libarchive");
#endif
}

// A wrong password must keep the pending operation and ask again; the second,
// correct password completes the listing.
// 密码错误时必须保留待处理操作并再次询问；第二次输入正确密码后完成列表。
void TestManager::passwordRetrySucceeds()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString archive = createEncrypted7z(temp.path());
    if (archive.isEmpty()) {
        QSKIP("the 7z tool is required for this test");
    }

    AppSettings &settings = AppSettings::instance();
    const bool savedLibArchive = settings.useLibArchiveBackend();
    settings.setUseLibArchiveBackend(false);
    const auto restore = qScopeGuard([savedLibArchive] {
        AppSettings::instance().setUseLibArchiveBackend(savedLibArchive);
    });

    ArchiveManager &manager = ArchiveManager::instance();
    QSignalSpy openedSpy(&manager, &ArchiveManager::archiveOpened);
    QSignalSpy listSpy(&manager, &ArchiveManager::fileListReady);
    QSignalSpy errorSpy(&manager, &ArchiveManager::errorOccurred);

    // The context object removes the connection even if a QVERIFY returns early.
    // 上下文对象保证即使 QVERIFY 提前返回也会断开连接。
    QObject promptContext;
    int prompts = 0;
    QList<bool> wrongFlags;
    connect(&manager, &ArchiveManager::passwordRequested, &promptContext,
            [&](bool wrongPassword) {
                ++prompts;
                wrongFlags.append(wrongPassword);
                manager.retryWithPassword(
                    prompts == 1 ? QStringLiteral("wrong")
                                 : QStringLiteral("classified"));
            },
            Qt::DirectConnection);

    manager.openArchive(archive);
    QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 10000);

    // The encrypted header makes the first listing ask for a password.
    // 加密文件头使第一次列表就请求密码。
    manager.listContents();
    QTRY_COMPARE_WITH_TIMEOUT(listSpy.count(), 1, 30000);
    QCOMPARE(prompts, 2);
    QCOMPARE(wrongFlags, (QList<bool>{false, true}));
    QCOMPARE(errorSpy.count(), 0);
    QVERIFY(manager.nameEncoding() == QLatin1String("UTF-8")
            || !manager.nameEncoding().isEmpty());

    // Extraction with the correct password must not prompt again, and must
    // start from a fresh retry budget.
    // 使用正确密码解压不应再次弹窗，并重新计算尝试次数。
    QSignalSpy finishSpy(&manager, &ArchiveManager::operationFinished);
    ExtractOptions options;
    options.destDir = temp.filePath(QStringLiteral("out"));
    options.overwrite = OverwritePolicy::Always;
    options.password = QStringLiteral("classified");
    manager.extract(options);
    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 30000);
    QVERIFY2(finishSpy.first().at(0).toBool(),
             qPrintable(finishSpy.first().at(1).toString()));
    QCOMPARE(prompts, 2);
    QVERIFY(QFileInfo::exists(
        temp.filePath(QStringLiteral("out/secret.txt"))));

    manager.closeArchive();
}

// After the attempt budget is exhausted the manager reports WrongPassword and
// releases the pending operation; a new listing still works.
// 尝试次数用尽后管理器报告 WrongPassword 并释放待处理操作；随后重新列表仍可用。
void TestManager::passwordRetryLimit()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString archive = createEncrypted7z(temp.path());
    if (archive.isEmpty()) {
        QSKIP("the 7z tool is required for this test");
    }

    AppSettings &settings = AppSettings::instance();
    const bool savedLibArchive = settings.useLibArchiveBackend();
    settings.setUseLibArchiveBackend(false);
    const auto restore = qScopeGuard([savedLibArchive] {
        AppSettings::instance().setUseLibArchiveBackend(savedLibArchive);
    });

    ArchiveManager &manager = ArchiveManager::instance();
    QSignalSpy openedSpy(&manager, &ArchiveManager::archiveOpened);
    QSignalSpy listSpy(&manager, &ArchiveManager::fileListReady);
    QSignalSpy errorSpy(&manager, &ArchiveManager::errorOccurred);

    QObject promptContext;
    int prompts = 0;
    connect(&manager, &ArchiveManager::passwordRequested, &promptContext,
            [&](bool) {
                ++prompts;
                manager.retryWithPassword(QStringLiteral("wrong"));
            },
            Qt::DirectConnection);

    manager.openArchive(archive);
    QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 10000);

    manager.listContents();
    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 1, 30000);
    QCOMPARE(prompts, 3);
    QCOMPARE(errorSpy.first().at(0).value<ArchiveErrorCode>(),
             ArchiveErrorCode::WrongPassword);
    QCOMPARE(listSpy.count(), 0);

    // The failed retry chain must not poison later operations.
    // 失败的重试链不能影响后续操作。
    ListOptions options;
    options.password = QStringLiteral("classified");
    manager.listContents(options);
    QTRY_COMPARE_WITH_TIMEOUT(listSpy.count(), 1, 30000);

    manager.closeArchive();
}

// Creating RAR volumes through the manager replaces an older volume set.
// 通过管理器创建 RAR 分卷时，必须替换旧的分卷集。
void TestManager::rarSplitCompressionReplacesVolumes()
{
    if (!RarBackend::isCreateAvailable()) {
        QSKIP("the rar command is not installed");
    }

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString source = temp.filePath(QStringLiteral("payload.bin"));
    QByteArray payload;
    payload.reserve(12000);
    for (int i = 0; i < 12000; ++i) {
        payload.append(static_cast<char>((i * 17 + 3) & 0xff));
    }
    {
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(payload), qint64(payload.size()));
    }

    const QString archive = temp.filePath(QStringLiteral("volumes.rar"));
    ArchiveManager &manager = ArchiveManager::instance();

    CompressOptions splitOptions;
    splitOptions.archivePath = archive;
    splitOptions.sourceFiles = {source};
    splitOptions.compressionLevel = 0;
    splitOptions.volumeSize = 3000;

    QSignalSpy splitSpy(&manager, &ArchiveManager::operationFinished);
    manager.compress(splitOptions, ArchiveFormat::Rar5);
    QTRY_COMPARE_WITH_TIMEOUT(splitSpy.count(), 1, 30000);
    QVERIFY2(splitSpy.first().at(0).toBool(),
             qPrintable(splitSpy.first().at(1).toString()));

    const QString firstVolume = temp.filePath(QStringLiteral("volumes.part1.rar"));
    const QString secondVolume = temp.filePath(QStringLiteral("volumes.part2.rar"));
    QVERIFY(QFileInfo::exists(firstVolume));
    QVERIFY(QFileInfo::exists(secondVolume));

    // Re-create the same target as a single-volume archive: the old part
    // files must be removed before the new archive is promoted.
    // 将同一目标重新创建为单卷压缩包：旧分卷必须在新包替换前删除。
    CompressOptions singleOptions = splitOptions;
    singleOptions.volumeSize = 0;
    QSignalSpy singleSpy(&manager, &ArchiveManager::operationFinished);
    manager.compress(singleOptions, ArchiveFormat::Rar5);
    QTRY_COMPARE_WITH_TIMEOUT(singleSpy.count(), 1, 30000);
    QVERIFY2(singleSpy.first().at(0).toBool(),
             qPrintable(singleSpy.first().at(1).toString()));

    QVERIFY(QFileInfo::exists(archive));
    const QStringList remaining = QDir(temp.path()).entryList(
        {QStringLiteral("volumes.part*.rar")}, QDir::Files);
    QVERIFY2(remaining.isEmpty(), qPrintable(remaining.join(QStringLiteral(", "))));

    QSignalSpy openSpy(&manager, &ArchiveManager::archiveOpened);
    QSignalSpy listSpy(&manager, &ArchiveManager::fileListReady);
    manager.openArchive(archive);
    QTRY_COMPARE_WITH_TIMEOUT(openSpy.count(), 1, 10000);
    manager.listContents();
    QTRY_COMPARE_WITH_TIMEOUT(listSpy.count(), 1, 10000);
    QCOMPARE(manager.currentFormat(), ArchiveFormat::Rar5);
    QVERIFY(!manager.entries().isEmpty());

    manager.closeArchive();
}

// The "use libarchive" preference decides which backend serves a plain archive;
// split volumes and rar always stay on the command line.
// Opening one image must prepare all same-directory images for the system
// viewer.  A custom file URL handler stands in for the external viewer.
// 打开一张图片时必须准备好同目录所有图片，供系统查看器连续切换。
// 使用自定义 file URL 处理器代替外部查看器。
void TestManager::continuousPreviewExtractsSiblingImages()
{
    const QString fixture = QStringLiteral(QARCHIVER_TEST_DATA_DIR)
        + QStringLiteral("/连续看图测试.tar.gz");
    if (!QFileInfo::exists(fixture)) {
        QSKIP("continuous preview fixture is missing");
    }
    if (ProcessRunner::resolveToolPath(QStringLiteral("7z")).isEmpty()) {
        QSKIP("the 7z tool is required for the preview fixture");
    }

    ArchiveManager &manager = ArchiveManager::instance();
    QSignalSpy openedSpy(&manager, &ArchiveManager::archiveOpened);
    QSignalSpy listSpy(&manager, &ArchiveManager::fileListReady);
    manager.openArchive(fixture);
    QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 10000);
    manager.listContents();
    QTRY_COMPARE_WITH_TIMEOUT(listSpy.count(), 1, 10000);

    const QString target = QStringLiteral("连续看图测试/1.png");
    qint64 targetSize = 0;
    for (const ArchiveEntry &entry : manager.entries()) {
        if (entry.path == target) {
            targetSize = entry.size;
            break;
        }
    }
    QVERIFY(targetSize > 0);

    const QStringList images = PreviewUtils::imageSiblings(manager.entries(), target);
    QCOMPARE(images.size(), 5);

    PreviewUrlCapture capture;
    QDesktopServices::setUrlHandler(QStringLiteral("file"), &capture, "handleUrl");
    PreviewManager::instance().cleanupOwn();
    PreviewManager::instance().preview(manager.currentPath(),
                                       manager.currentFormat(), target,
                                       targetSize, images, QString());

    QTRY_COMPARE_WITH_TIMEOUT(capture.urls.size(), 1, 15000);
    QDesktopServices::unsetUrlHandler(QStringLiteral("file"));

    const QString opened = capture.urls.first().toLocalFile();
    QVERIFY2(QFileInfo::exists(opened), qPrintable(opened));
    const QString previewRoot = QFileInfo(opened).absolutePath();
    QVERIFY(previewRoot.endsWith(QStringLiteral("连续看图测试")));

    for (const QString &image : images) {
        const QString extracted = QDir(previewRoot).filePath(
            QFileInfo(FileUtils::normalizeEntryPath(image)).fileName());
        QVERIFY2(QFileInfo::exists(extracted), qPrintable(extracted));
    }
    QVERIFY(!QFileInfo::exists(QDir(previewRoot).filePath(
        QStringLiteral("表情、微笑.svg"))));

    PreviewManager::instance().cleanupOwn();
    manager.closeArchive();
}

// A failed open must leave the currently open archive intact.
// 打开失败时必须保留当前已打开的压缩包。
void TestManager::openFailurePreservesCurrentArchive()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("current.txt"));
    {
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArrayLiteral("current")), qint64(7));
    }
    const QString archive = temp.filePath(QStringLiteral("current.tar"));
    {
        TarBackend backend(ArchiveFormat::Tar);
        CompressOptions options;
        options.archivePath = archive;
        options.sourceFiles = {source};
        bool finished = false;
        connect(&backend, &ArchiveBackend::operationFinished, &backend,
                [&finished](bool, const QString &) { finished = true; });
        backend.compress(options);
        QVERIFY(finished);
    }

    ArchiveManager &manager = ArchiveManager::instance();
    manager.closeArchive();
    QSignalSpy openedSpy(&manager, &ArchiveManager::archiveOpened);
    manager.openArchive(archive);
    QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 10000);

    QSignalSpy errorSpy(&manager, &ArchiveManager::errorOccurred);
    manager.openArchive(temp.filePath(QStringLiteral("missing.tar")));
    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 1, 10000);
    QCOMPARE(errorSpy.first().at(0).value<ArchiveErrorCode>(),
             ArchiveErrorCode::FileNotFound);
    QCOMPARE(manager.currentPath(), archive);
    QVERIFY(manager.backend() != nullptr);

    manager.closeArchive();
}

// Unsafe symlinks are detected before the CLI extractor runs; the user can
// skip them, or abort without creating the destination.
// 在命令行解压器运行前检测危险符号链接；用户可跳过，或中止且不创建目标目录。
void TestManager::unsafeSymlinkRequiresDecision()
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
    {
        QFile file(QDir(src).filePath(QStringLiteral("safe.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArrayLiteral("safe")), qint64(4));
    }
    QVERIFY(QFile::link(QStringLiteral("/etc/passwd"),
                        QDir(src).filePath(QStringLiteral("escape"))));

    const QString archive = temp.filePath(QStringLiteral("unsafe.tar"));
    {
        QProcess process;
        process.start(tar, {QStringLiteral("-cf"), archive,
                            QStringLiteral("-C"), src, QStringLiteral(".")});
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitCode(), 0);
    }

    ArchiveManager &manager = ArchiveManager::instance();
    manager.closeArchive();
    QSignalSpy openedSpy(&manager, &ArchiveManager::archiveOpened);
    QSignalSpy listSpy(&manager, &ArchiveManager::fileListReady);
    manager.openArchive(archive);
    QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 10000);
    manager.listContents();
    QTRY_COMPARE_WITH_TIMEOUT(listSpy.count(), 1, 10000);

    QString linkPath;
    for (const ArchiveEntry &entry : manager.entries()) {
        if (entry.isSymLink) {
            linkPath = entry.path;
            break;
        }
    }
    QVERIFY2(!linkPath.isEmpty(), "tar did not expose the symlink entry");

    int decisions = 0;
    manager.setSymlinkDecisionCallback(
        [&decisions](const QString &, const QString &) {
            ++decisions;
            return ArchiveManager::SymlinkDecision::SkipAll;
        });

    const QString dest = temp.filePath(QStringLiteral("out-skip"));
    QSignalSpy finishSpy(&manager, &ArchiveManager::operationFinished);
    ExtractOptions options;
    options.destDir = dest;
    options.overwrite = OverwritePolicy::Always;
    manager.extract(options);
    QTRY_COMPARE_WITH_TIMEOUT(finishSpy.count(), 1, 20000);
    QVERIFY2(finishSpy.first().at(0).toBool(),
             qPrintable(finishSpy.first().at(1).toString()));
    QCOMPARE(decisions, 1);
    QVERIFY(QFileInfo::exists(QDir(dest).filePath(QStringLiteral("safe.txt"))));
    QVERIFY(!QFileInfo::exists(QDir(dest).filePath(linkPath)));

    manager.setSymlinkDecisionCallback(
        [](const QString &, const QString &) {
            return ArchiveManager::SymlinkDecision::Abort;
        });
    const QString abortDest = temp.filePath(QStringLiteral("out-abort"));
    QSignalSpy errorSpy(&manager, &ArchiveManager::errorOccurred);
    ExtractOptions abortOptions;
    abortOptions.destDir = abortDest;
    abortOptions.overwrite = OverwritePolicy::Always;
    manager.extract(abortOptions);
    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 1, 10000);
    QCOMPARE(errorSpy.first().at(0).value<ArchiveErrorCode>(),
             ArchiveErrorCode::Cancelled);
    QVERIFY(!QFileInfo::exists(abortDest));

    manager.setSymlinkDecisionCallback(nullptr);
    manager.closeArchive();
}

// Backend selection follows preference and split-volume rules.
// 后端选择遵循偏好设置与分卷规则。
// libarchive is read-only.  Adding/deleting through it must transparently
// reopen the archive with the CLI backend and replay the operation.
// libarchive 是只读的。通过它添加/删除时必须透明地用命令行后端重新打开
// 压缩包并重放操作。
void TestManager::addDeleteReopensReadOnlyBackend()
{
#ifndef QARCHIVER_HAS_LIBARCHIVE
    QSKIP("built without the libarchive read backend");
#else
    if (!ArchiveBackendFactory::isLibArchiveAvailable()
        || ArchiveBackendFactory::isLibArchiveDisabledByEnvironment()) {
        QSKIP("libarchive read backend is unavailable");
    }

    AppSettings &settings = AppSettings::instance();
    const bool savedPreference = settings.useLibArchiveBackend();
    settings.setUseLibArchiveBackend(true);

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString source = temp.filePath(QStringLiteral("first.txt"));
    {
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("first") == 5);
    }
    const QString archive = temp.filePath(QStringLiteral("write-through.zip"));
    {
        ZipBackend backend;
        CompressOptions options;
        options.archivePath = archive;
        options.sourceFiles = {source};
        bool finished = false;
        bool success = false;
        connect(&backend, &ArchiveBackend::operationFinished, &backend,
                [&finished, &success](bool ok, const QString &) {
                    finished = true;
                    success = ok;
                });
        backend.compress(options);
        QVERIFY(finished);
        QVERIFY(success);
    }

    ArchiveManager &manager = ArchiveManager::instance();
    QSignalSpy openedSpy(&manager, &ArchiveManager::archiveOpened);
    manager.openArchive(archive);
    QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 10000);
    QVERIFY(qobject_cast<LibArchiveBackend *>(manager.backend()) != nullptr);
    QVERIFY(!manager.backend()->supportsAppend());

    const QString added = temp.filePath(QStringLiteral("added.txt"));
    {
        QFile file(added);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("added") == 5);
    }

    QSignalSpy addFinished(&manager, &ArchiveManager::operationFinished);
    AddFilesOptions addOptions;
    addOptions.archivePath = archive;
    addOptions.sourceFiles = {added};
    manager.addFiles(addOptions);
    QTRY_COMPARE_WITH_TIMEOUT(addFinished.count(), 1, 15000);
    QVERIFY2(addFinished.first().at(0).toBool(), "addFiles should succeed after reopening with the CLI backend");
    QVERIFY(manager.backend() != nullptr);
    QVERIFY(qobject_cast<LibArchiveBackend *>(manager.backend()) == nullptr);
    QVERIFY(manager.backend()->supportsAppend());

    QSignalSpy listSpy(&manager, &ArchiveManager::fileListReady);
    manager.listContents();
    QTRY_COMPARE_WITH_TIMEOUT(listSpy.count(), 1, 10000);
    bool found = false;
    for (const ArchiveEntry &entry : manager.entries()) {
        if (entry.path.contains(QStringLiteral("added.txt"))) {
            found = true;
            break;
        }
    }
    QVERIFY2(found, "the added file must appear in the archive");

    QSignalSpy deleteFinished(&manager, &ArchiveManager::operationFinished);
    DeleteFilesOptions deleteOptions;
    deleteOptions.archivePath = archive;
    deleteOptions.targetEntries = {QStringLiteral("added.txt")};
    manager.deleteFiles(deleteOptions);
    QTRY_COMPARE_WITH_TIMEOUT(deleteFinished.count(), 1, 15000);
    QVERIFY2(deleteFinished.first().at(0).toBool(), "deleteFiles should succeed on the CLI backend");

    QSignalSpy finalListSpy(&manager, &ArchiveManager::fileListReady);
    manager.listContents();
    QTRY_COMPARE_WITH_TIMEOUT(finalListSpy.count(), 1, 10000);
    for (const ArchiveEntry &entry : manager.entries()) {
        QVERIFY2(!entry.path.endsWith(QStringLiteral("added.txt")),
                 "the deleted file must be gone");
    }

    manager.closeArchive();
    settings.setUseLibArchiveBackend(savedPreference);
#endif
}

// An external-edit write-back on a libarchive-served archive is replayed on the
// CLI backend after the transparent reopen, so the edited member is replaced.
// 由 libarchive 后端服务的压缩包在外部编辑写回时会先透明重开为 CLI 后端，
// 随后重放写回，从而替换被编辑的成员。
void TestManager::externalEditWriteBackThroughReopen()
{
#ifndef QARCHIVER_HAS_LIBARCHIVE
    QSKIP("built without the libarchive read backend");
#else
    if (!ArchiveBackendFactory::isLibArchiveAvailable()
        || ArchiveBackendFactory::isLibArchiveDisabledByEnvironment()) {
        QSKIP("libarchive read backend is unavailable");
    }

    AppSettings &settings = AppSettings::instance();
    const bool savedPreference = settings.useLibArchiveBackend();
    settings.setUseLibArchiveBackend(true);

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    // Plain tar: writable, but served read-only by libarchive until reopened.
    // 未压缩 tar：可写，但在重开前由 libarchive 以只读方式提供服务。
    const QString sourceRoot = temp.filePath(QStringLiteral("src"));
    const QString memberPath = QStringLiteral("src/sub/a.txt");
    const QString sourceFile = temp.filePath(memberPath);
    QVERIFY(QDir().mkpath(QFileInfo(sourceFile).absolutePath()));
    {
        QFile file(sourceFile);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("original") == 8);
    }
    const QString archive = temp.filePath(QStringLiteral("edit.tar"));
    {
        TarBackend backend(ArchiveFormat::Tar);
        CompressOptions options;
        options.archivePath = archive;
        options.sourceFiles = {sourceRoot};
        bool finished = false;
        bool success = false;
        connect(&backend, &ArchiveBackend::operationFinished, &backend,
                [&finished, &success](bool ok, const QString &) {
                    finished = true;
                    success = ok;
                });
        backend.compress(options);
        QVERIFY(finished);
        QVERIFY(success);
    }

    ArchiveManager &manager = ArchiveManager::instance();
    QSignalSpy openedSpy(&manager, &ArchiveManager::archiveOpened);
    manager.openArchive(archive);
    QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 10000);
    QVERIFY(qobject_cast<LibArchiveBackend *>(manager.backend()) != nullptr);
    QVERIFY(manager.canModifyCurrentArchive());

    // Extract the archive to a preview-like root and edit the member there.
    // 把压缩包解压到类似预览根目录的位置，并在那里编辑成员。
    const QString previewRoot = temp.filePath(QStringLiteral("preview"));
    QSignalSpy extractFinished(&manager, &ArchiveManager::operationFinished);
    ExtractOptions extractOptions;
    extractOptions.destDir = previewRoot;
    extractOptions.overwrite = OverwritePolicy::Always;
    manager.extract(extractOptions);
    QTRY_COMPARE_WITH_TIMEOUT(extractFinished.count(), 1, 15000);
    QVERIFY(extractFinished.first().at(0).toBool());

    const QString localPath = previewRoot + QLatin1Char('/') + memberPath;
    QVERIFY2(QFileInfo::exists(localPath), qPrintable(localPath));
    {
        QFile file(localPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.write("edited") == 6);
    }

    QSignalSpy saveFinished(&manager, &ArchiveManager::operationFinished);
    AddFilesOptions saveOptions;
    saveOptions.archivePath = archive;
    saveOptions.sourceFiles = QStringList{memberPath};
    saveOptions.sourceRoot = previewRoot;
    saveOptions.nameEncoding = QStringLiteral("UTF-8");
    manager.addFiles(saveOptions);
    QTRY_COMPARE_WITH_TIMEOUT(saveFinished.count(), 1, 20000);
    QVERIFY2(saveFinished.first().at(0).toBool(),
             "the write-back must succeed after reopening with the CLI backend");
    QVERIFY(qobject_cast<LibArchiveBackend *>(manager.backend()) == nullptr);

    // The member was replaced, not duplicated. / 成员被替换，而不是重复添加。
    QSignalSpy listSpy(&manager, &ArchiveManager::fileListReady);
    manager.listContents();
    QTRY_COMPARE_WITH_TIMEOUT(listSpy.count(), 1, 10000);
    int fileCount = 0;
    for (const ArchiveEntry &entry : manager.entries()) {
        if (!entry.isDir) {
            ++fileCount;
        }
    }
    QCOMPARE(fileCount, 1);

    const QString verifyRoot = temp.filePath(QStringLiteral("verify"));
    QSignalSpy verifyFinished(&manager, &ArchiveManager::operationFinished);
    ExtractOptions verifyOptions;
    verifyOptions.destDir = verifyRoot;
    verifyOptions.overwrite = OverwritePolicy::Always;
    manager.extract(verifyOptions);
    QTRY_COMPARE_WITH_TIMEOUT(verifyFinished.count(), 1, 15000);
    QVERIFY(verifyFinished.first().at(0).toBool());

    QFile replaced(verifyRoot + QLatin1Char('/') + memberPath);
    QVERIFY(replaced.open(QIODevice::ReadOnly));
    QCOMPARE(replaced.readAll(), QByteArrayLiteral("edited"));

    manager.closeArchive();
    settings.setUseLibArchiveBackend(savedPreference);
#endif
}

void TestManager::backendSelectionHonoursPreference()
{
#ifdef QARCHIVER_HAS_LIBARCHIVE
    QVERIFY(ArchiveBackendFactory::isLibArchiveAvailable());

    AppSettings &settings = AppSettings::instance();
    const bool saved = settings.useLibArchiveBackend();

    settings.setUseLibArchiveBackend(false);
    QVERIFY(!settings.useLibArchiveBackend());
    {
        std::unique_ptr<ArchiveBackend> backend(ArchiveBackendFactory::createForRead(
            ArchiveFormat::Zip, QStringLiteral("archive.zip")));
        QVERIFY(qobject_cast<LibArchiveBackend *>(backend.get()) == nullptr);
        QVERIFY(qobject_cast<ZipBackend *>(backend.get()) != nullptr);
    }
    {
        std::unique_ptr<ArchiveBackend> backend(ArchiveBackendFactory::createForRead(
            ArchiveFormat::TarGz, QStringLiteral("archive.tar.gz")));
        QVERIFY(qobject_cast<LibArchiveBackend *>(backend.get()) == nullptr);
        QVERIFY(qobject_cast<TarBackend *>(backend.get()) != nullptr);
    }

    settings.setUseLibArchiveBackend(true);
    QVERIFY(settings.useLibArchiveBackend());
    {
        std::unique_ptr<ArchiveBackend> backend(ArchiveBackendFactory::createForRead(
            ArchiveFormat::Zip, QStringLiteral("archive.zip")));
        QVERIFY(qobject_cast<LibArchiveBackend *>(backend.get()) != nullptr);
    }
    {
        std::unique_ptr<ArchiveBackend> backend(ArchiveBackendFactory::createForRead(
            ArchiveFormat::TarGz, QStringLiteral("archive.tar.gz")));
        QVERIFY(qobject_cast<LibArchiveBackend *>(backend.get()) != nullptr);
    }

    // Not candidates for the libarchive backend, even when enabled: split
    // volumes have to be resolved by the CLI, and RAR stays on RarBackend.
    {
        std::unique_ptr<ArchiveBackend> backend(ArchiveBackendFactory::createForRead(
            ArchiveFormat::SevenZip, QStringLiteral("archive.7z.001")));
        QVERIFY(qobject_cast<SevenZipBackend *>(backend.get()) != nullptr);
    }
    {
        std::unique_ptr<ArchiveBackend> backend(ArchiveBackendFactory::createForRead(
            ArchiveFormat::Rar5, QStringLiteral("archive.rar")));
        QVERIFY(qobject_cast<LibArchiveBackend *>(backend.get()) == nullptr);
    }

    // The troubleshooting environment variable overrides the preference.
    qputenv("QARCHIVER_NO_LIBARCHIVE", "1");
    QVERIFY(ArchiveBackendFactory::isLibArchiveDisabledByEnvironment());
    {
        std::unique_ptr<ArchiveBackend> backend(ArchiveBackendFactory::createForRead(
            ArchiveFormat::Zip, QStringLiteral("archive.zip")));
        QVERIFY(qobject_cast<LibArchiveBackend *>(backend.get()) == nullptr);
        QVERIFY(qobject_cast<ZipBackend *>(backend.get()) != nullptr);
    }
    qunsetenv("QARCHIVER_NO_LIBARCHIVE");
    QVERIFY(!ArchiveBackendFactory::isLibArchiveDisabledByEnvironment());

    settings.setUseLibArchiveBackend(saved);
#else
    QVERIFY(!ArchiveBackendFactory::isLibArchiveAvailable());
    std::unique_ptr<ArchiveBackend> backend(ArchiveBackendFactory::createForRead(
        ArchiveFormat::Zip, QStringLiteral("archive.zip")));
    QVERIFY(qobject_cast<ZipBackend *>(backend.get()) != nullptr);
#endif
}

QTEST_GUILESS_MAIN(TestManager)

#include "tst_manager.moc"

// The worker thread stops cooperatively (no QThread::terminate()) and
// shutdown() is safe to call more than once.  Declared last: the singleton is
// not usable after shutdown.
// 工作线程协作停止（不使用 QThread::terminate()），shutdown() 可重复调用。
// 声明在最后：关闭后单例不再可用。
void TestManager::workerShutdownIsCooperative()
{
    ArchiveManager &manager = ArchiveManager::instance();
    manager.shutdown();
    manager.shutdown();
    QVERIFY(!manager.backend());
    QVERIFY(!manager.isBusy());
}
