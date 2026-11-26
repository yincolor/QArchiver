// QArchiver / 小Q解压
// Per-user archive file associations for Windows and Linux.
// Windows / Linux 当前用户的压缩包格式关联。

#include "utils/FileAssociations.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include "common/FormatRegistry.h"

#ifdef Q_OS_LINUX
#include "utils/MimeAppsList.h"
#endif

#include <string>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace {

const QStringList &formatExtensions()
{
    // Single source of truth: the format registry also drives the Open dialog.
    // 唯一事实来源：格式注册表同时驱动打开对话框。
    return FormatRegistry::associationSuffixes();
}

QString description(const QString &extension)
{
    const auto translate = [](const char *text) {
        return QCoreApplication::translate("FileAssociations", text);
    };
    if (extension == QLatin1String(".zip")) {
        return translate("ZIP archive");
    }
    if (extension == QLatin1String(".7z")) {
        return translate("7z archive");
    }
    if (extension == QLatin1String(".rar")) {
        return translate("RAR archive");
    }
    if (extension == QLatin1String(".tar")) {
        return translate("TAR archive");
    }
    if (extension == QLatin1String(".gz")) {
        return translate("Gzip file");
    }
    if (extension == QLatin1String(".bz2")) {
        return translate("Bzip2 file");
    }
    if (extension == QLatin1String(".xz")) {
        return translate("Xz file");
    }
    if (extension == QLatin1String(".tgz")
        || extension == QLatin1String(".tar.gz")) {
        return translate("Gzip tar archive");
    }
    if (extension == QLatin1String(".txz")
        || extension == QLatin1String(".tar.xz")) {
        return translate("Xz tar archive");
    }
    if (extension == QLatin1String(".tbz2")
        || extension == QLatin1String(".tar.bz2")) {
        return translate("Bzip2 tar archive");
    }
    if (extension == QLatin1String(".zst")) {
        return translate("Zstd file");
    }
    if (extension == QLatin1String(".tzst")
        || extension == QLatin1String(".tar.zst")) {
        return translate("Zstd tar archive");
    }
    if (extension == QLatin1String(".iso")) {
        return translate("Disc image");
    }
    return extension;
}

#ifdef Q_OS_WIN
QString progIdFor(const QString &extension)
{
    return QStringLiteral("QArchiver") + extension;
}

QString applicationCommand()
{
    const QString exe =
        QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    return QStringLiteral("\"%1\" \"%2\"").arg(exe, QStringLiteral("%1"));
}
#endif

#ifdef Q_OS_WIN
// ---------------------------------------------------------------------------
// Windows: HKCU\Software\Classes ProgID + OpenWithProgids.
// Windows：HKCU\Software\Classes 下的 ProgID 与 OpenWithProgids。
// ---------------------------------------------------------------------------

QString classesKey(const QString &subKey)
{
    return QStringLiteral("Software\\Classes\\") + subKey;
}

LPCWSTR wide(const QString &text)
{
    return reinterpret_cast<LPCWSTR>(text.utf16());
}

bool writeValue(const QString &subKey, const QString &name,
                const QString &value, DWORD type = REG_SZ)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, wide(classesKey(subKey)), 0,
                        nullptr, 0, KEY_WRITE, nullptr, &key, nullptr)
        != ERROR_SUCCESS) {
        return false;
    }
    const DWORD bytes = type == REG_SZ
        ? static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)) : 0;
    const LONG result = RegSetValueExW(
        key, name.isEmpty() ? nullptr : wide(name), 0, type,
        type == REG_SZ
            ? reinterpret_cast<const BYTE *>(value.utf16()) : nullptr,
        bytes);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool writeEmptyValue(const QString &subKey, const QString &name)
{
    return writeValue(subKey, name, QString(), REG_NONE);
}

QString readValue(const QString &subKey, const QString &name)
{
    const QString keyPath = classesKey(subKey);
    std::wstring buffer(4096, L'\0');
    DWORD size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    const LONG result = RegGetValueW(
        HKEY_CURRENT_USER, wide(keyPath),
        name.isEmpty() ? nullptr : wide(name), RRF_RT_REG_SZ, nullptr,
        buffer.data(), &size);
    if (result != ERROR_SUCCESS) {
        return QString();
    }
    return QString::fromWCharArray(buffer.c_str());
}

bool deleteValue(const QString &subKey, const QString &name)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, wide(classesKey(subKey)), 0,
                      KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    const LONG result = RegDeleteValueW(
        key, name.isEmpty() ? nullptr : wide(name));
    RegCloseKey(key);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool deleteTree(const QString &subKey)
{
    const LONG result =
        RegDeleteTreeW(HKEY_CURRENT_USER, wide(classesKey(subKey)));
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool keyExists(const QString &subKey)
{
    HKEY key = nullptr;
    const LONG result = RegOpenKeyExW(HKEY_CURRENT_USER,
                                      wide(classesKey(subKey)), 0,
                                      KEY_READ, &key);
    if (key) {
        RegCloseKey(key);
    }
    return result == ERROR_SUCCESS;
}

bool valueExists(const QString &subKey, const QString &name)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, wide(classesKey(subKey)), 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    const LONG result = RegQueryValueExW(
        key, name.isEmpty() ? nullptr : wide(name), nullptr, &type,
        nullptr, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

void notifyShell()
{
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
#endif // Q_OS_WIN

#ifdef Q_OS_LINUX
// ---------------------------------------------------------------------------
// Linux: xdg-mime + ~/.config/mimeapps.list.
// Linux：xdg-mime 与 ~/.config/mimeapps.list。
// ---------------------------------------------------------------------------

QString mimeTypeFor(const QString &extension)
{
    if (extension == QLatin1String(".zip")) {
        return QStringLiteral("application/zip");
    }
    if (extension == QLatin1String(".7z")) {
        return QStringLiteral("application/x-7z-compressed");
    }
    if (extension == QLatin1String(".rar")) {
        return QStringLiteral("application/vnd.rar");
    }
    if (extension == QLatin1String(".tar")) {
        return QStringLiteral("application/x-tar");
    }
    if (extension == QLatin1String(".gz")
        || extension == QLatin1String(".tgz")
        || extension == QLatin1String(".tar.gz")) {
        return QStringLiteral("application/gzip");
    }
    if (extension == QLatin1String(".bz2")
        || extension == QLatin1String(".tbz2")
        || extension == QLatin1String(".tar.bz2")) {
        return QStringLiteral("application/x-bzip2");
    }
    if (extension == QLatin1String(".xz")
        || extension == QLatin1String(".txz")
        || extension == QLatin1String(".tar.xz")) {
        return QStringLiteral("application/x-xz");
    }
    if (extension == QLatin1String(".iso")) {
        return QStringLiteral("application/x-iso9660-image");
    }
    return QString();
}

QString mimeAppsPath()
{
    return QDir(QStandardPaths::writableLocation(
                    QStandardPaths::ConfigLocation))
        .filePath(QStringLiteral("mimeapps.list"));
}

bool runXdgMime(const QStringList &arguments)
{
    QProcess process;
    process.start(QStringLiteral("xdg-mime"), arguments);
    if (!process.waitForStarted(3000)) {
        return false;
    }
    return process.waitForFinished(8000) && process.exitCode() == 0;
}

bool queryXdgMime(const QString &mimeType)
{
    QProcess process;
    process.start(QStringLiteral("xdg-mime"),
                  {QStringLiteral("query"), QStringLiteral("default"),
                   mimeType});
    if (!process.waitForStarted(3000) || !process.waitForFinished(8000)) {
        return false;
    }
    return process.exitCode() == 0
        && QString::fromUtf8(process.readAllStandardOutput()).trimmed()
               == FileAssociations::desktopEntryFileName();
}

bool mimeAppsHasDefault(const QString &mimeType)
{
    return MimeAppsList::hasDesktopId(
        mimeAppsPath(), QStringLiteral("Default Applications"), mimeType,
        FileAssociations::desktopEntryFileName());
}

void editMimeAppsList(const QString &mimeType, bool add)
{
    // Do not use QSettings here: its INI backend writes group names as
    // "[Default%20Applications]" and escapes '/' in keys, which the desktop
    // environment cannot parse.  MimeAppsList keeps the standard syntax.
    // 不能使用 QSettings：它的 INI 后端会把组名写成 "[Default%20Applications]"
    // 并转义键中的 '/'，桌面环境无法解析；MimeAppsList 保留标准语法。
    MimeAppsList::setAssociation(mimeAppsPath(), mimeType,
                                 FileAssociations::desktopEntryFileName(),
                                 add);
}
#endif // Q_OS_LINUX

} // namespace

namespace FileAssociations {

QString desktopEntryId()
{
    return QStringLiteral("yincolor.qarchiver");
}

QString desktopEntryFileName()
{
    return QStringLiteral("yincolor.qarchiver.desktop");
}

QStringList extensions()
{
    return formatExtensions();
}

QString descriptionFor(const QString &extension)
{
    return description(extension);
}

bool isSupported()
{
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    return true;
#else
    return false;
#endif
}

bool registerFormats(const QStringList &extensionsToRegister, QString *error)
{
    if (extensionsToRegister.isEmpty()) {
        return true;
    }
#ifdef Q_OS_WIN
    Q_UNUSED(error);
    const QString command = applicationCommand();
    const QString exe = QDir::toNativeSeparators(
        QCoreApplication::applicationFilePath());
    const QString icon = QStringLiteral("\"%1\",0").arg(exe);
    for (const QString &extension : extensionsToRegister) {
        const QString progId = progIdFor(extension);
        const QString desc = description(extension);
        if (!writeValue(progId, QString(), desc)
            || !writeValue(progId + QStringLiteral("\\DefaultIcon"),
                           QString(), icon)
            || !writeValue(progId + QStringLiteral("\\shell\\open\\command"),
                           QString(), command)
            || !writeValue(extension, QString(), progId)
            || !writeEmptyValue(extension + QStringLiteral("\\OpenWithProgids"),
                                progId)) {
            if (error) {
                *error = QCoreApplication::translate(
                    "FileAssociations",
                    "Could not write the association for %1.").arg(extension);
            }
            return false;
        }
    }
    writeValue(QStringLiteral("Applications\\QArchiver.exe"),
               QStringLiteral("FriendlyAppName"),
               QStringLiteral("QArchiver (小Q解压)"));
    writeValue(QStringLiteral("Applications\\QArchiver.exe\\shell\\open\\command"),
               QString(), command);
    for (const QString &extension : extensionsToRegister) {
        writeEmptyValue(
            QStringLiteral("Applications\\QArchiver.exe\\SupportedTypes"),
            extension);
    }
    notifyShell();
    return true;
#elif defined(Q_OS_LINUX)
    for (const QString &extension : extensionsToRegister) {
        const QString mimeType = mimeTypeFor(extension);
        if (mimeType.isEmpty()) {
            continue;
        }
        const bool ok = runXdgMime(
            {QStringLiteral("default"),
             FileAssociations::desktopEntryFileName(), mimeType});
        // Keep mimeapps.list authoritative even when xdg-mime is unavailable.
        // 即使没有 xdg-mime，也直接维护 mimeapps.list。
        editMimeAppsList(mimeType, true);
        if (!ok && !mimeAppsHasDefault(mimeType)) {
            if (error) {
                *error = QCoreApplication::translate(
                    "FileAssociations",
                    "Could not register %1. Install the desktop entry and "
                    "the 'xdg-mime' tool.").arg(extension);
            }
            return false;
        }
    }
    return true;
#else
    Q_UNUSED(extensionsToRegister);
    if (error) {
        *error = QCoreApplication::translate(
            "FileAssociations", "File associations are not supported on this platform.");
    }
    return false;
#endif
}

bool unregisterFormats(const QStringList &extensionsToUnregister, QString *error)
{
    if (extensionsToUnregister.isEmpty()) {
        return true;
    }
#ifdef Q_OS_WIN
    Q_UNUSED(error);
    for (const QString &extension : extensionsToUnregister) {
        const QString progId = progIdFor(extension);
        deleteValue(extension + QStringLiteral("\\OpenWithProgids"), progId);
        if (readValue(extension, QString()) == progId) {
            deleteValue(extension, QString());
        }
        deleteTree(progId);
        deleteValue(QStringLiteral("Applications\\QArchiver.exe\\SupportedTypes"),
                    extension);
    }
    notifyShell();
    return true;
#elif defined(Q_OS_LINUX)
    Q_UNUSED(error);
    for (const QString &extension : extensionsToUnregister) {
        const QString mimeType = mimeTypeFor(extension);
        if (!mimeType.isEmpty()) {
            editMimeAppsList(mimeType, false);
        }
    }
    QProcess::startDetached(QStringLiteral("update-desktop-database"),
                            {QDir::homePath()
                                 + QStringLiteral("/.local/share/applications")});
    return true;
#else
    Q_UNUSED(extensionsToUnregister);
    if (error) {
        *error = QCoreApplication::translate(
            "FileAssociations", "File associations are not supported on this platform.");
    }
    return false;
#endif
}

bool isFormatRegistered(const QString &extension)
{
#ifdef Q_OS_WIN
    return keyExists(progIdFor(extension))
        && valueExists(extension + QStringLiteral("\\OpenWithProgids"),
                       progIdFor(extension));
#elif defined(Q_OS_LINUX)
    const QString mimeType = mimeTypeFor(extension);
    return !mimeType.isEmpty()
        && (queryXdgMime(mimeType) || mimeAppsHasDefault(mimeType));
#else
    Q_UNUSED(extension);
    return false;
#endif
}

bool openDefaultAppsSettings(QString *error)
{
#ifdef Q_OS_WIN
    if (QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("ms-settings:defaultapps")})) {
        return true;
    }
    if (QProcess::startDetached(QStringLiteral("control.exe"),
                                {QStringLiteral("/name"),
                                 QStringLiteral("Microsoft.DefaultPrograms")})) {
        return true;
    }
    if (error) {
        *error = QCoreApplication::translate(
            "FileAssociations", "Could not open the Windows default apps page.");
    }
    return false;
#else
    Q_UNUSED(error);
    return false;
#endif
}

} // namespace FileAssociations
