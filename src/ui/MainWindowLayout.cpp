// QArchiver / 小Q解压
// MainWindow layout: modern and classic chrome, toolbars, sidebars, content
// views, status bar and view-mode switches.
// MainWindow 布局：现代与经典界面外观、工具栏、侧边栏、内容视图、
// 状态栏与视图模式切换。

#include "ui/MainWindow.h"
#include "ui/MainWindow_p.h"

#include "ui/MainWindowHelpers.h"
#include "ui/icons/IconUtils.h"

#include "ui/models/FileListModel.h"
#include "ui/models/FilterProxyModel.h"
#include "ui/models/FileListViewDelegate.h"
#include "ui/services/PreviewManager.h"
#include "ui/services/PreviewUtils.h"
#include "ui/services/DragOutManager.h"
#include "ui/widgets/ArchiveTreeView.h"
#include "ui/widgets/ArchiveGridView.h"
#include "ui/widgets/BackgroundWidget.h"
#include "ui/widgets/SidebarWidget.h"
#include "ui/widgets/ModernToolBar.h"
#include "ui/widgets/ModernNavBar.h"
#include "ui/widgets/BreadcrumbPathBar.h"
#include "ui/widgets/ArchiveFolderPane.h"
#include "ui/widgets/WelcomePage.h"
#include "ui/dialogs/ExtractDialog.h"
#include "ui/dialogs/CompressDialog.h"
#include "ui/dialogs/PasswordDialog.h"
#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/PreferencesDialog.h"
#include "ui/dialogs/BenchmarkDialog.h"
#include "utils/Logger.h"
#include "utils/MimeTypeHelper.h"
#include "utils/SevenZipTool.h"
#include "ui/icons/MessageBoxUtils.h"
#include "utils/FileUtils.h"
#include "ui/icons/FileTypeHelper.h"
#include "ui/theme/ThemeManager.h"

#include "core/ArchiveManager.h"
#include "common/ArchiveError.h"
#include "common/ArchiveOptions.h"
#include "common/ArchiveConstants.h"
#include "core/AppSettings.h"

#include "backends/ArchiveBackend.h"

#include <QSignalBlocker>
#include <QSizeGrip>
#include <QSlider>
#include <QToolButton>


void MainWindow::setupModernUi()
{
    // The large toolbar and folder/details panes need a little more room than
    // the classic layout, especially with English labels.
    // 大号工具栏与文件夹/详细信息面板比经典布局需要更多空间，英文标签尤其如此。
    setMinimumSize(980, 600);
    setupMenuBar();

    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("ModernRoot"));
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_modernToolBar = new ModernToolBar(root);
    m_modernToolBar->addToolAction(m_openAct);
    m_modernToolBar->addToolAction(m_addAct);
    m_modernToolBar->addSeparator();
    m_modernToolBar->addToolAction(m_extractAct);
    m_modernToolBar->addToolAction(m_oneClickAct);
    m_modernToolBar->addToolAction(m_newAct);
    m_modernToolBar->addToolAction(m_deleteAct);
    m_modernToolBar->addToolAction(m_testAct);
    m_modernToolBar->addSeparator();
    m_modernToolBar->addToolAction(m_prefsAct);
    m_modernToolBar->addToolAction(m_aboutAct);
    layout->addWidget(m_modernToolBar);

    m_modernNavBar = new ModernNavBar(root);
    layout->addWidget(m_modernNavBar);

    m_bodySplitter = new QSplitter(Qt::Horizontal, root);
    m_bodySplitter->setChildrenCollapsible(false);
    m_bodySplitter->setHandleWidth(1);

    m_folderPane = new ArchiveFolderPane(m_bodySplitter);
    m_folderPane->setMinimumWidth(180);
    m_folderPane->setMaximumWidth(340);
    m_bodySplitter->addWidget(m_folderPane);

    auto *content = new QWidget(m_bodySplitter);
    content->setObjectName(QStringLiteral("ModernContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_centralStack = new QStackedWidget(content);
    m_centralStack->setObjectName(QStringLiteral("CentralStack"));
    m_centralStack->setFrameShape(QFrame::NoFrame);

    m_welcomePage = new WelcomePage(m_centralStack);

    auto *archivePage = new QWidget(m_centralStack);
    archivePage->setObjectName(QStringLiteral("ArchivePage"));
    auto *archiveLayout = new QVBoxLayout(archivePage);
    archiveLayout->setContentsMargins(0, 0, 0, 0);
    archiveLayout->setSpacing(0);
    m_viewStack = new QStackedWidget(archivePage);
    m_treeView = createArchiveTree(m_viewStack);
    m_gridView = createArchiveGrid(m_viewStack);
    m_viewStack->addWidget(m_treeView);
    m_viewStack->addWidget(m_gridView);
    archiveLayout->addWidget(m_viewStack, 1);

    m_centralStack->addWidget(m_welcomePage);
    m_centralStack->addWidget(archivePage);
    m_centralStack->setCurrentIndex(0);
    contentLayout->addWidget(m_centralStack, 1);

    m_bodySplitter->addWidget(content);
    m_bodySplitter->setStretchFactor(0, 0);
    m_bodySplitter->setStretchFactor(1, 1);
    m_bodySplitter->setSizes({230, 950});

    layout->addWidget(m_bodySplitter, 1);
    setCentralWidget(root);

    connect(m_modernNavBar, &ModernNavBar::backRequested,
            this, &MainWindow::onNavBack);
    connect(m_modernNavBar, &ModernNavBar::forwardRequested,
            this, &MainWindow::onNavForward);
    connect(m_modernNavBar, &ModernNavBar::upRequested,
            this, &MainWindow::onNavUp);
    connect(m_modernNavBar, &ModernNavBar::archiveSegmentActivated,
            this, &MainWindow::onFolderDirectoryActivated);
    connect(m_modernNavBar, &ModernNavBar::localSegmentActivated,
            this, &MainWindow::onFolderDirectoryActivated);
    connect(m_modernNavBar, &ModernNavBar::searchChanged,
            this, &MainWindow::onSearchChanged);
    connect(m_modernNavBar, &ModernNavBar::viewModeChanged,
            this, &MainWindow::onContentViewModeChanged);
    connect(m_modernNavBar, &ModernNavBar::sortMenuRequested, this,
            [this](const QPoint &globalPos) {
        auto *menu = createSortMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(globalPos);
    });
    connect(m_folderPane, &ArchiveFolderPane::directoryActivated,
            this, &MainWindow::onFolderDirectoryActivated);
    connect(m_welcomePage, &WelcomePage::openRequested,
            this, &MainWindow::onOpen);
    connect(m_welcomePage, &WelcomePage::oneClickExtractRequested,
            this, &MainWindow::onOneClickExtract);
    connect(m_welcomePage, &WelcomePage::fileManagerRequested, this, [this]() {
        browseLocalDirectory(AppSettings::instance().localBrowsePath());
    });
    connect(m_welcomePage, &WelcomePage::recentFileActivated,
            this, &MainWindow::onSidebarFileOpened);

    m_modernNavBar->setSearchText(m_searchText);
}

void MainWindow::setupClassicUi()
{
    setMinimumSize(880, 560);
    setupClassicToolbar();
    setupMenuBar();
    setupClassicCentralWidget();
    setupClassicSidebar();
    applyToolButtonStyle();
    applySidebarVisibility();
}

void MainWindow::setupClassicToolbar()
{
    auto *tb = addToolBar(tr("Main"));
    m_mainToolBar = tb;
    tb->setObjectName(QStringLiteral("MainToolBar"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tb->setIconSize(QSize(32, 32));

    tb->addAction(m_openAct);
    tb->addAction(m_newAct);

    tb->addSeparator();
    tb->addAction(m_extractAct);
    tb->addAction(m_oneClickAct);
    tb->addAction(m_testAct);
    tb->addAction(m_addAct);
    tb->addAction(m_deleteAct);

    tb->addSeparator();
    tb->addAction(m_closeAct);
    tb->addAction(m_prefsAct);
    tb->addAction(m_aboutAct);

    auto *spacer = new QWidget(this);
    spacer->setObjectName(QStringLiteral("ToolbarSpacer"));
    spacer->setAttribute(Qt::WA_TranslucentBackground);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    tb->addAction(m_quitAct);

    for (QAction *action : tb->actions()) {
        if (!action->isSeparator() && action->toolTip().isEmpty()) {
            action->setToolTip(action->text());
        }
    }
}

void MainWindow::setupClassicCentralWidget()
{
    m_backgroundWidget = new BackgroundWidget(this);
    m_centralStack = new QStackedWidget(m_backgroundWidget);
    m_centralStack->setObjectName(QStringLiteral("CentralStack"));
    m_centralStack->setFrameShape(QFrame::NoFrame);
    m_centralStack->setAutoFillBackground(false);

    // Placeholder page (shown when no archive is open)
    // 占位页（未打开压缩包时显示）
    auto *placeholder = new QWidget(m_centralStack);
    placeholder->setObjectName(QStringLiteral("PlaceholderPage"));
    placeholder->setAutoFillBackground(false);
    auto *placeholderLayout = new QVBoxLayout(placeholder);
    placeholderLayout->addStretch(1);
    m_placeholderLabel = new QLabel(
        tr("No archive open.\nUse the Open button or drag a file here."),
        placeholder);
    m_placeholderLabel->setObjectName(QStringLiteral("PlaceholderLabel"));
    m_placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLayout->addWidget(m_placeholderLabel);
    auto *openBtn = new QPushButton(Icons::aero(QStringLiteral("Open.png")),
                                    tr("Choose Archive..."), placeholder);
    openBtn->setObjectName(QStringLiteral("PlaceholderOpenButton"));
    openBtn->setIconSize(QSize(16, 16));
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::onOpen);
    placeholderLayout->addWidget(openBtn, 0, Qt::AlignHCenter);
    placeholderLayout->addStretch(1);

    // Archive page: search field + tree
    // 归档页：搜索框 + 树
    auto *archivePage = new QWidget(m_centralStack);
    archivePage->setObjectName(QStringLiteral("ArchivePage"));
    archivePage->setAutoFillBackground(false);
    auto *archiveLayout = new QVBoxLayout(archivePage);
    archiveLayout->setContentsMargins(0, 0, 0, 0);
    archiveLayout->setSpacing(2);

    // Classic navigation sits immediately left of the filter field, in the
    // requested order: Forward, Back, Up, Search.
    // 经典导航按钮位于筛选框左侧，按指定顺序排列：前进、后退、上一级、搜索。
    auto *searchRow = new QWidget(archivePage);
    searchRow->setObjectName(QStringLiteral("ClassicSearchRow"));
    auto *searchRowLayout = new QHBoxLayout(searchRow);
    searchRowLayout->setContentsMargins(0, 0, 0, 0);
    searchRowLayout->setSpacing(2);

    auto addNavButton = [searchRow, searchRowLayout](QAction *action) {
        auto *button = new QToolButton(searchRow);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(20, 20));
        button->setAutoRaise(true);
        searchRowLayout->addWidget(button);
    };
    addNavButton(m_forwardAct);
    addNavButton(m_backAct);
    addNavButton(m_upAct);

    // Breadcrumb location bar in the Windows 7 / Aero style.
    // Windows 7 / Aero 风格的面包屑路径框。
    m_classicBreadcrumb = new BreadcrumbPathBar(searchRow);
    searchRowLayout->addWidget(m_classicBreadcrumb, 1);
    connect(m_classicBreadcrumb,
            &BreadcrumbPathBar::archiveSegmentActivated, this,
            &MainWindow::onFolderDirectoryActivated);
    connect(m_classicBreadcrumb,
            &BreadcrumbPathBar::localSegmentActivated, this,
            &MainWindow::onFolderDirectoryActivated);

    m_searchEdit = new QLineEdit(searchRow);
    m_searchEdit->setObjectName(QStringLiteral("SearchEdit"));
    m_searchEdit->setPlaceholderText(tr("Filter by name or path (wildcards allowed)..."));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(280);
    m_searchAction = new QAction(QString(), m_searchEdit);
    m_searchAction->setEnabled(false);
    m_searchEdit->addAction(m_searchAction, QLineEdit::LeadingPosition);
    updateSearchIcon();
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &MainWindow::onSearchChanged);
    searchRowLayout->addWidget(m_searchEdit);
    archiveLayout->addWidget(searchRow);

    m_viewStack = new QStackedWidget(archivePage);
    m_treeView = createArchiveTree(m_viewStack);
    m_gridView = createArchiveGrid(m_viewStack);
    m_viewStack->addWidget(m_treeView);
    m_viewStack->addWidget(m_gridView);
    archiveLayout->addWidget(m_viewStack, 1);

    m_centralStack->addWidget(placeholder);   // index 0
    m_centralStack->addWidget(archivePage);   // index 1
    m_centralStack->setCurrentIndex(0);

    auto *backgroundLayout = new QVBoxLayout(m_backgroundWidget);
    backgroundLayout->setContentsMargins(0, 0, 0, 0);
    backgroundLayout->setSpacing(0);
    backgroundLayout->addWidget(m_centralStack);
    setCentralWidget(m_backgroundWidget);

    connect(m_centralStack, &QStackedWidget::currentChanged,
            this, &MainWindow::updatePlaceholderBackground);
    m_treeView->reloadBackgroundFromSettings();
    updatePlaceholderBackground();
}

ArchiveTreeView *MainWindow::createArchiveTree(QWidget *parent)
{
    auto *tree = new ArchiveTreeView(parent);
    tree->setObjectName(QStringLiteral("ArchiveTree"));
    tree->setModel(m_proxyModel);
    tree->setItemDelegate(new FileListViewDelegate(tree));
    tree->setFrameShape(QFrame::NoFrame);
    tree->viewport()->setAutoFillBackground(false);
    connect(tree->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this, tree]() {
        if (tree == currentContentView()) {
            onSelectionChanged();
        }
    });
    auto *header = tree->header();
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    header->setStretchLastSection(false);
    header->setSectionsClickable(true);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setMinimumSectionSize(60);
    connect(header, &QHeaderView::sortIndicatorChanged, this,
            [this](int section, Qt::SortOrder order) {
        applySort(section, order);
    });
    tree->setIconSize(QSize(20, 20));
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree, &QTreeView::customContextMenuRequested,
            this, &MainWindow::onTreeContextMenu);
    connect(tree, &ArchiveTreeView::entryDoubleClicked,
            this, &MainWindow::onEntryDoubleClicked);
    connect(tree, &ArchiveTreeView::dragOutRequested,
            this, &MainWindow::onEntriesDragOut);
    connect(tree, &ArchiveTreeView::zoomRequested, this, [this](int direction) {
        applyItemSize(m_itemSize + (direction > 0 ? 8 : -8));
    });
    connect(&AppSettings::instance(), &AppSettings::backgroundImageChanged,
            tree, &ArchiveTreeView::reloadBackgroundFromSettings);

    applyHeaderColumns();
    tree->reloadBackgroundFromSettings();
    return tree;
}

ArchiveGridView *MainWindow::createArchiveGrid(QWidget *parent)
{
    auto *grid = new ArchiveGridView(parent);
    grid->setModel(m_proxyModel);
    connect(grid, &ArchiveGridView::entryActivated,
            this, &MainWindow::onEntryDoubleClicked);
    connect(grid, &ArchiveGridView::dragOutRequested,
            this, &MainWindow::onEntriesDragOut);
    connect(grid, &ArchiveGridView::zoomRequested, this, [this](int direction) {
        applyItemSize(m_itemSize + (direction > 0 ? 8 : -8));
    });
    connect(grid->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this, grid]() {
        if (grid == currentContentView()) {
            onSelectionChanged();
        }
    });
    connect(grid, &QWidget::customContextMenuRequested,
            this, &MainWindow::onTreeContextMenu);
    return grid;
}

void MainWindow::setupClassicSidebar()
{
    m_sidebarDock = new QDockWidget(tr("Recent Files"), this);
    m_sidebarDock->setObjectName(QStringLiteral("SidebarDock"));
    m_sidebarDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto *sidebar = new SidebarWidget(m_sidebarDock);
    m_sidebarDock->setWidget(sidebar);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebarDock);
    connect(sidebar, &SidebarWidget::fileOpened,
            this, &MainWindow::onSidebarFileOpened);

    applySidebarVisibility();
}

void MainWindow::createBusyWidget()
{
    m_busyLabel = new QLabel(this);
    m_busyLabel->setObjectName(QStringLiteral("BusyLabel"));
    m_busyBar = new QProgressBar(this);
    m_busyBar->setObjectName(QStringLiteral("BusyProgress"));
    m_busyBar->setRange(0, 0);
    m_busyBar->setFixedWidth(120);
    m_busyBar->setTextVisible(false);
    m_busyWidget = new QWidget(this);
    m_busyWidget->setObjectName(QStringLiteral("BusyWidget"));
    auto *busyLayout = new QHBoxLayout(m_busyWidget);
    busyLayout->setContentsMargins(0, 0, 0, 0);
    busyLayout->setSpacing(6);
    busyLayout->addWidget(m_busyLabel);
    busyLayout->addWidget(m_busyBar);
    m_busyWidget->hide();
}

void MainWindow::setupStatusBar()
{
    statusBar()->setObjectName(QStringLiteral("MainStatusBar"));
    createBusyWidget();

    if (m_modernUi) {
        m_statusSummaryLeft = new QLabel(this);
        m_statusSummaryLeft->setObjectName(QStringLiteral("StatusSummaryLeft"));
        m_statusSummaryRight = new QLabel(this);
        m_statusSummaryRight->setObjectName(QStringLiteral("StatusSummaryRight"));
        statusBar()->addWidget(m_statusSummaryLeft, 1);
        statusBar()->addPermanentWidget(m_busyWidget);
        statusBar()->addPermanentWidget(m_statusSummaryRight);
        // Item-size slider at the bottom-right, next to the status summary.
        // 条目大小滑块位于右下角，紧邻状态摘要。
        m_sizeSlider = new QSlider(Qt::Horizontal, this);
        m_sizeSlider->setObjectName(QStringLiteral("StatusSizeSlider"));
        m_sizeSlider->setRange(48, 128);
        m_sizeSlider->setSingleStep(8);
        m_sizeSlider->setPageStep(16);
        m_sizeSlider->setFixedWidth(110);
        m_sizeSlider->setToolTip(tr("Icon size (Ctrl + wheel)"));
        connect(m_sizeSlider, &QSlider::valueChanged,
                this, &MainWindow::applyItemSize);
        statusBar()->addPermanentWidget(m_sizeSlider);
    } else {
        statusBar()->addPermanentWidget(m_busyWidget);
    }
    // A size grip keeps the frameless window resizable from its corner.
    // 无边框窗口通过右下角尺寸手柄调整大小。
    auto *sizeGrip = new QSizeGrip(this);
    sizeGrip->setObjectName(QStringLiteral("WindowSizeGrip"));
    statusBar()->addPermanentWidget(sizeGrip);
    statusBar()->showMessage(tr("Ready"), 2000);
}

void MainWindow::applyContentViewMode()
{
    if (!m_treeView || !m_model) {
        return;
    }
    const QString mode = contentViewMode();
    // A local directory listing is always a single level: the view may look
    // like a tree, but entries must never expand into paths on disk.
    // 本地目录列表始终只有一层：即使外观为树形，也不能把磁盘路径展开成层级。
    const bool flat = m_localBrowse || mode != QStringLiteral("tree");
    const bool modeChanged = m_model->isFlatMode() != flat;
    if (modeChanged) {
        m_model->setFlatMode(flat);
    }
    // A newly created view starts in tree mode even when the model is already
    // flat (e.g. switching from modern list to classic). Always synchronize the
    // view itself, not just when the model changes, or double-clicking folders
    // is handled as expand/collapse instead of navigation.
    // 新建视图默认是树模式，即使模型已经是扁平模式（例如从现代列表切到
    // 经典布局）。必须始终同步视图自身状态，否则双击文件夹会被当成展开/收起
    // 而不是进入目录。
    if (m_treeView->isFlatMode() != flat) {
        m_treeView->setFlatMode(flat);
    }
    // List mode keeps the synthetic ".." row; grid mode navigates with the
    // Up action only. Tree mode never uses the row.
    // 列表模式保留合成的 ".." 行；网格模式只通过“上一级”动作返回；
    // 树模式本身不使用该行。
    m_model->setShowParentEntry(mode == QStringLiteral("list"));
    // Modern views show thumbnails everywhere; classic only in the grid.
    // 现代视图全部显示缩略图；经典布局只在网格视图显示。
    m_model->setThumbnailsEnabled(m_modernUi
                                  || mode == QStringLiteral("grid"));
    applyHeaderColumns();

    // Sort through the shared sort state: the grid view has no header and the
    // tree header only mirrors the state.
    // 通过共享排序状态排序：网格视图没有表头，树表头只反映该状态。
    applySort(m_sortColumn, m_sortOrder);

    if (m_viewStack && m_gridView) {
        QWidget *active = (mode == QStringLiteral("grid"))
            ? static_cast<QWidget *>(m_gridView)
            : static_cast<QWidget *>(m_treeView);
        m_viewStack->setCurrentWidget(active);
    }

    if (flat && mode == QStringLiteral("tree")) {
        m_treeView->scrollToTop();
    } else if (mode == QStringLiteral("tree")) {
        m_treeView->expandToDepth(0);
    } else if (mode == QStringLiteral("grid") && m_gridView) {
        m_gridView->scrollToTop();
    } else {
        m_treeView->scrollToTop();
    }

    // Switching between flat list/grid and hierarchical tree changes what
    // "current directory" means.  Reset the shared history for both layouts;
    // otherwise a stale tree/list directory can leave Back/Up enabled after
    // the view switch.  Grid <-> list keeps the current directory and history.
    // 在扁平列表/网格与层次树之间切换时，“当前目录”的含义会变化；两种布局
    // 都要重置共享历史，否则树/列表切换后可能残留 Back/Up 状态。网格与列表
    // 之间切换保留当前目录和历史。
    if (!m_localBrowse && (modeChanged || m_navHistory.isEmpty())) {
        resetNavigationHistory(m_model->currentDirectory());
    }
    if (m_modernUi) {
        if (!m_localBrowse && m_folderPane) {
            m_folderPane->setCurrentDirectory(m_navCurrentDir);
        }
        if (m_modernNavBar) {
            m_modernNavBar->setViewMode(mode);
        }
    }
    updateStatusSummary();
    updateSelectionDetails();
}

void MainWindow::applyHeaderColumns()
{
    if (!m_treeView) {
        return;
    }
    QHeaderView *header = m_treeView->header();
    header->setSectionHidden(FileListModel::TypeColumn, !m_modernUi);
    header->setSectionHidden(FileListModel::EncryptedColumn, m_modernUi);

    if (m_modernUi) {
        header->setFixedHeight(34);
        header->setStretchLastSection(false);
        header->setSectionResizeMode(FileListModel::NameColumn,
                                     QHeaderView::Stretch);
        header->resizeSection(FileListModel::SizeColumn, 100);
        header->resizeSection(FileListModel::PackedSizeColumn, 110);
        header->resizeSection(FileListModel::TypeColumn, 150);
        header->resizeSection(FileListModel::ModifiedColumn, 150);
    } else {
        header->setFixedHeight(24);
        // Keep the classic Explorer-like layout but let the last column fill
        // the remaining width instead of leaving a blank strip.
        // 保留经典布局，但让最后一列填满剩余宽度，避免右侧空白。
        header->setStretchLastSection(true);
        header->setSectionResizeMode(FileListModel::NameColumn,
                                     QHeaderView::Interactive);
        header->resizeSection(FileListModel::NameColumn, 360);
        header->resizeSection(FileListModel::SizeColumn, 100);
        header->resizeSection(FileListModel::PackedSizeColumn, 100);
        header->resizeSection(FileListModel::ModifiedColumn, 150);
        header->resizeSection(FileListModel::EncryptedColumn, 90);
    }
}

void MainWindow::updateSearchIcon()
{
    if (!m_searchAction) {
        return;
    }
    m_searchAction->setIcon(Icons::themed(QStringLiteral("search")));
}

void MainWindow::onContentViewModeChanged(const QString &mode)
{
    const QString normalized =
        (mode == QStringLiteral("list") || mode == QStringLiteral("tree"))
        ? mode : QStringLiteral("grid");
    if (normalized != contentViewMode()) {
        AppSettings::instance().setContentViewMode(normalized);
    }
    if (m_modernNavBar) {
        m_modernNavBar->setViewMode(normalized);
    }
    applyContentViewMode();
}

void MainWindow::applySort(int column, Qt::SortOrder order)
{
    if (!m_model || !m_proxyModel) {
        return;
    }
    if (column < 0 || column >= FileListModel::ColumnCount) {
        column = FileListModel::NameColumn;
    }
    m_sortColumn = column;
    m_sortOrder = order;
    AppSettings::instance().setSort(column, order);

    m_proxyModel->sort(column, order);
    if (m_treeView) {
        QHeaderView *header = m_treeView->header();
        const QSignalBlocker blocker(header);
        header->setSortIndicator(column, order);
    }
    updateSortUi();
}

void MainWindow::updateSortUi()
{
    if (!m_modernNavBar) {
        return;
    }
    QString label;
    switch (m_sortColumn) {
    case FileListModel::SizeColumn:      label = tr("Size"); break;
    case FileListModel::PackedSizeColumn: label = tr("Packed size"); break;
    case FileListModel::TypeColumn:      label = tr("Type"); break;
    case FileListModel::ModifiedColumn:  label = tr("Modified"); break;
    case FileListModel::EncryptedColumn: label = tr("Encrypted"); break;
    case FileListModel::NameColumn:
    default:                             label = tr("Name"); break;
    }
    m_modernNavBar->setSortState(
        tr("Sort: %1 (%2)")
            .arg(label,
                 m_sortOrder == Qt::AscendingOrder ? tr("ascending")
                                                   : tr("descending")),
        m_sortOrder == Qt::DescendingOrder);
}

void MainWindow::applyItemSize(int size)
{
    applyItemSizeInternal(size, true);
}

void MainWindow::applyItemSizeInternal(int size, bool persist)
{
    size = qBound(48, size, 128);
    m_itemSize = size;
    // Avoid writing QSettings on every slider tick / Ctrl+wheel event.  UI
    // construction passes persist=false and must not touch the saved value.
    // 避免每次拖动滑块/Ctrl+滚轮都同步写 QSettings；构建界面时
    // persist=false，不应覆盖已保存的值。
    if (persist) {
        if (m_itemSizeSaveTimer) {
            m_itemSizeSaveTimer->start();
        } else {
            AppSettings::instance().setItemSize(size);
        }
    }
    if (m_sizeSlider) {
        const QSignalBlocker blocker(m_sizeSlider);
        m_sizeSlider->setValue(size);
    }
    if (m_gridView) {
        m_gridView->setIconSize(QSize(size, size));
        m_gridView->setGridSize(
            QSize(qMax(128, size + 24), size + 52));
        m_gridView->updateGridHeight();
    }
    if (m_treeView) {
        // Classic list/tree rows are designed for compact Explorer-like rows;
        // using the modern 48px icon there would exceed the 24px row height
        // and overlap neighbouring rows.  Keep the modern sizing untouched.
        // 经典列表/树使用紧凑的资源管理器式行高；沿用现代的 48px 图标会
        // 超过 24px 行高并与相邻行重叠。现代布局的尺寸保持不变。
        const int treeIcon = m_modernUi ? qMin(48, size) : 16;
        m_treeView->setIconSize(QSize(treeIcon, treeIcon));
    }
}

QMenu *MainWindow::createSortMenu(QWidget *parent)
{
    auto *menu = new QMenu(tr("Sort by"), parent);
    const auto addColumn = [this, menu](int column, const QString &label) {
        QAction *action = menu->addAction(label);
        action->setCheckable(true);
        action->setChecked(m_sortColumn == column);
        connect(action, &QAction::triggered, this,
                [this, column]() { applySort(column, m_sortOrder); });
    };

    addColumn(FileListModel::NameColumn, tr("Name"));
    addColumn(FileListModel::SizeColumn, tr("Size"));
    if (!m_localBrowse) {
        addColumn(FileListModel::PackedSizeColumn, tr("Packed size"));
    }
    addColumn(FileListModel::TypeColumn, tr("Type"));
    addColumn(FileListModel::ModifiedColumn, tr("Modified"));

    menu->addSeparator();
    QAction *ascending = menu->addAction(tr("Ascending"));
    ascending->setCheckable(true);
    ascending->setChecked(m_sortOrder == Qt::AscendingOrder);
    connect(ascending, &QAction::triggered, this, [this]() {
        applySort(m_sortColumn, Qt::AscendingOrder);
    });

    QAction *descending = menu->addAction(tr("Descending"));
    descending->setCheckable(true);
    descending->setChecked(m_sortOrder == Qt::DescendingOrder);
    connect(descending, &QAction::triggered, this, [this]() {
        applySort(m_sortColumn, Qt::DescendingOrder);
    });
    return menu;
}

void MainWindow::applyToolButtonStyle()
{
    if (!m_mainToolBar) {
        return;
    }
    const QString style = AppSettings::instance().toolButtonStyle();
    Qt::ToolButtonStyle buttonStyle = Qt::ToolButtonTextUnderIcon;
    if (style == QStringLiteral("textBesideIcon")) {
        buttonStyle = Qt::ToolButtonTextBesideIcon;
    } else if (style == QStringLiteral("textOnly")) {
        buttonStyle = Qt::ToolButtonTextOnly;
    } else if (style == QStringLiteral("iconOnly")) {
        buttonStyle = Qt::ToolButtonIconOnly;
    }
    m_mainToolBar->setToolButtonStyle(buttonStyle);
}

void MainWindow::applySidebarVisibility()
{
    if (m_sidebarDock) {
        m_sidebarDock->setVisible(AppSettings::instance().showRecentFiles());
    }
}
