// QArchiver / 小Q解压
// PreferencesDialog: tabbed settings, one-click extraction and benchmark entry.
// PreferencesDialog：分页设置、一键解压与性能测试入口。

#include "core/ArchiveBackendFactory.h"
#include "ui/dialogs/PreferencesDialog.h"

#include "ui/dialogs/BenchmarkDialog.h"
#include "ui/dialogs/FileAssociationDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QScopeGuard>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSize>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

#include "backends/rar/RarBackend.h"
#include "core/AppSettings.h"
#include "core/ArchiveManager.h"
#include "ui/background/BackgroundImage.h"
#include "ui/icons/IconUtils.h"
#include "utils/SevenZipTool.h"
#include "ui/theme/ThemeManager.h"

namespace {

QLabel *hintLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setObjectName(QStringLiteral("WelcomeSubtitle"));
    return label;
}

QPushButton *makeBrowseButton(QWidget *parent)
{
    auto *button = new QPushButton(
        Icons::aero(QStringLiteral("folder.png")), QObject::tr("Browse..."),
        parent);
    button->setIconSize(QSize(16, 16));
    return button;
}

} // namespace

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setMinimumSize(620, 520);
    resize(680, 640);

    auto *tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);

    auto *generalPage = new QWidget(tabs);
    auto *generalLayout = new QVBoxLayout(generalPage);
    generalLayout->setContentsMargins(14, 14, 14, 14);
    generalLayout->setSpacing(10);

    auto *extractPage = new QWidget(tabs);
    auto *extractLayout = new QVBoxLayout(extractPage);
    extractLayout->setContentsMargins(14, 14, 14, 14);
    extractLayout->setSpacing(10);

    auto *interfacePage = new QWidget(tabs);
    auto *interfaceLayout = new QVBoxLayout(interfacePage);
    interfaceLayout->setContentsMargins(14, 14, 14, 14);
    interfaceLayout->setSpacing(10);

    auto *toolsPage = new QWidget(tabs);
    auto *toolsLayout = new QVBoxLayout(toolsPage);
    toolsLayout->setContentsMargins(14, 14, 14, 14);
    toolsLayout->setSpacing(10);

    // --- General ---------------------------------------------------------
    {
        auto &settings = AppSettings::instance();
        m_defaultFormatCombo = new QComboBox(generalPage);
        m_defaultFormatCombo->addItem(tr("Zip"), static_cast<int>(ArchiveFormat::Zip));
        m_defaultFormatCombo->addItem(tr("7z"), static_cast<int>(ArchiveFormat::SevenZip));
        if (RarBackend::isCreateAvailable()) {
            m_defaultFormatCombo->addItem(tr("RAR"), static_cast<int>(ArchiveFormat::Rar5));
        }
        m_defaultFormatCombo->addItem(tr("Tar"), static_cast<int>(ArchiveFormat::Tar));
        m_defaultFormatCombo->addItem(tr("Tar.gz"), static_cast<int>(ArchiveFormat::TarGz));
        const int formatIndex = m_defaultFormatCombo->findData(
            static_cast<int>(settings.defaultCompressFormat()));
        m_defaultFormatCombo->setCurrentIndex(formatIndex >= 0 ? formatIndex : 0);

        m_defaultLevelSpin = new QSpinBox(generalPage);
        m_defaultLevelSpin->setRange(0, 9);
        m_defaultLevelSpin->setValue(settings.defaultCompressionLevel());

        m_overwriteCombo = new QComboBox(generalPage);
        m_overwriteCombo->addItem(tr("Ask"), static_cast<int>(OverwritePolicy::Ask));
        m_overwriteCombo->addItem(tr("Always overwrite"), static_cast<int>(OverwritePolicy::Always));
        m_overwriteCombo->addItem(tr("Never overwrite"), static_cast<int>(OverwritePolicy::Never));
        const int overwriteIndex = m_overwriteCombo->findData(
            static_cast<int>(settings.overwritePolicy()));
        m_overwriteCombo->setCurrentIndex(overwriteIndex >= 0 ? overwriteIndex : 0);

        m_preservePathsCheck = new QCheckBox(
            tr("Preserve directory structure when extracting"), generalPage);
        m_preservePathsCheck->setChecked(settings.preservePaths());

        auto *compressionGroup = new QGroupBox(tr("Compression"), generalPage);
        auto *compressionForm = new QFormLayout(compressionGroup);
        compressionForm->setHorizontalSpacing(12);
        compressionForm->setVerticalSpacing(8);
        compressionForm->addRow(tr("Default compression &format:"), m_defaultFormatCombo);
        compressionForm->addRow(tr("Default compression &level:"), m_defaultLevelSpin);
        compressionForm->addRow(tr("&Overwrite policy:"), m_overwriteCombo);
        compressionForm->addRow(QString(), m_preservePathsCheck);

        m_openAfterCheck = new QCheckBox(
            tr("Open destination after extraction"), generalPage);
        m_openAfterCheck->setChecked(settings.openDestAfterFinish());
        m_followSymlinksCheck = new QCheckBox(
            tr("Follow symlinks when compressing"), generalPage);
        m_followSymlinksCheck->setChecked(settings.followSymlinks());
        m_rememberPasswordsCheck = new QCheckBox(
            tr("Remember passwords for the session"), generalPage);
        m_rememberPasswordsCheck->setChecked(settings.rememberPasswords());
        m_rememberPasswordsCheck->setToolTip(
            tr("Passwords are kept in memory only and are never written to disk."));

        m_libArchiveCheck = new QCheckBox(
            tr("Read archives with libarchive (no external process)"), generalPage);
        const bool libArchiveUsable = ArchiveBackendFactory::isLibArchiveAvailable()
            && !ArchiveBackendFactory::isLibArchiveDisabledByEnvironment();
        m_libArchiveCheck->setChecked(settings.useLibArchiveBackend()
                                      && libArchiveUsable);
        if (libArchiveUsable) {
            m_libArchiveCheck->setToolTip(
                tr("zip, tar, 7z and gz/bz2/xz archives are read inside the "
                   "application. Archives that libarchive cannot open (encrypted "
                   "7z, split volumes, rar) automatically switch to the command "
                   "line tools. The change applies to the next archive you open."));
            m_libArchiveHint = hintLabel(
                tr("Applies to the next archive you open; the currently open "
                   "archive keeps the backend it started with."), generalPage);
        } else {
            m_libArchiveCheck->setChecked(false);
            m_libArchiveCheck->setEnabled(false);
            m_libArchiveCheck->setToolTip(
                ArchiveBackendFactory::isLibArchiveAvailable()
                    ? tr("Disabled by the QARCHIVER_NO_LIBARCHIVE environment variable.")
                    : tr("This build was compiled without libarchive."));
        }

        auto *behaviorGroup = new QGroupBox(tr("Behaviour"), generalPage);
        auto *behaviorLayout = new QVBoxLayout(behaviorGroup);
        behaviorLayout->setSpacing(7);
        behaviorLayout->addWidget(m_openAfterCheck);
        behaviorLayout->addWidget(m_followSymlinksCheck);
        behaviorLayout->addWidget(m_rememberPasswordsCheck);
        behaviorLayout->addWidget(m_libArchiveCheck);
        if (m_libArchiveHint) {
            behaviorLayout->addWidget(m_libArchiveHint);
        }

        generalLayout->addWidget(compressionGroup);
        generalLayout->addWidget(behaviorGroup);
        generalLayout->addStretch(1);
    }

    // --- Extract ---------------------------------------------------------
    {
        auto &settings = AppSettings::instance();
        m_maxPreviewSpin = new QSpinBox(extractPage);
        m_maxPreviewSpin->setRange(1, 500);
        m_maxPreviewSpin->setSuffix(tr(" MB"));
        m_maxPreviewSpin->setValue(
            static_cast<int>(settings.maxPreviewSize() / (1024 * 1024)));

        m_continuousPreviewSpin = new QSpinBox(extractPage);
        m_continuousPreviewSpin->setRange(0, 9999);
        m_continuousPreviewSpin->setSpecialValueText(tr("Always"));
        m_continuousPreviewSpin->setSuffix(tr(" images"));
        m_continuousPreviewSpin->setValue(
            settings.continuousImageDialogThreshold());
        m_continuousPreviewSpin->setToolTip(
            tr("Show a preparation dialog when a continuous-preview batch "
               "contains more than this many images. 0 shows it for every "
               "batch."));

        m_externalEditCheck = new QCheckBox(
            tr("Ask before saving externally edited files back into the "
               "archive"), extractPage);
        m_externalEditCheck->setChecked(settings.externalEditWatch());
        m_externalEditCheck->setToolTip(
            tr("When you open a file from the archive in the system "
               "application and save it there, QArchiver asks whether the "
               "change should be stored in the archive. Supported for zip, 7z "
               "and uncompressed tar archives."));

        m_oneClickDestModeCombo = new QComboBox(extractPage);
        m_oneClickDestModeCombo->addItem(
            tr("Next to the archive (recommended)"),
            QStringLiteral("archive"));
        m_oneClickDestModeCombo->addItem(
            tr("Custom folder"), QStringLiteral("custom"));
        const int destModeIndex = m_oneClickDestModeCombo->findData(
            settings.oneClickExtractDestMode());
        m_oneClickDestModeCombo->setCurrentIndex(destModeIndex >= 0
                                                    ? destModeIndex : 0);

        m_oneClickDestEdit = new QLineEdit(settings.oneClickExtractDest(),
                                           extractPage);
        const QString documents = QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation);
        m_oneClickDestEdit->setPlaceholderText(
            documents.isEmpty() ? QDir::homePath() : documents);
        m_oneClickBrowseButton = makeBrowseButton(extractPage);
        auto *destinationRow = new QHBoxLayout;
        destinationRow->setSpacing(6);
        destinationRow->addWidget(m_oneClickDestEdit, 1);
        destinationRow->addWidget(m_oneClickBrowseButton);

        m_oneClickCloseCheck = new QCheckBox(
            tr("Close QArchiver after one-click extraction"), extractPage);
        m_oneClickCloseCheck->setChecked(settings.oneClickCloseAfterExtract());
        m_oneClickCloseCheck->setToolTip(
            tr("When enabled, the window closes after a successful one-click "
               "extraction. Failed or cancelled extractions keep the window "
               "open."));

        auto *oneClickGroup = new QGroupBox(tr("One-click Extract"), extractPage);
        auto *oneClickForm = new QFormLayout(oneClickGroup);
        oneClickForm->setHorizontalSpacing(12);
        oneClickForm->setVerticalSpacing(8);
        oneClickForm->addRow(tr("Destination:"), m_oneClickDestModeCombo);
        oneClickForm->addRow(tr("Custom folder:"), destinationRow);
        oneClickForm->addRow(QString(), m_oneClickCloseCheck);
        oneClickForm->addRow(QString(), hintLabel(
            tr("Each archive is extracted into a folder named after it. If that "
               "folder already exists, a unique name such as \"name (1)\" is "
               "used, so existing files are never overwritten silently."),
            oneClickGroup));

        auto *previewGroup = new QGroupBox(tr("Preview"), extractPage);
        auto *previewForm = new QFormLayout(previewGroup);
        previewForm->setHorizontalSpacing(12);
        previewForm->setVerticalSpacing(8);
        previewForm->addRow(tr("Maximum preview &size:"), m_maxPreviewSpin);
        previewForm->addRow(tr("Continuous preview threshold:"),
                            m_continuousPreviewSpin);
        previewForm->addRow(QString(), m_externalEditCheck);

        connect(m_oneClickDestModeCombo, &QComboBox::currentIndexChanged,
                this, [this]() { updateOneClickControls(); });
        connect(m_oneClickBrowseButton, &QPushButton::clicked, this, [this]() {
            const QString folder = QFileDialog::getExistingDirectory(
                this, tr("Select One-click Extract Folder"),
                m_oneClickDestEdit->text());
            if (!folder.isEmpty()) {
                m_oneClickDestEdit->setText(folder);
                m_oneClickDestModeCombo->setCurrentIndex(
                    m_oneClickDestModeCombo->findData(QStringLiteral("custom")));
            }
        });
        updateOneClickControls();

        extractLayout->addWidget(oneClickGroup);
        extractLayout->addWidget(previewGroup);
        extractLayout->addStretch(1);
    }

    // --- Interface -------------------------------------------------------
    {
        auto &settings = AppSettings::instance();
        m_layoutModeCombo = new QComboBox(interfacePage);
        m_layoutModeCombo->setObjectName(QStringLiteral("PrefsLayoutModeCombo"));
        m_layoutModeCombo->addItem(tr("Modern (HaoZip-style)"),
                                   QStringLiteral("modern"));
        m_layoutModeCombo->addItem(tr("Classic (previous layout)"),
                                   QStringLiteral("classic"));
        const int layoutIndex = m_layoutModeCombo->findData(
            settings.uiLayoutMode());
        m_layoutModeCombo->setCurrentIndex(layoutIndex >= 0 ? layoutIndex : 0);
        m_layoutModeCombo->setToolTip(
            tr("Modern is the flat HaoZip-style layout with a folder pane and a "
               "details pane. Classic keeps the previous toolbar and optional "
               "recent-files sidebar. The change applies immediately."));

        m_themeCombo = new QComboBox(interfacePage);
        m_themeCombo->addItem(tr("System"), QStringLiteral("system"));
        m_themeCombo->addItem(tr("Light"), QStringLiteral("light"));
        m_themeCombo->addItem(tr("Dark"), QStringLiteral("dark"));
        const int themeIndex = m_themeCombo->findData(settings.theme());
        m_themeCombo->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);

        m_viewModeCombo = new QComboBox(interfacePage);
        m_viewModeCombo->setObjectName(QStringLiteral("PrefsViewModeCombo"));
        m_viewModeCombo->addItem(tr("Grid view (default)"),
                                 QStringLiteral("grid"));
        m_viewModeCombo->addItem(tr("List view"), QStringLiteral("list"));
        m_viewModeCombo->addItem(tr("Tree view"), QStringLiteral("tree"));
        const int viewModeIndex = m_viewModeCombo->findData(
            settings.contentViewMode());
        m_viewModeCombo->setCurrentIndex(viewModeIndex >= 0 ? viewModeIndex : 0);

        m_toolbarStyleCombo = new QComboBox(interfacePage);
        m_toolbarStyleCombo->setObjectName(QStringLiteral("PrefsToolbarStyleCombo"));
        m_toolbarStyleCombo->addItem(tr("Icons above text"),
                                     QStringLiteral("textUnderIcon"));
        m_toolbarStyleCombo->addItem(tr("Icons beside text"),
                                     QStringLiteral("textBesideIcon"));
        m_toolbarStyleCombo->addItem(tr("Text only"),
                                     QStringLiteral("textOnly"));
        m_toolbarStyleCombo->addItem(tr("Icons only (tooltips)"),
                                     QStringLiteral("iconOnly"));
        const int toolbarStyleIndex = m_toolbarStyleCombo->findData(
            settings.toolButtonStyle());
        m_toolbarStyleCombo->setCurrentIndex(toolbarStyleIndex >= 0
                                                 ? toolbarStyleIndex : 0);
        m_toolbarStyleCombo->setToolTip(
            tr("How the toolbar buttons show text and icons."));

        auto *layoutGroup = new QGroupBox(tr("Layout and appearance"),
                                          interfacePage);
        auto *layoutForm = new QFormLayout(layoutGroup);
        m_layoutForm = layoutForm;
        layoutForm->setHorizontalSpacing(12);
        layoutForm->setVerticalSpacing(8);
        layoutForm->addRow(tr("&Interface layout:"), m_layoutModeCombo);
        layoutForm->addRow(tr("&Theme:"), m_themeCombo);
        layoutForm->addRow(tr("Archive content &view:"), m_viewModeCombo);
        // Rows that only exist in one layout are hidden, not disabled.
        // 只在某一种布局中生效的设置直接隐藏，而不是置灰。
        layoutForm->addRow(tr("&Toolbar style:"), m_toolbarStyleCombo);

        m_showRecentFilesCheck = new QCheckBox(
            tr("Show recent &files sidebar"), interfacePage);
        m_showRecentFilesCheck->setObjectName(QStringLiteral("PrefsRecentFilesCheck"));
        m_showRecentFilesCheck->setChecked(settings.showRecentFiles());
        m_recentFilesLimitSpin = new QSpinBox(interfacePage);
        m_recentFilesLimitSpin->setRange(1, 100);
        m_recentFilesLimitSpin->setValue(settings.recentFilesLimit());

        auto *recentGroup = new QGroupBox(tr("Recent files"), interfacePage);
        auto *recentForm = new QFormLayout(recentGroup);
        m_recentForm = recentForm;
        recentForm->setHorizontalSpacing(12);
        recentForm->setVerticalSpacing(8);
        recentForm->addRow(m_showRecentFilesCheck);
        recentForm->addRow(tr("Recent files &limit:"), m_recentFilesLimitSpin);

        m_backgroundPathEdit = new QLineEdit(settings.backgroundImagePath(),
                                             interfacePage);
        m_backgroundPathEdit->setPlaceholderText(tr("No background image"));
        auto *backgroundBrowse = makeBrowseButton(interfacePage);
        m_backgroundClearButton = new QPushButton(tr("Clear"), interfacePage);
        auto *backgroundPathLayout = new QHBoxLayout;
        backgroundPathLayout->setSpacing(6);
        backgroundPathLayout->addWidget(m_backgroundPathEdit, 1);
        backgroundPathLayout->addWidget(backgroundBrowse);
        backgroundPathLayout->addWidget(m_backgroundClearButton);

        m_backgroundAlignCombo = new QComboBox(interfacePage);
        m_backgroundAlignCombo->addItem(tr("Top left"), QStringLiteral("topLeft"));
        m_backgroundAlignCombo->addItem(tr("Top"), QStringLiteral("top"));
        m_backgroundAlignCombo->addItem(tr("Top right"), QStringLiteral("topRight"));
        m_backgroundAlignCombo->addItem(tr("Left"), QStringLiteral("left"));
        m_backgroundAlignCombo->addItem(tr("Center"), QStringLiteral("center"));
        m_backgroundAlignCombo->addItem(tr("Right"), QStringLiteral("right"));
        m_backgroundAlignCombo->addItem(tr("Bottom left"), QStringLiteral("bottomLeft"));
        m_backgroundAlignCombo->addItem(tr("Bottom"), QStringLiteral("bottom"));
        m_backgroundAlignCombo->addItem(tr("Bottom right"), QStringLiteral("bottomRight"));
        const int backgroundAlignIndex = m_backgroundAlignCombo->findData(
            settings.backgroundImageAlignment());
        m_backgroundAlignCombo->setCurrentIndex(
            backgroundAlignIndex >= 0 ? backgroundAlignIndex
                                      : m_backgroundAlignCombo->count() - 1);

        const QSize backgroundSize = settings.backgroundImageSize();
        m_backgroundWidthSpin = new QSpinBox(interfacePage);
        m_backgroundWidthSpin->setRange(0, 8192);
        m_backgroundWidthSpin->setSpecialValueText(tr("Auto"));
        m_backgroundWidthSpin->setSuffix(tr(" px"));
        m_backgroundWidthSpin->setValue(backgroundSize.width());
        m_backgroundHeightSpin = new QSpinBox(interfacePage);
        m_backgroundHeightSpin->setRange(0, 8192);
        m_backgroundHeightSpin->setSpecialValueText(tr("Auto"));
        m_backgroundHeightSpin->setSuffix(tr(" px"));
        m_backgroundHeightSpin->setValue(backgroundSize.height());
        auto *backgroundSizeLayout = new QHBoxLayout;
        backgroundSizeLayout->setSpacing(6);
        backgroundSizeLayout->addWidget(m_backgroundWidthSpin);
        backgroundSizeLayout->addWidget(new QLabel(QStringLiteral("×"),
                                                   interfacePage));
        backgroundSizeLayout->addWidget(m_backgroundHeightSpin);
        backgroundSizeLayout->addStretch(1);

        m_backgroundOpacitySpin = new QSpinBox(interfacePage);
        m_backgroundOpacitySpin->setRange(0, 100);
        m_backgroundOpacitySpin->setSuffix(QStringLiteral(" %"));
        m_backgroundOpacitySpin->setValue(
            qRound(settings.backgroundImageOpacity() * 100.0));

        auto *backgroundGroup = new QGroupBox(tr("Background image"),
                                              interfacePage);
        auto *backgroundForm = new QFormLayout(backgroundGroup);
        backgroundForm->setHorizontalSpacing(12);
        backgroundForm->setVerticalSpacing(8);
        backgroundForm->addRow(tr("Background &image:"), backgroundPathLayout);
        backgroundForm->addRow(tr("Image &alignment:"), m_backgroundAlignCombo);
        backgroundForm->addRow(tr("Image &size:"), backgroundSizeLayout);
        backgroundForm->addRow(tr("Image &opacity:"), m_backgroundOpacitySpin);
        backgroundForm->addRow(hintLabel(
            tr("The image is painted behind the list and tree views; the grid "
               "view does not show it."), backgroundGroup));

        connect(m_layoutModeCombo, &QComboBox::currentIndexChanged, this,
                [this]() { updateLayoutVisibility(); });
        connect(m_backgroundPathEdit, &QLineEdit::textChanged, this,
                [this](const QString &) { updateBackgroundControls(); });
        connect(backgroundBrowse, &QPushButton::clicked, this, [this]() {
            const QString path = QFileDialog::getOpenFileName(
                this, tr("Select Background Image"),
                m_backgroundPathEdit->text(),
                tr("PNG images (*.png);;All images (*.png *.jpg *.jpeg *.bmp "
                   "*.gif *.webp);;All files (*)"));
            if (!path.isEmpty()) {
                m_backgroundPathEdit->setText(path);
            }
        });
        connect(m_backgroundClearButton, &QPushButton::clicked, this,
                [this]() { m_backgroundPathEdit->clear(); });
        updateBackgroundControls();
        updateLayoutVisibility();

        interfaceLayout->addWidget(layoutGroup);
        interfaceLayout->addWidget(recentGroup);
        interfaceLayout->addWidget(backgroundGroup);
        interfaceLayout->addStretch(1);
    }

    // --- Tools -----------------------------------------------------------
    {
        auto *intro = hintLabel(
            tr("The 7-Zip benchmark measures compression and decompression "
               "throughput using the same engine that handles your archives. "
               "It is a quick way to check CPU performance and thermal "
               "throttling; no archive data is modified."), toolsPage);

        auto *benchmarkButton = new QPushButton(
            Icons::modern(QStringLiteral("toolbox.png")),
            tr("Run 7-Zip Benchmark..."), toolsPage);
        benchmarkButton->setIconSize(QSize(18, 18));
        const QString toolPath = SevenZipTool::resolvePath();
        benchmarkButton->setEnabled(!toolPath.isEmpty());
        connect(benchmarkButton, &QPushButton::clicked, this, [this]() {
            BenchmarkDialog dialog(this);
            dialog.exec();
        });

        auto *pathLabel = new QLabel(
            toolPath.isEmpty()
                ? tr("7-Zip helper: not found. Install 7-Zip or place the bundled "
                     "helper in the '7zip' directory next to the application.")
                : tr("7-Zip helper: %1").arg(toolPath),
            toolsPage);
        pathLabel->setWordWrap(true);
        pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        pathLabel->setObjectName(QStringLiteral("ModernDetailsValue"));

        auto *toolsGroup = new QGroupBox(tr("Benchmark"), toolsPage);
        auto *toolsGroupLayout = new QVBoxLayout(toolsGroup);
        toolsGroupLayout->setSpacing(10);
        toolsGroupLayout->addWidget(intro);
        toolsGroupLayout->addWidget(benchmarkButton, 0, Qt::AlignLeft);
        toolsGroupLayout->addWidget(pathLabel);

        auto *note = hintLabel(
            tr("The classic layout and the modern layout share the same "
               "archive engine and settings. Switch layouts at any time under "
               "Interface."), toolsPage);

        toolsLayout->addWidget(toolsGroup);

        // File associations / 文件关联
        auto *assocGroup = new QGroupBox(tr("File Associations"), toolsPage);
        auto *assocLayout = new QVBoxLayout(assocGroup);
        assocLayout->setSpacing(10);
        auto *assocIntro = hintLabel(
            tr("Choose which archive formats should open with QArchiver. "
               "Windows stores per-user associations; Linux updates the "
               "desktop MIME defaults."),
            toolsPage);
        auto *assocButton = new QPushButton(
            Icons::modern(QStringLiteral("settings.png")),
            tr("Manage File Associations..."), toolsPage);
        assocButton->setIconSize(QSize(18, 18));
        connect(assocButton, &QPushButton::clicked, this, [this]() {
            FileAssociationDialog dialog(this);
            dialog.exec();
        });
        assocLayout->addWidget(assocIntro);
        assocLayout->addWidget(assocButton, 0, Qt::AlignLeft);
        toolsLayout->addWidget(assocGroup);
        toolsLayout->addWidget(note);
        toolsLayout->addStretch(1);
    }

    tabs->addTab(generalPage, tr("General"));
    tabs->addTab(extractPage, tr("Extract"));
    tabs->addTab(interfacePage, tr("Interface"));
    tabs->addTab(toolsPage, tr("Tools"));

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setCenterButtons(false);
    if (auto *ok = buttons->button(QDialogButtonBox::Ok)) {
        ok->setIcon(Icons::aero(QStringLiteral("yes.ico")));
        ok->setText(tr("OK"));
        ok->setIconSize(QSize(16, 16));
        ok->setMinimumWidth(75);
    }
    if (auto *cancel = buttons->button(QDialogButtonBox::Cancel)) {
        cancel->setIcon(Icons::aero(QStringLiteral("no.ico")));
        cancel->setText(tr("Cancel"));
        cancel->setIconSize(QSize(16, 16));
        cancel->setMinimumWidth(75);
    }
    connect(buttons, &QDialogButtonBox::accepted,
            this, &PreferencesDialog::saveSettings);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);
    layout->addWidget(tabs, 1);
    layout->addWidget(buttons);
}

void PreferencesDialog::updateLayoutVisibility()
{
    // Options that only apply to one layout are hidden instead of disabled, so
    // the Interface page stays short and has no dead entries.
    // 仅在一种布局中生效的选项直接隐藏（而不是置灰），让界面页保持简洁。
    const bool classic = m_layoutModeCombo
        && m_layoutModeCombo->currentData().toString() == QStringLiteral("classic");
    if (m_layoutForm) {
        // The content-view choice applies to both layouts. Classic has no
        // toolbar switcher, but this row is the supported entry point there.
        // 内容视图对两种布局都生效。经典布局没有工具栏切换按钮，
        // 偏好设置中的这一行就是它的切换入口。
        m_layoutForm->setRowVisible(m_viewModeCombo, true);
        m_layoutForm->setRowVisible(m_toolbarStyleCombo, classic);
    }
    if (m_recentForm) {
        // The recent-files sidebar only exists in the classic layout; the limit
        // applies to both layouts (classic sidebar and modern welcome page).
        // 最近文件侧边栏只存在于经典布局；上限对两种布局都生效
        // （经典侧边栏与现代欢迎页）。
        m_recentForm->setRowVisible(m_showRecentFilesCheck, classic);
    }
}

void PreferencesDialog::updateOneClickControls()
{
    const bool custom = m_oneClickDestModeCombo
        && m_oneClickDestModeCombo->currentData().toString()
            == QStringLiteral("custom");
    if (m_oneClickDestEdit) {
        m_oneClickDestEdit->setEnabled(custom);
    }
    if (m_oneClickBrowseButton) {
        m_oneClickBrowseButton->setEnabled(custom);
    }
}

void PreferencesDialog::updateBackgroundControls()
{
    const bool hasImage = m_backgroundPathEdit
        && !m_backgroundPathEdit->text().trimmed().isEmpty();
    if (m_backgroundClearButton) {
        m_backgroundClearButton->setEnabled(hasImage);
    }
    if (m_backgroundAlignCombo) {
        m_backgroundAlignCombo->setEnabled(hasImage);
    }
    if (m_backgroundWidthSpin) {
        m_backgroundWidthSpin->setEnabled(hasImage);
    }
    if (m_backgroundHeightSpin) {
        m_backgroundHeightSpin->setEnabled(hasImage);
    }
    if (m_backgroundOpacitySpin) {
        m_backgroundOpacitySpin->setEnabled(hasImage);
    }
}

void PreferencesDialog::saveSettings()
{
    auto &s = AppSettings::instance();

    // ~25 setters: batch them into a single disk sync.
    // 约 25 次设置：合并为一次磁盘写入。
    s.beginBatch();
    const auto endBatch = qScopeGuard([&s]() { s.endBatch(); });

    // Layout first: ThemeManager::applyTheme() reads it to choose the modern or
    // Aero style sheet.
    // 先保存布局：ThemeManager::applyTheme() 会据此选择现代或 Aero 样式表。
    s.setUiLayoutMode(m_layoutModeCombo->currentData().toString());

    s.setDefaultCompressFormat(
        static_cast<ArchiveFormat>(m_defaultFormatCombo->currentData().toInt()));
    s.setDefaultCompressionLevel(m_defaultLevelSpin->value());
    s.setOverwritePolicy(
        static_cast<OverwritePolicy>(m_overwriteCombo->currentData().toInt()));
    s.setPreservePaths(m_preservePathsCheck->isChecked());
    s.setOpenDestAfterFinish(m_openAfterCheck->isChecked());
    s.setMaxPreviewSize(qint64(m_maxPreviewSpin->value()) * 1024 * 1024);
    s.setContinuousImageDialogThreshold(m_continuousPreviewSpin->value());
    s.setExternalEditWatch(m_externalEditCheck->isChecked());
    s.setFollowSymlinks(m_followSymlinksCheck->isChecked());
    s.setRememberPasswords(m_rememberPasswordsCheck->isChecked());
    if (m_libArchiveCheck->isEnabled()) {
        s.setUseLibArchiveBackend(m_libArchiveCheck->isChecked());
    }
    s.setTheme(m_themeCombo->currentData().toString());
    s.setContentViewMode(m_viewModeCombo->currentData().toString());
    s.setToolButtonStyle(m_toolbarStyleCombo->currentData().toString());
    s.setShowRecentFiles(m_showRecentFilesCheck->isChecked());
    s.setRecentFilesLimit(m_recentFilesLimitSpin->value());

    const QString oldDestMode = s.oneClickExtractDestMode();
    const bool oldCloseAfter = s.oneClickCloseAfterExtract();
    s.setOneClickExtractDestMode(m_oneClickDestModeCombo->currentData().toString());
    s.setOneClickExtractDest(m_oneClickDestEdit->text().trimmed());
    s.setOneClickCloseAfterExtract(m_oneClickCloseCheck->isChecked());
    if (s.oneClickExtractDestMode() != oldDestMode
        || s.oneClickCloseAfterExtract() != oldCloseAfter) {
        // An explicit preference change replaces the first-use prompt.
        // 用户显式修改设置后不再弹出首次提示。
        s.setOneClickCloseAsked(true);
    }

    s.setBackgroundImageOptions(
        m_backgroundPathEdit->text().trimmed(),
        m_backgroundAlignCombo->currentData().toString(),
        QSize(m_backgroundWidthSpin->value(), m_backgroundHeightSpin->value()),
        m_backgroundOpacitySpin->value() / 100.0);

    // Apply the theme immediately so the change is visible without restarting.
    // 立即应用主题，无需重启即可看到变化。
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        ThemeManager::applyTheme(*app);
    }
}
