// QArchiver / 小Q解压
// Logger implementation: formatting, mutexing and log rotation.
// Logger 实现：格式化、互斥保护与日志轮转。

#include "utils/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QTextStream>
#include <QStandardPaths>
#include <QFileInfo>

Logger::Logger()
{
    // Enable debug logging via QARCHIVER_DEBUG=1.  SAM_DEBUG is kept as a
    // compatibility alias for older scripts and bug reports.
    // 通过 QARCHIVER_DEBUG=1 启用调试日志；SAM_DEBUG 作为旧脚本/反馈的兼容别名保留。
    if (qEnvironmentVariableIsSet("QARCHIVER_DEBUG")
        || qEnvironmentVariableIsSet("SAM_DEBUG")) {
        m_minLevel = Debug;
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
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
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
