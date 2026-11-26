#ifndef QARCHIVER_CORE_OPENCONTROLLER_H
#define QARCHIVER_CORE_OPENCONTROLLER_H

// QArchiver / 小Q解压
// Open/fallback state machine extracted from ArchiveManager.
// 从 ArchiveManager 中抽出的打开/回退状态机。

#include <QString>

#include "common/ArchiveConstants.h"

class ArchiveBackend;

// Owns the transient state of opening an archive and of the libarchive -> CLI
// fallback.  ArchiveManager only keeps the final backend; the backend that is
// still opening is owned here so a failed open can never be mistaken for the
// current archive.
// 独占“正在打开”的压缩包与 libarchive -> CLI 回退的临时状态。ArchiveManager
// 只持有最终后端；仍在打开中的后端由本类独占，因此打开失败的后端不可能被
// 误当作当前压缩包。
//
// Invariants (checked by assertInvariants()):
//  * an opening transition always has a non-empty path and no final backend
//    owned here;
//  * openingBackend is non-null only while opening is true;
//  * fallbackInProgress implies fallbackDone, no opening transition and the
//    final backend is not served by libarchive;
//  * usingLibArchive always describes the final backend held by the manager.
// 不变量（由 assertInvariants() 检查）：
//  * 打开转换期间路径非空，且本类不持有最终后端；
//  * openingBackend 非空时 opening 必为 true；
//  * fallbackInProgress 蕴含 fallbackDone、没有打开转换，且最终后端不再由
//    libarchive 提供；
//  * usingLibArchive 始终描述管理器持有的最终后端。
class OpenController {
public:
    OpenController() = default;
    ~OpenController();

    Q_DISABLE_COPY(OpenController)

    // Result of a successful open transition. / 打开转换成功的结果。
    struct Opened {
        ArchiveBackend *backend = nullptr;
        QString path;
        ArchiveFormat format = ArchiveFormat::Unknown;
    };

    // Start an opening transition.  @p path may be any volume of a split
    // archive; format detection resolves the first volume before
    // createOpeningBackend() is called.
    // 开始一次打开转换。@p path 可以是分卷的任意一卷；格式检测在调用
    // createOpeningBackend() 前解析首卷。
    void begin(const QString &path, bool forceCli);

    // Abort an opening transition before a backend was created (format
    // detection failed).  Clears the pending force-CLI flag as well.
    // 在后端创建前中止打开转换（格式检测失败），同时清除待用的强制 CLI 标志。
    void abort();

    // Create the backend for the detected format and make it the opening
    // backend.  When forceCli was requested by begin(), the CLI factory is used
    // instead of the read policy.  Returns null and aborts the transition when
    // no backend exists for @p format.
    // 为检测出的格式创建后端并将其设为“正在打开”的后端。begin() 请求强制 CLI
    // 时改用命令行工厂而不走读取策略。@p format 没有可用后端时返回空并中止转换。
    ArchiveBackend *createOpeningBackend(ArchiveFormat format,
                                         QString *errorMessage = nullptr);

    // Whether @p backend is still the backend being opened.
    // @p backend 是否仍是正在打开的后端。
    bool ownsOpening(const ArchiveBackend *backend) const;

    // Complete the transition when @p backend reports archiveOpened.  Fills
    // @p opened and returns true when @p backend was the opening backend.
    // 当 @p backend 报告 archiveOpened 时完成转换。若 @p backend 正是正在打开的
    // 后端，填充 @p opened 并返回 true。
    bool accept(ArchiveBackend *backend, Opened *opened);

    // Detach the opening backend so the caller can destroy it.  Returns null
    // when @p backend (or the current opening backend when @p backend is null)
    // is not the one being opened; in that case no state is changed.
    // 分离正在打开的后端供调用方销毁。若 @p backend（为空时指当前正在打开的后端）
    // 不是正在打开的后端则返回空且不改变状态。
    ArchiveBackend *takeOpening(const ArchiveBackend *backend = nullptr);

    // Clear every transition/fallback flag and return the opening backend (if
    // any) to the caller for destruction.
    // 清除所有转换/回退标志，并把仍在打开的后端（如有）交还调用方销毁。
    ArchiveBackend *reset();

    bool isOpening() const { return m_opening; }

    // Whether the final backend is the in-process libarchive reader.
    // 最终后端是否为进程内 libarchive 读取器。
    bool usingLibArchive() const { return m_usingLibArchive; }

    // Whether a libarchive -> CLI fallback is still allowed.  The caller must
    // also verify that the failing backend is the current final backend.
    // 是否仍允许 libarchive -> CLI 回退。调用方还须确认出错的后端是当前最终后端。
    bool canFallback() const;

    // Mark the fallback as started.  Keeps fallbackInProgress() true until the
    // replacement backend reports archiveOpened via finishFallback().
    // 标记回退已开始。在替代后端通过 finishFallback() 报告 archiveOpened 前，
    // fallbackInProgress() 保持 true。
    void beginFallback();

    bool fallbackInProgress() const { return m_fallbackInProgress; }

    // The replacement backend opened successfully; consume the fallback marker.
    // 替代后端已成功打开；消费回退标记。
    void finishFallback();

    // Debug-only invariant check.  Compiled out in release builds.
    // 仅调试用的不变量检查；发布构建中为空。
    void assertInvariants(const char *where) const;

private:
    bool m_opening = false;
    QString m_openingPath;
    ArchiveFormat m_openingFormat = ArchiveFormat::Unknown;
    ArchiveBackend *m_openingBackend = nullptr;
    bool m_forceCli = false;

    bool m_usingLibArchive = false;
    bool m_fallbackDone = false;
    bool m_fallbackInProgress = false;
};

#endif // QARCHIVER_CORE_OPENCONTROLLER_H
