// QArchiver / 小Q解压
// Small helpers shared by the MainWindow implementation files.
// 由 MainWindow 各实现文件共享的小型辅助函数。

#include "ui/MainWindowHelpers.h"

#include <QCoreApplication>
#include <QLocale>

#include "common/FormatRegistry.h"

namespace MainWindowHelpers {

QString archiveDialogFilter()
{
    // Build the pattern list from FormatRegistry so the Open dialog cannot
    // drift when a format is added; only the labels are translated.  The
    // "MainWindow" context keeps the existing translations valid.
    // 从 FormatRegistry 构建模式列表，新增格式时打开对话框不会漂移；只翻译
    // 标签。沿用 "MainWindow" 上下文，使现有翻译继续有效。
    return QCoreApplication::translate("MainWindow", "Archives")
        + QStringLiteral(" (")
        + FormatRegistry::openGlobPatterns().join(QLatin1Char(' '))
        + QStringLiteral(");;")
        + QCoreApplication::translate("MainWindow", "All Files")
        + QStringLiteral(" (*)");
}

QString formatFileSize(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes);
}

} // namespace MainWindowHelpers
