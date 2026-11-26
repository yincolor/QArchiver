#ifndef QARCHIVER_UI_ONECLICKEXTRACTCONTROLLER_H
#define QARCHIVER_UI_ONECLICKEXTRACTCONTROLLER_H

// QArchiver / 小Q解压
// State and destination planning for one-click extraction.
// 一键解压的状态与目标目录规划。

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include "common/ArchiveOptions.h"

// Owns the small amount of state behind one-click extraction and builds the
// extraction plan (destination, options, close-after behaviour).  The main
// window keeps the UI orchestration: dialogs, status bar, progress dialog and
// closing the window.
// 持有“一键解压”的少量状态，并生成解压计划（目标目录、选项、完成后是否关闭）。
// 主窗口只负责界面编排：对话框、状态栏、进度对话框与关闭窗口。
class OneClickExtractController {
    Q_DECLARE_TR_FUNCTIONS(OneClickExtractController)

public:
    struct Plan {
        bool ok = false;
        QString errorMessage;
        QString destination;
        ExtractOptions options;
        bool closeAfter = false;
        bool openDestAfterFinish = false;
    };

    // State / 状态
    void request() { m_pending = true; }
    bool isPending() const { return m_pending; }
    void clearPending() { m_pending = false; }
    void start(bool closeAfter);
    bool isActive() const { return m_active; }
    // Finish the active one-click extraction and return its close-after choice.
    // 结束当前一键解压并返回“完成后关闭”的选择。
    bool takeFinished();
    void reset();

    // "notes.tar.gz" -> "notes". / "notes.tar.gz" -> "notes"。
    static QString archiveBaseName(const QString &archivePath);

    // Root chosen by the preferences: next to the archive or a custom folder.
    // 偏好设置选择的目标根目录：压缩包所在目录或自定义文件夹。
    QString destinationRoot(const QString &archivePath) const;

    // Create the destination root if needed and build the extraction plan.
    // 如有需要创建目标根目录，并生成解压计划。
    Plan plan(const QString &archivePath, const QStringList &selected,
              const QString &password, bool closeAfter) const;

private:
    bool m_pending = false;
    bool m_active = false;
    bool m_closeAfter = false;
};

#endif // QARCHIVER_UI_ONECLICKEXTRACTCONTROLLER_H
