// QArchiver / 小Q解压
// Unit tests for filesystem, encoding and 7-Zip list parsing helpers.
// 文件系统、编码与 7-Zip 列表解析辅助函数的单元测试。

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <limits>

#ifndef Q_OS_WIN
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "backends/sevenzip/SevenZipListParser.h"
#include "common/FormatRegistry.h"
#include "utils/EncodingDetector.h"
#include "utils/FileUtils.h"
#include "utils/LogLineBuffer.h"
#include "utils/MimeAppsList.h"

class TestUtils : public QObject {
    Q_OBJECT

private slots:
    void normalizeEntryPath_data();
    void normalizeEntryPath();
    void traversalDetection();
    void destinationSymlinkEscape();
    void sevenZipListParser();
    void checkedSizeAccumulation();
    void safeSymlinkTargets_data();
    void safeSymlinkTargets();
    void unsafeExtractedSymlinks();
    void rawTemporaryDirectoryCleanup();
    void parserPreservesSpacesAndLinks();
    void formatRegistrySuffixes();
    void formatRegistryFiltersAreConsistent();
    void legacyEncodingDetection();
    void logLineBufferSplitsLines();
    void archiveStripExtension();
    void mimeAppsListPreservesStandardFormat();
    void mimeAppsListCreatesMissingSections();
};

// Data rows for archive path normalization.
// 压缩包路径规范化的数据行。
void TestUtils::normalizeEntryPath_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("backslashes") << QStringLiteral("dir\\sub\\file.txt")
                                 << QStringLiteral("dir/sub/file.txt");
    QTest::newRow("drive letter") << QStringLiteral("C:\\dir\\file.txt")
                                  << QStringLiteral("dir/file.txt");
    QTest::newRow("leading slash") << QStringLiteral("/dir/file.txt")
                                   << QStringLiteral("dir/file.txt");
    QTest::newRow("dot dot") << QStringLiteral("dir/../file.txt")
                              << QStringLiteral("file.txt");
}

// Normalize separators, dot segments and drive prefixes.
// 规范化分隔符、点路径与盘符前缀。
void TestUtils::normalizeEntryPath()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(FileUtils::normalizeEntryPath(input), expected);
}

// Reject Zip Slip paths without false positives on ".." in names.
// 拒绝 Zip Slip 路径，同时避免把名称中的 ".." 误判。
void TestUtils::traversalDetection()
{
    QVERIFY(FileUtils::isPathTraversal(QStringLiteral("../etc/passwd"),
                                       QStringLiteral("/tmp/dest")));
    QVERIFY(FileUtils::isPathTraversal(QStringLiteral("a/../../etc/passwd"),
                                       QStringLiteral("/tmp/dest")));
    QVERIFY(!FileUtils::isPathTraversal(QStringLiteral("foo..bar"),
                                        QStringLiteral("/tmp/dest")));
    QVERIFY(!FileUtils::isPathTraversal(QStringLiteral("safe/file.txt"),
                                        QStringLiteral("/tmp/dest")));
}

// A pre-existing symlink inside the destination must not become a write-through
// channel to a directory outside it (the temporary tree is prepared before the
// targets exist, so the old "only check existing targets" test missed this).
// 目标目录中预先存在的符号链接不能成为写到目录外的通道（临时目录在目标存在
// 之前就已准备好，旧的“只检查已存在目标”逻辑漏掉了这种情况）。
void TestUtils::destinationSymlinkEscape()
{
#ifdef Q_OS_WIN
    QSKIP("QFile::link creates .lnk shortcuts on Windows, not Unix symlinks");
#endif
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString dest = temp.filePath(QStringLiteral("dest"));
    const QString outside = temp.filePath(QStringLiteral("outside"));
    const QString staging = QDir(dest).filePath(QStringLiteral(".tmp_extract_test"));
    QVERIFY(QDir().mkpath(QDir(staging).filePath(QStringLiteral("link"))));
    QVERIFY(QDir().mkpath(outside));
    QVERIFY(QFile::link(outside, QDir(dest).filePath(QStringLiteral("link"))));

    const QString escaped = QDir(outside).filePath(QStringLiteral("file.txt"));
    QVERIFY(FileUtils::isPathTraversal(QStringLiteral("link/file.txt"), dest));

    {
        QFile file(QDir(staging).filePath(QStringLiteral("link/file.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("escaped"), qint64(7));
    }
    {
        QFile file(QDir(staging).filePath(QStringLiteral("safe.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("safe"), qint64(4));
    }

    const int failed = FileUtils::moveExtractedContents(
        staging, dest, OverwritePolicy::Always, true);
    QVERIFY(failed > 0); // the escaping entries are refused / 逃逸的条目被拒绝
    QVERIFY(!QFileInfo::exists(escaped));
    QVERIFY(QFileInfo::exists(QDir(dest).filePath(QStringLiteral("safe.txt"))));
}

// Parse directory, file and symlink blocks from 7z -slt output.
// 解析 7z -slt 输出中的目录、文件与符号链接块。
void TestUtils::sevenZipListParser()
{
    const QString output = QStringLiteral(
        "Path = archive.tar\n"
        "Type = tar\n"
        "----------\n"
        "Path = my dir\n"
        "Folder = +\n"
        "Size = 0\n"
        "Mode = drwxr-xr-x\n"
        "\n"
        "Path = my dir/my file.txt\n"
        "Folder = -\n"
        "Size = 123\n"
        "Mode = -rw-r--r--\n"
        "\n"
        "Path = my dir/link.txt\n"
        "Folder = -\n"
        "Size = 5\n"
        "Mode = lrwxrwxrwx\n"
        "Symbolic Link = my file.txt\n");

    const QList<ArchiveEntry> entries = SevenZipListParser::parse(output);
    QCOMPARE(entries.size(), 3);

    QVERIFY(entries.at(0).isDir);
    QCOMPARE(entries.at(0).path, QStringLiteral("my dir"));

    QVERIFY(!entries.at(1).isDir);
    QVERIFY(!entries.at(1).isSymLink);
    QCOMPARE(entries.at(1).size, qint64(123));
    QCOMPARE(entries.at(1).path, QStringLiteral("my dir/my file.txt"));

    QVERIFY(entries.at(2).isSymLink);
    QCOMPARE(entries.at(2).path, QStringLiteral("my dir/link.txt"));
}

// Size sums must reject overflow and negative sizes instead of wrapping.
// 大小累加必须拒绝溢出与负值，而不是回绕。
void TestUtils::checkedSizeAccumulation()
{
    ArchiveEntry maximum;
    maximum.path = QStringLiteral("maximum.bin");
    maximum.size = std::numeric_limits<qint64>::max();
    ArchiveEntry one;
    one.path = QStringLiteral("one.bin");
    one.size = 1;

    QCOMPARE(FileUtils::totalUnpackedSize({maximum, one}),
             std::numeric_limits<qint64>::max());

    qint64 checked = 0;
    QVERIFY(!FileUtils::totalUnpackedSizeChecked({maximum, one}, &checked));

    checked = 0;
    QVERIFY(FileUtils::totalUnpackedSizeChecked({maximum}, &checked));
    QCOMPARE(checked, std::numeric_limits<qint64>::max());
    QVERIFY(!FileUtils::addSizeChecked(checked, 1));

    ArchiveEntry negative;
    negative.path = QStringLiteral("negative.bin");
    negative.size = -1;
    checked = 0;
    QVERIFY(!FileUtils::totalUnpackedSizeChecked({negative}, &checked));
    QVERIFY(!FileUtils::addSizeChecked(checked, -1));
}

void TestUtils::safeSymlinkTargets_data()
{
    QTest::addColumn<QString>("entry");
    QTest::addColumn<QString>("target");
    QTest::addColumn<bool>("safe");

    QTest::newRow("sibling") << QStringLiteral("dir/link")
                             << QStringLiteral("../file.txt") << true;
    QTest::newRow("nested") << QStringLiteral("dir/link")
                            << QStringLiteral("sub/file.txt") << true;
    QTest::newRow("normalize") << QStringLiteral("dir/link")
                               << QStringLiteral("sub/../file.txt") << true;
    QTest::newRow("escape-root") << QStringLiteral("link")
                                 << QStringLiteral("../file.txt") << false;
    QTest::newRow("escape") << QStringLiteral("dir/link")
                            << QStringLiteral("../../file.txt") << false;
    QTest::newRow("absolute") << QStringLiteral("dir/link")
                              << QStringLiteral("/etc/passwd") << false;
    QTest::newRow("empty") << QStringLiteral("dir/link") << QString() << false;
}

void TestUtils::safeSymlinkTargets()
{
    QFETCH(QString, entry);
    QFETCH(QString, target);
    QFETCH(bool, safe);

    QCOMPARE(FileUtils::isSafeSymlinkTarget(entry, target), safe);
}

void TestUtils::unsafeExtractedSymlinks()
{
#ifdef Q_OS_WIN
    QSKIP("QFile::link creates .lnk shortcuts on Windows, not Unix symlinks");
#endif
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QVERIFY(QDir(temp.path()).mkpath(QStringLiteral("sub")));

    QFile inside(QDir(temp.path()).filePath(QStringLiteral("inside.txt")));
    QVERIFY(inside.open(QIODevice::WriteOnly));
    inside.write("inside");
    inside.close();

    const QString safeLink = QDir(temp.path()).filePath(QStringLiteral("sub/safe"));
    const QString unsafeLink = QDir(temp.path()).filePath(QStringLiteral("sub/unsafe"));
    QVERIFY(QFile::link(QStringLiteral("../inside.txt"), safeLink));
    QVERIFY(QFile::link(QStringLiteral("../../outside.txt"), unsafeLink));

    const QStringList unsafe = FileUtils::unsafeExtractedSymlinks(temp.path());
    QCOMPARE(unsafe.size(), 1);
    QCOMPARE(unsafe.first(), QStringLiteral("sub/unsafe"));
}

// The temporary extraction tree can contain legacy-codepage names that QDir
// cannot represent; the dedicated remover must still delete the whole tree
// (otherwise cancelled/failed extractions leave ".tmp_extract_*" behind).
// 解压临时目录可能包含 QDir 无法表示的旧代码页文件名；专用清理函数必须仍能
// 删除整棵树（否则取消/失败的解压会残留 ".tmp_extract_*"）。
void TestUtils::rawTemporaryDirectoryCleanup()
{
#ifdef Q_OS_WIN
    QSKIP("raw legacy-codepage names are only produced on POSIX");
#else
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString tree = temp.filePath(QStringLiteral("tree"));
    QVERIFY(QDir().mkpath(tree + QStringLiteral("/sub")));

    const QByteArray encoded =
        EncodingDetector::encode(QStringLiteral("中文.txt"), QStringLiteral("GBK"));
    QVERIFY(!encoded.isEmpty());
    const QByteArray rawPath =
        QFile::encodeName(tree + QStringLiteral("/sub")) + '/' + encoded;
    const int fd = ::open(rawPath.constData(), O_WRONLY | O_CREAT, 0600);
    QVERIFY(fd >= 0);
    ::close(fd);
    QCOMPARE(::access(rawPath.constData(), F_OK), 0);

    QVERIFY(FileUtils::removeTemporaryDirectory(tree));
    QVERIFY(!QDir(tree).exists());
#endif
}

// Streaming parser keeps member-name spaces and captures symlink/hardlink data.
// 流式解析器保留成员名空格，并捕获符号链接/硬链接数据。
void TestUtils::parserPreservesSpacesAndLinks()
{
    SevenZipListParser parser;
    parser.feedLine(QStringLiteral("Path =  leading.txt "));
    parser.feedLine(QStringLiteral("Folder = -"));
    parser.feedLine(QStringLiteral("Size = 3"));
    parser.feedLine(QStringLiteral("Hard Link = target.txt"));
    parser.feedLine(QStringLiteral("Path = dir/link"));
    parser.feedLine(QStringLiteral("Folder = -"));
    parser.feedLine(QStringLiteral("Symbolic Link = ../target.txt"));

    const QList<ArchiveEntry> entries = parser.takeEntries();
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).path, QStringLiteral(" leading.txt "));
    QVERIFY(entries.at(0).isHardLink);
    QCOMPARE(entries.at(0).hardLinkTarget, QStringLiteral("target.txt"));
    QVERIFY(entries.at(1).isSymLink);
    QCOMPARE(entries.at(1).linkTarget, QStringLiteral("../target.txt"));
}

// Suffix detection must not confuse ordinary .r?? files with RAR volumes.
// 后缀识别不能把普通 .r?? 文件误判为 RAR 分卷。
void TestUtils::formatRegistrySuffixes()
{
    QCOMPARE(FormatRegistry::formatFromSuffix(QStringLiteral("notes.rtf")),
             ArchiveFormat::Unknown);
    QCOMPARE(FormatRegistry::formatFromSuffix(QStringLiteral("package.rpm")),
             ArchiveFormat::Unknown);
    QCOMPARE(FormatRegistry::formatFromSuffix(QStringLiteral("readme.rst")),
             ArchiveFormat::Unknown);
    QVERIFY(!FormatRegistry::isArchivePath(QStringLiteral("notes.rtf")));

    QCOMPARE(FormatRegistry::formatFromSuffix(QStringLiteral("data.r00")),
             ArchiveFormat::Rar4);
    QCOMPARE(FormatRegistry::formatFromSuffix(QStringLiteral("data.r01")),
             ArchiveFormat::Rar4);
    QCOMPARE(FormatRegistry::formatFromSuffix(QStringLiteral("data.rar")),
             ArchiveFormat::Rar5);
    QCOMPARE(FormatRegistry::formatFromSuffix(QStringLiteral("data.part2.rar")),
             ArchiveFormat::Rar5);
    QCOMPARE(FormatRegistry::formatFromSuffix(QStringLiteral("data.zip.001")),
             ArchiveFormat::Zip);
    QCOMPARE(FormatRegistry::formatFromSuffix(QStringLiteral("data.7z.001")),
             ArchiveFormat::SevenZip);
    QCOMPARE(FormatRegistry::formatFromSuffix(QStringLiteral("data.tar.zst")),
             ArchiveFormat::TarZst);
    QCOMPARE(FormatRegistry::formatFromSuffix(QStringLiteral("data.tzst")),
             ArchiveFormat::TarZst);
    QCOMPARE(FormatRegistry::formatFromSuffix(QStringLiteral("data.zst")),
             ArchiveFormat::Zstd);

    QVERIFY(FormatRegistry::isArchivePath(QStringLiteral("data.tar.zst")));
    QVERIFY(FormatRegistry::isArchivePath(QStringLiteral("data.zst")));

    QVERIFY(FormatRegistry::isSplitVolume(QStringLiteral("data.r00")));
    QVERIFY(FormatRegistry::isSplitVolume(QStringLiteral("data.part1.rar")));
    QVERIFY(FormatRegistry::isSplitVolume(QStringLiteral("data.zip.001")));
    QVERIFY(FormatRegistry::isArchivePath(QStringLiteral("data.r00")));
    QVERIFY(FormatRegistry::isArchivePath(QStringLiteral("data.part1.rar")));
}

// One-click extraction uses the archive base name, not a volume suffix.
// 一键解压使用压缩包基名，而不是分卷后缀。
void TestUtils::archiveStripExtension()
{
    QCOMPARE(FormatRegistry::stripArchiveExtension(
                 QStringLiteral("data.part1.rar")),
             QStringLiteral("data"));
    QCOMPARE(FormatRegistry::stripArchiveExtension(
                 QStringLiteral("data.part12.rar")),
             QStringLiteral("data"));
    QCOMPARE(FormatRegistry::stripArchiveExtension(
                 QStringLiteral("data.7z.001")),
             QStringLiteral("data"));
    QCOMPARE(FormatRegistry::stripArchiveExtension(
                 QStringLiteral("data.r00")),
             QStringLiteral("data"));
    QCOMPARE(FormatRegistry::stripArchiveExtension(
                 QStringLiteral("data.tar.gz")),
             QStringLiteral("data"));
    QCOMPARE(FormatRegistry::stripArchiveExtension(
                 QStringLiteral("plain.txt")),
             QStringLiteral("plain.txt"));
}

// Standard mimeapps.list syntax must survive a read/modify/write cycle.
// 标准 mimeapps.list 语法在读写后必须保持不变。
void TestUtils::mimeAppsListPreservesStandardFormat()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("mimeapps.list"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArrayLiteral(
        "# user overrides\n"
        "[Default Applications]\n"
        "application/zip=file-roller.desktop;\n"
        "text/plain=org.gnome.TextEditor.desktop\n"
        "\n"
        "[Added Associations]\n"
        "application/zip=file-roller.desktop;\n"
        "\n"
        "[Removed Associations]\n"
        "image/png=example.desktop;\n"));
    file.close();

    QString error;
    QVERIFY2(MimeAppsList::setAssociation(
                 path, QStringLiteral("application/zip"),
                 QStringLiteral("yincolor.qarchiver.desktop"), true, &error),
             qPrintable(error));

    QCOMPARE(MimeAppsList::desktopIds(
                 path, QStringLiteral("Default Applications"),
                 QStringLiteral("application/zip")),
             (QStringList{QStringLiteral("yincolor.qarchiver.desktop"),
                          QStringLiteral("file-roller.desktop")}));
    QCOMPARE(MimeAppsList::desktopIds(
                 path, QStringLiteral("Added Associations"),
                 QStringLiteral("application/zip")),
             (QStringList{QStringLiteral("yincolor.qarchiver.desktop"),
                          QStringLiteral("file-roller.desktop")}));
    QVERIFY(MimeAppsList::hasDesktopId(
        path, QStringLiteral("Default Applications"),
        QStringLiteral("application/zip"), QStringLiteral("yincolor.qarchiver.desktop")));

    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(content.contains(QStringLiteral("[Default Applications]")));
    QVERIFY(content.contains(QStringLiteral("[Added Associations]")));
    QVERIFY(content.contains(QStringLiteral("[Removed Associations]")));
    QVERIFY(content.contains(QStringLiteral("image/png=example.desktop;")));
    QVERIFY(content.contains(QStringLiteral(
        "application/zip=yincolor.qarchiver.desktop;file-roller.desktop;")));
    QVERIFY(!content.contains(QStringLiteral("%20")));
    QVERIFY(!content.contains(QStringLiteral("application\\zip")));

    QVERIFY2(MimeAppsList::setAssociation(
                 path, QStringLiteral("application/zip"),
                 QStringLiteral("yincolor.qarchiver.desktop"), false, &error),
             qPrintable(error));
    QCOMPARE(MimeAppsList::desktopIds(
                 path, QStringLiteral("Default Applications"),
                 QStringLiteral("application/zip")),
             QStringList{QStringLiteral("file-roller.desktop")});
}

// Missing groups are appended using the standard freedesktop headers.
// 缺失的节必须使用标准 freedesktop 标题追加。
void TestUtils::mimeAppsListCreatesMissingSections()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("mimeapps.list"));

    QString error;
    QVERIFY2(MimeAppsList::setAssociation(
                 path, QStringLiteral("application/x-7z-compressed"),
                 QStringLiteral("yincolor.qarchiver.desktop"), true, &error),
             qPrintable(error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(file.readAll());
    QVERIFY(content.contains(QStringLiteral("[Default Applications]")));
    QVERIFY(content.contains(QStringLiteral("[Added Associations]")));
    QVERIFY(content.contains(QStringLiteral(
        "application/x-7z-compressed=yincolor.qarchiver.desktop;")));

    // Removing from a missing file must not create an empty mimeapps.list.
    // 从不存在的文件移除关联时，不应创建空的 mimeapps.list。
    const QString missing =
        QDir(temp.path()).filePath(QStringLiteral("missing.list"));
    QVERIFY(MimeAppsList::setAssociation(
        missing, QStringLiteral("application/zip"),
        QStringLiteral("yincolor.qarchiver.desktop"), false, &error));
    QVERIFY(!QFileInfo::exists(missing));
}

// Legacy filename detection must not depend on the process locale for
// unambiguous byte strings, and hinted decoding must round-trip.
// 对无歧义的字节串，旧式文件名检测不能依赖进程 locale；
// 带提示的解码必须可逆。
// The Open-dialog patterns and the file-association suffixes are generated
// from the same registry, so a new format cannot be added to one list only.
// 打开对话框模式与文件关联后缀来自同一注册表，新增格式时不会只更新一份列表。
void TestUtils::formatRegistryFiltersAreConsistent()
{
    const QStringList suffixes = FormatRegistry::associationSuffixes();
    QVERIFY(!suffixes.isEmpty());
    for (const QString &suffix : suffixes) {
        QVERIFY2(suffix.startsWith(QLatin1Char('.')), qPrintable(suffix));
        QVERIFY2(FormatRegistry::formatFromSuffix(
                     QStringLiteral("archive") + suffix)
                     != ArchiveFormat::Unknown,
                 qPrintable(suffix));
    }

    const QStringList patterns = FormatRegistry::openGlobPatterns();
    for (const QString &suffix : suffixes) {
        QVERIFY2(patterns.contains(QStringLiteral("*") + suffix),
                 qPrintable(suffix));
    }

    // Split volumes must stay selectable; only their first volume has a
    // magic signature.
    // 分卷必须仍可在对话框中选中；只有首卷带魔数。
    QVERIFY(patterns.contains(QStringLiteral("*.7z.*")));
    QVERIFY(patterns.contains(QStringLiteral("*.zip.*")));
    QVERIFY(patterns.contains(QStringLiteral("*.part*.rar")));
    QVERIFY(patterns.contains(QStringLiteral("*.r??")));
}

void TestUtils::legacyEncodingDetection()
{
    // "测试文本㐀_GBK.txt" in GB18030.  "㐀" (U+3400) is stored as the
    // four-byte sequence 81 39 ee 39, which is invalid UTF-8, Big5 and
    // Shift-JIS, so detection is unambiguous on every locale.
    // GB18030 的“测试文本㐀_GBK.txt”。“㐀”（U+3400）存储为四字节序列
    // 81 39 ee 39，该序列在 UTF-8、Big5、Shift-JIS 下均非法，
    // 因此在任何 locale 下检测都无歧义。
    const QByteArray gbkName =
        QByteArray::fromHex("b2e2cad4cec4b1be8139ee395f47424b2e747874");
    QCOMPARE(EncodingDetector::detect(gbkName), QStringLiteral("GB18030"));
    QCOMPARE(EncodingDetector::decode(gbkName), QStringLiteral("测试文本㐀_GBK.txt"));
    QCOMPARE(EncodingDetector::decode(gbkName, QStringLiteral("GBK")),
             QStringLiteral("测试文本㐀_GBK.txt"));

    // Big5 overlaps with GB18030, so an explicit hint is what makes it correct.
    // Big5 与 GB18030 重叠，因此只有显式提示才能正确解码。
    const QByteArray big5Name = QByteArray::fromHex("a4a4a4e52e747874"); // 中文.txt
    QCOMPARE(EncodingDetector::decode(big5Name, QStringLiteral("Big5")),
             QStringLiteral("中文.txt"));

    // UTF-8 must stay untouched.
    // UTF-8 必须保持不变。
    const QByteArray utf8Name = QStringLiteral("测试文本.txt").toUtf8();
    QCOMPARE(EncodingDetector::detect(utf8Name), QStringLiteral("UTF-8"));
    QCOMPARE(EncodingDetector::decode(utf8Name), QStringLiteral("测试文本.txt"));

    // GB18030 and Big5 share most of their byte ranges, so "first decodable
    // wins" used to depend on the locale.  The plausibility ranking has to
    // tell them apart on every locale.
    // GB18030 与 Big5 的字节范围大部分重叠，过去“谁先解码成功就算谁”导致结果
    // 依赖 locale。可信度排序必须在任何 locale 下都能区分两者。
    const QByteArray gbkZhongWen = QByteArray::fromHex("d6d0cec4"); // 中文
    QCOMPARE(EncodingDetector::detect(gbkZhongWen), QStringLiteral("GB18030"));
    QCOMPARE(EncodingDetector::decode(gbkZhongWen), QStringLiteral("中文"));

    const QByteArray big5ZhongWen = QByteArray::fromHex("a4a4a4e5"); // 中文
    QCOMPARE(EncodingDetector::detect(big5ZhongWen), QStringLiteral("Big5"));
    QCOMPARE(EncodingDetector::decode(big5ZhongWen), QStringLiteral("中文"));

    const QByteArray big5Name2 = QByteArray::fromHex("b4fab8d5a4e5a5f3"); // 測試文件
    QCOMPARE(EncodingDetector::detect(big5Name2), QStringLiteral("Big5"));
    QCOMPARE(EncodingDetector::decode(big5Name2), QStringLiteral("測試文件"));

    const QByteArray big5Name3 =
        QByteArray::fromHex("b6b5a5d8b669abd7b3f8a769c0c9aed7badeb27a"); // 項目進度報告檔案管理
    QCOMPARE(EncodingDetector::detect(big5Name3), QStringLiteral("Big5"));
    QCOMPARE(EncodingDetector::decode(big5Name3), QStringLiteral("項目進度報告檔案管理"));

    // Simplified text stays GB18030 even though part of it is valid Big5.
    // 简体文本即使部分字节合法于 Big5，也必须仍判为 GB18030。
    const QByteArray gbkName2 = QByteArray::fromHex("b2e2cad4cec4b1becec4bcfe"); // 测试文本文件
    QCOMPARE(EncodingDetector::detect(gbkName2), QStringLiteral("GB18030"));

    // Japanese kana identify Shift-JIS regardless of the locale.
    // 日文假名在任何 locale 下都能识别出 Shift-JIS。
    const QByteArray sjisName =
        QByteArray::fromHex("836583588367837483408343838b"); // テストファイル
    QCOMPARE(EncodingDetector::detect(sjisName), QStringLiteral("Shift-JIS"));
    QCOMPARE(EncodingDetector::decode(sjisName), QStringLiteral("テストファイル"));

    // Code pages 7-Zip accepts through -scs<id> (0 = no mapping).
    // 7-Zip 通过 -scs<id> 接受的代码页（0 表示无映射）。
    QCOMPARE(EncodingDetector::codePageId(QStringLiteral("GB18030")), 936);
    QCOMPARE(EncodingDetector::codePageId(QStringLiteral("GBK")), 936);
    QCOMPARE(EncodingDetector::codePageId(QStringLiteral("Big5")), 950);
    QCOMPARE(EncodingDetector::codePageId(QStringLiteral("Shift-JIS")), 932);
    QCOMPARE(EncodingDetector::codePageId(QStringLiteral("UTF-8")), 0);
}

void TestUtils::logLineBufferSplitsLines()
{
    LogLineBuffer buffer;

    // Plain LF terminated lines; the unterminated tail stays buffered.
    // 普通 LF 结尾的行；未结束的残余内容留在缓冲区。
    QCOMPARE(buffer.append(QByteArrayLiteral("alpha\nbeta\ngamma")),
             QStringList({"alpha", "beta"}));
    QVERIFY(!buffer.isEmpty());
    QCOMPARE(buffer.flush(), QStringList({"gamma"}));
    QVERIFY(buffer.isEmpty());

    // 7-Zip rewrites progress with a bare CR; CRLF counts as one terminator, so
    // a rewritten progress line must not glue itself onto the next line.
    // 7-Zip 用单独的 CR 重写进度；CRLF 只算一个行结束符，
    // 因此被重写的进度行不会和下一行粘在一起。
    buffer.clear();
    QCOMPARE(buffer.append(QByteArrayLiteral("10%\r20%\r\n30%\r")),
             QStringList({"10%", "20%", "30%"}));
    QVERIFY(buffer.isEmpty());

    // Blank / whitespace-only lines would only scroll the log.
    // 空白行只会让日志滚动，不应追加。
    buffer.clear();
    QVERIFY(buffer.append(QByteArrayLiteral("\n \r\n\t\n")).isEmpty());
    QVERIFY(buffer.isEmpty());

    // A chunk boundary in the middle of a line must not emit half a line.
    // 数据块在行中间边界时不能输出半行。
    buffer.clear();
    QVERIFY(buffer.append(QByteArrayLiteral("part")).isEmpty());
    QCOMPARE(buffer.append(QByteArrayLiteral("ial\n")), QStringList({"partial"}));

    // A multi-byte UTF-8 character split across chunks is decoded only once the
    // line is complete.
    // 跨数据块的多字节 UTF-8 字符只在该行完成后才解码。
    buffer.clear();
    const QByteArray utf8Line = QStringLiteral("测试\n").toUtf8();
    QVERIFY(buffer.append(utf8Line.left(4)).isEmpty());
    QCOMPARE(buffer.append(utf8Line.mid(4)), QStringList({QStringLiteral("测试")}));

    // A whitespace-only remainder is dropped on flush as well.
    // 残余内容仅为空白时，flush 也会将其丢弃。
    buffer.clear();
    QVERIFY(buffer.append(QByteArrayLiteral("   ")).isEmpty());
    QVERIFY(buffer.flush().isEmpty());
}

QTEST_GUILESS_MAIN(TestUtils)

#include "tst_utils.moc"
