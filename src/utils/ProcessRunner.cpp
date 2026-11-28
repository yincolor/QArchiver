// QArchiver / 小Q解压
// Tool discovery, process I/O draining, cancellation and password masking.
// 工具查找、进程 I/O 排空、取消与密码脱敏。

#include "utils/ProcessRunner.h"

#include "utils/EncodingDetector.h"
#include "utils/Logger.h"
#include "utils/PlatformProcess.h"
#include "utils/SevenZipTool.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

// Keep log lines short: at most a few hundred characters of tool output.
// 控制日志长度：外部工具输出最多保留几百个字符。
QString summarize(const QString &text, int maxChars = 400)
{
    QString flat = text.trimmed();
    flat.replace(QLatin1Char('\n'), QLatin1Char(' '));
    flat.replace(QStringLiteral("  "), QStringLiteral(" "));
    if (flat.size() > maxChars) {
        flat = flat.left(maxChars) + QStringLiteral("...");
    }
    return flat.isEmpty() ? QStringLiteral("(no output)") : flat;
}

void logCommand(const QString &program, const QStringList &args,
                const QString &workingDir, int timeoutMs)
{
    LOG_DEBUG("ProcessRunner", "run: %s (cwd=%s, timeout=%dms)",
              ProcessRunner::maskedCommand(program, args).toUtf8().constData(),
              (workingDir.isEmpty() ? QStringLiteral("<inherit>") : workingDir)
                  .toUtf8()
                  .constData(),
              timeoutMs);
}

QString decodeLine(const QByteArray &bytes, const QString &encodingHint)
{
    if (encodingHint.isEmpty()) {
        return EncodingDetector::decode(bytes);
    }
    if (encodingHint.compare(QStringLiteral("UTF-8"), Qt::CaseInsensitive) == 0) {
        return QString::fromUtf8(bytes);
    }
    return EncodingDetector::decode(bytes, encodingHint);
}

bool isExecutableFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return false;
    }
#ifdef Q_OS_WIN
    // Windows has no Unix execute bit; the known candidate names are enough.
    // Windows 没有 Unix 可执行位；已知候选文件名足以判断。
    return true;
#else
    return info.isExecutable();
#endif
}

QString environmentOverrideName(const QString &toolName)
{
    QString upper = toolName.toUpper();
    for (QChar &ch : upper) {
        if (!ch.isLetterOrNumber()) {
            ch = QLatin1Char('_');
        }
    }
    return QStringLiteral("QARCHIVER_%1_PATH").arg(upper);
}

QStringList toolCandidates(const QString &toolName)
{
    QStringList candidates;
    if (toolName == QLatin1String("7z")) {
        // 7zzs is the statically linked Linux build; 7z.exe needs the matching
        // 7z.dll next to it on Windows.
        // 7zzs 是 Linux 静态链接版本；Windows 的 7z.exe 需要同目录下匹配的 7z.dll。
        candidates << QStringLiteral("7zzs")
                   << QStringLiteral("7zz")
                   << QStringLiteral("7z")
                   << QStringLiteral("7za")
                   << QStringLiteral("qarchiver-7z");
#ifdef Q_OS_WIN
        candidates << QStringLiteral("7z.exe")
                   << QStringLiteral("7za.exe")
                   << QStringLiteral("qarchiver-7z.exe");
#endif
    } else {
        candidates << toolName;
#ifdef Q_OS_WIN
        if (!toolName.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) {
            candidates << toolName + QStringLiteral(".exe");
        }
#endif
    }
    return candidates;
}

} // namespace

bool ProcessRunner::isToolAvailable(const QString &toolName)
{
    return !resolveToolPath(toolName).isEmpty();
}

QString ProcessRunner::resolveToolPath(const QString &toolName)
{
    // 7z is special: a program-relative candidate must pass --help before it
    // is accepted, so a stray/invalid file cannot shadow a working system 7z.
    // 7z 特殊处理：程序相关目录中的候选必须通过 --help 校验，
    // 避免无效文件遮蔽可用的系统 7z。
    if (toolName == QLatin1String("7z")) {
        return SevenZipTool::resolvePath();
    }

    // 1. Explicit override, primarily used by the test suite and packagers.
    // 1. 显式覆盖，主要供测试套件和打包者使用。
    const QByteArray overrideName = environmentOverrideName(toolName).toUtf8();
    const QString overridePath = qEnvironmentVariable(overrideName.constData());
    if (!overridePath.isEmpty()) {
        if (!isExecutableFile(overridePath)) {
            return QString();
        }
        return QDir::cleanPath(QFileInfo(overridePath).absoluteFilePath());
    }

    // 2. Bundled helper next to the executable / in libexec.
    // 2. 可执行文件旁或 libexec 中随程序分发的辅助程序。
    const QStringList bundled = bundledToolPaths(toolName);
    if (!bundled.isEmpty()) {
        return bundled.first();
    }

    // 3. System PATH as the final fallback. / 3. 最后回退到系统 PATH。
    const QStringList system = systemToolPaths(toolName);
    return system.isEmpty() ? QString() : system.first();
}

QStringList ProcessRunner::bundledDirectories()
{
    return SevenZipTool::bundledDirectories();
}

QStringList ProcessRunner::bundledToolPaths(const QString &toolName)
{
    QStringList paths;
    const QStringList candidates = toolCandidates(toolName);
    const QStringList dirs = bundledDirectories();
    for (const QString &dir : dirs) {
        const QDir candidateDir(dir);
        for (const QString &candidate : candidates) {
            const QString path = candidateDir.filePath(candidate);
            if (!isExecutableFile(path)) {
                continue;
            }
            const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
            if (!paths.contains(absolute)) {
                paths << absolute;
            }
        }
    }
    return paths;
}

QStringList ProcessRunner::systemToolPaths(const QString &toolName)
{
    QStringList paths;
    const QStringList candidates = toolCandidates(toolName);
    for (const QString &candidate : candidates) {
        const QString path = QStandardPaths::findExecutable(candidate);
        if (!path.isEmpty()) {
            const QString clean = QFileInfo(path).absoluteFilePath();
            if (!paths.contains(clean)) {
                paths << clean;
            }
        }
    }
    return paths;
}

QString ProcessRunner::maskedCommand(const QString &program, const QStringList &args)
{
    QStringList masked;
    masked.reserve(args.size());
    for (const QString &arg : args) {
        // 7-Zip: -p<pwd>; rar: -p<pwd> and -hp<pwd> (encrypted headers).
        // 7-Zip：-p<密码>；rar：-p<密码> 与 -hp<密码>（加密文件头）。
        if ((arg.startsWith(QStringLiteral("-p"))
             || arg.startsWith(QStringLiteral("-hp")))
            && arg.size() > 2) {
            const int prefix = arg.startsWith(QStringLiteral("-hp")) ? 3 : 2;
            masked << arg.left(prefix) + QStringLiteral("***");
        } else {
            masked << arg;
        }
    }
    return program + QLatin1Char(' ') + masked.join(QLatin1Char(' '));
}

ProcessResult ProcessRunner::run(const QString &program,
                                 const QStringList &args,
                                 const QString &encodingHint,
                                 const QProcessEnvironment &env,
                                 const QString &workingDir,
                                 int timeoutMs,
                                 const std::atomic_bool *cancel,
                                 const LineCallback &onLine,
                                 const ProcessOutputOptions &outputOptions)
{
    ProcessResult result;

    QProcess proc;
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    qarchiverHideConsoleWindow(proc);
    if (!env.isEmpty()) {
        proc.setProcessEnvironment(env);
    }
    if (!workingDir.isEmpty()) {
        proc.setWorkingDirectory(workingDir);
    }

    // Log the invocation (password arguments masked) so a failed operation can
    // be reproduced from app.log alone.
    // 记录调用（密码参数已脱敏），使失败操作仅凭 app.log 即可复现。
    logCommand(program, args, workingDir, timeoutMs);

    proc.start(program, args);
    if (!proc.waitForStarted(5000)) {
        result.started = false;
        result.stdErr = proc.errorString();
        LOG_ERROR("ProcessRunner", "%s could not be started: %s",
                  program.toUtf8().constData(),
                  proc.errorString().toUtf8().constData());
        return result;
    }
    result.started = true;

    QByteArray outBuffer;
    QByteArray errBuffer;

    auto appendLine = [&](const QByteArray &raw, bool isError) {
        QByteArray line = raw;
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        const QString text = decodeLine(line, encodingHint);
        if (text.isEmpty()) {
            return;
        }

        const qint64 textLimit = isError ? outputOptions.maxStdErrBytes
                                         : outputOptions.maxStdOutBytes;
        const qint64 rawLimit = isError ? outputOptions.maxStdErrBytes
                                        : outputOptions.maxRawSampleBytes;
        QString &textTarget = isError ? result.stdErr : result.stdOut;
        QByteArray &rawTarget = isError ? result.stdErrRaw : result.stdOutRaw;

        if (textLimit < 0 || textTarget.size() < textLimit) {
            QString piece = text;
            if (textLimit > 0) {
                const qint64 remaining = textLimit - textTarget.size();
                if (piece.size() > remaining) {
                    piece.truncate(static_cast<qsizetype>(remaining));
                }
            }
            textTarget += piece;
            textTarget += QLatin1Char('\n');
        }

        if (rawLimit < 0 || rawTarget.size() < rawLimit) {
            QByteArray piece = line;
            if (rawLimit > 0) {
                const qint64 remaining = rawLimit - rawTarget.size();
                if (piece.size() > remaining) {
                    piece.truncate(static_cast<qsizetype>(remaining));
                }
            }
            rawTarget += piece;
            rawTarget += '\n';
        }

        if (onLine) {
            onLine(text, isError);
        }
    };

    auto drainCompleteLines = [&](QByteArray &buffer, bool isError) {
        // 7-Zip uses '\r' for progress updates on Windows and '\n' for normal
        // output; accept both as line separators.
        // 7-Zip 在 Windows 上用 '\r' 刷新进度、用 '\n' 输出普通行；两者都按
        // 行分隔符处理。
        int start = 0;
        for (;;) {
            int separator = -1;
            for (int i = start; i < buffer.size(); ++i) {
                if (buffer.at(i) == '\n' || buffer.at(i) == '\r') {
                    separator = i;
                    break;
                }
            }
            if (separator < 0) {
                break;
            }
            appendLine(buffer.mid(start, separator - start), isError);
            start = separator + 1;
            if (start < buffer.size() && buffer.at(separator) == '\r'
                && buffer.at(start) == '\n') {
                ++start;
            }
        }
        if (start > 0) {
            buffer.remove(0, start);
        }
    };

    auto readChannels = [&]() {
        outBuffer += proc.readAllStandardOutput();
        errBuffer += proc.readAllStandardError();
        drainCompleteLines(outBuffer, false);
        drainCompleteLines(errBuffer, true);
    };

    QElapsedTimer timer;
    timer.start();

    while (proc.state() != QProcess::NotRunning) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            proc.terminate();
            if (!proc.waitForFinished(2000)) {
                proc.kill();
                proc.waitForFinished(2000);
            }
            break;
        }
        if (timeoutMs > 0 && timer.elapsed() > timeoutMs) {
            result.timedOut = true;
            proc.kill();
            proc.waitForFinished(2000);
            break;
        }

        proc.waitForReadyRead(200);
        readChannels();
    }

    // Collect whatever is still buffered after the process exited.
    // 进程退出后收集仍留在缓冲区中的数据。
    readChannels();

    // Flush a trailing line without a newline. / 输出末尾没有换行时也刷新该行。
    if (!outBuffer.isEmpty()) {
        appendLine(outBuffer, false);
        outBuffer.clear();
    }
    if (!errBuffer.isEmpty()) {
        appendLine(errBuffer, true);
        errBuffer.clear();
    }

    result.exitCode = proc.exitCode();
    result.exitStatus = proc.exitStatus();

    const qint64 elapsed = timer.elapsed();
    if (result.timedOut) {
        LOG_WARN("ProcessRunner", "%s timed out after %lldms",
                 program.toUtf8().constData(), static_cast<long long>(elapsed));
    } else if (result.cancelled) {
        LOG_INFO("ProcessRunner", "%s cancelled after %lldms",
                 program.toUtf8().constData(), static_cast<long long>(elapsed));
    } else if (!result.success()) {
        LOG_WARN("ProcessRunner",
                 "%s exited with code %d after %lldms; stderr: %s",
                 maskedCommand(program, args).toUtf8().constData(),
                 result.exitCode, static_cast<long long>(elapsed),
                 summarize(result.stdErr).toUtf8().constData());
    } else {
        LOG_DEBUG("ProcessRunner", "%s finished in %lldms",
                  maskedCommand(program, args).toUtf8().constData(),
                  static_cast<long long>(elapsed));
    }
    return result;
}
