// QArchiver / 小Q解压
// Tests for preferences and password dialog wiring.
// 偏好设置与密码对话框连接测试。

#include <QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include "backends/sevenzip/ZipBackend.h"
#include "common/FormatRegistry.h"
#include "core/ArchiveBackendFactory.h"
#include "core/AppSettings.h"
#include "core/ArchiveManager.h"
#include "core/RecentFilesManager.h"
#include "ui/MainWindowHelpers.h"
#include "ui/icons/FileTypeHelper.h"
#include "ui/models/FileListViewDelegate.h"
#include "ui/models/FileListModel.h"
#include "ui/services/ThumbnailManager.h"
#include "ui/dialogs/PasswordDialog.h"
#include "ui/dialogs/PreferencesDialog.h"
#include "ui/dialogs/BenchmarkDialog.h"
#include "ui/services/UpdateChecker.h"
#include "utils/ProcessRunner.h"

// The preferences dialog is the only place where the libarchive switch is
// exposed, so the wiring between the checkbox and AppSettings is tested here.
class TestPreferences : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void libArchiveCheckboxReflectsSetting();
    void libArchiveCheckboxPersistsChoice();
    void libArchiveCheckboxDisabledWithoutLibrary();
    void passwordDialogUsesRememberSetting();
    void configurableLimitsAreClamped();
    void settingsBatchWrites();
    void localBrowsePathIsDebounced();
    void archiveDialogFilterCoversFormats();
    void recentFilesLimitTrimsImmediately();
    void newUiSettingsRoundTrip();
    void layoutSpecificRowsFollowLayoutMode();
    void benchmarkDialogBuilds();
    void updateVersionComparison();
    void listModeDirectoryIndexPreservesEntries();
    void gridModeHidesParentEntry();
    void thumbnailsEnabledRole();
    void localThumbnailContextRole();
    void localThumbnailGenerated();
    void localSvgThumbnailGenerated();
    void thumbnailBatchExtraction();
    void thumbnailCacheLifecycle();
    void thumbnailDisplaySize();
    void splitVolumeIcons();
    void fileTypeKindPriority();
    void externalEditWatchRoundTrip();

private:
    static QCheckBox *passwordRememberCheck(PasswordDialog &dialog)
    {
        const auto boxes = dialog.findChildren<QCheckBox *>();
        return boxes.size() == 1 ? boxes.first() : nullptr;
    }

    static QCheckBox *libArchiveCheck(PreferencesDialog &dialog)
    {
        const auto boxes = dialog.findChildren<QCheckBox *>();
        for (QCheckBox *box : boxes) {
            if (box->text().contains(QStringLiteral("libarchive"),
                                     Qt::CaseInsensitive)) {
                return box;
            }
        }
        return nullptr;
    }

    static void acceptDialog(PreferencesDialog &dialog)
    {
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
        QVERIFY(ok);
        ok->click();
    }

    QString m_settingsFile;
};

// Create an isolated settings environment.
// 创建隔离的设置环境。
void TestPreferences::initTestCase()
{
    // Keep the developer's real configuration untouched.
    QCoreApplication::setOrganizationName(QStringLiteral("QArchiverTests"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_preferences"));
    m_settingsFile = QSettings(QSettings::IniFormat, QSettings::UserScope,
                               QStringLiteral("QArchiverTests"),
                               QStringLiteral("tst_preferences"))
                         .fileName();
    QFile::remove(m_settingsFile);
}

// Restore the previous settings environment.
// 恢复之前的设置环境。
void TestPreferences::cleanupTestCase()
{
    QFile::remove(m_settingsFile);
    QDir().rmdir(QFileInfo(m_settingsFile).absolutePath());
}

// The checkbox reflects the persisted preference.
// 复选框反映已持久化的偏好设置。
void TestPreferences::libArchiveCheckboxReflectsSetting()
{
    if (!ArchiveBackendFactory::isLibArchiveAvailable()) {
        QSKIP("built without libarchive: the switch is always off and disabled");
    }

    AppSettings &settings = AppSettings::instance();
    const bool saved = settings.useLibArchiveBackend();

    settings.setUseLibArchiveBackend(false);
    {
        PreferencesDialog dialog;
        QCheckBox *box = libArchiveCheck(dialog);
        QVERIFY2(box, "the libarchive checkbox is missing");
        QVERIFY(!box->isChecked());
    }

    settings.setUseLibArchiveBackend(true);
    {
        PreferencesDialog dialog;
        QCheckBox *box = libArchiveCheck(dialog);
        QVERIFY(box);
        QVERIFY(box->isChecked());
    }

    settings.setUseLibArchiveBackend(saved);
}

// Changing the checkbox persists the preference.
// 修改复选框会持久化偏好设置。
void TestPreferences::libArchiveCheckboxPersistsChoice()
{
    if (!ArchiveBackendFactory::isLibArchiveAvailable()) {
        QSKIP("built without libarchive: the switch is always off and disabled");
    }

    AppSettings &settings = AppSettings::instance();
    const bool saved = settings.useLibArchiveBackend();
    settings.setUseLibArchiveBackend(!saved);

    {
        PreferencesDialog dialog;
        QCheckBox *box = libArchiveCheck(dialog);
        QVERIFY(box);
        QCOMPARE(box->isChecked(), !saved);

        // Toggling the box and pressing OK must persist the new value.
        box->setChecked(saved);
        acceptDialog(dialog);
        QCOMPARE(settings.useLibArchiveBackend(), saved);
    }

    settings.setUseLibArchiveBackend(saved);
}

// The option is disabled when libarchive is unavailable.
// libarchive 不可用时该选项被禁用。
void TestPreferences::libArchiveCheckboxDisabledWithoutLibrary()
{
    PreferencesDialog dialog;
    QCheckBox *box = libArchiveCheck(dialog);
    QVERIFY(box);

    if (ArchiveBackendFactory::isLibArchiveAvailable()) {
        QVERIFY(box->isEnabled());
    } else {
        QVERIFY(!box->isEnabled());
        QVERIFY(!box->isChecked());
    }

    // The environment escape hatch also turns the switch off.
    qputenv("QARCHIVER_NO_LIBARCHIVE", "1");
    {
        PreferencesDialog envDialog;
        QCheckBox *envBox = libArchiveCheck(envDialog);
        QVERIFY(envBox);
        QVERIFY(!envBox->isEnabled());
        QVERIFY(!envBox->isChecked());
    }
    qunsetenv("QARCHIVER_NO_LIBARCHIVE");
}

// Password dialog defaults to and never rewrites the preference.
// 密码对话框以偏好设置为默认值，且绝不回写偏好设置。
void TestPreferences::passwordDialogUsesRememberSetting()
{
    AppSettings &settings = AppSettings::instance();
    const bool saved = settings.rememberPasswords();

    settings.setRememberPasswords(false);
    {
        PasswordDialog dialog;
        QCheckBox *box = passwordRememberCheck(dialog);
        QVERIFY(box);
        QVERIFY(!dialog.rememberPassword());
        QVERIFY(!box->isChecked());

        // Opting in for this prompt must not write the preference.
        box->setChecked(true);
        QVERIFY(dialog.rememberPassword());
        QVERIFY(!settings.rememberPasswords());
    }

    settings.setRememberPasswords(true);
    {
        PasswordDialog dialog;
        QCheckBox *box = passwordRememberCheck(dialog);
        QVERIFY(box);
        QVERIFY(dialog.rememberPassword());
        QVERIFY(box->isChecked());

        // Opting out for this prompt must not write the preference either.
        box->setChecked(false);
        QVERIFY(!dialog.rememberPassword());
        QVERIFY(settings.rememberPasswords());
    }

    // A retry prompt explains that the previous password was rejected.
    // 重试提示会说明上一次密码被拒绝。
    {
        PasswordDialog dialog(nullptr, true);
        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("PasswordHint"));
        QVERIFY(hint);
        QVERIFY(hint->text().contains(QStringLiteral("incorrect"),
                                      Qt::CaseInsensitive));
        QVERIFY(!hint->text().contains(QStringLiteral("This archive is encrypted")));
    }

    settings.setRememberPasswords(saved);
}

// New configurable limits have stable defaults and strict ranges.
// 新增可配置上限具有稳定默认值并严格限制范围。
void TestPreferences::configurableLimitsAreClamped()
{
    AppSettings &settings = AppSettings::instance();

    settings.setContinuousImageDialogThreshold(-1);
    QCOMPARE(settings.continuousImageDialogThreshold(), 0);
    settings.setContinuousImageDialogThreshold(10000);
    QCOMPARE(settings.continuousImageDialogThreshold(), 9999);
    settings.setContinuousImageDialogThreshold(150);
    QCOMPARE(settings.continuousImageDialogThreshold(), 150);

    settings.setRecentFilesLimit(0);
    QCOMPARE(settings.recentFilesLimit(), 1);
    settings.setRecentFilesLimit(101);
    QCOMPARE(settings.recentFilesLimit(), 100);
    settings.setRecentFilesLimit(7);
    QCOMPARE(settings.recentFilesLimit(), 7);
}

// The Open dialog is generated from the format registry, so every registry
// pattern must be present and the translated labels must stay in place.
// 打开对话框由格式注册表生成：必须包含全部模式，并保留可翻译的标签。
void TestPreferences::archiveDialogFilterCoversFormats()
{
    const QString filter = MainWindowHelpers::archiveDialogFilter();
    QVERIFY(filter.startsWith(QStringLiteral("Archives (")));
    for (const QString &pattern : FormatRegistry::openGlobPatterns()) {
        QVERIFY2(filter.contains(pattern), qPrintable(pattern));
    }
    QVERIFY(filter.endsWith(QStringLiteral(";;All Files (*)")));
}

// beginBatch()/endBatch() make the new values visible immediately while
// writing the file only once at the end.
// beginBatch()/endBatch() 让新值立即可见，而文件只在结束时写入一次。
void TestPreferences::settingsBatchWrites()
{
    AppSettings &s = AppSettings::instance();
    const int savedLevel = s.defaultCompressionLevel();
    const int batchedLevel = savedLevel == 7 ? 8 : 7;

    // Read the settings file directly: QSettings instances for the same file
    // share an in-memory cache, so a second QSettings would not prove that the
    // batch really reached the disk.
    // 直接读取设置文件：同一文件的 QSettings 实例共享内存缓存，第二个实例无法
    // 证明批次真的落盘。
    const auto diskLevelIs = [this](int level) {
        QFile file(m_settingsFile);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        const QString text = QString::fromUtf8(file.readAll());
        return text.contains(QStringLiteral("defaultLevel=%1").arg(level));
    };

    s.beginBatch();
    s.setDefaultCompressionLevel(batchedLevel);
    QCOMPARE(s.defaultCompressionLevel(), batchedLevel);

    // Nested batches must not flush the outer one.
    // 嵌套批次不能提前写入外层批次。
    s.beginBatch();
    s.endBatch();
    QCOMPARE(s.defaultCompressionLevel(), batchedLevel);
    QVERIFY(!diskLevelIs(batchedLevel));

    s.endBatch();
    QVERIFY(diskLevelIs(batchedLevel));

    s.setDefaultCompressionLevel(savedLevel);
    QCOMPARE(s.defaultCompressionLevel(), savedLevel);
}

// The last-visited local directory is written after a short debounce so rapid
// navigation does not sync the settings file on every step.
// 上次浏览的本地目录在短暂延迟后写入，连续导航不会每步都写设置文件。
void TestPreferences::localBrowsePathIsDebounced()
{
    AppSettings &s = AppSettings::instance();
    const QString saved = s.localBrowsePath();
    const QString marker = QStringLiteral("/tmp/qarchiver-debounce-test");

    const auto diskContains = [this](const QString &text) {
        QFile file(m_settingsFile);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        return QString::fromUtf8(file.readAll()).contains(text);
    };

    s.setLocalBrowsePath(marker);
    QCOMPARE(s.localBrowsePath(), marker); // visible in memory immediately
    QTest::qWait(150);                     // shorter than the debounce interval
    QVERIFY(!diskContains(marker));

    QTRY_VERIFY_WITH_TIMEOUT(diskContains(marker), 3000);
    s.setLocalBrowsePath(saved);
}

// Reducing the recent-files limit trims the stored history without waiting
// for the next "add", and the sidebar notification is emitted.
// 降低最近文件上限时立即裁剪已保存历史，无需等待下一次“添加”，
// 并发出侧边栏通知。
void TestPreferences::recentFilesLimitTrimsImmediately()
{
    AppSettings &settings = AppSettings::instance();
    RecentFilesManager &recent = RecentFilesManager::instance();

    settings.setRecentFilesLimit(3);
    settings.setRecentFiles({QStringLiteral("a"), QStringLiteral("b"),
                             QStringLiteral("c"), QStringLiteral("d"),
                             QStringLiteral("e")});
    QCOMPARE(settings.recentFiles().size(), 3);

    QSignalSpy changedSpy(&recent, &RecentFilesManager::changed);
    settings.setRecentFilesLimit(1);
    QCOMPARE(settings.recentFiles().size(), 1);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(recent.list().size(), 1);
}

// Modern/classic layout and one-click settings have stable values.
// 现代/经典布局与一键解压设置具有稳定的取值。
void TestPreferences::newUiSettingsRoundTrip()
{
    AppSettings &settings = AppSettings::instance();
    const QString savedLayout = settings.uiLayoutMode();
    const QString savedDestMode = settings.oneClickExtractDestMode();
    const QString savedDest = settings.oneClickExtractDest();
    const bool savedClose = settings.oneClickCloseAfterExtract();
    const bool savedAsked = settings.oneClickCloseAsked();
    const QString savedViewMode = settings.contentViewMode();

    settings.setUiLayoutMode(QStringLiteral("classic"));
    QCOMPARE(settings.uiLayoutMode(), QStringLiteral("classic"));
    settings.setUiLayoutMode(QStringLiteral("modern"));
    QCOMPARE(settings.uiLayoutMode(), QStringLiteral("modern"));
    settings.setUiLayoutMode(QStringLiteral("unsupported"));
    QCOMPARE(settings.uiLayoutMode(), QStringLiteral("modern"));

    settings.setOneClickExtractDestMode(QStringLiteral("custom"));
    settings.setOneClickExtractDest(QStringLiteral("/tmp/one-click"));
    settings.setOneClickCloseAfterExtract(true);
    settings.setOneClickCloseAsked(true);
    QCOMPARE(settings.oneClickExtractDestMode(), QStringLiteral("custom"));
    QCOMPARE(settings.oneClickExtractDest(), QStringLiteral("/tmp/one-click"));
    QVERIFY(settings.oneClickCloseAfterExtract());
    QVERIFY(settings.oneClickCloseAsked());

    settings.setContentViewMode(QStringLiteral("grid"));
    QCOMPARE(settings.contentViewMode(), QStringLiteral("grid"));
    settings.setContentViewMode(QStringLiteral("list"));
    QCOMPARE(settings.contentViewMode(), QStringLiteral("list"));
    settings.setContentViewMode(QStringLiteral("tree"));
    QCOMPARE(settings.contentViewMode(), QStringLiteral("tree"));
    settings.setContentViewMode(QStringLiteral("unsupported"));
    QCOMPARE(settings.contentViewMode(), QStringLiteral("grid"));

    settings.setOneClickExtractDestMode(QStringLiteral("archive"));
    settings.setOneClickExtractDest(savedDest);
    settings.setOneClickCloseAfterExtract(savedClose);
    settings.setOneClickCloseAsked(savedAsked);
    settings.setUiLayoutMode(savedLayout);
    settings.setContentViewMode(savedViewMode);
}

// The benchmark dialog can be created without a display and exposes its
// thread-count control.
// 性能测试对话框可在无显示环境下创建，并包含线程数控件。
// Rows that only apply to one interface layout are hidden, not disabled.
// 只在某一种界面布局中生效的行会被隐藏，而不是置灰。
void TestPreferences::layoutSpecificRowsFollowLayoutMode()
{
    auto &settings = AppSettings::instance();
    const QString savedLayout = settings.uiLayoutMode();

    settings.setUiLayoutMode(QStringLiteral("modern"));
    {
        PreferencesDialog dialog;
        auto *viewMode = dialog.findChild<QComboBox *>(
            QStringLiteral("PrefsViewModeCombo"));
        auto *toolbarStyle = dialog.findChild<QComboBox *>(
            QStringLiteral("PrefsToolbarStyleCombo"));
        auto *recentCheck = dialog.findChild<QCheckBox *>(
            QStringLiteral("PrefsRecentFilesCheck"));
        QVERIFY(viewMode);
        QVERIFY(toolbarStyle);
        QVERIFY(recentCheck);

        // Modern: archive content view available, classic-only rows hidden.
        // 现代：内容视图可选，经典布局专属行隐藏。
        QVERIFY(!viewMode->isHidden());
        QVERIFY(toolbarStyle->isHidden());
        QVERIFY(recentCheck->isHidden());

        // Switching the layout combo updates the page immediately, without
        // saving and reopening the dialog.
        // 切换布局下拉框时立即更新页面，无需保存并重开对话框。
        auto *layoutCombo = dialog.findChild<QComboBox *>(
            QStringLiteral("PrefsLayoutModeCombo"));
        QVERIFY(layoutCombo);
        layoutCombo->setCurrentIndex(
            layoutCombo->findData(QStringLiteral("classic")));
        // The content-view choice applies to both layouts; classic only adds
        // the toolbar-style and recent-files rows.
        // 内容视图对两种布局都生效；经典布局额外显示工具栏样式与最近文件行。
        QVERIFY(!viewMode->isHidden());
        QVERIFY(!toolbarStyle->isHidden());
        QVERIFY(!recentCheck->isHidden());
    }

    // The stored layout mode is not touched until the dialog is accepted.
    // 对话框未确认前不会改动已保存的布局模式。
    QCOMPARE(settings.uiLayoutMode(), QStringLiteral("modern"));

    settings.setUiLayoutMode(savedLayout);
}

void TestPreferences::benchmarkDialogBuilds()
{
    BenchmarkDialog dialog;
    auto *threads = dialog.findChild<QComboBox *>();
    QVERIFY(threads != nullptr);
    QVERIFY(threads->count() > 1);
    QVERIFY(threads->isEnabled());
    QVERIFY(dialog.findChild<QPlainTextEdit *>() != nullptr);
}

// List mode uses the directory index: implicit parents, duplicates and the
// original member spelling survive navigation.
// 列表模式使用目录索引：隐式父目录、重复项和原始成员名在导航后仍保留。
void TestPreferences::updateVersionComparison()
{
    QVERIFY(UpdateChecker::isNewerVersion(QStringLiteral("v2.4.2"),
                                         QStringLiteral("2.4.1")));
    QVERIFY(UpdateChecker::isNewerVersion(QStringLiteral("v2.4.1-1"),
                                         QStringLiteral("2.4.1")));
    QVERIFY(!UpdateChecker::isNewerVersion(QStringLiteral("v2.4.1"),
                                          QStringLiteral("2.4.1")));
    QVERIFY(!UpdateChecker::isNewerVersion(QStringLiteral("v2.4.0"),
                                          QStringLiteral("2.4.1")));
}

void TestPreferences::listModeDirectoryIndexPreservesEntries()
{
    FileListModel model;
    QList<ArchiveEntry> entries;
    auto add = [&entries](const QString &path, qint64 size = 0) {
        ArchiveEntry entry;
        entry.path = path;
        entry.size = size;
        entries.append(entry);
    };
    add(QStringLiteral("./root.txt"), 1);
    add(QStringLiteral("dir/a.txt"), 2);
    add(QStringLiteral("dir/a.txt"), 3);
    add(QStringLiteral("dir/sub/b.txt"), 4);

    model.setEntries(entries);
    model.setFlatMode(true);

    QCOMPARE(model.rowCount(), 2); // implicit "dir" + root.txt
    QVERIFY(model.enterDirectory(QStringLiteral("dir")));
    QCOMPARE(model.rowCount(), 4); // ".." + two duplicate a.txt rows + sub
    QVERIFY(model.enterDirectory(QStringLiteral("sub")));
    QCOMPARE(model.rowCount(), 2); // ".." + b.txt
    QCOMPARE(model.index(1, FileListModel::NameColumn)
                 .data(FileListModel::PathRole).toString(),
             QStringLiteral("dir/sub/b.txt"));
    QVERIFY(model.goUp());
    QVERIFY(model.goUp());
    QCOMPARE(model.rowCount(), 2);
}

// Grid mode uses the same flat model but must not show the synthetic ".."
// row; the Up action performs that navigation instead.
// 网格模式复用扁平模型，但不应显示合成的 ".." 行；返回上一级由 Up 动作完成。
void TestPreferences::gridModeHidesParentEntry()
{
    FileListModel model;
    QList<ArchiveEntry> entries;
    auto add = [&entries](const QString &path) {
        ArchiveEntry entry;
        entry.path = path;
        entries.append(entry);
    };
    add(QStringLiteral("dir/a.txt"));
    add(QStringLiteral("dir/sub/b.txt"));

    model.setEntries(entries);
    model.setFlatMode(true);
    QVERIFY(model.enterDirectory(QStringLiteral("dir")));
    QCOMPARE(model.rowCount(), 3); // ".." + a.txt + sub
    QVERIFY(model.index(0, FileListModel::NameColumn)
                .data(FileListModel::IsParentRole).toBool());

    model.setShowParentEntry(false);
    QCOMPARE(model.rowCount(), 2); // a.txt + sub
    for (int row = 0; row < model.rowCount(); ++row) {
        QVERIFY(!model.index(row, FileListModel::NameColumn)
                     .data(FileListModel::IsParentRole).toBool());
    }

    QVERIFY(model.enterDirectory(QStringLiteral("sub")));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.index(0, FileListModel::NameColumn)
                 .data(FileListModel::PathRole).toString(),
             QStringLiteral("dir/sub/b.txt"));
    QVERIFY(model.goUp());
    QCOMPARE(model.rowCount(), 2);

    model.setShowParentEntry(true);
    QCOMPARE(model.rowCount(), 3); // ".." restored for list mode
    QVERIFY(model.index(0, FileListModel::NameColumn)
                .data(FileListModel::IsParentRole).toBool());
}

// The model only exposes thumbnail requests when the active view enables
// them; classic list/tree leave the flag off, classic grid turns it on.
// 只有当前视图启用时模型才请求缩略图；经典列表/树关闭，经典网格开启。
void TestPreferences::thumbnailsEnabledRole()
{
    FileListModel model;
    ArchiveEntry entry;
    entry.path = QStringLiteral("photos/a.png");
    entry.size = 42;
    QList<ArchiveEntry> entries;
    entries.append(entry);
    model.setEntries(entries);
    model.setArchiveContext(QStringLiteral("archive.zip"),
                            ArchiveFormat::Zip, QString());

    const QModelIndex index = model.indexForPath(entry.path);
    QVERIFY(index.isValid());
    QVERIFY(!index.data(FileListModel::IsThumbnailRole).toBool());

    model.setThumbnailsEnabled(true);
    QVERIFY(index.data(FileListModel::IsThumbnailRole).toBool());

    model.setThumbnailsEnabled(false);
    QVERIFY(!index.data(FileListModel::IsThumbnailRole).toBool());
}

// Local directory entries use the same thumbnail role as archive members, but
// with a local directory context instead of an archive path.
// 本地目录条目与压缩包成员共用缩略图角色，只是使用本地目录上下文而不是
// 压缩包路径。
void TestPreferences::localThumbnailContextRole()
{
    FileListModel model;
    ArchiveEntry entry;
    entry.path = QStringLiteral("photos/a.png");
    entry.size = 42;
    entry.mtime = QDateTime::currentDateTime();
    model.setEntries({entry});
    model.setThumbnailsEnabled(true);
    model.setArchiveContext(QString(), ArchiveFormat::Unknown, QString());

    const QModelIndex index = model.indexForPath(entry.path);
    QVERIFY(index.isValid());
    QVERIFY(!index.data(FileListModel::IsThumbnailRole).toBool());

    const QString localRoot = QDir::tempPath();
    model.setLocalContext(localRoot);
    QCOMPARE(model.localContext(), localRoot);
    QVERIFY(index.data(FileListModel::IsThumbnailRole).toBool());

    // Switching back to archive context (even an empty one) clears the local
    // root so archive thumbnails cannot read files from the old directory.
    // 切回压缩包上下文（即使路径为空）会清除本地目录，避免压缩包缩略图从旧
    // 目录读取文件。
    model.setArchiveContext(QString(), ArchiveFormat::Unknown, QString());
    QCOMPARE(model.localContext(), QString());
    QVERIFY(!index.data(FileListModel::IsThumbnailRole).toBool());
}

// A local image is rendered asynchronously into the same persistent thumbnail
// cache used by archive entries.
// 本地图片与压缩包条目一样，异步渲染到同一个持久缩略图缓存。
void TestPreferences::localThumbnailGenerated()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString imagePath = temp.filePath(QStringLiteral("image.png"));
    QImage image(40, 20, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(imagePath));

    const QFileInfo info(imagePath);
    QSignalSpy readySpy(&ThumbnailManager::instance(),
                        &ThumbnailManager::localThumbnailReady);
    // Requesting the icon is what queues the asynchronous render.  The test
    // binaries do not embed the application's Qt resources (they are linked
    // into the QArchiver executable only), so the placeholder is a null QIcon
    // here since Qt 6.8 changed QIcon::isNull() to report a missing resource.
    // 请求图标才会排队异步渲染。测试程序没有内嵌应用资源（资源只链接进
    // QArchiver 可执行文件），而 Qt 6.8 起 QIcon::isNull() 会把缺失资源的
    // 占位图标报告为空，所以这里不能断言占位图标非空。
    const QIcon fallback = ThumbnailManager::instance().localIcon(
        imagePath, info.size(), info.lastModified());
    Q_UNUSED(fallback);

    QTRY_VERIFY_WITH_TIMEOUT(readySpy.count() >= 1, 5000);
    QCOMPARE(readySpy.first().at(0).toString(), imagePath);

    // The completed thumbnail is now served from the in-memory cache.
    // 完成后再次请求会直接命中内存缓存。
    const QIcon cached = ThumbnailManager::instance().localIcon(
        imagePath, info.size(), info.lastModified());
    QVERIFY(!cached.isNull());
    QVERIFY(!cached.availableSizes().isEmpty());
}

// SVG thumbnails are rendered with QSvgRenderer rather than QImageReader, so
// they do not depend on the qsvg image-format plugin being deployed.
// SVG 缩略图使用 QSvgRenderer 渲染，不依赖 qsvg 图片格式插件是否部署。
void TestPreferences::localSvgThumbnailGenerated()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString svgPath = temp.filePath(QStringLiteral("icon.SVG"));
    QFile file(svgPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray svg = QByteArrayLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        "width=\"64\" height=\"32\">"
        "<rect width=\"64\" height=\"32\" fill=\"red\"/>"
        "</svg>");
    QCOMPARE(file.write(svg), svg.size());
    file.close();

    const QFileInfo info(svgPath);
    QSignalSpy readySpy(&ThumbnailManager::instance(),
                        &ThumbnailManager::localThumbnailReady);
    ThumbnailManager::instance().localIcon(
        svgPath, info.size(), info.lastModified());

    QTRY_VERIFY_WITH_TIMEOUT(readySpy.count() >= 1, 5000);
    QCOMPARE(readySpy.first().at(0).toString(), svgPath);

    const QIcon cached = ThumbnailManager::instance().localIcon(
        svgPath, info.size(), info.lastModified());
    QVERIFY(!cached.isNull());
    const QList<QSize> sizes = cached.availableSizes();
    QVERIFY(!sizes.isEmpty());
    // The 2:1 SVG must keep its aspect ratio in the 128px thumbnail box.
    // 2:1 的 SVG 在 128px 缩略图框内必须保持宽高比。
    QCOMPARE(sizes.constFirst(), QSize(128, 64));
}

// Several thumbnails from the same archive are collected in one batch and
// extracted with a single backend call; all of them must still become available,
// including an SVG rendered without the qsvg image-format plugin.
// 同一压缩包的多个缩略图会合并成一批、只调用一次后端解压；包括不依赖 qsvg
// 图片格式插件渲染的 SVG 在内，全部都必须可用。
void TestPreferences::thumbnailBatchExtraction()
{
    if (ProcessRunner::resolveToolPath(QStringLiteral("7z")).isEmpty()) {
        QSKIP("the 7z tool is required for this test");
    }

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QImage image(48, 24, QImage::Format_ARGB32);
    image.fill(Qt::green);
    const QString firstPath = temp.filePath(QStringLiteral("a.png"));
    const QString secondPath = temp.filePath(QStringLiteral("b.png"));
    const QString svgPath = temp.filePath(QStringLiteral("c.svg"));
    QVERIFY(image.save(firstPath));
    QVERIFY(image.save(secondPath));
    QFile svgFile(svgPath);
    QVERIFY(svgFile.open(QIODevice::WriteOnly));
    const QByteArray svg = QByteArrayLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        "width=\"32\" height=\"32\">"
        "<circle cx=\"16\" cy=\"16\" r=\"16\" fill=\"blue\"/>"
        "</svg>");
    QCOMPARE(svgFile.write(svg), svg.size());
    svgFile.close();

    const QString archive = temp.filePath(QStringLiteral("images.zip"));
    ZipBackend writer;
    QSignalSpy finished(&writer, &ArchiveBackend::operationFinished);
    QSignalSpy errors(&writer, &ArchiveBackend::errorOccurred);
    CompressOptions options;
    options.archivePath = archive;
    options.sourceFiles = {firstPath, secondPath, svgPath};
    options.compressionLevel = 0;
    writer.compress(options);
    QVERIFY(errors.isEmpty());
    QCOMPARE(finished.count(), 1);
    QVERIFY(finished.first().at(0).toBool());

    ThumbnailManager &thumbs = ThumbnailManager::instance();
    thumbs.clearArchiveCache();
    QSignalSpy readySpy(&thumbs, &ThumbnailManager::thumbnailReady);
    thumbs.icon(archive, ArchiveFormat::Zip, QStringLiteral("a.png"), 0,
                QString());
    thumbs.icon(archive, ArchiveFormat::Zip, QStringLiteral("b.png"), 0,
                QString());
    thumbs.icon(archive, ArchiveFormat::Zip, QStringLiteral("c.svg"), 0,
                QString());

    QTRY_VERIFY_WITH_TIMEOUT(readySpy.count() >= 3, 15000);

    const QIcon cachedFirst = thumbs.icon(
        archive, ArchiveFormat::Zip, QStringLiteral("a.png"), 0, QString());
    const QIcon cachedSecond = thumbs.icon(
        archive, ArchiveFormat::Zip, QStringLiteral("b.png"), 0, QString());
    const QIcon cachedSvg = thumbs.icon(
        archive, ArchiveFormat::Zip, QStringLiteral("c.svg"), 0, QString());
    QVERIFY(!cachedFirst.availableSizes().isEmpty());
    QVERIFY(!cachedSecond.availableSizes().isEmpty());
    QVERIFY(!cachedSvg.availableSizes().isEmpty());

    thumbs.cleanupOwn();
}

// Thumbnail caches must be releasable per context and clean up leftovers from
// crashed runs; clearing the in-memory state keeps the disk cache reusable.
// 缩略图缓存必须能按上下文释放并清理崩溃残留；清理内存状态后磁盘缓存仍可复用。
void TestPreferences::thumbnailCacheLifecycle()
{
    const bool wasTestMode = QStandardPaths::isTestModeEnabled();
    QStandardPaths::setTestModeEnabled(true);
    const auto restore = qScopeGuard([wasTestMode] {
        QStandardPaths::setTestModeEnabled(wasTestMode);
    });

    ThumbnailManager &thumbs = ThumbnailManager::instance();
    const QString root =
        QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
            .filePath(QStringLiteral("thumbnails"));

    // A crashed extraction can leave an "extract-*" directory behind.
    // 崩溃的解压可能残留 "extract-*" 目录。
    const QString leftover = QDir(root).filePath(
        QStringLiteral("extract-tst-%1")
            .arg(QUuid::createUuid().toString(QUuid::Id128)));
    QVERIFY(QDir().mkpath(leftover + QStringLiteral("/sub")));
    thumbs.cleanupStale();
    QVERIFY(!QDir(leftover).exists());

    // Generate a thumbnail, drop the in-memory cache, then verify the disk
    // cache still serves it: clearArchiveCache() only frees memory.
    // 生成缩略图、清空内存缓存后确认磁盘缓存仍可用：clearArchiveCache()
    // 只释放内存。
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString imagePath = temp.filePath(QStringLiteral("image.png"));
    QImage image(24, 12, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(imagePath));
    const QFileInfo info(imagePath);

    QSignalSpy readySpy(&thumbs, &ThumbnailManager::localThumbnailReady);
    thumbs.localIcon(imagePath, info.size(), info.lastModified());
    QTRY_VERIFY_WITH_TIMEOUT(readySpy.count() >= 1, 5000);

    thumbs.clearArchiveCache();
    const QIcon cached =
        thumbs.localIcon(imagePath, info.size(), info.lastModified());
    QVERIFY(!cached.isNull());
    QVERIFY(!cached.availableSizes().isEmpty());

    // Shutdown waits for worker tasks and must not crash.
    thumbs.cleanupOwn();
}

// Thumbnails in the modern list are fitted to the row height; width follows
// the source aspect ratio but is capped so it cannot squeeze the file name.
// 现代列表缩略图适配行高；宽度按源比例计算，并设上限避免挤压文件名。
void TestPreferences::thumbnailDisplaySize()
{
    const QSize limit(96, 22);

    const QSize landscape = FileListViewDelegate::thumbnailDisplaySize(
        QSize(128, 85), limit);
    QCOMPARE(landscape.height(), 22);
    QVERIFY(landscape.width() <= 96);
    QVERIFY(qAbs(landscape.width() / double(landscape.height())
                 - 128.0 / 85.0) < 0.1);

    const QSize portrait = FileListViewDelegate::thumbnailDisplaySize(
        QSize(85, 128), limit);
    QCOMPARE(portrait.height(), 22);
    QVERIFY(portrait.width() < portrait.height());

    const QSize panorama = FileListViewDelegate::thumbnailDisplaySize(
        QSize(1000, 100), limit);
    QCOMPARE(panorama.width(), 96);
    QVERIFY(panorama.height() <= 22);

    // A very thin panorama must not collapse to zero height.
    // 极扁的全景图不能被缩放成 0 高。
    const QSize thinPanorama = FileListViewDelegate::thumbnailDisplaySize(
        QSize(1000, 1), limit);
    QCOMPARE(thinPanorama.width(), 96);
    QCOMPARE(thinPanorama.height(), 1);

    QVERIFY(FileListViewDelegate::thumbnailDisplaySize(QSize(), limit).isEmpty());
    QVERIFY(FileListViewDelegate::thumbnailDisplaySize(QSize(10, 10), QSize())
                .isEmpty());
}

// Split-volume suffixes must map to the archive icons, while ordinary .r??
// files such as .rtf must not be treated as archives.
// 分卷后缀必须映射到对应压缩包图标；.rtf 等普通 .r?? 文件不能当成压缩包。
void TestPreferences::fileTypeKindPriority()
{
    // E-books must win over the generic document set. / 电子书优先于通用文档类型。
    QCOMPARE(FileTypeHelper::kindFor(QStringLiteral("book.epub"), false),
             FileTypeHelper::Kind::Ebook);
    QCOMPARE(FileTypeHelper::kindFor(QStringLiteral("book.mobi"), false),
             FileTypeHelper::Kind::Ebook);

    // Disc images are openable archives now, but keep their own icon family and
    // label. / 光盘镜像现在是可打开的压缩包，但仍使用专属图标族与标签。
    QCOMPARE(FileTypeHelper::kindFor(QStringLiteral("installer.iso"), false),
             FileTypeHelper::Kind::Iso);
    QCOMPARE(FileTypeHelper::iconPath(QStringLiteral("installer.iso"), false, true),
             QStringLiteral(":/icons/file-types/modern/file-iso.png"));
    QCOMPARE(FileTypeHelper::formatLabel(ArchiveFormat::Iso),
             QStringLiteral("ISO"));

    // Batch files are source code, not executables. / 批处理文件归类为源码。
    QCOMPARE(FileTypeHelper::kindFor(QStringLiteral("build.bat"), false),
             FileTypeHelper::Kind::Code);
    QCOMPARE(FileTypeHelper::kindFor(QStringLiteral("build.cmd"), false),
             FileTypeHelper::Kind::Executable);
}

// The external-edit prompt can be switched off and stays persisted.
// 外部编辑写回提示可关闭，并且设置会被持久化。
void TestPreferences::externalEditWatchRoundTrip()
{
    AppSettings &settings = AppSettings::instance();
    const bool saved = settings.externalEditWatch();

    settings.setExternalEditWatch(false);
    QVERIFY(!AppSettings::instance().externalEditWatch());
    {
        PreferencesDialog dialog;
        QCheckBox *box = nullptr;
        for (QCheckBox *candidate : dialog.findChildren<QCheckBox *>()) {
            if (candidate->text().contains(QStringLiteral("externally edited"),
                                           Qt::CaseInsensitive)) {
                box = candidate;
                break;
            }
        }
        QVERIFY2(box, "the external-edit checkbox is missing");
        QVERIFY(!box->isChecked());

        // Turning it on through the dialog persists the choice.
        // 在对话框中打开后会持久化该选择。
        box->setChecked(true);
        acceptDialog(dialog);
    }
    QVERIFY(AppSettings::instance().externalEditWatch());

    settings.setExternalEditWatch(saved);
}

void TestPreferences::splitVolumeIcons()
{
    QCOMPARE(FileTypeHelper::iconPath(QStringLiteral("data.part1.rar"), false, true),
             QStringLiteral(":/icons/file-types/modern/file-rar.png"));
    QCOMPARE(FileTypeHelper::iconPath(QStringLiteral("data.r00"), false, true),
             QStringLiteral(":/icons/file-types/modern/file-rar.png"));
    QCOMPARE(FileTypeHelper::iconPath(QStringLiteral("data.zip.001"), false, true),
             QStringLiteral(":/icons/file-types/modern/file-zip.png"));
    QCOMPARE(FileTypeHelper::iconPath(QStringLiteral("data.7z.001"), false, true),
             QStringLiteral(":/icons/file-types/modern/file-7z.png"));
    QCOMPARE(FileTypeHelper::iconPath(QStringLiteral("data.tar.zst"), false, true),
             QStringLiteral(":/icons/file-types/modern/file-tar.png"));
    QCOMPARE(FileTypeHelper::iconPath(QStringLiteral("notes.rtf"), false, true),
             QStringLiteral(":/icons/file-types/modern/file-text.png"));

    // Classic layout uses its own complete file-type set instead of falling
    // back to the generic classic file/folder icons.
    // 经典布局使用自己完整的文件类型图标集，而不是退回通用文件/文件夹图标。
    QCOMPARE(FileTypeHelper::iconPath(QStringLiteral("data.part1.rar"), false, false),
             QStringLiteral(":/icons/file-types/classic/file-rar.png"));
    QCOMPARE(FileTypeHelper::iconPath(QStringLiteral("data.r00"), false, false),
             QStringLiteral(":/icons/file-types/classic/file-rar.png"));
    QCOMPARE(FileTypeHelper::iconPath(QStringLiteral("data.zip.001"), false, false),
             QStringLiteral(":/icons/file-types/classic/file-zip.png"));
    QCOMPARE(FileTypeHelper::iconPath(QStringLiteral("data.7z.001"), false, false),
             QStringLiteral(":/icons/file-types/classic/file-7z.png"));
    QCOMPARE(FileTypeHelper::iconPath(QStringLiteral("data.tar.zst"), false, false),
             QStringLiteral(":/icons/file-types/classic/file-tar.png"));
    QCOMPARE(FileTypeHelper::iconPath(QStringLiteral("notes.rtf"), false, false),
             QStringLiteral(":/icons/file-types/classic/file-text.png"));
    QCOMPARE(FileTypeHelper::iconPath(QStringLiteral("folder"), true, false),
             QStringLiteral(":/icons/file-types/classic/folder.png"));
    QCOMPARE(FileTypeHelper::iconPath(QStringLiteral("unknown.xyz"), false, false),
             QStringLiteral(":/icons/file-types/classic/file.png"));
}

QTEST_MAIN(TestPreferences)

#include "tst_preferences.moc"
