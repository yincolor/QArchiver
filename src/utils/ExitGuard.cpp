// QArchiver / 小Q解压
// ExitGuard implementation: async-signal-safe signal logging and session
// markers for detecting kills and crashes.
// ExitGuard 实现：异步信号安全的信号日志，以及用于识别被杀/崩溃的会话标记。

#include "utils/ExitGuard.h"

#include "utils/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <csignal>
#include <ctime>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#include <windows.h>
#endif

namespace {

// Append descriptor shared with the signal handler.  The handler only calls
// ::write(), so no Qt allocation, mutex or locale state is touched while the
// process is dying.
// 与信号处理共享的追加描述符。处理函数只调用 ::write()，进程濒死时不会触碰
// Qt 分配、互斥量或 locale 状态。
int g_logFd = -1;

QString g_sessionDir;
QString g_sessionFile;

// --- Async-signal-safe formatting helpers -------------------------------
// --- 异步信号安全的格式化辅助函数 ---------------------------------------

int appendLiteral(char *buffer, int pos, const char *text)
{
    while (*text != '\0') {
        buffer[pos++] = *text++;
    }
    return pos;
}

int appendUnsigned(char *buffer, int pos, unsigned long long value, int width)
{
    char digits[24];
    int count = 0;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0 && count < static_cast<int>(sizeof(digits)));
    while (count < width && count < static_cast<int>(sizeof(digits))) {
        digits[count++] = '0';
    }
    while (count > 0) {
        buffer[pos++] = digits[--count];
    }
    return pos;
}

void writeRaw(const char *text, int length)
{
    if (g_logFd < 0 || length <= 0) {
        return;
    }
#ifdef Q_OS_UNIX
    const ssize_t written = ::write(g_logFd, text, static_cast<size_t>(length));
    (void)written;
    // Also echo to stderr so the line shows up in a terminal session.
    // 同时写到 stderr，使终端会话中也能看到。
    const ssize_t echoed = ::write(STDERR_FILENO, text, static_cast<size_t>(length));
    (void)echoed;
#else
    const int written = ::_write(g_logFd, text, static_cast<unsigned>(length));
    (void)written;
    const int echoed = ::_write(2, text, static_cast<unsigned>(length));
    (void)echoed;
#endif
}

const char *signalName(int sig)
{
    switch (sig) {
#ifdef SIGTERM
    case SIGTERM: return "SIGTERM";
#endif
#ifdef SIGINT
    case SIGINT:  return "SIGINT";
#endif
#ifdef SIGHUP
    case SIGHUP:  return "SIGHUP";
#endif
#ifdef SIGQUIT
    case SIGQUIT: return "SIGQUIT";
#endif
#ifdef SIGSEGV
    case SIGSEGV: return "SIGSEGV";
#endif
#ifdef SIGABRT
    case SIGABRT: return "SIGABRT";
#endif
#ifdef SIGBUS
    case SIGBUS:  return "SIGBUS";
#endif
#ifdef SIGILL
    case SIGILL:  return "SIGILL";
#endif
#ifdef SIGFPE
    case SIGFPE:  return "SIGFPE";
#endif
    default:      return "unknown signal";
    }
}

// Asynchronous signals end the process; fault signals (SEGV, ...) must be
// allowed to re-execute the faulting instruction instead.
// 异步信号会终止进程；故障信号（SEGV 等）必须放行以重新执行出错指令。
bool signalEndsProcess(int sig)
{
    switch (sig) {
#ifdef SIGTERM
    case SIGTERM:
#endif
#ifdef SIGINT
    case SIGINT:
#endif
#ifdef SIGHUP
    case SIGHUP:
#endif
#ifdef SIGQUIT
    case SIGQUIT:
#endif
        return true;
    default:
        return false;
    }
}

void writeSignalLine(int sig)
{
    char line[256];
    int pos = 0;

    pos = appendLiteral(line, pos, "[");
    const std::time_t now = std::time(nullptr);
    std::tm local {};
#ifdef Q_OS_UNIX
    // Local time, matching the timestamps Logger writes.
    // 本地时间，与 Logger 写入的时间戳一致。
    const bool haveTime = ::localtime_r(&now, &local) != nullptr;
#else
    const bool haveTime = ::localtime_s(&local, &now) == 0;
#endif
    if (haveTime) {
        pos = appendUnsigned(line, pos,
                             static_cast<unsigned long long>(local.tm_year + 1900), 4);
        pos = appendLiteral(line, pos, "-");
        pos = appendUnsigned(line, pos,
                             static_cast<unsigned long long>(local.tm_mon + 1), 2);
        pos = appendLiteral(line, pos, "-");
        pos = appendUnsigned(line, pos,
                             static_cast<unsigned long long>(local.tm_mday), 2);
        pos = appendLiteral(line, pos, "T");
        pos = appendUnsigned(line, pos,
                             static_cast<unsigned long long>(local.tm_hour), 2);
        pos = appendLiteral(line, pos, ":");
        pos = appendUnsigned(line, pos,
                             static_cast<unsigned long long>(local.tm_min), 2);
        pos = appendLiteral(line, pos, ":");
        pos = appendUnsigned(line, pos,
                             static_cast<unsigned long long>(local.tm_sec), 2);
        // No zone suffix: local time, like the rest of app.log.
        // 不带时区后缀：与 app.log 其余部分一样使用本地时间。
    } else {
        pos = appendLiteral(line, pos, "unknown-time");
    }
    pos = appendLiteral(line, pos, "] [ERROR] [ExitGuard] pid ");
#ifdef Q_OS_UNIX
    pos = appendUnsigned(line, pos,
                         static_cast<unsigned long long>(::getpid()), 0);
#else
    pos = appendUnsigned(line, pos,
                         static_cast<unsigned long long>(::GetCurrentProcessId()), 0);
#endif
    pos = appendLiteral(line, pos, " was terminated by signal ");
    pos = appendUnsigned(line, pos, static_cast<unsigned long long>(sig), 0);
    pos = appendLiteral(line, pos, " (");
    pos = appendLiteral(line, pos, signalName(sig));
    pos = appendLiteral(line, pos, ") before it could exit cleanly\n");

    writeRaw(line, pos);
}

// Keep this handler trivially simple: no Qt, no allocations, no locks.
// 处理函数保持极简：不用 Qt、不分配内存、不加锁。
extern "C" void exitSignalHandler(int sig)
{
    writeSignalLine(sig);

#ifdef Q_OS_UNIX
    // SA_RESETHAND already restored the default action.  Fault signals return
    // so the faulting instruction runs again under the default action;
    // asynchronous signals are re-raised to give the usual exit status/core.
    // SA_RESETHAND 已恢复默认动作。故障信号直接返回，让出错指令在默认动作下
    // 重新执行；异步信号则重新 raise，得到通常的退出状态/核心转储。
    if (!signalEndsProcess(sig)) {
        return;
    }
    ::raise(sig);
#else
    std::signal(sig, SIG_DFL);
    std::raise(sig);
#endif
    // Only reached if the C library refuses to terminate on re-raise.
    // 仅当 C 库拒绝在重新 raise 后终止时才会走到这里。
    ::_exit(128 + sig);
}

QString sessionDirectory()
{
    const QString logPath = Logger::instance().logFilePath();
    if (logPath.isEmpty()) {
        return QString();
    }
    return QFileInfo(logPath).absolutePath() + QStringLiteral("/sessions");
}

bool processIsAlive(qint64 pid)
{
    if (pid <= 0) {
        return false;
    }
#ifdef Q_OS_UNIX
    if (::kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    // EPERM means the process exists but belongs to another user.
    // EPERM 表示进程存在但属于其他用户。
    return errno == EPERM;
#else
    HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                  static_cast<DWORD>(pid));
    if (!handle) {
        return false;
    }
    DWORD exitCode = 0;
    const bool alive = ::GetExitCodeProcess(handle, &exitCode)
        && exitCode == STILL_ACTIVE;
    ::CloseHandle(handle);
    return alive;
#endif
}

} // namespace

void ExitGuard::install(const QString &logFilePath)
{
    if (logFilePath.isEmpty()) {
        return;
    }

    const QByteArray nativePath = QFile::encodeName(logFilePath);
#ifdef Q_OS_UNIX
    // O_CLOEXEC: archive tools must not inherit the log descriptor.
    // O_CLOEXEC：压缩工具不能继承日志描述符。
    g_logFd = ::open(nativePath.constData(),
                     O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
#else
    g_logFd = ::_open(nativePath.constData(),
                      _O_WRONLY | _O_APPEND | _O_CREAT | _O_TEXT,
                      _S_IREAD | _S_IWRITE);
#endif
    if (g_logFd < 0) {
        LOG_WARN("ExitGuard",
                 "Cannot open %s for signal logging; abnormal exits will only "
                 "be reported by the session marker",
                 logFilePath.toUtf8().constData());
    }

#ifdef Q_OS_UNIX
    struct sigaction action {};
    action.sa_handler = &exitSignalHandler;
    sigemptyset(&action.sa_mask);
    // Restore the default disposition before the handler runs, so re-raising
    // terminates the process without recursion.
    // 处理函数运行前先恢复默认动作，重新 raise 时不会递归。
    action.sa_flags = SA_RESETHAND;
    const int handledSignals[] = {SIGTERM, SIGINT, SIGHUP, SIGQUIT,
                                  SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE};
    for (const int sig : handledSignals) {
        if (sigaction(sig, &action, nullptr) != 0) {
            LOG_DEBUG("ExitGuard", "Cannot install a handler for signal %d", sig);
        }
    }
#else
    for (const int sig : {SIGINT, SIGTERM, SIGABRT}) {
        std::signal(sig, &exitSignalHandler);
    }
#endif
}

void ExitGuard::beginSession()
{
    g_sessionDir = sessionDirectory();
    if (g_sessionDir.isEmpty()) {
        return;
    }
    if (!QDir().mkpath(g_sessionDir)) {
        LOG_WARN("ExitGuard", "Cannot create session marker directory %s",
                 g_sessionDir.toUtf8().constData());
        g_sessionDir.clear();
        return;
    }

    const qint64 pid = QCoreApplication::applicationPid();
    const QFileInfoList markers =
        QDir(g_sessionDir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);

    for (const QFileInfo &info : markers) {
        bool ok = false;
        const qint64 other = info.fileName().toLongLong(&ok);
        if (!ok || other <= 0) {
            // Not a marker (or corrupted): drop it.
            // 不是标记文件（或已损坏）：删除。
            QFile::remove(info.absoluteFilePath());
            continue;
        }
        if (other != pid && processIsAlive(other)) {
            LOG_INFO("ExitGuard",
                     "Another QArchiver instance is running (pid %lld); keeping "
                     "its session marker",
                     static_cast<long long>(other));
            continue;
        }
        if (other != pid) {
            // The process is gone and its marker was never removed: the run was
            // killed (e.g. kill -9, OOM) or crashed before the clean exit path.
            // 进程已不存在但标记未删除：该次运行被强杀（如 kill -9、OOM）或在
            // 走到正常退出路径前崩溃。
            LOG_WARN("ExitGuard",
                     "Previous session (pid %lld) did not exit cleanly; it was "
                     "killed, crashed, or the session state was left behind",
                     static_cast<long long>(other));
        }
        QFile::remove(info.absoluteFilePath());
    }

    g_sessionFile = QDir(g_sessionDir).filePath(QString::number(pid));
    QFile marker(g_sessionFile);
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        LOG_WARN("ExitGuard", "Cannot create session marker %s",
                 g_sessionFile.toUtf8().constData());
        g_sessionFile.clear();
        return;
    }
    marker.write(QByteArray::number(pid) + '\n');
    marker.close();
    LOG_DEBUG("ExitGuard", "Session marker %s",
              g_sessionFile.toUtf8().constData());
}

void ExitGuard::endSession()
{
    if (g_sessionFile.isEmpty()) {
        return;
    }
    if (QFile::remove(g_sessionFile)) {
        LOG_DEBUG("ExitGuard", "Session marker removed (clean exit)");
    } else {
        LOG_DEBUG("ExitGuard", "Cannot remove session marker %s",
                  g_sessionFile.toUtf8().constData());
    }
    g_sessionFile.clear();
}
