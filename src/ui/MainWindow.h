#ifndef MAINWINDOW_H
#define MAINWINDOW_H

// QArchiver / 小Q解压
// Main application window with the modern (HaoZip-style) and classic layouts.
// 主应用窗口：现代（好压风格）与经典两种布局。

#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>

#include "common/ArchiveEntry.h"
#include "common/ArchiveError.h"
#include "common/ArchiveOptions.h"

class FileListModel;
class FilterProxyModel;
class ArchiveTreeView;
class ArchiveGridView;
class BackgroundWidget;
class ModernToolBar;
class ModernNavBar;
class ArchiveFolderPane;
class BreadcrumbPathBar;
class WelcomePage;
class ImmersiveTitleBar;
class WindowResizeBorders;
class ProgressDialog;
class ArchiveBackend;
class QStackedWidget;
class QDockWidget;
class QToolBar;
class QSplitter;

struct SevenZipToolInfo;

class QAction;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressBar;
class QTimer;
class QAbstractItemView;
class QSlider;

// Test hook: tests/tst_titlebar.cpp drives the layout switch without the
// modal Preferences dialog, so the immersive title bar can be checked after a
// teardown/build cycle.
// 测试钩子：tests/tst_titlebar.cpp 无需弹出模态偏好设置对话框即可驱动布局
// 切换，从而检查重建流程之后沉浸式标题栏是否仍然存在。
class TestTitleBar;

// Main application window.
// It owns two interchangeable chrome layouts share the same models, actions and
// archive state:
//   * modern  - HaoZip-style header, folder pane, details pane and start page;
//   * classic - the original toolbar + optional recent-files dock layout.
// The layout can be switched at runtime from Preferences.
// 主应用窗口。
// 两套可互换的界面布局共享同一套模型、动作和压缩包状态：
//   * modern  - 好压风格的头部、文件夹面板、详细信息面板与起始页；
//   * classic - 原有的工具栏与可选最近文件停靠面板布局。
// 可在偏好设置中运行时切换。
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // @p startupContentPending tells the first UI build that the process was
    // started with an archive to open (or a shell-integration task), so a
    // classic layout must not scan the local directory before that work begins.
    // @p startupContentPending 告知首次构建界面：进程启动时会打开压缩包（或执行
    // 外壳集成任务），因此经典布局不应在该工作开始前扫描本地目录。
    explicit MainWindow(QWidget *parent = nullptr,
                        bool startupContentPending = false);
    ~MainWindow() override;

    // Open an archive passed on the command line / 打开命令行传入的压缩包。
    void openArchivePath(const QString &path);
    // One-click extract an archive passed on the command line.
    // 对命令行传入的压缩包执行一键解压。
    void extractOneClickPath(const QString &path);
    // Open the Preferences or Benchmark dialog (command-line convenience).
    // 打开偏好设置或性能测试对话框（命令行便利入口）。
    void openPreferences();
    void openBenchmark();
    // Shell-integration mode: keep the main window hidden and show only the
    // operation dialogs; quit when the operation finishes.
    // 外壳集成模式：不显示主窗口，只显示操作对话框，操作结束后退出。
    void setDialogOnly(bool enabled) { m_dialogOnly = enabled; }
    // After an Explorer-menu compression succeeds, show the main window and
    // open the newly created archive.
    // 资源管理器菜单压缩成功后显示主窗口并打开新建压缩包。
    void setOpenAfterCompress(bool enabled) { m_openAfterCompress = enabled; }

    // Browse a local directory while no archive is open.  The same view,
    // search and navigation machinery is reused for local folders.
    // 未打开压缩包时浏览本地目录。复用同一套视图、搜索与导航机制。
    void browseLocalDirectory(const QString &path);
    bool isBrowsingLocalDirectory() const { return m_localBrowse; }

    // Compress paths without opening the compress dialog (Explorer context
    // menu and command line).  An empty target path derives
    // "<name>.<ext>" next to the sources.
    // 不打开压缩对话框直接压缩（资源管理器右键菜单与命令行）。
    // 目标路径为空时在源文件旁生成“<名称>.<扩展名>”。
    void compressPaths(const QStringList &sources, ArchiveFormat format,
                       const QString &targetPath, bool closeAfterFinish = false);
    // Compress paths using the normal compress dialog.
    // 使用常规压缩对话框压缩指定路径。
    void compressPathsWithDialog(const QStringList &sources);
    // Entry point for "--compress <format>"; parses the format token.
    // “--compress <格式>”的入口；负责解析格式名。
    void compressFromCommandLine(const QStringList &sources,
                                 const QString &formatName,
                                 const QString &targetPath);

protected:
    void showEvent(QShowEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void onOpen();
    void onNewArchive();
    void onExtractAll();
    void onExtractSelected();
    void onOneClickExtract();
    void onTestArchive();
    void onAddFiles();
    void onDeleteSelected();
    void onPreferences();
    void onBenchmark();
    void onCloseArchive();
    void onAbout();
    void onCheckUpdates();
    void checkSevenZipFallback();
    void showSevenZipFallbackInfo(const SevenZipToolInfo &info);

    void onArchiveOpening(const QString &filePath);
    void onArchiveOpened(const QString &filePath);
    void onFileListReady(const QList<ArchiveEntry> &entries);
    void onProgressChanged(int percent, int total, int current, int etaSec);
    void onOutputReady(const QString &line);
    void onPasswordRequested();
    void onErrorOccurred(ArchiveErrorCode code, const QString &msg);
    void onOperationFinished(bool success, const QString &msg);

    void onEntryDoubleClicked(const QString &path);
    void onTreeContextMenu(const QPoint &pos);
    void onSearchChanged(const QString &text);
    void onSidebarFileOpened(const QString &path);
    void onEntriesDragOut(const QStringList &paths);
    void onSelectionChanged();
    void onNavBack();
    void onNavForward();
    void onNavUp();
    void onFolderDirectoryActivated(const QString &path);
    void onContentViewModeChanged(const QString &mode);

private:
    friend class TestTitleBar;
    friend class TestNavState;

    // UI construction / 界面构建
    void setupActions();
    void applyActionIcons();
    void buildUi();
    void teardownUi();
    void setupMenuBar();
    void setupModernUi();
    void setupClassicUi();
    void setupClassicToolbar();
    void setupClassicCentralWidget();
    void setupClassicSidebar();
    void setupStatusBar();
    void createBusyWidget();
    ArchiveTreeView *createArchiveTree(QWidget *parent);
    ArchiveGridView *createArchiveGrid(QWidget *parent);
    void finishDialogOnly();

    // View state / 视图状态
    void applyContentViewMode();
    void applyItemSizeInternal(int size, bool persist);
    void applyToolButtonStyle();
    void applySidebarVisibility();
    void applyHeaderColumns();
    void updateSearchIcon();
    void updatePlaceholderBackground();
    void updateActionsState();
    void updateSelectionDetails();
    void updateStatusSummary();
    void updateNavigationState();
    void setNavLocationText(const QString &archiveName,
                            const QString &internalDir = QString());
    void setNavLocalLocation(const QString &directory);
    void setNavPlaceholder(const QString &text);
    void updateNavLocation();
    void updateFolderPaneState();
    void showPlaceholder(const QString &message);
    void showWelcomePage();
    void showArchivePage();
    void showStartPage();
    void setSearchText(const QString &text);
    void navigateToDirectory(const QString &path, bool addHistory);
    void navigateHistory(int delta);
    void resetNavigationHistory(const QString &directory = QString());
    QAbstractItemView *currentContentView() const;
    QString contentViewMode() const;

    // Sorting (shared by the tree header, the sort button and context menus).
    // 排序（树表头、排序按钮与右键菜单共用）。
    void applySort(int column, Qt::SortOrder order);
    QMenu *createSortMenu(QWidget *parent);
    void updateSortUi();
    // Icon size for the grid/list views (slider / Ctrl+wheel).
    // 网格/列表视图的图标大小（滑块 / Ctrl+滚轮）。
    void applyItemSize(int size);

    // Operations / 操作
    void showProgressDialog(const QString &title);
    void setBusyIndicatorVisible(bool visible, const QString &text = QString());
    QStringList selectedEntryPaths() const;
    QList<ArchiveEntry> selectedEntries() const;
    QStringList chooseSourceItems(const QString &title);
    void openFile(const QString &path);
    void startExtract(const ExtractOptions &opts);
    void addFilesToCurrentArchive(const QStringList &files);

    // External-edit write-back (see ExternalEditManager): confirm and save a
    // file that was edited outside QArchiver back into its archive.
    // 外部编辑写回（见 ExternalEditManager）：确认并把在 QArchiver 外编辑过的
    // 文件存回其压缩包。
    void setupExternalEditHandlers();
    void startExternalEditSave(const AddFilesOptions &options,
                               const QString &localPath);
    void dispatchQueuedExternalSave();
    void finishExternalSave(bool success);

    // One-click extraction / 一键解压
    void beginOneClickExtract();
    bool askOneClickClosePreference();
    QString oneClickDestinationRoot() const;
    static QString oneClickArchiveBaseName(const QString &archivePath);

    // Local directory browsing / 本地目录浏览
    void loadLocalDirectory(const QString &path, bool addHistory);
    void refreshLocalDirectory();
    QStringList selectedLocalPaths() const;
    QString localPathForEntry(const ArchiveEntry &entry) const;
    void openLocalEntry(const QString &name);
    void openLocalWith(const QString &path);
    void openEntryWith(const QString &path);
    void updateLocalModeUi();
    QMenu *createCompressToMenu(const QStringList &sources, QWidget *parent);
    QString suggestedArchivePath(const QStringList &sources,
                                 ArchiveFormat format) const;

    // Compression without a dialog / 无对话框压缩
    void startImmediateCompress(const QStringList &sources, ArchiveFormat format,
                                const QString &targetPath, bool closeAfterFinish);

    // Models / 模型
    FileListModel *m_model = nullptr;
    FilterProxyModel *m_proxyModel = nullptr;

    // Shared view / 共用视图
    ArchiveTreeView *m_treeView = nullptr;
    ArchiveGridView *m_gridView = nullptr;
    QStackedWidget *m_viewStack = nullptr;
    QStackedWidget *m_centralStack = nullptr;

    // Immersive title bar / 沉浸式标题栏
    ImmersiveTitleBar *m_titleBar = nullptr;

    // Frameless edge/corner resize zones / 无边框边缘与角落缩放区
    WindowResizeBorders *m_resizeBorders = nullptr;
    // Initial window placement is done once; user moves are never overridden.
    // 仅在首次显示时确定窗口位置，绝不覆盖用户之后手动移动的位置。
    bool m_initialPlacementDone = false;

    // Classic layout / 经典布局
    QLineEdit *m_searchEdit = nullptr;
    BreadcrumbPathBar *m_classicBreadcrumb = nullptr;
    QLabel *m_placeholderLabel = nullptr;
    QAction *m_searchAction = nullptr;
    BackgroundWidget *m_backgroundWidget = nullptr;
    QDockWidget *m_sidebarDock = nullptr;
    QToolBar *m_mainToolBar = nullptr;

    // Modern layout / 现代布局
    ModernToolBar *m_modernToolBar = nullptr;
    ModernNavBar *m_modernNavBar = nullptr;
    ArchiveFolderPane *m_folderPane = nullptr;
    WelcomePage *m_welcomePage = nullptr;
    QSplitter *m_bodySplitter = nullptr;

    // Actions / 动作
    QAction *m_backAct = nullptr;
    QAction *m_forwardAct = nullptr;
    QAction *m_upAct = nullptr;
    QAction *m_openAct = nullptr;
    QAction *m_newAct = nullptr;
    QAction *m_extractAct = nullptr;
    QAction *m_oneClickAct = nullptr;
    QAction *m_testAct = nullptr;
    QAction *m_addAct = nullptr;
    QAction *m_deleteAct = nullptr;
    QAction *m_closeAct = nullptr;
    QAction *m_prefsAct = nullptr;
    QAction *m_checkUpdatesAct = nullptr;
    QAction *m_aboutAct = nullptr;
    QAction *m_quitAct = nullptr;

    // Dialogs / 对话框
    QPointer<ProgressDialog> m_progressDialog;

    // Busy indicator and modern summary labels / 忙碌指示器与现代摘要标签
    QWidget *m_busyWidget = nullptr;
    QLabel *m_busyLabel = nullptr;
    QProgressBar *m_busyBar = nullptr;
    QLabel *m_statusSummaryLeft = nullptr;
    QLabel *m_statusSummaryRight = nullptr;
    QSlider *m_sizeSlider = nullptr;

    // State / 状态
    QString m_currentPassword;   // Password for the current archive / 当前压缩包密码
    bool m_dragPrepareInProgress = false;  // Guard against re-entrant drags / 防止重入拖拽
    bool m_selfDropAttempted = false;      // Entry dropped back onto ourselves / 条目被拖回自身
    QString m_sessionPassword;   // Optional session-wide remembered password / 可选的会话级记住密码
    bool m_openDestAfterExtract = false;
    QString m_lastExtractDest;
    bool m_modernUi = true;
    bool m_startupContentPending = false;
    bool m_listReady = false;
    QString m_searchText;
    int m_sortColumn = 0;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    int m_itemSize = 96;
    QTimer *m_itemSizeSaveTimer = nullptr;

    // Navigation history shared by the modern nav bar and classic filter row.
    // 现代导航栏与经典筛选行共用的导航历史。
    QStringList m_navHistory;
    int m_navIndex = -1;
    QString m_navCurrentDir;

    // One-click extraction state / 一键解压状态
    bool m_oneClickPending = false;
    bool m_oneClickExtractActive = false;
    bool m_oneClickCloseAfter = false;

    // Local directory browsing state / 本地目录浏览状态
    bool m_localBrowse = false;
    QString m_localBrowseDir;

    // Immediate compression state (context menu / command line)
    // 立即压缩状态（右键菜单/命令行）
    bool m_compressCloseAfter = false;
    QString m_lastCompressTarget;

    // External-edit write-back state / 外部编辑写回状态
    QString m_externalSaveLocalPath;    // Write currently in flight / 进行中的写回
    int m_externalSaveRetriesLeft = 60; // Busy-archive retry budget / 忙碌重试次数
    AddFilesOptions m_queuedExternalSave;   // Waiting for the archive to open
    QString m_queuedExternalSaveLocalPath;

    // Shell-integration dialog-only mode / 外壳集成“仅对话框”模式
    bool m_dialogOnly = false;
    bool m_openAfterCompress = false;

    enum class UiOp { None, Open, List, Extract, Compress, Add, Delete, Test };
    UiOp m_currentOp = UiOp::None;
};

#endif // MAINWINDOW_H
