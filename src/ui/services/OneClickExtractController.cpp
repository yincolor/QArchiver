// QArchiver / 小Q解压
// OneClickExtractController implementation.
// OneClickExtractController 实现。

#include "ui/services/OneClickExtractController.h"

#include "common/FormatRegistry.h"
#include "core/AppSettings.h"
#include "utils/FileUtils.h"
#include "utils/MimeTypeHelper.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

void OneClickExtractController::start(bool closeAfter)
{
    m_pending = false;
    m_active = true;
    m_closeAfter = closeAfter;
}

bool OneClickExtractController::takeFinished()
{
    const bool closeAfter = m_closeAfter;
    m_active = false;
    m_closeAfter = false;
    return closeAfter;
}

void OneClickExtractController::reset()
{
    m_pending = false;
    m_active = false;
    m_closeAfter = false;
}

QString OneClickExtractController::archiveBaseName(const QString &archivePath)
{
    QString name =
        FormatRegistry::stripArchiveExtension(QFileInfo(archivePath).fileName());
    if (name.isEmpty()) {
        name = QStringLiteral("archive");
    }
    return name;
}

QString OneClickExtractController::destinationRoot(const QString &archivePath) const
{
    const AppSettings &settings = AppSettings::instance();
    QString root;
    if (settings.oneClickExtractDestMode() == QStringLiteral("custom")) {
        root = settings.oneClickExtractDest();
    } else {
        root = QFileInfo(archivePath).absolutePath();
    }
    if (root.isEmpty()) {
        root = QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation);
    }
    if (root.isEmpty()) {
        root = QDir::homePath();
    }
    return QDir::cleanPath(root);
}

OneClickExtractController::Plan OneClickExtractController::plan(
    const QString &archivePath, const QStringList &selected,
    const QString &password, bool closeAfter) const
{
    Plan result;

    const QString root = destinationRoot(archivePath);
    if (root.isEmpty() || !QDir().mkpath(root)) {
        result.errorMessage =
            tr("Cannot create the destination directory:\n%1").arg(root);
        return result;
    }
    if (!QFileInfo(root).isDir() || !QFileInfo(root).isWritable()) {
        result.errorMessage =
            tr("The destination directory is not writable:\n%1").arg(root);
        return result;
    }

    // Split archives are named after their first volume, so all volumes produce
    // the same folder instead of ".001", ".002" siblings.
    // 分卷压缩包按首卷命名，使所有分卷得到同一个文件夹，而不是“.001/.002”副本。
    const QString baseSource = MimeTypeHelper::isSplitVolume(archivePath)
        ? MimeTypeHelper::resolveFirstVolume(archivePath)
        : archivePath;
    const QString baseName = archiveBaseName(baseSource);
    QString destination = QDir(root).filePath(baseName);
    if (QFileInfo::exists(destination) || QFileInfo(destination).isSymLink()) {
        destination = FileUtils::uniqueDestinationPath(destination);
    }

    ExtractOptions options;
    options.destDir = destination;
    options.preservePaths = true;
    options.overwrite = OverwritePolicy::Always;
    options.password = password;
    options.openDestAfterFinish = false;
    if (!selected.isEmpty()) {
        options.selectedFiles = selected;
    }

    result.ok = true;
    result.destination = destination;
    result.options = options;
    result.closeAfter = closeAfter;
    result.openDestAfterFinish =
        AppSettings::instance().openDestAfterFinish() && !closeAfter;
    return result;
}
