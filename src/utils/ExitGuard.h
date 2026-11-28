#ifndef EXITGUARD_H
#define EXITGUARD_H

// QArchiver / 小Q解压
// Abnormal-exit detection: signal logging plus per-process session markers.
// 异常退出检测：信号日志与每进程会话标记。
//
// Two independent mechanisms are used because neither covers every case:
// 使用两套互补机制，因为单独一套无法覆盖所有情况：
//
//  1. Signal handlers append one line to the log before the process dies
//     (SIGTERM/SIGINT/SIGHUP/SIGQUIT and the fatal fault signals).  This
//     explains *why* the process ended while it is still running.
//  2. A session marker file is created at startup and removed on a clean
//     exit.  At the next start, any marker whose process is gone proves that
//     the previous run never reached the clean exit path — this is the only
//     way to detect SIGKILL (kill -9), the OOM killer or a hard crash.
//  1. 信号处理在进程死亡前追加一行日志（SIGTERM/SIGINT/SIGHUP/SIGQUIT 以及
//     致命故障信号），说明“为什么”结束。
//  2. 启动时创建会话标记文件，正常退出时删除。下次启动时，任何进程已不存在的
//     标记都能证明上次没有走到正常退出路径——这是检测 SIGKILL（kill -9）、
//     OOM 或硬崩溃的唯一办法。

#include <QString>

namespace ExitGuard {

// Install the handlers, writing to the already configured log file.
// 安装信号处理，写入已配置好的日志文件。
void install(const QString &logFilePath);

// Create this process's session marker and report markers left behind by
// earlier runs that did not exit cleanly.
// 创建本进程的会话标记，并报告此前未正常退出而遗留的标记。
void beginSession();

// Remove this process's marker; call on every clean exit path.
// 移除本进程的标记；所有正常退出路径都必须调用。
void endSession();

} // namespace ExitGuard

#endif // EXITGUARD_H
