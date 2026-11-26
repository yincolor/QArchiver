#ifndef MAINWINDOW_P_H
#define MAINWINDOW_P_H

// QArchiver / 小Q解压
// Private includes shared by the MainWindow translation units.
// 由各 MainWindow 编译单元共用的私有头文件。
//
// Every MainWindow*.cpp used to repeat the same Qt includes; keeping them in
// one place makes the per-file include lists readable.  The qarchiver_ui PCH
// additionally compiles the most common headers once.
// 每个 MainWindow*.cpp 过去都重复同一批 Qt include；集中到这里后，各文件
// 的 include 列表更易读。qarchiver_ui 的 PCH 还会把这些常用头文件编译一次。

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <limits>

#endif // MAINWINDOW_P_H
