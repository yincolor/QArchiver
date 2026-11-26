// QArchiver / 小Q解压
// Tests for continuous image preview selection and batch extraction.
// 连续看图图片选取与批量解压测试。

#include <QtTest>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "backends/tar/TarBackend.h"
#include "ui/services/PreviewManager.h"
#include "ui/services/PreviewUtils.h"
#include "utils/FileUtils.h"
#include "utils/ProcessRunner.h"

namespace {

ArchiveEntry makeEntry(const QString &path, bool isDir = false,
                       bool isSymLink = false)
{
    ArchiveEntry entry;
    entry.path = path;
    entry.isDir = isDir;
    entry.isSymLink = isSymLink;
    return entry;
}

QSet<QString> toSet(const QStringList &paths)
{
    return QSet<QString>(paths.begin(), paths.end());
}

} // namespace

class TestPreview : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void supportedExtensions();
    void selectsSameDirectoryImages();
    void skipsSymlinksAndSubdirectories();
    void extractsSelectedImages();
    void preparationDialogThreshold();
    void clearArchiveCacheRemovesPreviews();

private:
    QList<ArchiveEntry> listFixture();
    QString m_fixture;
    QList<ArchiveEntry> m_entries;
};

void TestPreview::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("QArchiverTests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_preview"));
    qRegisterMetaType<QList<ArchiveEntry>>("QList<ArchiveEntry>");
    qRegisterMetaType<ArchiveErrorCode>("ArchiveErrorCode");

    if (ProcessRunner::resolveToolPath(QStringLiteral("7z")).isEmpty()) {
        QSKIP("the 7z tool is required for the preview fixture");
    }

    m_fixture = QStringLiteral(QARCHIVER_TEST_DATA_DIR)
        + QStringLiteral("/连续看图测试.tar.gz");
    QVERIFY2(QFileInfo::exists(m_fixture), qPrintable(m_fixture));

    m_entries = listFixture();
    QVERIFY(!m_entries.isEmpty());
}

QList<ArchiveEntry> TestPreview::listFixture()
{
    TarBackend backend(ArchiveFormat::TarGz);
    QSignalSpy listSpy(&backend, &ArchiveBackend::fileListReady);
    QSignalSpy errorSpy(&backend, &ArchiveBackend::errorOccurred);

    backend.openArchive(m_fixture);
    backend.listContents();

    if (!errorSpy.isEmpty() || listSpy.isEmpty()) {
        return {};
    }
    return listSpy.first().at(0).value<QList<ArchiveEntry>>();
}

void TestPreview::supportedExtensions()
{
    for (const QString &suffix : {QStringLiteral(".jpg"),
                                  QStringLiteral(".jpeg"),
                                  QStringLiteral(".png"),
                                  QStringLiteral(".svg"),
                                  QStringLiteral(".webp"),
                                  QStringLiteral(".gif"),
                                  QStringLiteral(".bmp"),
                                  QStringLiteral(".tiff"),
                                  QStringLiteral(".heic"),
                                  QStringLiteral(".avif")}) {
        QVERIFY(PreviewUtils::isSupportedImagePath(QStringLiteral("image") + suffix));
        QVERIFY(PreviewUtils::isSupportedImagePath(
            QStringLiteral("IMAGE") + suffix.toUpper()));
    }

    QVERIFY(!PreviewUtils::isSupportedImagePath(QStringLiteral("image.txt")));
    QVERIFY(!PreviewUtils::isSupportedImagePath(QStringLiteral("imagepng")));
}

void TestPreview::selectsSameDirectoryImages()
{
    const QStringList root = PreviewUtils::imageSiblings(
        m_entries, QStringLiteral("连续看图测试/1.png"));
    QCOMPARE(root.size(), 5);
    QCOMPARE(toSet(root), (QSet<QString>{
        QStringLiteral("连续看图测试/1.png"),
        QStringLiteral("连续看图测试/2.png"),
        QStringLiteral("连续看图测试/3.png"),
        QStringLiteral("连续看图测试/4.jpg"),
        QStringLiteral("连续看图测试/5.webp")
    }));

    const QStringList dir1 = PreviewUtils::imageSiblings(
        m_entries, QStringLiteral("连续看图测试/dir1/点赞.svg"));
    QCOMPARE(dir1.size(), 3);
    QCOMPARE(toSet(dir1), (QSet<QString>{
        QStringLiteral("连续看图测试/dir1/表情、微笑.svg"),
        QStringLiteral("连续看图测试/dir1/点赞.svg"),
        QStringLiteral("连续看图测试/dir1/火箭.svg")
    }));

    QCOMPARE(PreviewUtils::parentDirectory(QStringLiteral("a/b.png")),
             QStringLiteral("a"));
    QVERIFY(PreviewUtils::parentDirectory(QStringLiteral("a.png")).isEmpty());
}

void TestPreview::skipsSymlinksAndSubdirectories()
{
    const QList<ArchiveEntry> entries = {
        makeEntry(QStringLiteral("dir/a.PNG")),
        makeEntry(QStringLiteral("dir/b.JPG")),
        makeEntry(QStringLiteral("dir/c.WebP")),
        makeEntry(QStringLiteral("dir/d.GIF")),
        makeEntry(QStringLiteral("dir/e.BMP")),
        makeEntry(QStringLiteral("dir/f.SVG")),
        makeEntry(QStringLiteral("dir/not-image.txt")),
        makeEntry(QStringLiteral("dir/sub/inside.png")),
        makeEntry(QStringLiteral("dir/link.png"), false, true),
        makeEntry(QStringLiteral("dir/folder.png"), true, false),
        makeEntry(QStringLiteral("other/outside.png"))
    };

    const QStringList paths = PreviewUtils::imageSiblings(
        entries, QStringLiteral("dir/a.PNG"));
    QCOMPARE(paths.size(), 6);
    QCOMPARE(toSet(paths), (QSet<QString>{
        QStringLiteral("dir/a.PNG"),
        QStringLiteral("dir/b.JPG"),
        QStringLiteral("dir/c.WebP"),
        QStringLiteral("dir/d.GIF"),
        QStringLiteral("dir/e.BMP"),
        QStringLiteral("dir/f.SVG")
    }));
    QVERIFY(!paths.contains(QStringLiteral("dir/link.png")));
    QVERIFY(!paths.contains(QStringLiteral("dir/folder.png")));
    QVERIFY(!paths.contains(QStringLiteral("dir/sub/inside.png")));
    QVERIFY(!paths.contains(QStringLiteral("other/outside.png")));
}

void TestPreview::extractsSelectedImages()
{
    const QStringList images = PreviewUtils::imageSiblings(
        m_entries, QStringLiteral("连续看图测试/1.png"));
    QCOMPARE(images.size(), 5);

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    TarBackend backend(ArchiveFormat::TarGz);
    QSignalSpy finishSpy(&backend, &ArchiveBackend::operationFinished);
    backend.openArchive(m_fixture);

    ExtractOptions options;
    options.destDir = temp.path();
    options.selectedFiles = images;
    options.overwrite = OverwritePolicy::Always;
    options.preservePaths = true;
    backend.extract(options);

    QCOMPARE(finishSpy.count(), 1);
    QVERIFY2(finishSpy.first().at(0).toBool(),
             qPrintable(finishSpy.first().at(1).toString()));

    for (const QString &path : images) {
        const QString normalized = FileUtils::normalizeEntryPath(path);
        const QString extracted = QDir(temp.path()).filePath(normalized);
        QVERIFY2(QFileInfo::exists(extracted), qPrintable(extracted));
    }

    QVERIFY(!QFileInfo::exists(QDir(temp.path()).filePath(
        QStringLiteral("连续看图测试/dir1/点赞.svg"))));
    backend.closeArchive();
}

// The preparation dialog appears strictly above the threshold; 0 means any
// non-cached batch, and fully cached batches never show it.
// 准备对话框仅在严格超过阈值时出现；0 表示任意未完全缓存批次，
// 已全部缓存时从不显示。
// Closing an archive must drop its preview directory instead of leaving it in
// the cache until the process exits.
// 关闭压缩包时必须删除其预览目录，而不是留到进程退出。
void TestPreview::clearArchiveCacheRemovesPreviews()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString archive = temp.filePath(QStringLiteral("sample.zip"));
    {
        QFile file(archive);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("not a real archive"), qint64(18));
    }

    // Reproduce the per-archive directory identity used by PreviewManager.
    // 复现 PreviewManager 使用的按归档目录标识。
    const QFileInfo info(archive);
    const QString identity =
        archive + QLatin1Char('|') + QString::number(info.size())
        + QLatin1Char('|')
        + QString::number(info.lastModified().toMSecsSinceEpoch());
    const QString hash = QCryptographicHash::hash(
        identity.toUtf8(), QCryptographicHash::Md5).toHex();
    const QString previewRoot = QDir(
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("preview"));
    const QString archiveDir =
        QDir(QDir(previewRoot).filePath(
                 QString::number(QCoreApplication::applicationPid())))
            .filePath(hash);
    QVERIFY(QDir().mkpath(archiveDir));
    {
        QFile marker(QDir(archiveDir).filePath(QStringLiteral("marker")));
        QVERIFY(marker.open(QIODevice::WriteOnly));
    }

    PreviewManager::instance().clearArchiveCache(archive);
    QVERIFY(!QDir(archiveDir).exists());

    // An empty path is a no-op and must not throw or delete the process dir.
    PreviewManager::instance().clearArchiveCache(QString());
}

void TestPreview::preparationDialogThreshold()
{
    QVERIFY(!PreviewUtils::shouldShowPreparationDialog(200, 200, false));
    QVERIFY(PreviewUtils::shouldShowPreparationDialog(201, 200, false));
    QVERIFY(PreviewUtils::shouldShowPreparationDialog(1, 0, false));
    QVERIFY(!PreviewUtils::shouldShowPreparationDialog(201, 200, true));
    QVERIFY(!PreviewUtils::shouldShowPreparationDialog(0, 0, false));
}

QTEST_GUILESS_MAIN(TestPreview)

#include "tst_preview.moc"
