#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

// QArchiver / 小Q解压
// GitHub release update checker.
// GitHub Release 版本更新检查。

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// Queries the latest release of the upstream repository and reports whether a
// newer version is available. Network work is asynchronous.
// 查询上游最新 Release，并异步报告是否有新版本。
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    static UpdateChecker &instance();

    // Start a check. When @p silent is true, failures are not emitted as
    // dialogs by the caller.
    // 开始检查；silent 为 true 时调用方不弹错误框。
    void check(bool silent = false);

    // Exposed for tests: compare "2.4.1" / "v2.4.1-1" style versions.
    // 供测试使用：比较 "2.4.1" / "v2.4.1-1" 形式版本号。
    static bool isNewerVersion(const QString &candidate, const QString &current);

signals:
    void updateAvailable(const QString &version, const QString &releaseUrl,
                         const QString &releaseNotes);
    void upToDate(const QString &currentVersion);
    void checkFailed(const QString &message);

private:
    UpdateChecker();
    Q_DISABLE_COPY(UpdateChecker)

    QNetworkAccessManager *m_network = nullptr;
    bool m_inFlight = false;
    bool m_silent = false;
};

#endif // UPDATECHECKER_H
