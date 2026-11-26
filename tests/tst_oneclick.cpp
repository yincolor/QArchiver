// QArchiver / 小Q解压
// Tests for one-click extraction planning and state.
// 一键解压的目标规划与状态测试。

#include <QtTest>

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "core/AppSettings.h"
#include "ui/services/OneClickExtractController.h"

class TestOneClick : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    void archiveBaseName_data();
    void archiveBaseName();
    void stateTransitions();
    void planNextToArchive();
    void planAvoidsExistingDestination();
    void planUsesCustomRoot();
    void planNamesSplitVolumeAfterFirstVolume();

private:
    QString m_settingsFile;
    QString m_savedMode;
    QString m_savedDest;
    bool m_savedOpenDest = false;
};

void TestOneClick::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("QArchiverTests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_oneclick"));

    AppSettings &settings = AppSettings::instance();
    m_savedMode = settings.oneClickExtractDestMode();
    m_savedDest = settings.oneClickExtractDest();
    m_savedOpenDest = settings.openDestAfterFinish();

    m_settingsFile = QSettings(QSettings::IniFormat, QSettings::UserScope,
                               QStringLiteral("QArchiverTests"),
                               QStringLiteral("tst_oneclick"))
                         .fileName();
}

void TestOneClick::cleanupTestCase()
{
    AppSettings &settings = AppSettings::instance();
    settings.setOneClickExtractDestMode(m_savedMode);
    settings.setOneClickExtractDest(m_savedDest);
    settings.setOpenDestAfterFinish(m_savedOpenDest);
    QFile::remove(m_settingsFile);
    QDir().rmdir(QFileInfo(m_settingsFile).absolutePath());
}

void TestOneClick::init()
{
    AppSettings &settings = AppSettings::instance();
    settings.setOneClickExtractDestMode(QStringLiteral("archive"));
    settings.setOneClickExtractDest(QString());
    settings.setOpenDestAfterFinish(false);
}

void TestOneClick::archiveBaseName_data()
{
    QTest::addColumn<QString>("archive");
    QTest::addColumn<QString>("base");

    QTest::newRow("tar.gz") << QStringLiteral("notes.tar.gz")
                            << QStringLiteral("notes");
    QTest::newRow("zip") << QStringLiteral("photos.zip")
                         << QStringLiteral("photos");
    QTest::newRow("no-extension") << QStringLiteral("raw")
                                  << QStringLiteral("raw");
    QTest::newRow("empty-name") << QStringLiteral(".zip")
                                << QStringLiteral("archive");
}

void TestOneClick::archiveBaseName()
{
    QFETCH(QString, archive);
    QFETCH(QString, base);
    QCOMPARE(OneClickExtractController::archiveBaseName(archive), base);
}

void TestOneClick::stateTransitions()
{
    OneClickExtractController controller;
    QVERIFY(!controller.isPending());
    QVERIFY(!controller.isActive());

    controller.request();
    QVERIFY(controller.isPending());
    controller.clearPending();
    QVERIFY(!controller.isPending());

    controller.start(false);
    QVERIFY(!controller.isPending());
    QVERIFY(controller.isActive());
    QVERIFY(!controller.takeFinished());
    QVERIFY(!controller.isActive());

    controller.request();
    controller.start(true);
    QVERIFY(controller.isActive());
    QVERIFY(controller.takeFinished());
    QVERIFY(!controller.isActive());

    controller.request();
    controller.start(true);
    controller.reset();
    QVERIFY(!controller.isPending());
    QVERIFY(!controller.isActive());
    QVERIFY(!controller.takeFinished());
}

void TestOneClick::planNextToArchive()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString archive = temp.filePath(QStringLiteral("notes.tar.gz"));

    OneClickExtractController controller;
    const auto plan = controller.plan(archive, {QStringLiteral("a.txt")},
                                      QStringLiteral("secret"), false);
    QVERIFY2(plan.ok, qPrintable(plan.errorMessage));
    QCOMPARE(plan.destination, temp.filePath(QStringLiteral("notes")));
    QCOMPARE(plan.options.destDir, plan.destination);
    QCOMPARE(plan.options.password, QStringLiteral("secret"));
    QCOMPARE(plan.options.overwrite, OverwritePolicy::Always);
    QVERIFY(plan.options.preservePaths);
    QVERIFY(!plan.options.openDestAfterFinish);
    QCOMPARE(plan.options.selectedFiles, QStringList{QStringLiteral("a.txt")});
    QVERIFY(!plan.closeAfter);
    QVERIFY(!plan.openDestAfterFinish);
}

void TestOneClick::planAvoidsExistingDestination()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString archive = temp.filePath(QStringLiteral("notes.zip"));
    QVERIFY(QDir().mkpath(temp.filePath(QStringLiteral("notes"))));

    OneClickExtractController controller;
    const auto plan = controller.plan(archive, {}, QString(), false);
    QVERIFY2(plan.ok, qPrintable(plan.errorMessage));
    QCOMPARE(plan.destination, temp.filePath(QStringLiteral("notes (1)")));
    QVERIFY(plan.options.selectedFiles.isEmpty());
}

void TestOneClick::planUsesCustomRoot()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString customRoot = temp.filePath(QStringLiteral("custom/output"));
    AppSettings &settings = AppSettings::instance();
    settings.setOneClickExtractDestMode(QStringLiteral("custom"));
    settings.setOneClickExtractDest(customRoot);
    settings.setOpenDestAfterFinish(true);

    OneClickExtractController controller;
    const auto plan = controller.plan(temp.filePath(QStringLiteral("a.7z")),
                                      {}, QString(), false);
    QVERIFY2(plan.ok, qPrintable(plan.errorMessage));
    QCOMPARE(plan.destination, customRoot + QStringLiteral("/a"));
    QVERIFY(QFileInfo(customRoot).isDir());
    QVERIFY(plan.openDestAfterFinish);
}

void TestOneClick::planNamesSplitVolumeAfterFirstVolume()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString first = temp.filePath(QStringLiteral("data.7z.001"));
    const QString second = temp.filePath(QStringLiteral("data.7z.002"));
    QVERIFY(QFile(first).open(QIODevice::WriteOnly));
    QVERIFY(QFile(second).open(QIODevice::WriteOnly));

    OneClickExtractController controller;
    const auto plan = controller.plan(second, {}, QString(), true);
    QVERIFY2(plan.ok, qPrintable(plan.errorMessage));
    QCOMPARE(plan.destination, temp.filePath(QStringLiteral("data")));
    QVERIFY(plan.closeAfter);
    QVERIFY(!plan.openDestAfterFinish);
}

QTEST_GUILESS_MAIN(TestOneClick)
#include "tst_oneclick.moc"
