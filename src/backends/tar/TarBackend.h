#ifndef TARBACKEND_H
#define TARBACKEND_H

// QArchiver / 小Q解压
// Tar family backend implemented with the bundled 7-Zip helper.
// 使用内置 7-Zip 辅助程序实现的 tar 系列后端。

#include "backends/ArchiveBackend.h"

#include <QTemporaryDir>

#include <memory>

// Backend for .tar, .tar.gz/.tgz, .tar.xz/.txz, .tar.bz2/.tbz2 and
// read-only .tar.zst/.tzst (design doc 4.2/5.1/5.2).  All tar operations are
// delegated to the bundled 7-Zip helper.  Compressed tar files are single-
// stream formats, so extraction/listing happens in two stages through a
// temporary uncompressed .tar.  The bundled 7-Zip can read zstd but cannot
// create it, so tar.zst creation is rejected explicitly.
// .tar、.tar.gz/.tgz、.tar.xz/.txz、.tar.bz2/.tbz2 以及只读的 .tar.zst/.tzst
// 后端（设计文档 4.2/5.1/5.2）。所有 tar 操作都委托给内置 7-Zip 辅助程序。
// 压缩 tar 是单流格式，因此解压/列表通过临时未压缩 .tar 分两阶段完成。
// 内置 7-Zip 能读取 zstd 但不能创建 zstd，因此显式拒绝创建 tar.zst。
class TarBackend : public ArchiveBackend {
    Q_OBJECT
public:
    explicit TarBackend(ArchiveFormat fmt = ArchiveFormat::Tar, QObject *parent = nullptr);

    void openArchive(const QString &filePath) override;
    void listContents(const ListOptions &options = {}) override;
    void extract(const ExtractOptions &options) override;
    void compress(const CompressOptions &options) override;
    void addFiles(const AddFilesOptions &options) override;
    void deleteFiles(const DeleteFilesOptions &options) override;
    void testArchive(const TestOptions &options = {}) override;
    void closeArchive() override;

    ArchiveFormat format() const override { return m_format; }
    QString nameEncoding() const override { return m_nameEncoding; }
    bool isReadOnly() const override
    {
        // 7-Zip can read zstd but not create it; keep tar.zst read-only.
        // 7-Zip 能读取 zstd 但不能创建，因此 tar.zst 保持只读。
        return m_format == ArchiveFormat::TarZst;
    }
    bool supportsEncryption() const override { return false; }
    bool supportsVolume() const override { return false; }
    bool supportsAppend() const override;

private:
    enum class InnerTarResult {
        Ok,
        Cancelled,
        Failed
    };

    bool isCompressed() const;
    QString compressionSwitch() const;
    // Validate and normalize compression level; return -1 to omit
    // 校验并规范化压缩级别；返回 -1 表示省略该参数
    int validateCompressionLevel(int level) const;
    // Extract the single .tar member from a compressed tar stream into
    // @p outerDir.  Returns the path of the inner tar on success.
    // 从压缩 tar 流中把唯一的 .tar 成员解压到 @p outerDir；
    // 成功时返回内部 tar 的路径。
    InnerTarResult extractInnerTar(const QString &sevenZip,
                                   const QString &outerDir,
                                   QString &innerTarPath,
                                   QString &errorText);
    // Return the uncompressed inner tar, reusing the previously extracted
    // copy while the outer archive's size and modification time are unchanged.
    // 返回未压缩的内层 tar；外层压缩包大小与修改时间未变时复用上次的结果。
    InnerTarResult resolveInnerTar(const QString &sevenZip,
                                   QString &innerTarPath,
                                   QString &errorText);
    ArchiveFormat m_format;
    QList<ArchiveEntry> m_entries;   // Cached entry list for progress calculation / 缓存的条目列表，用于进度计算
    QString m_nameEncoding = QStringLiteral("UTF-8");

    // Session cache for the two-stage compressed-tar path: listing, testing
    // and extraction no longer decompress the outer stream each time.
    // 压缩 tar 两阶段路径的会话缓存：列表、测试与解压不再每次都解压外层流。
    std::unique_ptr<QTemporaryDir> m_innerTarDir;
    QString m_innerTarPath;
    qint64 m_innerTarSize = -1;
    qint64 m_innerTarMtime = 0;
};

#endif // TARBACKEND_H
