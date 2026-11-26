#ifndef QARCHIVER_COMMON_FORMATREGISTRY_H
#define QARCHIVER_COMMON_FORMATREGISTRY_H

// QArchiver / 小Q解压
// Single source of truth for archive file-name suffixes.
// 压缩包文件名后缀的唯一事实来源。

#include <QString>
#include <QStringList>

#include "common/ArchiveConstants.h"

// Archive suffix registry shared by format detection, the file-type icons and
// the UI (open filters, one-click destination names, compress dialog).
// Keeping the suffix knowledge in one place avoids lists that silently drift
// apart when a format is added.
// 格式检测、文件类型图标与界面（打开过滤器、一键解压目录名、压缩对话框）
// 共用的后缀注册表。把后缀知识集中在一处，避免新增格式时多份列表不一致。
namespace FormatRegistry {

// Lower-cased effective extension including the dot:
// "a.TAR.GZ" -> ".tar.gz", "a.7z.001" -> ".7z.001", "a.part2.rar" -> ".partNN.rar".
// 小写化的有效扩展名（含点）。
QString extensionOf(const QString &filePath);

// Same as extensionOf() without the leading dot:
// "a.tar.gz" -> "tar.gz", "a.png" -> "png", "a" -> "".
// 同 extensionOf()，但不含点。
QString kindSuffix(const QString &filePath);

// Suffix-only format detection (no magic-byte inspection).
// 仅按后缀判定格式（不做魔数检查）。
ArchiveFormat formatFromSuffix(const QString &filePath);

// Parse a format name used by the command line and the Explorer context menu
// ("zip", "7z", "tar", "tar.gz", "tgz", ...).  A leading dot and the
// case are ignored; unknown names return ArchiveFormat::Unknown.
// 解析命令行与资源管理器右键菜单使用的格式名（"zip"、"7z"、"tar"、
// "tar.gz"、"tgz" 等）。忽略前导点与大小写；未知名称返回
// ArchiveFormat::Unknown。
ArchiveFormat formatFromName(const QString &name);

// Whether the path uses a split-volume naming scheme (.7z.001, .part1.rar, .r00).
// 路径是否使用分卷命名方案。
bool isSplitVolume(const QString &filePath);

// Whether the path looks like an archive QArchiver can open.
// 路径是否看起来是 QArchiver 可以打开的压缩包。
bool isArchivePath(const QString &filePath);

// Remove the archive suffix from a file name ("a.tar.gz" -> "a",
// "a.7z.001" -> "a").  Returns the input unchanged when no suffix matches.
// 去掉文件名中的压缩包后缀（"a.tar.gz" -> "a"、"a.7z.001" -> "a"）。
// 没有匹配后缀时原样返回。
QString stripArchiveExtension(const QString &fileName);

// Broader "archive-like" suffixes used for file-type icons; includes formats
// QArchiver cannot open itself (cab/arj/ace/...).  Entries have no leading dot.
// 用于文件类型图标的更宽泛“压缩包类”后缀；包含 QArchiver 自身无法打开的
// 格式（cab/arj/ace/...）。条目不含点。
const QStringList &archiveLikeSuffixes();

// Glob patterns of every format QArchiver can open, including split-volume
// patterns, for file dialogs.  Keeping this next to the suffix registry makes
// the Open dialog follow new formats automatically.
// QArchiver 可打开格式的 glob 模式（含分卷），供文件对话框使用。放在后缀
// 注册表旁边，使新增格式时打开对话框自动跟随。
const QStringList &openGlobPatterns();

// Suffixes (with leading dot) of every format QArchiver can open, suitable for
// desktop/file-association lists (no split-volume patterns).
// QArchiver 可打开格式的后缀（含点），供桌面/文件关联列表使用（不含分卷模式）。
const QStringList &associationSuffixes();

} // namespace FormatRegistry

#endif // QARCHIVER_COMMON_FORMATREGISTRY_H
