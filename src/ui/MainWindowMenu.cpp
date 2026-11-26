// QArchiver / 小Q解压
// MainWindow menu/action construction: QAction setup, action icons and menus.
// MainWindow 菜单/动作构建：QAction 构建、动作图标与菜单。

#include "ui/MainWindow.h"
#include "ui/MainWindow_p.h"

#include "ui/dialogs/FileAssociationDialog.h"
#include "ui/icons/IconUtils.h"
#include "ui/widgets/ImmersiveTitleBar.h"

#include "core/AppSettings.h"
#include "core/ArchiveManager.h"

void MainWindow::setupActions()
{
    // Classic-layout navigation.  The modern layout uses ModernNavBar's own
    // buttons; these actions are added to the classic filter row only.
    // 经典布局导航。现代布局使用 ModernNavBar 自带的按钮；
    // 这三个动作只加入经典布局的筛选行。
    m_backAct = new QAction(tr("Back"), this);
    m_backAct->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    connect(m_backAct, &QAction::triggered, this, &MainWindow::onNavBack);

    m_forwardAct = new QAction(tr("Forward"), this);
    m_forwardAct->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
    connect(m_forwardAct, &QAction::triggered, this, &MainWindow::onNavForward);

    m_upAct = new QAction(tr("Up"), this);
    m_upAct->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    connect(m_upAct, &QAction::triggered, this, &MainWindow::onNavUp);

    m_openAct = new QAction(tr("Open"), this);
    m_openAct->setShortcut(QKeySequence::Open);
    connect(m_openAct, &QAction::triggered, this, &MainWindow::onOpen);

    m_newAct = new QAction(tr("Compress"), this);
    m_newAct->setShortcut(QKeySequence::New);
    m_newAct->setToolTip(tr("Create a new archive (Ctrl+N)"));
    connect(m_newAct, &QAction::triggered, this, &MainWindow::onNewArchive);

    m_extractAct = new QAction(tr("Extract To..."), this);
    m_extractAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(m_extractAct, &QAction::triggered, this, &MainWindow::onExtractAll);

    m_oneClickAct = new QAction(tr("One-click Extract"), this);
    m_oneClickAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    connect(m_oneClickAct, &QAction::triggered,
            this, &MainWindow::onOneClickExtract);

    m_testAct = new QAction(tr("Test"), this);
    m_testAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(m_testAct, &QAction::triggered, this, &MainWindow::onTestArchive);

    m_addAct = new QAction(tr("Add"), this);
    m_addAct->setToolTip(tr("Add files to the current archive"));
    connect(m_addAct, &QAction::triggered, this, &MainWindow::onAddFiles);

    m_deleteAct = new QAction(tr("Delete"), this);
    m_deleteAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Delete));
    connect(m_deleteAct, &QAction::triggered,
            this, &MainWindow::onDeleteSelected);

    m_closeAct = new QAction(tr("Close"), this);
    m_closeAct->setShortcut(QKeySequence::Close);
    m_closeAct->setToolTip(tr("Close the current archive"));
    connect(m_closeAct, &QAction::triggered, this, &MainWindow::onCloseArchive);

    m_prefsAct = new QAction(tr("Preferences"), this);
    connect(m_prefsAct, &QAction::triggered, this, &MainWindow::onPreferences);

    m_aboutAct = new QAction(tr("About"), this);
    connect(m_aboutAct, &QAction::triggered, this, &MainWindow::onAbout);

    m_checkUpdatesAct = new QAction(tr("Check for Updates..."), this);
    connect(m_checkUpdatesAct, &QAction::triggered,
            this, &MainWindow::onCheckUpdates);

    m_quitAct = new QAction(tr("Quit"), this);
    m_quitAct->setShortcut(QKeySequence::Quit);
    connect(m_quitAct, &QAction::triggered, qApp, &QApplication::quit);

    applyActionIcons();
}

void MainWindow::applyActionIcons()
{
    const auto classic = [](const QString &name) {
        return Icons::aero(name);
    };
    const auto modern = [](const QString &name) {
        return Icons::modern(name);
    };

    if (m_modernUi) {
        m_backAct->setIcon(modern(QStringLiteral("back.png")));
        m_forwardAct->setIcon(modern(QStringLiteral("forward.png")));
        m_upAct->setIcon(modern(QStringLiteral("up.png")));
        m_openAct->setIcon(modern(QStringLiteral("open.png")));
        m_newAct->setIcon(modern(QStringLiteral("compress.png")));
        m_extractAct->setIcon(modern(QStringLiteral("extract.png")));
        m_oneClickAct->setIcon(modern(QStringLiteral("oneclick.png")));
        m_testAct->setIcon(modern(QStringLiteral("test.png")));
        m_addAct->setIcon(modern(QStringLiteral("add.png")));
        m_deleteAct->setIcon(modern(QStringLiteral("delete.png")));
        m_closeAct->setIcon(modern(QStringLiteral("close.png")));
        m_prefsAct->setIcon(modern(QStringLiteral("settings.png")));
        m_checkUpdatesAct->setIcon(modern(QStringLiteral("refresh.png")));
        m_aboutAct->setIcon(modern(QStringLiteral("help.png")));
        m_quitAct->setIcon(modern(QStringLiteral("close.png")));
    } else {
        m_backAct->setIcon(classic(QStringLiteral("left.ico")));
        m_forwardAct->setIcon(classic(QStringLiteral("right.ico")));
        m_upAct->setIcon(classic(QStringLiteral("up.ico")));
        m_openAct->setIcon(classic(QStringLiteral("Open.png")));
        m_newAct->setIcon(classic(QStringLiteral("New.png")));
        m_extractAct->setIcon(classic(QStringLiteral("Extract.png")));
        m_oneClickAct->setIcon(classic(QStringLiteral("Extract.png")));
        m_testAct->setIcon(classic(QStringLiteral("test.png")));
        m_addAct->setIcon(classic(QStringLiteral("add.png")));
        m_deleteAct->setIcon(classic(QStringLiteral("Close.png")));
        m_closeAct->setIcon(classic(QStringLiteral("Close.png")));
        m_prefsAct->setIcon(classic(QStringLiteral("Preferences.png")));
        m_checkUpdatesAct->setIcon(classic(QStringLiteral("Open.png")));
        m_aboutAct->setIcon(classic(QStringLiteral("About.png")));
        m_quitAct->setIcon(classic(QStringLiteral("Quit.png")));
    }

    for (QAction *action : {m_backAct, m_forwardAct, m_upAct,
                            m_openAct, m_newAct, m_extractAct, m_oneClickAct,
                            m_testAct, m_addAct, m_deleteAct, m_closeAct,
                            m_prefsAct, m_checkUpdatesAct, m_aboutAct, m_quitAct}) {
        if (!action) {
            continue;
        }
        QString tip = action->text();
        if (action->shortcut().isEmpty()) {
            action->setToolTip(tip);
        } else {
            tip += QStringLiteral(" (%1)")
                       .arg(action->shortcut().toString(QKeySequence::NativeText));
            action->setToolTip(tip);
        }
    }
}

void MainWindow::setupMenuBar()
{
    // The immersive title bar always hosts the menus; never fall back to
    // QMainWindow::menuBar(), which would replace the title bar in the
    // menu-widget slot with a lazily created QMenuBar.
    // 菜单始终由沉浸式标题栏承载；不能回退到 QMainWindow::menuBar()，
    // 否则它会用一个新建的 QMenuBar 顶掉 menuWidget 槽位里的标题栏。
    Q_ASSERT(m_titleBar);
    if (!m_titleBar) {
        return;
    }
    QMenuBar *bar = m_titleBar->menuBarWidget();
    bar->clear();
    bar->setNativeMenuBar(false);

    auto *fileMenu = bar->addMenu(tr("&File"));
    fileMenu->addAction(m_openAct);
    fileMenu->addAction(m_newAct);
    fileMenu->addSeparator();

    auto *startPageAction = fileMenu->addAction(tr("Start Page"));
    connect(startPageAction, &QAction::triggered, this, [this]() {
        // Showing the start page must not leave an archive loaded behind it;
        // close the archive first so the folder pane and window title match
        // the visible page.
        // 显示起始页时不能把压缩包留在后台：先关闭压缩包，使侧栏和标题栏与
        // 可见页面一致。
        if (ArchiveManager::instance().isBusy()) {
            return;
        }
        if (ArchiveManager::instance().backend()) {
            onCloseArchive();
            if (ArchiveManager::instance().backend()) {
                return;
            }
        }
        showStartPage();
    });

    auto *browseLocalAction = fileMenu->addAction(tr("Browse Local Files..."));
    connect(browseLocalAction, &QAction::triggered, this, [this]() {
        const QString startDir = m_localBrowse
            ? m_localBrowseDir
            : AppSettings::instance().localBrowsePath();
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Browse Local Files"), startDir,
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!dir.isEmpty()) {
            loadLocalDirectory(dir, false);
        }
    });

    fileMenu->addSeparator();
    fileMenu->addAction(m_closeAct);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAct);

    auto *actionMenu = bar->addMenu(tr("&Actions"));
    actionMenu->addAction(m_extractAct);
    actionMenu->addAction(m_oneClickAct);
    actionMenu->addSeparator();
    actionMenu->addAction(m_addAct);
    actionMenu->addAction(m_deleteAct);
    actionMenu->addSeparator();
    actionMenu->addAction(m_testAct);

    auto *toolsMenu = bar->addMenu(tr("&Tools"));
    auto *benchmarkAction = toolsMenu->addAction(
        Icons::modern(QStringLiteral("toolbox.png")), tr("7-Zip Benchmark..."));
    connect(benchmarkAction, &QAction::triggered,
            this, &MainWindow::onBenchmark);
    auto *associationsAction = toolsMenu->addAction(
        Icons::modern(QStringLiteral("settings.png")),
        tr("File Associations..."));
    connect(associationsAction, &QAction::triggered, this, [this]() {
        FileAssociationDialog dialog(this);
        dialog.exec();
    });
    toolsMenu->addAction(m_prefsAct);

    auto *helpMenu = bar->addMenu(tr("&Help"));
    helpMenu->addAction(m_checkUpdatesAct);
    helpMenu->addSeparator();
    helpMenu->addAction(m_aboutAct);

    bar->show();
}
