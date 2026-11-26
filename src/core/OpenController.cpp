// QArchiver / 小Q解压
// OpenController implementation: opening transition and fallback bookkeeping.
// OpenController 实现：打开转换与回退状态维护。

#include "core/OpenController.h"

#include "backends/ArchiveBackend.h"
#include "core/ArchiveBackendFactory.h"

#include "utils/Logger.h"

#ifdef QARCHIVER_HAS_LIBARCHIVE
#include "backends/libarchive/LibArchiveBackend.h"
#endif

OpenController::~OpenController()
{
    // The manager owns and destroys backends; detaching here would leak.
    // 后端由管理器负责销毁；此处分离会造成泄漏。
    Q_ASSERT(!m_openingBackend);
}

void OpenController::begin(const QString &path, bool forceCli)
{
    Q_ASSERT_X(!m_opening && !m_openingBackend, "OpenController::begin",
               "an opening transition is already in progress");
    m_opening = true;
    m_openingPath = path;
    m_openingFormat = ArchiveFormat::Unknown;
    m_forceCli = forceCli;
    assertInvariants("begin");
}

void OpenController::abort()
{
    // No backend may have been created yet; if one was, the caller must take it
    // first so it can be destroyed on the worker thread.
    // 此时不应已创建后端；若已创建，调用方须先取走以便在工作线程销毁。
    Q_ASSERT(!m_openingBackend);
    m_opening = false;
    m_openingPath.clear();
    m_openingFormat = ArchiveFormat::Unknown;
    m_forceCli = false;
    assertInvariants("abort");
}

ArchiveBackend *OpenController::createOpeningBackend(ArchiveFormat format,
                                                     QString *errorMessage)
{
    Q_ASSERT_X(m_opening && !m_openingBackend, "OpenController::createOpeningBackend",
               "no opening transition or a backend already exists");
    m_openingFormat = format;

    const bool forceCli = m_forceCli;
    // The flag is single-use: a later ordinary open must use the read policy.
    // 该标志仅使用一次：之后的普通打开必须走正常的读取策略。
    m_forceCli = false;

    ArchiveBackend *backend = forceCli
        ? ArchiveBackendFactory::create(format)
        : ArchiveBackendFactory::createForRead(format, m_openingPath);
    if (!backend) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("no backend");
        }
        abort();
        return nullptr;
    }

    m_openingBackend = backend;
    assertInvariants("createOpeningBackend");
    return backend;
}

bool OpenController::ownsOpening(const ArchiveBackend *backend) const
{
    return backend != nullptr && backend == m_openingBackend;
}

bool OpenController::accept(ArchiveBackend *backend, Opened *opened)
{
    if (!ownsOpening(backend)) {
        return false;
    }

    Q_ASSERT(opened);
    opened->backend = backend;
    opened->path = m_openingPath;
    opened->format = m_openingFormat;

    m_openingBackend = nullptr;
    m_opening = false;
    m_openingPath.clear();
    m_openingFormat = ArchiveFormat::Unknown;
    m_forceCli = false;
    m_fallbackDone = false;
    m_fallbackInProgress = false;
#ifdef QARCHIVER_HAS_LIBARCHIVE
    m_usingLibArchive = qobject_cast<LibArchiveBackend *>(backend) != nullptr;
#else
    m_usingLibArchive = false;
#endif
    assertInvariants("accept");
    return true;
}

ArchiveBackend *OpenController::takeOpening(const ArchiveBackend *backend)
{
    if (!m_openingBackend) {
        return nullptr;
    }
    if (backend && backend != m_openingBackend) {
        return nullptr;
    }

    ArchiveBackend *detached = m_openingBackend;
    m_openingBackend = nullptr;
    m_opening = false;
    m_openingPath.clear();
    m_openingFormat = ArchiveFormat::Unknown;
    m_forceCli = false;
    assertInvariants("takeOpening");
    return detached;
}

ArchiveBackend *OpenController::reset()
{
    ArchiveBackend *detached = m_openingBackend;
    m_openingBackend = nullptr;
    m_opening = false;
    m_openingPath.clear();
    m_openingFormat = ArchiveFormat::Unknown;
    m_forceCli = false;
    m_usingLibArchive = false;
    m_fallbackDone = false;
    m_fallbackInProgress = false;
    assertInvariants("reset");
    return detached;
}

bool OpenController::canFallback() const
{
    return m_usingLibArchive && !m_fallbackDone && !m_opening;
}

void OpenController::beginFallback()
{
    Q_ASSERT(canFallback());
    LOG_INFO("OpenController", "Starting libarchive -> CLI fallback");
    m_fallbackDone = true;
    m_usingLibArchive = false;
    m_fallbackInProgress = true;
    assertInvariants("beginFallback");
}

void OpenController::finishFallback()
{
    Q_ASSERT(m_fallbackInProgress);
    m_fallbackInProgress = false;
    assertInvariants("finishFallback");
}

void OpenController::assertInvariants(const char *where) const
{
    const auto fail = [where](const char *expression) {
        LOG_ERROR("OpenController", "invariant %s failed at %s", expression, where);
        Q_ASSERT_X(false, "OpenController::assertInvariants", expression);
    };

    if (m_opening && m_openingPath.isEmpty()) {
        fail("opening transition has an empty path");
    }
    if (!m_opening && !m_openingPath.isEmpty()) {
        fail("path set outside an opening transition");
    }
    if (m_openingBackend && !m_opening) {
        fail("opening backend without an opening transition");
    }
    if (m_openingBackend && m_openingPath.isEmpty()) {
        fail("opening backend without a target path");
    }
    if (m_fallbackInProgress
        && (m_opening || m_usingLibArchive || !m_fallbackDone)) {
        fail("fallbackInProgress state is inconsistent");
    }
    if (m_fallbackInProgress && m_openingFormat != ArchiveFormat::Unknown) {
        fail("fallback in progress while a format is still pending");
    }
}
