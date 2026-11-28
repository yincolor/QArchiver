// QArchiver / 小Q解压
// Logger implementation: formatting, mutexing and log rotation.
// Logger 实现：格式化、互斥保护与日志轮转。

#include "utils/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QTextStream>
#include <QStandardPaths>
#include <QFileInfo>

#include <cstdlib>

namespace {

// Forward Qt's own messages (qDebug/qWarning/qCritical) into the same sink so
// library and QPA warnings end up in app.log instead of only on stderr.
// 把 Qt 自身的消息（qDebug/qWarning/qCritical）导入同一日志出口，
// 使 Qt 与 QPA 的警告也写入 app.log，而不是只出现在标准错误。
void qtMessageHandler(QtMsgType type, const QMessageLogContext &context,
                      const QString &message)
{
    Logger::Level level = Logger::Info;
    switch (type) {
    case QtDebugMsg:    level = Logger::Debug;   break;
    case QtInfoMsg:     level = Logger::Info;    break;
    case QtWarningMsg:  level = Logger::Warning; break;
    case QtCriticalMsg: level = Logger::Error;   break;
    case QtFatalMsg:    level = Logger::Error;   break;
    }

    QString tag = (context.category && *context.category)
        ? QString::fromLatin1(context.category)
        : QStringLiteral("Qt");
    if (tag == QLatin1String("default")) {
        tag = QStringLiteral("Qt");
    }

    QString text = message;
    if (level == Logger::Debug && context.file && *context.file) {
        // Source location is only useful while debugging.
        // 仅在调试时附加源文件位置。
        text += QStringLiteral(" (%1:%2)")
                    .arg(QString::fromUtf8(context.file))
                    .arg(context.line);
    }

    Logger::instance().log(level, tag, text);

    if (type == QtFatalMsg) {
        std::abort();
    }
}

} // namespace

Logger::Logger()
{
    // Enable debug logging via QARCHIVER_DEBUG=1.  SAM_DEBUG is kept as a
    // compatibility alias for older scripts and bug reports.
    // 通过 QARCHIVER_DEBUG=1 启用调试日志；SAM_DEBUG 作为旧脚本/反馈的兼容别名保留。
    if (qEnvironmentVariableIsSet("QARCHIVER_DEBUG")
        || qEnvironmentVariableIsSet("SAM_DEBUG")) {
        m_minLevel = Debug;
    }

    // QARCHIVER_LOG_LEVEL=debug|info|warning|error overrides the default
    // level without enabling the full debug stream.
    // QARCHIVER_LOG_LEVEL=debug|info|warning|error 可覆盖默认级别，
    // 无需全局开启调试输出。
    const QString level =
        qEnvironmentVariable("QARCHIVER_LOG_LEVEL").trimmed().toLower();
    if (level == QLatin1String("debug")) {
        m_minLevel = Debug;
    } else if (level == QLatin1String("info")) {
        m_minLevel = Info;
    } else if (level == QLatin1String("warning") || level == QLatin1String("warn")) {
        m_minLevel = Warning;
    } else if (level == QLatin1String("error")) {
        m_minLevel = Error;
    }

    // QARCHIVER_NO_QT_LOG=1 keeps Qt's default message handler.
    // QARCHIVER_NO_QT_LOG=1 时保留 Qt 默认的消息处理。
    if (!qEnvironmentVariableIsSet("QARCHIVER_NO_QT_LOG")) {
        qInstallMessageHandler(&qtMessageHandler);
    }
}

Logger &Logger::instance()
{
    static Logger instance;
    return instance;
}

void Logger::setMinLevel(Level lvl)
{
    QMutexLocker locker(&m_mutex);
    m_minLevel = lvl;
}

const char *Logger::levelName() const
{
    switch (m_minLevel) {
    case Debug:   return "debug";
    case Info:    return "info";
    case Warning: return "warning";
    case Error:   return "error";
    }
    return "info";
}

void Logger::setLogFile(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen()) {
        m_file.close();
    }
    // Ensure parent directory exists / 确保父目录存在
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    m_logPath = path;
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        fputs("Logger: failed to open log file\n", stderr);
    }
}

void Logger::log(Level lvl, const QString &tag, const QString &msg)
{
    QMutexLocker locker(&m_mutex);
    if (lvl < m_minLevel) {
        return;
    }

    const char *levelStr = "DEBUG";
    switch (lvl) {
    case Debug:   levelStr = "DEBUG";   break;
    case Info:    levelStr = "INFO";    break;
    case Warning: levelStr = "WARN";    break;
    case Error:   levelStr = "ERROR";   break;
    }

    const QString line = QString("[%1] [%2] [%3] %4\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
        .arg(levelStr)
        .arg(tag)
        .arg(msg);

    // Also output to stderr / 同时输出到标准错误
    fputs(line.toUtf8().constData(), stderr);

    if (m_file.isOpen()) {
        m_file.write(line.toUtf8());
        m_file.flush();
        rotateIfNeeded();
    }
}

void Logger::rotateIfNeeded()
{
    if (m_file.size() < m_maxFileSize) {
        return;
    }
    // Rotate: current -> .1, drop old .1 / 轮转：当前文件 -> .1，丢弃旧 .1
    const QString oldPath = m_file.fileName() + ".1";
    m_file.close();
    QFile::remove(oldPath);
    QFile::rename(m_file.fileName(), oldPath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        fputs("Logger: failed to reopen rotated log file\n", stderr);
    }
}
