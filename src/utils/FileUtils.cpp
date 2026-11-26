// QArchiver / 小Q解压
// FileUtils implementation: path normalization, traversal checks and moves.
// FileUtils 实现：路径规范化、路径穿越检查与文件移动。

#include "utils/FileUtils.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QThread>
#include <QUuid>

#include <limits>

#include "common/ArchiveEntry.h"
#include "utils/EncodingDetector.h"

#ifndef Q_OS_WIN
#include <cerrno>
#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

QString rawSymlinkTarget(const QString &path)
{
#ifndef Q_OS_WIN
    const QByteArray nativePath = QFile::encodeName(path);
    QByteArray buffer(4096, Qt::Uninitialized);
    for (;;) {
        const ssize_t size = ::readlink(nativePath.constData(), buffer.data(),
                                        static_cast<size_t>(buffer.size()));
        if (size < 0) {
            return QString();
        }
        if (size < buffer.size()) {
            buffer.truncate(static_cast<int>(size));
            return QString::fromLocal8Bit(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
#else
    return QFileInfo(path).symLinkTarget();
#endif
}

bool isAbsoluteLinkTarget(const QString &target)
{
    if (target.isEmpty()) {
        return true;
    }
    if (QDir::isAbsolutePath(target)
        || target.startsWith(QLatin1Char('/'))
        || target.startsWith(QLatin1Char('\\'))) {
        return true;
    }
    static const QRegularExpression driveRe(QStringLiteral("^[A-Za-z]:"));
    return driveRe.match(target).hasMatch();
}

QStringList pathComponents(const QString &path)
{
    return path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

bool copyMetadata(const QString &src, const QString &dest)
{
    const QFileInfo info(src);
    if (info.isSymLink()) {
        return true;
    }

    QFile destFile(dest);
    if (!destFile.open(QIODevice::ReadOnly)) {
        return false;
    }
    destFile.setFileTime(info.lastModified(), QFileDevice::FileModificationTime);
    destFile.close();

    return QFile::setPermissions(dest, info.permissions());
}

// Windows can keep a just-written file locked for a moment (antivirus scanner,
// search indexer or an image viewer that already has the cache file open).
// Retry briefly instead of failing the whole extraction with a sharing
// violation.  On POSIX the first failure is returned immediately.
// Windows 可能在刚写完文件后短暂占用它（杀毒扫描、索引服务，或已经打开缓存
// 图片的查看器）。这里短暂重试，避免整个解压因共享冲突失败。
// POSIX 上首次失败即返回。
bool removeFileWithRetry(const QString &path)
{
    const auto missing = [&path]() {
        const QFileInfo info(path);
        return !info.exists() && !info.isSymLink();
    };
    if (missing()) {
        return true;
    }
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (QFile::remove(path) || missing()) {
            return true;
        }
#ifdef Q_OS_WIN
        QThread::msleep(80);
#else
        break;
#endif
    }
    return false;
}

bool renameFileWithRetry(const QString &src, const QString &dest)
{
    QFile srcFile(src);
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (srcFile.rename(dest)) {
            return true;
        }
        if (!QFileInfo::exists(src)) {
            return false;
        }
#ifdef Q_OS_WIN
        QThread::msleep(80);
#else
        break;
#endif
    }
    return false;
}

// Resolve the nearest existing ancestor of @p path to a canonical path.
// Symlinks that already exist are resolved, which is what catches a
// destination tree that pre-contains "link -> outside".
// 把 @p path 最近的已存在祖先解析为规范化路径。已存在的符号链接会被解析，
// 用它可以发现目标目录中预先存在的 "link -> outside"。
QString canonicalExistingPrefix(const QString &path)
{
    QString current = QDir::cleanPath(path);
    for (;;) {
        const QFileInfo info(current);
        if (info.exists()) {
            const QString canonical = info.canonicalFilePath();
            if (!canonical.isEmpty()) {
                return canonical;
            }
        }
        const QString parent = info.absolutePath();
        if (parent.isEmpty() || parent == current) {
            return QString();
        }
        current = parent;
    }
}

} // namespace

qint64 FileUtils::estimateFreeSpace(const QString &path)
{
    QString dir = path;
    QFileInfo fi(dir);
    if (fi.isFile()) {
        dir = fi.absolutePath();
    }

    // Walk up to the first existing directory. / 向上查找第一个存在的目录。
    while (!dir.isEmpty() && !QFileInfo::exists(dir)) {
        const QString parent = QFileInfo(dir).absolutePath();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }

    QStorageInfo storage(dir);
    if (!storage.isValid() || !storage.isReady()) {
        return -1;
    }
    return storage.bytesAvailable();
}

int FileUtils::countFiles(const QStringList &paths)
{
    int count = 0;
    for (const QString &path : paths) {
        QFileInfo fi(path);
        if (!fi.exists()) {
            continue;
        }
        if (fi.isFile()) {
            ++count;
        } else if (fi.isDir()) {
            QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                ++count;
            }
        }
    }
    return count;
}

bool FileUtils::addSizeChecked(qint64 &total, qint64 value)
{
    if (value < 0) {
        return false;
    }
    if (total > std::numeric_limits<qint64>::max() - value) {
        return false;
    }
    total += value;
    return true;
}

qint64 FileUtils::totalUnpackedSize(const QList<ArchiveEntry> &entries)
{
    qint64 total = 0;
    for (const ArchiveEntry &entry : entries) {
        if (entry.isDir) {
            continue;
        }
        if (!addSizeChecked(total, entry.size)) {
            return std::numeric_limits<qint64>::max();
        }
    }
    return total;
}

bool FileUtils::totalUnpackedSizeChecked(const QList<ArchiveEntry> &entries,
                                         qint64 *total)
{
    if (!total) {
        return false;
    }
    qint64 sum = 0;
    for (const ArchiveEntry &entry : entries) {
        if (entry.isDir) {
            continue;
        }
        if (!addSizeChecked(sum, entry.size)) {
            return false;
        }
    }
    *total = sum;
    return true;
}

bool FileUtils::removeDirectoryRecursively(const QString &path)
{
    QDir dir(path);
    return dir.removeRecursively();
}

bool FileUtils::removeDirectoryRecursivelyRaw(const QString &path)
{
#ifndef Q_OS_WIN
    const QByteArray nativePath = QFile::encodeName(path);
    if (nativePath.isEmpty()) {
        return true;
    }
    if (::access(nativePath.constData(), F_OK) != 0) {
        return true;
    }
    // FTW_DEPTH removes children before their directory; FTW_PHYS keeps
    // symlinks as links instead of following them out of the tree.
    // FTW_DEPTH 保证先删除子项再删除目录；FTW_PHYS 让符号链接按链接本身
    // 删除，而不是跟随到目录树之外。
    const auto removeEntry = [](const char *entryPath, const struct stat *,
                                int, struct FTW *) {
        return ::remove(entryPath);
    };
    return ::nftw(nativePath.constData(), removeEntry, 16,
                  FTW_DEPTH | FTW_PHYS) == 0;
#else
    return removeDirectoryRecursively(path);
#endif
}

bool FileUtils::removeTemporaryDirectory(const QString &path)
{
    // Same implementation as the raw variant: the legacy names are only
    // possible on POSIX, where Raw performs the byte-wise traversal.
    // 与 Raw 版本实现相同：旧代码页文件名只可能在 POSIX 上出现，那里 Raw
    // 会按字节遍历删除。
    return removeDirectoryRecursivelyRaw(path);
}

bool FileUtils::moveFileAtomically(const QString &src, const QString &dest)
{
    // Same-filesystem rename preserves permissions, timestamps and symlinks.
    // 同一文件系统内重命名可保留权限、时间戳和符号链接。
    if (renameFileWithRetry(src, dest)) {
        return true;
    }

    const QFileInfo srcInfo(src);
    if (srcInfo.isSymLink()) {
        const QString target = srcInfo.symLinkTarget();
        removeFileWithRetry(dest);
        if (QFile::link(target, dest)) {
            return QFile::remove(src);
        }
        return false;
    }

    // Cross-filesystem fallback: copy then delete. / 跨文件系统回退：复制后删除。
    removeFileWithRetry(dest);
    if (QFile::copy(src, dest)) {
        copyMetadata(src, dest);
        return QFile::remove(src);
    }
    return false;
}

int FileUtils::moveExtractedContents(const QString &srcDir, const QString &destDir,
                                     OverwritePolicy overwrite, bool preservePaths)
{
    QDir src(srcDir);
    QDir().mkpath(destDir);

    // Snapshot first: moving files while QDirIterator walks the tree can skip
    // entries on some filesystems.
    // 先做快照：在 QDirIterator 遍历时移动文件，某些文件系统可能漏掉条目。
    struct Item {
        QString source;
        QString relative;
        bool isDir = false;
        bool isSymLink = false;
    };
    QList<Item> items;

    QDirIterator it(srcDir,
                    QDir::Files | QDir::Dirs | QDir::System
                        | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        items.append({it.filePath(), src.relativeFilePath(it.filePath()),
                      info.isDir(), info.isSymLink()});
    }

    int failed = 0;
    for (const Item &item : items) {
        QString relative = preservePaths
            ? item.relative
            : QFileInfo(item.relative).fileName();
        if (relative.isEmpty()) {
            continue;
        }

        // Re-check at move time: the destination may contain a pre-existing
        // symlink ("link -> outside") that the pre-extraction check could not
        // see before the temporary tree was filled.
        // 移动前复查：目标目录中可能预先存在符号链接（"link -> outside"），
        // 预检在临时目录填充之前无法发现它。
        if (FileUtils::isPathTraversal(relative, destDir)) {
            ++failed;
            continue;
        }

        const QString destPath = QDir(destDir).filePath(relative);

        if (item.isDir && !item.isSymLink) {
            if (!QDir().mkpath(destPath)) {
                ++failed;
            }
            continue;
        }

        QDir().mkpath(QFileInfo(destPath).absolutePath());

        QFileInfo destInfo(destPath);
        if (destInfo.exists() || destInfo.isSymLink()) {
            switch (overwrite) {
            case OverwritePolicy::Never:
                continue;
            case OverwritePolicy::Rename: {
                const QString renamed = uniqueDestinationPath(destPath);
                QDir().mkpath(QFileInfo(renamed).absolutePath());
                if (!moveFileAtomically(item.source, renamed)) {
                    ++failed;
                }
                continue;
            }
            case OverwritePolicy::Ask:
            case OverwritePolicy::Always:
                if (destInfo.isDir() && !destInfo.isSymLink()) {
                    removeDirectoryRecursively(destPath);
                } else {
                    removeFileWithRetry(destPath);
                }
                break;
            }
        }

        if (!moveFileAtomically(item.source, destPath)) {
            ++failed;
        }
    }

    // Keep the temporary directory when something failed so the caller can
    // report/retry instead of silently deleting extracted data.
    // 失败时保留临时目录，便于调用方报告或重试，而不是静默删除已解压数据。
    if (failed == 0) {
        // The staging tree may still hold entries skipped by OverwritePolicy
        // and legacy-codepage names, so use the temporary-tree remover.
        // 暂存目录可能仍包含被 OverwritePolicy 跳过以及旧代码页名称的条目，
        // 因此使用临时目录清理函数。
        removeTemporaryDirectory(srcDir);
    }
    return failed;
}

QString FileUtils::createTempExtractDir(const QString &destDir)
{
    const QString uuid = QUuid::createUuid().toString(QUuid::Id128);
    const QString tempName = QStringLiteral(".tmp_extract_%1").arg(uuid);
    const QString tempPath = QDir(destDir).absoluteFilePath(tempName);
    QDir().mkpath(tempPath);
    return tempPath;
}

QString FileUtils::normalizeEntryPath(const QString &path)
{
    QString normalized = path;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));

    // Strip a Windows drive prefix ("C:"). / 去掉 Windows 盘符前缀（"C:"）。
    if (normalized.size() >= 2 && normalized.at(1) == QLatin1Char(':')) {
        normalized = normalized.mid(2);
    }

    normalized = QDir::cleanPath(normalized);
    while (normalized.startsWith(QLatin1Char('/'))) {
        normalized = normalized.mid(1);
    }
    return normalized;
}

bool FileUtils::isPathTraversal(const QString &entryPath, const QString &destDir)
{
    const QString normalized = normalizeEntryPath(entryPath);
    if (normalized.isEmpty()) {
        return false;
    }

    // Reject any ".." path component. Substring matching would incorrectly
    // reject legitimate names such as "foo..bar".
    // 拒绝任何 ".." 路径分量。若用子串匹配，会误判 "foo..bar" 这类合法名称。
    const QStringList components = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &component : components) {
        if (component == QStringLiteral("..")) {
            return true;
        }
    }

    const QString cleanDest = QDir::cleanPath(QDir(destDir).absolutePath());
    const QString cleanResolved = QDir::cleanPath(QDir(cleanDest).absoluteFilePath(normalized));

    if (cleanResolved == cleanDest) {
        return false;
    }
    if (!cleanResolved.startsWith(cleanDest + QLatin1Char('/'))) {
        return true;
    }

    const QString canonicalDest = canonicalExistingPrefix(cleanDest);

    // If the target already exists, also compare canonical paths to catch
    // symlinked parent directories that escape the destination.
    // 目标已存在时再比较规范化路径，以捕获通过符号链接父目录逃逸的情况。
    const QFileInfo resolvedInfo(cleanResolved);
    if (resolvedInfo.exists()) {
        const QString canonicalResolved = resolvedInfo.canonicalFilePath();
        if (!canonicalDest.isEmpty() && !canonicalResolved.isEmpty()
            && canonicalResolved != canonicalDest
            && !canonicalResolved.startsWith(canonicalDest + QLatin1Char('/'))) {
            return true;
        }
    }

    // Check the deepest existing ancestor of the parent directory as well.
    // Without it a pre-existing "dest/link -> outside" passes the check while
    // the move would follow the link and write outside the destination.
    // 还要检查父目录中最深的已存在祖先。否则预先存在的 "dest/link -> outside"
    // 会让检查通过，而移动时却跟随链接写到目标目录之外。
    const QString parentPath = resolvedInfo.absolutePath();
    const QString canonicalParent = canonicalExistingPrefix(parentPath);
    if (!canonicalDest.isEmpty() && !canonicalParent.isEmpty()
        && canonicalParent != canonicalDest
        && !canonicalParent.startsWith(canonicalDest + QLatin1Char('/'))) {
        return true;
    }

    return false;
}


bool FileUtils::isSafeSymlinkTarget(const QString &entryPath,
                                    const QString &target)
{
    if (isAbsoluteLinkTarget(target)) {
        return false;
    }

    const QString normalizedEntry = normalizeEntryPath(entryPath);
    const int slash = normalizedEntry.lastIndexOf(QLatin1Char('/'));
    const QString parent = slash < 0 ? QString() : normalizedEntry.left(slash);

    // A relative link resolves from its parent directory.  Walk the target's
    // components and reject any ".." that would climb above the archive root.
    int depth = pathComponents(parent).size();
    QString normalizedTarget = target;
    normalizedTarget.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QStringList components = pathComponents(normalizedTarget);
    for (const QString &component : components) {
        if (component == QStringLiteral("..")) {
            if (--depth < 0) {
                return false;
            }
        } else if (component != QStringLiteral(".")) {
            ++depth;
        }
    }
    return true;
}

QStringList FileUtils::unsafeExtractedSymlinks(const QString &srcDir)
{
    QStringList unsafe;
    const QString root = QDir(srcDir).absolutePath();
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    if (canonicalRoot.isEmpty()) {
        return unsafe;
    }

    QDirIterator it(root,
                    QDir::AllEntries | QDir::Hidden | QDir::System
                        | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (!info.isSymLink()) {
            continue;
        }

        const QString relative = QDir(root).relativeFilePath(path);
        const QString target = rawSymlinkTarget(path);
        if (!isSafeSymlinkTarget(relative, target)) {
            unsafe << normalizeEntryPath(relative);
            continue;
        }

        // Follow an existing target chain and compare canonical paths.  A
        // dangling target is allowed: it may be extracted later in the archive.
        const QString targetPath = QDir::cleanPath(
            QDir(info.absolutePath()).absoluteFilePath(target));
        const QString resolvedRelative = QDir(root).relativeFilePath(targetPath);
        if (resolvedRelative == QStringLiteral("..")
            || resolvedRelative.startsWith(QStringLiteral("../"))) {
            unsafe << normalizeEntryPath(relative);
            continue;
        }

        const QFileInfo targetInfo(targetPath);
        if (targetInfo.exists() || targetInfo.isSymLink()) {
            const QString canonicalTarget = targetInfo.canonicalFilePath();
            if (!canonicalTarget.isEmpty()
                && canonicalTarget != canonicalRoot
                && !canonicalTarget.startsWith(canonicalRoot + QLatin1Char('/'))) {
                unsafe << normalizeEntryPath(relative);
            }
        }
    }
    return unsafe;
}

bool FileUtils::isProcessRunning(qint64 pid)
{
    if (pid <= 0) {
        return false;
    }
#ifdef Q_OS_WIN
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                static_cast<DWORD>(pid));
    if (!handle) {
        // Access denied usually means another user's process; a stale cache
        // directory is safer to remove than to leak forever.
        // 拒绝访问通常意味着其他用户的进程；清理过期缓存目录比永久泄漏更安全。
        return false;
    }
    DWORD exitCode = 0;
    const bool alive = GetExitCodeProcess(handle, &exitCode)
        && exitCode == STILL_ACTIVE;
    CloseHandle(handle);
    return alive;
#else
    return QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid));
#endif
}

QString FileUtils::uniqueDestinationPath(const QString &path)
{
    const QFileInfo info(path);
    const QString dir = info.absolutePath();
    const QString name = info.fileName();

    const int dot = name.lastIndexOf(QLatin1Char('.'));
    const QString base = dot > 0 ? name.left(dot) : name;
    const QString suffix = dot > 0 ? name.mid(dot) : QString();

    for (int index = 1; index < 100000; ++index) {
        const QString candidate = QDir(dir).filePath(
            QStringLiteral("%1 (%2)%3").arg(base).arg(index).arg(suffix));
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return path;
}

bool FileUtils::stageFileForArchiving(const QString &sourceFile,
                                      const QString &stagingRoot,
                                      const QString &entryPath,
                                      const QString &encoding,
                                      QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    if (sourceFile.isEmpty() || stagingRoot.isEmpty()) {
        return fail(QStringLiteral("missing source file or staging root"));
    }
    const QString normalized = normalizeEntryPath(entryPath);
    if (normalized.isEmpty()
        || normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts)
               .contains(QStringLiteral(".."))) {
        return fail(QStringLiteral("unsafe archive member path: ") + entryPath);
    }
    if (!QFileInfo::exists(sourceFile)) {
        return fail(QStringLiteral("edited file no longer exists: ") + sourceFile);
    }

    const bool utf8Name = encoding.isEmpty()
        || encoding.compare(QStringLiteral("UTF-8"), Qt::CaseInsensitive) == 0;

#ifdef Q_OS_WIN
    // 7z.exe converts the listfile name from the requested code page to UTF-16
    // and looks the file up by that Unicode name, so the staged copy keeps the
    // ordinary (Unicode) path.
    // 7z.exe 会把列表文件中的名称从指定代码页转换为 UTF-16，并按该 Unicode
    // 名称查找文件，因此暂存副本使用普通的（Unicode）路径。
    Q_UNUSED(utf8Name);
    const QString destination = QDir(stagingRoot).filePath(normalized);
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        return fail(QStringLiteral("cannot create staging directory"));
    }
    QFile::remove(destination);
    if (!QFile::copy(sourceFile, destination)) {
        return fail(QStringLiteral("cannot stage the edited file"));
    }
    return true;
#else
    if (utf8Name) {
        const QString destination = QDir(stagingRoot).filePath(normalized);
        if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
            return fail(QStringLiteral("cannot create staging directory"));
        }
        QFile::remove(destination);
        if (!QFile::copy(sourceFile, destination)) {
            return fail(QStringLiteral("cannot stage the edited file"));
        }
        return true;
    }

    // Legacy code page: build the member name as raw bytes so 7-Zip finds a
    // file whose name matches the bytes stored in the archive.
    // 旧代码页：按原始字节构造成员名，使 7-Zip 找到名称与该压缩包内存储字节
    // 一致的文件。
    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return fail(QStringLiteral("empty archive member path"));
    }

    QByteArray directory = QFile::encodeName(stagingRoot);
    for (int index = 0; index + 1 < parts.size(); ++index) {
        const QByteArray part = EncodingDetector::encode(parts.at(index), encoding);
        if (part.isEmpty()) {
            return fail(QStringLiteral("cannot encode path component: ")
                        + parts.at(index));
        }
        directory += '/';
        directory += part;
        if (::mkdir(directory.constData(), 0700) != 0 && errno != EEXIST) {
            return fail(QStringLiteral("cannot create staging directory"));
        }
    }

    const QByteArray fileName = EncodingDetector::encode(parts.last(), encoding);
    if (fileName.isEmpty()) {
        return fail(QStringLiteral("cannot encode file name: ") + parts.last());
    }
    QByteArray destination = directory;
    destination += '/';
    destination += fileName;

    const QByteArray nativeSource = QFile::encodeName(sourceFile);
    ::unlink(destination.constData());
    if (::link(nativeSource.constData(), destination.constData()) == 0) {
        return true;
    }

    // Different file systems: fall back to a byte copy through a raw path.
    QFile source(sourceFile);
    if (!source.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("cannot read the edited file"));
    }
    const int fd = ::open(destination.constData(),
                          O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return fail(QStringLiteral("cannot create the staged copy"));
    }
    bool copied = true;
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(1 << 20);
        if (chunk.isEmpty()) {
            break;
        }
        const char *data = chunk.constData();
        qint64 remaining = chunk.size();
        while (remaining > 0) {
            const ssize_t written = ::write(fd, data, static_cast<size_t>(remaining));
            if (written <= 0) {
                copied = false;
                break;
            }
            data += written;
            remaining -= written;
        }
        if (!copied) {
            break;
        }
    }
    ::close(fd);
    if (!copied) {
        ::unlink(destination.constData());
        return fail(QStringLiteral("cannot copy the edited file"));
    }
    return true;
#endif
}
