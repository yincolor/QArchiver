// QArchiver / 小Q解压
// Regression tests for the location bar state after a write-through reopen.
// 只读后端写回退（重新打开同一压缩包）后路径框状态的回归测试。
//
// Regression: saving a file back into a zip that is served by the read-only
// libarchive backend reopens the archive with the CLI backend.  That reopen
// emits archiveOpening() but (by design) no archiveOpened(), so every UI state
// that only archiveOpened() used to refresh stayed stale - most visibly the
// location bar kept showing "Opening <archive>..." even though the archive was
// open and the save had completed.
// 回归问题：把文件保存回由只读 libarchive 后端提供的 zip 时，压缩包会用命令行
// 后端重新打开。该重开只发出 archiveOpening()，按设计不再发出 archiveOpened()，
// 因此只由 archiveOpened() 刷新的界面状态全部过期——最明显的是路径框一直显示
// “正在打开 <压缩包>...”，即使压缩包已打开且保存已经完成。

#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "backends/sevenzip/ZipBackend.h"
#ifdef QARCHIVER_HAS_LIBARCHIVE
#include "backends/libarchive/LibArchiveBackend.h"
#endif
#include "core/AppSettings.h"
#include "core/ArchiveBackendFactory.h"
#include "core/ArchiveManager.h"
#include "ui/MainWindow.h"
#include "ui/models/FileListModel.h"
#include "ui/widgets/BreadcrumbPathBar.h"

namespace {

bool writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(data) == data.size();
}

// The location bar of whichever layout is active. / 当前布局的路径框。
BreadcrumbPathBar *locationBar(MainWindow &window)
{
    const auto bars = window.findChildren<BreadcrumbPathBar *>();
    for (BreadcrumbPathBar *bar : bars) {
        if (bar->isVisible()) {
            return bar;
        }
    }
    return bars.isEmpty() ? nullptr : bars.first();
}

// Placeholder mode shows a "BreadcrumbPlaceholder" label instead of segments.
// 占位模式显示 "BreadcrumbPlaceholder" 标签而不是路径层级。
QString placeholderText(BreadcrumbPathBar *bar)
{
    if (!bar) {
        return QString();
    }
    const auto *label = bar->findChild<QLabel *>(
        QStringLiteral("BreadcrumbPlaceholder"));
    return label ? label->text() : QString();
}

} // namespace

class TestNavState : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void classicStartPageUsesLocalBrowser();
    void localDirectoryListingIsAsync();
    void locationBarRefreshesAfterWriteThroughReopen_data();
    void locationBarRefreshesAfterWriteThroughReopen();

private:
    QString m_settingsFile;
    QString m_savedLayout;
    bool m_overrideSet = false;
    bool m_sevenZipAvailable = false;
};

void TestNavState::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("QArchiverTests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_navstate"));
    m_settingsFile = QSettings(QSettings::IniFormat, QSettings::UserScope,
                               QStringLiteral("QArchiverTests"),
                               QStringLiteral("tst_navstate"))
                         .fileName();
    QFile::remove(m_settingsFile);
    m_savedLayout = AppSettings::instance().uiLayoutMode();

    // Keep the startup "System 7-Zip fallback" warning out of the test: it is a
    // modal dialog that would block the event loop.
    // 避免启动时的“系统 7-Zip 回退”提示框：它是模态对话框，会阻塞事件循环。
    if (qEnvironmentVariableIsEmpty("QARCHIVER_7Z_PATH")) {
        const QString tool = QStandardPaths::findExecutable(QStringLiteral("7z"));
        if (!tool.isEmpty()) {
            qputenv("QARCHIVER_7Z_PATH", tool.toUtf8());
            m_overrideSet = true;
        }
    }
    m_sevenZipAvailable = m_overrideSet
        || !qEnvironmentVariableIsEmpty("QARCHIVER_7Z_PATH");
}

void TestNavState::cleanupTestCase()
{
    AppSettings::instance().setUiLayoutMode(m_savedLayout);
    QFile::remove(m_settingsFile);
    QDir().rmdir(QFileInfo(m_settingsFile).absolutePath());
    if (m_overrideSet) {
        qunsetenv("QARCHIVER_7Z_PATH");
    }
}

void TestNavState::init()
{
    AppSettings::instance().setUseLibArchiveBackend(true);
}

// In the classic layout the local browser replaces the modern welcome page.
// It must open on normal startup, but stay out of the way when the command
// line already has an archive to open (or a shell task to run).
// 经典布局用本地文件浏览代替现代欢迎页。正常启动时应直接进入；命令行已带
// 压缩包（或外壳任务）时则不应抢先扫描本地目录。
void TestNavState::classicStartPageUsesLocalBrowser()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString savedBrowsePath = AppSettings::instance().localBrowsePath();
    AppSettings::instance().setLocalBrowsePath(temp.path());

    AppSettings::instance().setUiLayoutMode(QStringLiteral("classic"));
    {
        MainWindow window;
        QVERIFY(window.isBrowsingLocalDirectory());
    }
    {
        // The first build is deferred while a command-line archive/shell task
        // is pending, so no local directory is scanned yet.
        // 命令行压缩包/外壳任务未完成时首次构建不扫描本地目录。
        MainWindow window(nullptr, true);
        QVERIFY(!window.isBrowsingLocalDirectory());
    }

    AppSettings::instance().setUiLayoutMode(QStringLiteral("modern"));
    {
        MainWindow window;
        QVERIFY(!window.isBrowsingLocalDirectory());

        // Switching modern -> classic with no archive open enters the local
        // browser (the classic layout's start page).
        // 没有打开压缩包时从现代切到经典，进入本地文件浏览（经典起始页）。
        AppSettings::instance().setUiLayoutMode(QStringLiteral("classic"));
        window.teardownUi();
        window.buildUi();
        QVERIFY(window.isBrowsingLocalDirectory());
    }

    AppSettings::instance().setLocalBrowsePath(savedBrowsePath);
    AppSettings::instance().setUiLayoutMode(m_savedLayout);
}

// Local directory enumeration runs off the GUI thread; a stale scan must not
// overwrite the model after the user navigated again.
// 本地目录枚举在 GUI 线程之外执行；用户重新导航后旧扫描不能覆盖模型。
void TestNavState::localDirectoryListingIsAsync()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString first = temp.filePath(QStringLiteral("first"));
    const QString second = temp.filePath(QStringLiteral("second"));
    QVERIFY(QDir().mkpath(first));
    QVERIFY(QDir().mkpath(second));
    QVERIFY(writeFile(QDir(first).filePath(QStringLiteral("only-first.txt")),
                      QByteArrayLiteral("1")));
    QVERIFY(writeFile(QDir(second).filePath(QStringLiteral("only-second.txt")),
                      QByteArrayLiteral("2")));

    const QString savedPath = AppSettings::instance().localBrowsePath();
    const QString savedLayout = AppSettings::instance().uiLayoutMode();
    AppSettings::instance().setUiLayoutMode(QStringLiteral("classic"));
    AppSettings::instance().setLocalBrowsePath(first);
    {
        MainWindow window;
        QVERIFY(window.isBrowsingLocalDirectory());

        // The entries arrive through the event loop, not synchronously.
        // 条目通过事件循环到达，而不是同步填充。
        QTRY_VERIFY_WITH_TIMEOUT(window.m_model->rowCount() == 1, 5000);
        QCOMPARE(window.m_model->index(0, 0).data(Qt::DisplayRole).toString(),
                 QStringLiteral("only-first.txt"));

        // Two rapid navigations: the newest directory must win.
        // 连续两次导航：最新目录必须胜出。
        window.browseLocalDirectory(second);
        window.browseLocalDirectory(first);
        QTest::qWait(200);
        QTRY_VERIFY_WITH_TIMEOUT(window.m_model->rowCount() == 1, 5000);
        QCOMPARE(window.m_model->index(0, 0).data(Qt::DisplayRole).toString(),
                 QStringLiteral("only-first.txt"));
        QVERIFY(window.m_localBrowseDir == first);
    }

    AppSettings::instance().setLocalBrowsePath(savedPath);
    AppSettings::instance().setUiLayoutMode(savedLayout);
}

void TestNavState::locationBarRefreshesAfterWriteThroughReopen_data()
{
    QTest::addColumn<QString>("layout");
    QTest::newRow("modern") << QStringLiteral("modern");
    QTest::newRow("classic") << QStringLiteral("classic");
}

// The location bar must show the archive after the write-back, not the
// "Opening ..." placeholder that the write-through reopen leaves behind.
// 写回后路径框必须显示压缩包，而不是重开过程留下的“正在打开 ...”占位文本。
void TestNavState::locationBarRefreshesAfterWriteThroughReopen()
{
#ifndef QARCHIVER_HAS_LIBARCHIVE
    QSKIP("built without the libarchive read backend");
#else
    if (!ArchiveBackendFactory::isLibArchiveAvailable()
        || ArchiveBackendFactory::isLibArchiveDisabledByEnvironment()) {
        QSKIP("libarchive read backend is unavailable");
    }

    if (!m_sevenZipAvailable) {
        // Without a CLI 7-Zip the window shows a modal "system 7-Zip" warning
        // that would block the event loop of this test.
        // 没有 CLI 7-Zip 时主窗口会弹出“系统 7-Zip”模态警告，阻塞本测试的事件循环。
        QSKIP("no 7-Zip CLI available for the write-through reopen");
    }

    QFETCH(QString, layout);
    AppSettings::instance().setUiLayoutMode(layout);

    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    // Archive with a nested member, served read-only by libarchive.
    // 含嵌套成员的压缩包，由 libarchive 以只读方式提供服务。
    const QString sourceRoot = temp.filePath(QStringLiteral("src"));
    const QString memberPath = QStringLiteral("src/sub/a.txt");
    QVERIFY(writeFile(temp.filePath(memberPath), QByteArrayLiteral("original")));
    const QString archive = temp.filePath(QStringLiteral("notes.zip"));
    {
        ZipBackend writer;
        CompressOptions options;
        options.archivePath = archive;
        options.sourceFiles = {sourceRoot};
        bool finished = false;
        connect(&writer, &ArchiveBackend::operationFinished, &writer,
                [&finished](bool ok, const QString &) { finished = ok; });
        writer.compress(options);
        QVERIFY(finished);
    }

    MainWindow window;
    window.show();

    ArchiveManager &manager = ArchiveManager::instance();
    QSignalSpy openedSpy(&manager, &ArchiveManager::archiveOpened);
    QSignalSpy listSpy(&manager, &ArchiveManager::fileListReady);
    window.openArchivePath(archive);
    QTRY_COMPARE_WITH_TIMEOUT(openedSpy.count(), 1, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(listSpy.count() >= 1, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(qobject_cast<LibArchiveBackend *>(manager.backend())
                                 != nullptr, 5000);

    BreadcrumbPathBar *bar = locationBar(window);
    QVERIFY2(bar, "the location bar is missing");
    QTRY_VERIFY_WITH_TIMEOUT(placeholderText(bar).isEmpty(), 5000);

    // Stage the edited member the way the external-edit flow does.
    // 按外部编辑流程的方式暂存被编辑的成员。
    const QString edited = temp.filePath(QStringLiteral("edited.txt"));
    QVERIFY(writeFile(edited, QByteArrayLiteral("edited in the editor")));
    const QString staging = temp.filePath(QStringLiteral("staging"));
    QVERIFY(QDir().mkpath(staging));
    QVERIFY(writeFile(staging + QLatin1Char('/') + memberPath,
                      QByteArrayLiteral("edited in the editor")));

    AddFilesOptions saveOptions;
    saveOptions.archivePath = archive;
    saveOptions.sourceFiles = QStringList{memberPath};
    saveOptions.sourceRoot = staging;
    saveOptions.nameEncoding = QStringLiteral("UTF-8");

    QSignalSpy finishSpy(&manager, &ArchiveManager::operationFinished);
    window.startExternalEditSave(saveOptions, edited);
    QTRY_VERIFY_WITH_TIMEOUT(finishSpy.count() >= 1, 20000);
    QVERIFY2(finishSpy.first().at(0).toBool(), "the write-back must succeed");

    // Let the UI finish its post-write re-list before looking at the bar.
    // 等界面完成写回后的重新列出，再检查路径框。
    QTRY_VERIFY_WITH_TIMEOUT(listSpy.count() >= 2, 15000);
    QCoreApplication::processEvents();

    QCOMPARE(placeholderText(bar), QString());
    QVERIFY2(bar->fullPathText().contains(QStringLiteral("notes.zip")),
             qPrintable(bar->fullPathText()));

    manager.closeArchive();
#endif
}

QTEST_MAIN(TestNavState)
#include "tst_navstate.moc"
