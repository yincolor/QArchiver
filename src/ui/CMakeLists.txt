# UI layer: main window, models, dialogs, widgets, themes, preview and drag-out.
# 界面层：主窗口、模型、对话框、控件、主题、预览与拖出。
add_library(qarchiver_ui STATIC
    # Main window (implementation is split across MainWindow*.cpp).
    # 主窗口（实现拆分为多个 MainWindow*.cpp）。
    MainWindow.cpp
    MainWindow.h
    MainWindow_p.h
    MainWindowHelpers.cpp
    MainWindowHelpers.h
    MainWindowActions.cpp
    MainWindowMenu.cpp
    MainWindowLayout.cpp
    MainWindowNavigation.cpp
    MainWindowExtraction.cpp
    MainWindowDragOut.cpp
    MainWindowLocalBrowser.cpp

    # Item models, proxy model and delegate.
    # 条目模型、代理模型与委托。
    models/FileListModel.cpp
    models/FileListModel.h
    models/FilterProxyModel.cpp
    models/FilterProxyModel.h
    models/FileListViewDelegate.cpp
    models/FileListViewDelegate.h

    # Background services: preview extraction and drag-out staging.
    # 后台服务：预览解压与拖出暂存。
    services/PreviewManager.cpp
    services/PreviewManager.h
    services/ThumbnailManager.cpp
    services/ThumbnailManager.h
    services/PreviewUtils.cpp
    services/PreviewUtils.h
    services/DragOutManager.cpp
    services/DragOutManager.h
    services/ExternalEditManager.cpp
    services/ExternalEditManager.h
    services/UpdateChecker.cpp
    services/UpdateChecker.h
    services/OneClickExtractController.cpp
    services/OneClickExtractController.h

    # Theming and icon helpers.
    # 主题与图标辅助代码。
    theme/ThemeManager.cpp
    theme/ThemeManager.h
    theme/AeroRowColors.h
    icons/IconUtils.cpp
    icons/IconUtils.h
    icons/FileTypeHelper.cpp
    icons/FileTypeHelper.h
    icons/MessageBoxUtils.h
    background/BackgroundImage.cpp
    background/BackgroundImage.h

    # Modal dialogs.
    # 模态对话框。
    dialogs/ExtractDialog.cpp
    dialogs/ExtractDialog.h
    dialogs/CompressDialog.cpp
    dialogs/CompressDialog.h
    dialogs/PasswordDialog.cpp
    dialogs/PasswordDialog.h
    dialogs/ProgressDialog.cpp
    dialogs/ProgressDialog.h
    dialogs/PreferencesDialog.cpp
    dialogs/PreferencesDialog.h
    dialogs/BenchmarkDialog.cpp
    dialogs/BenchmarkDialog.h
    dialogs/FileAssociationDialog.cpp
    dialogs/FileAssociationDialog.h

    # Reusable custom widgets.
    # 可复用自定义控件。
    widgets/ArchiveTreeView.cpp
    widgets/ArchiveTreeView.h
    widgets/BackgroundWidget.cpp
    widgets/BackgroundWidget.h
    widgets/SidebarWidget.cpp
    widgets/SidebarWidget.h
    widgets/ImmersiveTitleBar.cpp
    widgets/ImmersiveTitleBar.h
    widgets/ModernToolBar.cpp
    widgets/ModernToolBar.h
    widgets/ModernNavBar.cpp
    widgets/ModernNavBar.h
    widgets/WindowResizeBorders.cpp
    widgets/WindowResizeBorders.h
    widgets/BreadcrumbPathBar.cpp
    widgets/BreadcrumbPathBar.h
    widgets/ArchiveFolderPane.cpp
    widgets/ArchiveFolderPane.h
    widgets/ArchiveGridView.cpp
    widgets/ArchiveGridView.h
    widgets/WelcomePage.cpp
    widgets/WelcomePage.h
)

target_include_directories(qarchiver_ui PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(qarchiver_ui
    PUBLIC qarchiver_core Qt6::Widgets Qt6::Gui Qt6::Network
    PRIVATE Qt6::Concurrent Qt6::Svg
)

set_target_properties(qarchiver_ui PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

qarchiver_set_warnings(qarchiver_ui)

# Most UI translation units include the same Qt headers; compile them once.
# The list stays deliberately small so a missing include is still caught by
# the regular compilation of targets that do not use the PCH.
# 多数界面编译单元都包含相同的 Qt 头文件，这些头文件只编译一次。
# 列表保持精简，其他不使用 PCH 的目标仍会发现缺失的 include。
target_precompile_headers(qarchiver_ui PRIVATE
    <QtCore/QCoreApplication>
    <QtCore/QDir>
    <QtCore/QFileInfo>
    <QtCore/QString>
    <QtGui/QIcon>
    <QtGui/QPixmap>
    <QtWidgets/QApplication>
    <QtWidgets/QDialog>
    <QtWidgets/QLabel>
    <QtWidgets/QWidget>
)
