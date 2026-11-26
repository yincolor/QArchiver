#ifndef FILELISTMODEL_H
#define FILELISTMODEL_H

// QArchiver / 小Q解压
// Tree model that exposes archive entries to the view.
// 向视图暴露压缩包条目的树模型。

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QSet>

#include "common/ArchiveConstants.h"
#include "common/ArchiveEntry.h"

// Hierarchical model for archive entries (design doc 6).
// Builds synthetic parent directories so archives that only contain flat
// "dir/file" records still show a browsable tree.
// 压缩包条目的层次模型（设计文档 6）。
// 为只包含扁平 "dir/file" 记录的压缩包构建合成父目录，
// 使其仍能以可浏览的树形展示。
class FileListModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Columns {
        NameColumn = 0,
        SizeColumn,
        PackedSizeColumn,
        TypeColumn,
        ModifiedColumn,
        EncryptedColumn,
        ColumnCount
    };

    enum Roles {
        PathRole = Qt::UserRole, // full internal path
        IsDirRole,               // bool
        IsParentRole,            // bool: synthetic ".." entry in list mode
        IsThumbnailRole          // bool: DecorationRole is an image thumbnail
    };

    explicit FileListModel(QObject *parent = nullptr);
    ~FileListModel() override;

    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    void setEntries(const QList<ArchiveEntry> &entries);
    void setArchiveContext(const QString &archivePath, ArchiveFormat format,
                           const QString &password);
    // Local directory entries are real files, not archive members.  Setting a
    // local context makes DecorationRole read thumbnails directly from disk;
    // setArchiveContext() clears it again.
    // 本地目录条目是真实文件而不是压缩包成员。设置本地上下文后，
    // DecorationRole 会直接从磁盘读取缩略图；setArchiveContext() 会将其清除。
    void setLocalContext(const QString &directory);
    QString localContext() const { return m_localDirectory; }
    void refreshThumbnail(const QString &entryPath);
    bool isFlatMode() const { return m_flatMode; }
    void setFlatMode(bool flat);

    // Whether flat mode adds the synthetic ".." navigation row.  List mode
    // keeps it; grid mode hides it and relies on the Up action instead.
    // 扁平模式是否插入合成的 ".." 导航行。列表模式保留；网格模式隐藏，
    // 改用“上一级”动作返回。
    bool showParentEntry() const { return m_showParentEntry; }
    void setShowParentEntry(bool show);

    // Whether image entries may expose extracted thumbnails. Modern layouts
    // enable this for all views; classic enables it for the grid view only.
    // 是否允许图片条目显示提取的缩略图。现代布局所有视图启用；
    // 经典布局只在网格视图启用。
    bool thumbnailsEnabled() const { return m_thumbnailsEnabled; }
    void setThumbnailsEnabled(bool enabled);

    // Directory navigation for list mode.  Paths use normalized archive paths.
    // 列表模式下的目录导航；路径使用规范化后的压缩包路径。
    QString currentDirectory() const { return m_currentDir; }
    bool enterDirectory(const QString &path);
    bool goUp();
    bool canGoUp() const { return !m_currentDir.isEmpty(); }

    // Flat/list-mode sorting.  When enabled, the synthetic ".." row stays at
    // row 0 and never participates in sorting.
    // 扁平/列表模式排序；启用时合成的 ".." 行始终位于第 0 行且不参与排序。
    void sortFlat(int column, Qt::SortOrder order);

    QStringList allPaths() const;

    // Flat entry list as delivered by the backend (drag-out needs sizes).
    // 后端提供的扁平条目列表（拖出功能需要文件大小）。
    const QList<ArchiveEntry> &sourceEntries() const { return m_sourceEntries; }

    const ArchiveEntry *entryAt(const QModelIndex &index) const;
    QModelIndex indexForPath(const QString &path) const;

private:
    struct Node {
        ArchiveEntry entry;
        Node *parent = nullptr;
        QList<Node *> children;
        bool synthetic = false;
        bool isParent = false;
    };

    Node *m_root = nullptr;
    QHash<QString, Node *> m_nodes;
    QList<ArchiveEntry> m_sourceEntries;
    QString m_archivePath;
    QString m_localDirectory;
    ArchiveFormat m_archiveFormat = ArchiveFormat::Unknown;
    QString m_archivePassword;
    // List-mode directory index: normalized directory -> direct children.
    // 列表模式目录索引：规范化目录 -> 直接子项。
    QHash<QString, QList<ArchiveEntry>> m_childrenByDir;
    QSet<QString> m_directoryPaths;
    QSet<QString> m_implicitDirectoryPaths;
    bool m_flatMode = false;
    bool m_showParentEntry = true;
    bool m_thumbnailsEnabled = false;
    QString m_currentDir;
    int m_flatSortColumn = NameColumn;
    Qt::SortOrder m_flatSortOrder = Qt::AscendingOrder;

    void clear();
    void buildDirectoryIndex();
    void rebuild();
    void sortFlatNodes();
    static void deleteNode(Node *node);
    Node *ensureDirectory(Node *parent, const QString &path, const QString &name);
    Node *findNode(const QString &path) const;
    bool directoryExists(const QString &path) const;
    bool shouldShowThumbnail(const Node *node) const;
    void notifyThumbnailChanged(const QModelIndex &parent = QModelIndex());
    QString displayName(const Node *node) const;
};

#endif // FILELISTMODEL_H
