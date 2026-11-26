// QArchiver / 小Q解压
// FileListModel node construction, sorting and entry lookup.
// FileListModel 的节点构建、排序与条目查找。

#include "ui/icons/IconUtils.h"
#include "ui/theme/ThemeManager.h"
#include "ui/models/FileListModel.h"

#include "ui/icons/FileTypeHelper.h"
#include "ui/services/ThumbnailManager.h"
#include "utils/FileUtils.h"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QIcon>
#include <QLocale>

#include <algorithm>

FileListModel::FileListModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    m_root = new Node;
    m_root->synthetic = true;
}

FileListModel::~FileListModel()
{
    clear();
    delete m_root;
    m_root = nullptr;
}

void FileListModel::deleteNode(Node *node)
{
    if (!node) {
        return;
    }
    for (Node *child : node->children) {
        deleteNode(child);
    }
    delete node;
}

void FileListModel::clear()
{
    for (Node *child : m_root->children) {
        deleteNode(child);
    }
    m_root->children.clear();
    m_nodes.clear();
}

QModelIndex FileListModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column < 0 || column >= ColumnCount || row < 0) {
        return QModelIndex();
    }
    if (m_flatMode && parent.isValid()) {
        return QModelIndex();
    }

    const Node *parentNode = parent.isValid()
        ? static_cast<const Node *>(parent.internalPointer())
        : m_root;
    if (!parentNode || row >= parentNode->children.size()) {
        return QModelIndex();
    }

    return createIndex(row, column, parentNode->children.at(row));
}

QModelIndex FileListModel::parent(const QModelIndex &child) const
{
    if (!child.isValid() || m_flatMode) {
        return QModelIndex();
    }

    const Node *node = static_cast<const Node *>(child.internalPointer());
    if (!node || !node->parent || node->parent == m_root) {
        return QModelIndex();
    }

    const Node *parentNode = node->parent;
    const Node *grandParent = parentNode->parent;
    if (!grandParent) {
        return QModelIndex();
    }

    const int row = grandParent->children.indexOf(const_cast<Node *>(parentNode));
    if (row < 0) {
        return QModelIndex();
    }
    return createIndex(row, 0, const_cast<Node *>(parentNode));
}

int FileListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0 || (m_flatMode && parent.isValid())) {
        return 0;
    }
    const Node *node = parent.isValid()
        ? static_cast<const Node *>(parent.internalPointer())
        : m_root;
    return node ? node->children.size() : 0;
}

int FileListModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return ColumnCount;
}

QString FileListModel::displayName(const Node *node) const
{
    if (!node) {
        return QString();
    }
    if (node->isParent) {
        return QStringLiteral("..");
    }
    if (node->entry.path.isEmpty()) {
        return QString();
    }
    // The tree is keyed by the normalized path.  Use that form for the
    // displayed basename as well; entry.path keeps the original archive
    // member name (e.g. "./foo" or a Windows-style "dir\\file").
    // 树以规范化路径为键，显示的基本名称也使用该形式；
    // entry.path 保留原始压缩包成员名（如 "./foo" 或 Windows 风格的 "dir\\file"）。
    const QString normalized = FileUtils::normalizeEntryPath(node->entry.path);
    const int slash = normalized.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? normalized.mid(slash + 1) : normalized;
}

QVariant FileListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }

    const Node *node = static_cast<const Node *>(index.internalPointer());
    if (!node) {
        return {};
    }
    const ArchiveEntry &entry = node->entry;

    switch (role) {
    case PathRole:
        return entry.path;
    case IsDirRole:
        return entry.isDir;
    case IsParentRole:
        return node->isParent;
    case IsThumbnailRole:
        return index.column() == NameColumn && shouldShowThumbnail(node);
    case Qt::ToolTipRole:
        return node->isParent ? tr("Parent directory") : entry.path;
    case Qt::TextAlignmentRole:
        if (index.column() == SizeColumn || index.column() == PackedSizeColumn) {
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        }
        return {};
    case Qt::DecorationRole:
        if (index.column() == NameColumn) {
            const bool modern = ThemeManager::isModernLayout();
            if (node->isParent) {
                return modern ? Icons::modern(QStringLiteral("up.png"))
                              : Icons::aero(QStringLiteral("up.ico"));
            }
            if (shouldShowThumbnail(node)) {
                if (!m_localDirectory.isEmpty()) {
                    return ThumbnailManager::instance().localIcon(
                        QDir(m_localDirectory).filePath(entry.path),
                        entry.size, entry.mtime);
                }
                return ThumbnailManager::instance().icon(
                    m_archivePath, m_archiveFormat, entry.path, entry.size,
                    m_archivePassword);
            }
            return FileTypeHelper::icon(displayName(node), entry.isDir, modern);
        }
        return {};
    case Qt::FontRole:
        // The modern HaoZip-style list keeps folders at the normal weight;
        // the classic Explorer-like list uses bold directories.
        // 现代好压风格列表中的文件夹保持正常字重；经典资源管理器风格加粗。
        if (entry.isDir && !ThemeManager::isModernLayout()) {
            QFont font;
            font.setBold(true);
            return font;
        }
        return {};
    case Qt::DisplayRole:
        switch (index.column()) {
        case NameColumn:
            return displayName(node);
        case SizeColumn:
            return entry.isDir ? QString() : QLocale().formattedDataSize(entry.size);
        case PackedSizeColumn:
            if (entry.isDir) {
                return QString();
            }
            // Some read backends do not expose the compressed size; show a
            // dash instead of a misleading "0 bytes" for non-empty files.
            // 某些读取后端不提供压缩后大小；对非空文件显示 “-”，避免误报 0 字节。
            return (entry.size > 0 && entry.packedSize == 0)
                ? QStringLiteral("-")
                : QLocale().formattedDataSize(entry.packedSize);
        case TypeColumn:
            return node->isParent
                ? QString()
                : FileTypeHelper::description(displayName(node), entry.isDir);
        case ModifiedColumn:
            return entry.mtime.isValid()
                ? entry.mtime.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                : QString();
        case EncryptedColumn:
            return entry.isEncrypted ? tr("Yes") : QString();
        default:
            break;
        }
        break;
    default:
        break;
    }

    return {};
}

QVariant FileListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal) {
        return {};
    }
    if (role == Qt::TextAlignmentRole
        && (section == SizeColumn || section == PackedSizeColumn)) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case NameColumn:       return tr("Name");
    case SizeColumn:       return tr("Size");
    case PackedSizeColumn: return tr("Packed");
    case TypeColumn:       return tr("Type");
    case ModifiedColumn:   return tr("Modified");
    case EncryptedColumn:  return tr("Encrypted");
    default:               break;
    }
    return {};
}

Qt::ItemFlags FileListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    // Real entries can be dragged out of the view; the synthetic ".." row in
    // list mode cannot (it is navigation only).
    // 真实条目可从视图拖出；列表模式中的合成 ".." 行不可拖出（仅用于导航）。
    if (!index.data(IsParentRole).toBool()) {
        flags |= Qt::ItemIsDragEnabled;
    }
    return flags;
}

FileListModel::Node *FileListModel::ensureDirectory(Node *parent, const QString &path,
                                                    const QString &name)
{
    auto it = m_nodes.constFind(path);
    if (it != m_nodes.constEnd()) {
        return it.value();
    }

    auto *node = new Node;
    node->parent = parent;
    node->entry.path = path;
    node->entry.isDir = true;
    node->synthetic = true;
    node->entry.size = 0;
    node->entry.packedSize = 0;
    Q_UNUSED(name);
    parent->children.append(node);
    m_nodes.insert(path, node);
    return node;
}

FileListModel::Node *FileListModel::findNode(const QString &path) const
{
    return m_nodes.value(path, nullptr);
}

void FileListModel::setArchiveContext(const QString &archivePath,
                                      ArchiveFormat format,
                                      const QString &password)
{
    m_archivePath = archivePath;
    m_archiveFormat = format;
    m_archivePassword = password;
    setLocalContext(QString());
}

void FileListModel::setLocalContext(const QString &directory)
{
    // Context changes are always followed by setEntries() in the local-browser
    // flow.  Do not emit dataChanged here: the old rows still refer to the
    // previous directory and must not request thumbnails from the new one.
    // 本地浏览流程中上下文变化后总会调用 setEntries()。这里不能发 dataChanged：
    // 旧行仍属于上一个目录，不能按新目录去请求缩略图。
    m_localDirectory = directory;
}

void FileListModel::refreshThumbnail(const QString &entryPath)
{
    const QModelIndex index = indexForPath(entryPath);
    if (!index.isValid()) {
        return;
    }
    emit dataChanged(index, index, {Qt::DecorationRole});
}

void FileListModel::setEntries(const QList<ArchiveEntry> &entries)
{
    beginResetModel();
    m_sourceEntries = entries;
    buildDirectoryIndex();
    if (!m_currentDir.isEmpty() && !directoryExists(m_currentDir)) {
        m_currentDir.clear();
    }
    rebuild();
    endResetModel();
}

void FileListModel::buildDirectoryIndex()
{
    m_childrenByDir.clear();
    m_directoryPaths.clear();
    m_implicitDirectoryPaths.clear();

    QSet<QString> explicitPaths;
    QSet<QString> explicitDirectories;
    for (const ArchiveEntry &entry : m_sourceEntries) {
        QString originalPath = entry.path;
        while (originalPath.endsWith(QLatin1Char('/'))) {
            originalPath.chop(1);
        }
        const QString path = FileUtils::normalizeEntryPath(originalPath);
        if (path.isEmpty() || path == QStringLiteral(".")) {
            continue;
        }
        explicitPaths.insert(path);
        if (entry.isDir) {
            explicitDirectories.insert(path);
        }

        const QStringList components =
            path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString ancestor;
        for (int i = 0; i + 1 < components.size(); ++i) {
            ancestor = ancestor.isEmpty()
                ? components.at(i)
                : ancestor + QLatin1Char('/') + components.at(i);
            m_directoryPaths.insert(ancestor);
        }
    }
    m_directoryPaths.unite(explicitDirectories);

    // Direct children, preserving duplicates and the original member path.
    for (const ArchiveEntry &entry : m_sourceEntries) {
        QString originalPath = entry.path;
        while (originalPath.endsWith(QLatin1Char('/'))) {
            originalPath.chop(1);
        }
        const QString path = FileUtils::normalizeEntryPath(originalPath);
        if (path.isEmpty() || path == QStringLiteral(".")) {
            continue;
        }
        ArchiveEntry copy = entry;
        copy.path = originalPath;
        const int slash = path.lastIndexOf(QLatin1Char('/'));
        const QString parent = slash < 0 ? QString() : path.left(slash);
        m_childrenByDir[parent].append(copy);
    }

    // Synthetic directory rows for implicit parents.
    QSet<QString> added;
    for (const QString &dir : m_directoryPaths) {
        if (dir.isEmpty() || explicitPaths.contains(dir) || added.contains(dir)) {
            continue;
        }
        added.insert(dir);
        m_implicitDirectoryPaths.insert(dir);
        ArchiveEntry synthetic;
        synthetic.path = dir;
        synthetic.isDir = true;
        const int slash = dir.lastIndexOf(QLatin1Char('/'));
        const QString parent = slash < 0 ? QString() : dir.left(slash);
        m_childrenByDir[parent].append(synthetic);
    }
}

void FileListModel::setFlatMode(bool flat)
{
    if (m_flatMode == flat) {
        return;
    }
    beginResetModel();
    m_flatMode = flat;
    // Starting list mode always begins at the archive root.  Returning to
    // tree mode also resets the navigation cursor.
    // 进入列表模式时总是从压缩包根目录开始；返回树模式也会重置导航位置。
    m_currentDir.clear();
    rebuild();
    endResetModel();
}

void FileListModel::setThumbnailsEnabled(bool enabled)
{
    if (m_thumbnailsEnabled == enabled) {
        return;
    }
    m_thumbnailsEnabled = enabled;
    notifyThumbnailChanged();
}

bool FileListModel::shouldShowThumbnail(const Node *node) const
{
    if (!m_thumbnailsEnabled || !node || node->isParent) {
        return false;
    }
    const ArchiveEntry &entry = node->entry;
    const bool hasContext = !m_archivePath.isEmpty()
        || !m_localDirectory.isEmpty();
    return !entry.isDir && hasContext
        && FileTypeHelper::kindFor(displayName(node), false)
               == FileTypeHelper::Kind::Image
        && (!entry.isEncrypted || !m_archivePassword.isEmpty());
}

void FileListModel::notifyThumbnailChanged(const QModelIndex &parent)
{
    const int rows = rowCount(parent);
    if (rows <= 0) {
        return;
    }
    emit dataChanged(index(0, NameColumn, parent),
                     index(rows - 1, NameColumn, parent),
                     {Qt::DecorationRole, IsThumbnailRole});
    for (int row = 0; row < rows; ++row) {
        const QModelIndex child = index(row, NameColumn, parent);
        if (rowCount(child) > 0) {
            notifyThumbnailChanged(child);
        }
    }
}

void FileListModel::setShowParentEntry(bool show)
{
    if (m_showParentEntry == show) {
        return;
    }
    m_showParentEntry = show;
    // The flag only affects flat mode; tree mode is left untouched.
    // 该开关只影响扁平模式，树模式无需重建。
    if (!m_flatMode) {
        return;
    }
    beginResetModel();
    rebuild();
    endResetModel();
}

bool FileListModel::directoryExists(const QString &dir) const
{
    if (dir.isEmpty()) {
        return true;
    }
    return m_directoryPaths.contains(FileUtils::normalizeEntryPath(dir));
}

bool FileListModel::enterDirectory(const QString &path)
{
    if (!m_flatMode) {
        return false;
    }
    QString dir = FileUtils::normalizeEntryPath(path);
    if (!dir.isEmpty() && dir != QStringLiteral(".") && !directoryExists(dir)
        && !m_currentDir.isEmpty()) {
        // Also accept a child name relative to the currently displayed
        // directory (handy for tests and future breadcrumb widgets).
        // 也接受相对于当前显示目录的子项名称（便于测试及未来的面包屑控件）。
        const QString candidate =
            FileUtils::normalizeEntryPath(m_currentDir + QLatin1Char('/') + dir);
        if (directoryExists(candidate)) {
            dir = candidate;
        }
    }
    if (dir.isEmpty() || dir == QStringLiteral(".")) {
        if (m_currentDir.isEmpty()) {
            return true;
        }
        beginResetModel();
        m_currentDir.clear();
        rebuild();
        endResetModel();
        return true;
    }
    if (!directoryExists(dir) || m_currentDir == dir) {
        return false;
    }
    beginResetModel();
    m_currentDir = dir;
    rebuild();
    endResetModel();
    return true;
}

bool FileListModel::goUp()
{
    if (!m_flatMode || m_currentDir.isEmpty()) {
        return false;
    }
    const int slash = m_currentDir.lastIndexOf(QLatin1Char('/'));
    beginResetModel();
    m_currentDir = slash >= 0 ? m_currentDir.left(slash) : QString();
    rebuild();
    endResetModel();
    return true;
}

void FileListModel::rebuild()
{
    clear();

    if (m_flatMode) {
        if (m_showParentEntry && !m_currentDir.isEmpty()) {
            auto *up = new Node;
            up->parent = m_root;
            up->entry.path = QStringLiteral("..");
            up->entry.isDir = true;
            up->synthetic = true;
            up->isParent = true;
            m_root->children.append(up);
        }

        const QList<ArchiveEntry> children =
            m_childrenByDir.value(m_currentDir);
        for (const ArchiveEntry &entry : children) {
            if (entry.path.isEmpty()) {
                continue;
            }
            auto *node = new Node;
            node->parent = m_root;
            node->entry = entry;
            const QString normalized = FileUtils::normalizeEntryPath(entry.path);
            node->synthetic = entry.isDir
                && m_implicitDirectoryPaths.contains(normalized);
            m_root->children.append(node);
            m_nodes.insert(normalized, node);
        }
        sortFlatNodes();
        return;
    }

    for (ArchiveEntry entry : m_sourceEntries) {
        QString originalPath = entry.path;
        while (originalPath.endsWith(QLatin1Char('/'))) {
            originalPath.chop(1);
        }
        if (originalPath.isEmpty()) {
            continue;
        }

        const QString path = FileUtils::normalizeEntryPath(originalPath);
        if (path.isEmpty() || path == QStringLiteral(".")) {
            continue;
        }

        // Keep the archive's original member name (for example a leading
        // "./" or a literal backslash); extraction tools match that name
        // exactly.  The normalized path is only used as the tree key.
        // 保留压缩包原始成员名（例如前导 "./" 或字面反斜杠）；
        // 解压工具会精确匹配该名称。规范化路径仅用作树键。
        entry.path = originalPath;

        const QStringList components = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (components.isEmpty()) {
            continue;
        }

        Node *parent = m_root;
        QString currentPath;
        for (int i = 0; i < components.size() - 1; ++i) {
            currentPath = currentPath.isEmpty()
                ? components.at(i)
                : currentPath + QLatin1Char('/') + components.at(i);
            parent = ensureDirectory(parent, currentPath, components.at(i));
        }

        Node *node = findNode(path);
        if (!node) {
            node = new Node;
            node->parent = parent;
            node->entry = entry;
            parent->children.append(node);
            m_nodes.insert(path, node);
        } else {
            node->entry = entry;
            node->synthetic = false;
        }
    }
}


void FileListModel::sortFlat(int column, Qt::SortOrder order)
{
    if (column < 0 || column >= ColumnCount) {
        column = NameColumn;
    }
    m_flatSortColumn = column;
    m_flatSortOrder = order;
    if (!m_flatMode) {
        return;
    }

    beginResetModel();
    sortFlatNodes();
    endResetModel();
}

void FileListModel::sortFlatNodes()
{
    if (!m_flatMode || m_root->children.size() < 2) {
        return;
    }

    int first = 0;
    if (!m_root->children.isEmpty() && m_root->children.first()->isParent) {
        first = 1;
    }
    if (first >= m_root->children.size()) {
        return;
    }

    auto compare = [this](const Node *a, const Node *b) -> int {
        switch (m_flatSortColumn) {
        case NameColumn:
            return QString::compare(displayName(a), displayName(b),
                                    Qt::CaseInsensitive);
        case SizeColumn:
            if (a->entry.size == b->entry.size) {
                return 0;
            }
            return a->entry.size < b->entry.size ? -1 : 1;
        case PackedSizeColumn:
            if (a->entry.packedSize == b->entry.packedSize) {
                return 0;
            }
            return a->entry.packedSize < b->entry.packedSize ? -1 : 1;
        case TypeColumn:
            return QString::compare(
                FileTypeHelper::description(displayName(a), a->entry.isDir),
                FileTypeHelper::description(displayName(b), b->entry.isDir),
                Qt::CaseInsensitive);
        case ModifiedColumn:
            if (a->entry.mtime == b->entry.mtime) {
                return 0;
            }
            return a->entry.mtime < b->entry.mtime ? -1 : 1;
        case EncryptedColumn:
            return int(a->entry.isEncrypted) - int(b->entry.isEncrypted);
        default:
            return 0;
        }
    };

    std::stable_sort(m_root->children.begin() + first, m_root->children.end(),
                     [this, &compare](const Node *a, const Node *b) {
        int result = compare(a, b);
        if (result == 0) {
            // Stable, deterministic tie-breaker independent of sort order.
            // 与排序方向无关的稳定确定性决胜规则。
            result = QString::compare(displayName(a), displayName(b),
                                      Qt::CaseInsensitive);
        }
        return m_flatSortOrder == Qt::AscendingOrder ? result < 0 : result > 0;
    });
}

QStringList FileListModel::allPaths() const
{
    QStringList paths;
    paths.reserve(m_nodes.size());
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        paths << it.key();
    }
    paths.sort();
    return paths;
}

const ArchiveEntry *FileListModel::entryAt(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return nullptr;
    }
    const Node *node = static_cast<const Node *>(index.internalPointer());
    return node ? &node->entry : nullptr;
}

QModelIndex FileListModel::indexForPath(const QString &path) const
{
    const Node *node = findNode(FileUtils::normalizeEntryPath(path));
    if (!node || !node->parent) {
        return QModelIndex();
    }
    const Node *parentNode = node->parent;
    const int row = parentNode->children.indexOf(const_cast<Node *>(node));
    if (row < 0) {
        return QModelIndex();
    }
    return createIndex(row, 0, const_cast<Node *>(node));
}
