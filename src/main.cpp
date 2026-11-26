// QArchiver / 小Q解压
// Application entry point: logging, translations, theme and main window.
// 应用入口：日志、翻译、主题与主窗口。

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTimer>

#include "app/CommandLine.h"
#include "core/AppSettings.h"
#include "core/ArchiveManager.h"
#include "app/Translations.h"
#include "ui/MainWindow.h"
#include "ui/theme/ThemeManager.h"
#include "utils/FileAssociations.h"
#include "utils/Logger.h"

#ifndef QARCHIVER_VERSION
// Fallback for builds outside CMake; CMakeLists.txt passes the real version.
// CMake 之外的构建回退；CMakeLists.txt 会传入真实版本。
#define QARCHIVER_VERSION "2.4.3"
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Let the desktop associate the window with the installed desktop entry:
    // this is what launchers and docks use to pick the application icon.
    // 让桌面把窗口关联到已安装的桌面入口；启动器/任务栏据此显示应用图标。
    QGuiApplication::setDesktopFileName(FileAssociations::desktopEntryId());

    QCoreApplication::setOrganizationName(QStringLiteral("QArchiver"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("qarchiver.local"));
    QCoreApplication::setApplicationName(QStringLiteral("QArchiver"));
    QCoreApplication::setApplicationVersion(QStringLiteral(QARCHIVER_VERSION));

    // Log file sink: <cache>/QArchiver/app.log
    // 日志文件位置：<cache>/QArchiver/app.log
    const QString logDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    Logger::instance().setLogFile(QDir(logDir).filePath(QStringLiteral("app.log")));
    LOG_INFO("main", "QArchiver starting");

    const CommandLine::Options options = CommandLine::parse(app);

    // Install the UI language before any string is created; QLocale defaults
    // follow the applied catalog so sizes and standard dialogs match.
    // 在创建任何字符串前装载界面语言；QLocale 默认值随生效的翻译目录设置，
    // 使文件大小与标准对话框使用同一语言。
    Translations::install(app, options.language);

    // Apply the configured theme before any widget is created.
    // 在创建任何控件前应用配置的主题。
    ThemeManager::applyTheme(app);

    // Installer/uninstaller helper: update associations without opening a
    // window, then exit immediately.
    // 安装器/卸载器辅助：更新文件关联后直接退出，不打开窗口。
    if (options.registerAssociations || options.unregisterAssociations) {
        QString error;
        const bool success = options.registerAssociations
            ? FileAssociations::registerFormats(FileAssociations::extensions(),
                                                &error)
            : FileAssociations::unregisterFormats(
                  FileAssociations::extensions(), &error);
        if (!success) {
            LOG_WARN("main", "File association update failed: %s",
                     error.toUtf8().constData());
        }
        return success ? 0 : 1;
    }

    // Shell context-menu invocations should never show the main window; only
    // the operation dialogs (compress options / progress / errors) are shown.
    // 资源管理器右键菜单调用不显示主窗口，只显示操作对话框。
    const bool dialogOnly = (options.oneClick && !options.archives.isEmpty())
        || !options.compressFormat.isEmpty() || options.compressDialog;

    // The command line may open an archive or run a shell-integration task as
    // soon as the event loop starts.  Tell MainWindow before its first UI build
    // so a classic layout does not scan the local directory in the meantime.
    // 命令行可能在事件循环启动后立即打开压缩包或执行外壳任务。在首次构建界面
    // 前告知 MainWindow，避免经典布局在此期间扫描本地目录。
    const bool startupContentPending = dialogOnly
        || !options.archives.isEmpty();

    MainWindow window(nullptr, startupContentPending);
    window.setDialogOnly(dialogOnly);
    window.setOpenAfterCompress(options.openAfterCompress);
    if (!dialogOnly) {
        window.show();
    }

    // Compress from the Explorer context menu / a script: no dialog, the
    // archive is created next to the sources and the window closes afterwards.
    // 来自资源管理器右键菜单/脚本的压缩：不弹出对话框，在源文件旁创建压缩包，
    // 完成后自动关闭窗口。
    if (!options.compressFormat.isEmpty()) {
        QTimer::singleShot(0, &window,
                           [&window, sources = options.archives,
                            format = options.compressFormat,
                            output = options.compressOutput]() {
            window.compressFromCommandLine(sources, format, output);
        });
    } else if (options.compressDialog) {
        QTimer::singleShot(0, &window, [&window, sources = options.archives]() {
            window.compressPathsWithDialog(sources);
        });
    } else if (!options.archives.isEmpty()) {
        // Open a file passed by the desktop environment, a file manager or a
        // Windows shell integration.  --oneclick runs the configured one-click
        // extraction (and optionally closes the window afterwards).
        // 打开桌面环境、文件管理器或 Windows 外壳集成传入的文件。
        // --oneclick 会执行配置好的一键解压（并按设置决定是否关闭窗口）。
        const QString archivePath =
            QFileInfo(options.archives.first()).absoluteFilePath();
        if (options.oneClick) {
            QTimer::singleShot(0, &window, [&window, archivePath]() {
                window.extractOneClickPath(archivePath);
            });
        } else {
            QTimer::singleShot(0, &window, [&window, archivePath]() {
                window.openArchivePath(archivePath);
            });
        }
    }

    if (options.preferences) {
        QTimer::singleShot(0, &window, [&window]() { window.openPreferences(); });
    }
    if (options.benchmark) {
        QTimer::singleShot(0, &window, [&window]() { window.openBenchmark(); });
    }

    const int ret = app.exec();
    LOG_INFO("main", "QArchiver exiting with code %d", ret);

    // Stop the archive worker while QApplication is still alive instead of
    // relying on static-destruction order (and never terminate the thread).
    // 在 QApplication 仍存在时停止归档工作线程，而不是依赖静态析构顺序
    //（也绝不 terminate 线程）。
    ArchiveManager::instance().shutdown();
    AppSettings::instance().flushPendingWrites();
    return ret;
}
