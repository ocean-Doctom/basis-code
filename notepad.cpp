#include "notepad.h"
#include "codeeditor.h"
#include "chatgptpage.h"
#include "devtools.h"
#include "builtinextensions.h"
#include <QPushButton>
#include <QActionGroup>
#include <QCryptographicHash>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QDesktopServices>
#include <QClipboard>
#include <QUrl>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QSettings>
#include "windowtitlebar.h"
#include <QAbstractButton>
#include <QAction>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QFrame>
#include <QApplication>
#include <QRegularExpression>
#include <QScrollArea>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QDir>
#include <QFileSystemModel>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QIcon>
#include <QTreeView>

#include <QMenu>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QStatusBar>
#include <QStringConverter>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTextDocument>
#include <QPlainTextEdit>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>
#include <QDialog>
#include <QPixmap>
#include <QDateTime>
#include <QDirIterator>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#endif

static QIcon pluginIcon(const QString &name) {
    if (name != "deepseek") return QIcon(":/icons/language-" + name + ".svg");
    QPixmap tile(192, 192);
    tile.fill(Qt::transparent);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen); painter.setBrush(QColor("#edf3ff"));
    painter.drawRoundedRect(QRectF(4, 4, 184, 184), 44, 44);
    painter.drawPixmap(QRect(36, 36, 120, 120), QIcon(":/icons/deepseek-original.ico").pixmap(120, 120));
    return QIcon(tile);
}

static QString normalizedPath(const QString &path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

static bool samePath(const QString &first, const QString &second) {
#ifdef Q_OS_WIN
    return first.compare(second, Qt::CaseInsensitive) == 0;
#else
    return first == second;
#endif
}

static bool copyDirRecursive(const QString &src, const QString &dst) {
    if (!QDir().mkpath(dst)) return false;
    QDirIterator it(src, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString source = it.next();
        const QString relative = QDir(src).relativeFilePath(source);
        const QString target = QDir(dst).filePath(relative);
        const QFileInfo info(source);
        if (info.isDir()) {
            if (!QDir().mkpath(target)) return false;
        } else {
            if (!QFile::copy(source, target)) return false;
        }
    }
    return true;
}

Notepad::Notepad(QWidget *parent) : QMainWindow(parent) {
    setWindowIcon(QIcon(":/icons/basis-code.ico"));
    setWindowFlag(Qt::FramelessWindowHint);
    QFont uiFont = font();
#ifdef Q_OS_WIN
    uiFont.setFamily("Microsoft YaHei UI");
#endif
    uiFont.setPointSize(10);
    QApplication::setFont(uiFont);
    setFont(uiFont);
    resize(1180, 780);
    setMinimumSize(720, 480);
    QSettings preferences(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
    themeName = preferences.value("ui/theme", preferences.value("ui/darkTheme", false).toBool() ? "tokyo-night" : "light").toString();
    if (!QStringList{"light", "tokyo-night", "github-dark-default"}.contains(themeName)) themeName = "light";
    darkTheme = themeName != "light";
    QFile theme(themeName == "github-dark-default" ? ":/theme-github-dark.qss" : darkTheme ? ":/theme-dark.qss" : ":/theme.qss");
    if (theme.open(QIODevice::ReadOnly)) {
        baseTheme = QString::fromUtf8(theme.readAll());
        setStyleSheet(baseTheme);
    }
    createWorkspace();
    createActions();
    devTools = new DevTools(this);
    connect(devTools, &DevTools::toolchainRequested, this, [this](const QString &language) { openToolkit(language, QString()); });
    connect(devTools, &DevTools::projectRequested, this, [this](const QString &path) {
        if (openProject(path)) {
            if (QFileInfo::exists(path + "/main.cpp")) loadFile(path + "/main.cpp");
        } else QMessageBox::warning(this, tr("打开失败"), tr("项目文件夹不存在或不可读。"));
    });
    connect(devTools, &DevTools::activityChanged, this, [this](bool busy) {
        findChild<QPushButton *>("buildRunButton")->setEnabled(!busy);
        findChild<QPushButton *>("debugRunButton")->setEnabled(!busy);
    });
    refreshRecentProjects();
    setColorTheme(themeName);
    const QString defaultAgent = preferences.value("agents/default").toString();
    if (defaultAgent == "chatgpt" || defaultAgent == "deepseek") openAgent(defaultAgent);
    else chooseDefaultAgent();
    updateWindowCorners();
}

void Notepad::updateWindowCorners() {
    clearMask();
#ifdef Q_OS_WIN
    if (QApplication::platformName() != "windows")
        return;
    // DWM smooths the real window edge. Integer-pixel window regions disable it.
    constexpr DWORD cornerAttribute = 33; // DWMWA_WINDOW_CORNER_PREFERENCE
    constexpr DWORD borderAttribute = 34; // DWMWA_BORDER_COLOR
    const DWORD preference = isMaximized() || isFullScreen() ? 1 : 2;
    const COLORREF noBorder = 0xfffffffe; // DWMWA_COLOR_NONE
    const HWND handle = reinterpret_cast<HWND>(winId());
    DwmSetWindowAttribute(handle, cornerAttribute, &preference, sizeof(preference));
    DwmSetWindowAttribute(handle, borderAttribute, &noBorder, sizeof(noBorder));
#endif
}

void Notepad::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    updateWindowCorners();
    QTimer::singleShot(0, this, &Notepad::updateWindowCorners);
}

void Notepad::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    updateWindowCorners();
}

void Notepad::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange || event->type() == QEvent::ActivationChange) {
        updateWindowCorners();
        QTimer::singleShot(0, this, &Notepad::updateWindowCorners);
    }
}

void Notepad::createWorkspace() {
    auto *root = new QWidget(this);
    auto *outerLayout = new QVBoxLayout(root);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);
    auto *body = new QWidget(root);
    auto *rootLayout = new QHBoxLayout(body);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    setCentralWidget(root);

    auto *rail = new QFrame(root);
    rail->setObjectName("activityRail");
    rail->setFixedWidth(54);
    auto *railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(5, 14, 5, 10);
    railLayout->setSpacing(10);
    auto railButton = [rail, railLayout](const QString &icon,
                                         const QString &tip,
                                         const QString &name) {
        auto *button = new QToolButton(rail);
        button->setIcon(QIcon(":/icons/" + icon + ".svg"));
        button->setIconSize(QSize(22, 22));
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        button->setObjectName(name);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedSize(44, 42);
        railLayout->addWidget(button);
        return button;
    };
    sidebarButton = railButton("files", tr("项目资源管理器"), "explorerToggle");
    sidebarButton->setCheckable(true);
    sidebarButton->setChecked(true);
    extensionsButton = railButton("extensions", tr("扩展"), "extensionsButton");
    extensionsButton->setCheckable(true);
    runButton = railButton("run", tr("调试与运行"), "runButton");
    runButton->setCheckable(true);
    railLayout->addStretch();
    settingsButton = railButton("settings", tr("设置"), "settingsButton");
    rootLayout->addWidget(rail);

    splitter = new QSplitter(Qt::Horizontal, root);
    splitter->setObjectName("workspaceSplitter");
    splitter->setChildrenCollapsible(false);
    rootLayout->addWidget(splitter, 1);
    sidebar = new QFrame(splitter);
    sidebar->setObjectName("sidebar");
    sidebar->setMinimumWidth(190);
    sidebar->setMaximumWidth(420);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarPages = new QStackedWidget(sidebar);
    sidebarLayout->addWidget(sidebarPages);
    auto *explorer = new QWidget(sidebarPages);
    sidebarPages->addWidget(explorer);
    auto *sideLayout = new QVBoxLayout(explorer);
    sideLayout->setContentsMargins(12, 18, 12, 12);
    sideLayout->setSpacing(12);
    auto *projectHeader = new QWidget(explorer);
    projectHeader->setObjectName("projectHeader");
    auto *projectHeaderLayout = new QHBoxLayout(projectHeader);
    projectHeaderLayout->setContentsMargins(0, 0, 0, 0);
    projectHeaderLayout->setSpacing(4);
    backToProjects = new QToolButton(projectHeader);
    backToProjects->setObjectName("backToProjects");
    backToProjects->setArrowType(Qt::LeftArrow);
    backToProjects->setFixedSize(26, 26);
    backToProjects->setToolTip(tr("返回最近项目"));
    backToProjects->setAccessibleName(tr("返回最近项目"));
    projectHeaderLayout->addWidget(backToProjects);
    projectHeader->hide();
    sideLayout->addWidget(projectHeader);
    connect(backToProjects, &QToolButton::clicked, this, &Notepad::showRecentProjects);
    projectClickTimer = new QTimer(this);
    projectClickTimer->setSingleShot(true);
    connect(projectClickTimer, &QTimer::timeout, this, [this] {
        if (!pendingProject.isEmpty()) openProject(pendingProject);
        pendingProject.clear();
    });
    projectLabel = new QLabel(projectHeader);
    projectLabel->hide();
    projectLabel->setObjectName("projectName");
    projectLabel->setTextFormat(Qt::PlainText);
    projectLabel->installEventFilter(this);
    projectHeaderLayout->addWidget(projectLabel, 1);
    auto *workspaceList = new QListWidget(explorer);
    workspaceList->setObjectName("workspaceFolders");
    workspaceList->setMaximumHeight(140);
    workspaceList->hide();
    sideLayout->addWidget(workspaceList);
    connect(workspaceList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        openProject(item->data(Qt::UserRole).toString());
    });
    projectLabel->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(projectLabel, &QLabel::customContextMenuRequested, this, [this](const QPoint &point) {
        if (projectRoot.isEmpty()) return;
        QMenu menu(this);
        menu.addAction(tr("修改项目显示名称…"), this, [this] { renameProject(projectRoot, false); });
        menu.addAction(tr("修改文件夹名称…"), this, [this] { renameProject(projectRoot, true); });
        menu.exec(projectLabel->mapToGlobal(point));
    });
    recentProjectsPanel = new QWidget(explorer);
    recentProjectsPanel->setObjectName("recentProjectsPanel");
    auto *recentLayout = new QVBoxLayout(recentProjectsPanel);
    recentLayout->setContentsMargins(0, 8, 0, 0);
    auto *recentTitle = new QLabel(tr("最近项目"), recentProjectsPanel);
    recentTitle->setObjectName("recentProjectsTitle");
    recentLayout->addWidget(recentTitle);
    auto *recentList = new QListWidget(recentProjectsPanel);
    recentList->setObjectName("recentProjects");
    recentList->setFrameShape(QFrame::NoFrame);
    recentLayout->addWidget(recentList, 1);
    sideLayout->addWidget(recentProjectsPanel, 1);
    connect(recentList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        pendingProject = item->data(Qt::UserRole).toString();
        projectClickTimer->start(QApplication::doubleClickInterval() + 30);
    });
    connect(recentList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        projectClickTimer->stop(); pendingProject.clear();
        renameProject(item->data(Qt::UserRole).toString(), false);
    });
    recentList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(recentList, &QListWidget::customContextMenuRequested, this, [this, recentList](const QPoint &point) {
        auto *item = recentList->itemAt(point);
        if (!item) return;
        const auto path = item->data(Qt::UserRole).toString();
        QMenu menu(this);
        menu.addAction(tr("打开项目"), this, [this, path] { openProject(path); });
        menu.addAction(tr("修改项目显示名称…"), this, [this, path] { renameProject(path, false); });
        menu.addAction(tr("修改文件夹名称…"), this, [this, path] { renameProject(path, true); });
        menu.addSeparator();
        menu.addAction(tr("从最近项目移除"), this, [this, path] { removeRecentProject(path); });
        menu.exec(recentList->viewport()->mapToGlobal(point));
    });
    fileModel = new QFileSystemModel(this);
    fileModel->setObjectName("projectModel");
    fileModel->setReadOnly(true);
    fileModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    fileModel->setResolveSymlinks(false);
    projectTree = new QTreeView(sidebar);
    projectTree->setObjectName("projectTree");
    projectTree->setModel(fileModel);
    projectTree->setHeaderHidden(true);
    projectTree->setAnimated(false);
    projectTree->setUniformRowHeights(true);
    projectTree->setIndentation(16);
    projectTree->setIconSize(QSize(16, 16));
    projectTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    projectTree->setSelectionMode(QAbstractItemView::SingleSelection);
    projectTree->setFocusPolicy(Qt::StrongFocus);
    projectTree->installEventFilter(this);
    for (int column = 1; column < fileModel->columnCount(); ++column)
        projectTree->hideColumn(column);
    projectTree->hide();
    sideLayout->addWidget(projectTree, 1);
    connect(projectTree, &QTreeView::doubleClicked, this,
            [this](const QModelIndex &index) {
                if (!fileModel->isDir(index))
                    loadFile(fileModel->filePath(index));
            });
    projectTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(projectTree, &QTreeView::customContextMenuRequested, this,
            [this](const QPoint &point) {
                const QModelIndex index = projectTree->indexAt(point);
                const bool hasItem = index.isValid();
                const QString path = hasItem ? fileModel->filePath(index) : projectRoot;
                const bool isDir = hasItem ? fileModel->isDir(index) : true;
                const QString folder = isDir ? path : QFileInfo(path).absolutePath();
                const QFileInfo info(path);
                const bool isImage = hasItem && !isDir &&
                    (QStringList{"png", "jpg", "jpeg", "gif", "bmp", "webp", "svg", "ico"}.contains(info.suffix().toLower()));
                const bool isRoot = hasItem && samePath(path, projectRoot);

                QMenu menu(this);

                // 新建
                menu.addAction(tr("新建文件…"), this, [this, folder] { newFileInFolder(folder); });
                menu.addAction(tr("新建文件夹…"), this, [this, folder] { newFolderInFolder(folder); });
                menu.addSeparator();

                // 打开 / 预览
                QAction *reveal = menu.addAction(tr("在文件资源管理器中显示"), this, [this, path] { revealInExplorer(path); });
                reveal->setEnabled(hasItem);
                if (isImage) {
                    menu.addAction(tr("在图像预览中打开"), this, [this, path] { previewImage(path); });
                }
                if (hasItem && !isDir) {
                    menu.addAction(tr("固定到预览"), this, [this, path] { pinToPreview(path); });
                }
                QAction *openTerminal = menu.addAction(tr("在集成终端中打开"), this, [this, folder] { openTerminalAt(folder); });
                openTerminal->setEnabled(hasItem);
                menu.addSeparator();

                // 查找 / 工具
                menu.addAction(tr("在文件夹中查找…"), this, [this, folder] { searchInFolder(folder); });
                menu.addAction(tr("打开时间线"), this, [this, path] { showTimeline(path); })->setEnabled(hasItem);
                menu.addAction(tr("将文件夹添加到聊天"), this, [this, path] { addToChat(path); })->setEnabled(hasItem);
                menu.addSeparator();

                // 剪贴板
                QAction *cutAct = menu.addAction(tr("剪切"), QKeySequence::Cut, this, [this, path] { cutTreeItem(path); });
                cutAct->setEnabled(hasItem && !isRoot);
                QAction *copyAct = menu.addAction(tr("复制"), QKeySequence::Copy, this, [this, path] { copyTreeItem(path); });
                copyAct->setEnabled(hasItem && !isRoot);
                QAction *pasteAct = menu.addAction(tr("粘贴"), QKeySequence::Paste, this, [this, folder] { pasteTreeItem(folder); });
                pasteAct->setEnabled(!clipboardSource.isEmpty() && QFileInfo::exists(clipboardSource));
                menu.addSeparator();

                // 路径
                menu.addAction(tr("复制路径"), QKeySequence("Shift+Alt+C"), this, [this, path] { copyTreePath(path); })->setEnabled(hasItem);
                QAction *copyRel = menu.addAction(tr("复制相对路径"), QKeySequence("Ctrl+K, Ctrl+Shift+C"), this, [this, path] {
                    QApplication::clipboard()->setText(QDir(projectRoot).relativeFilePath(path));
                });
                copyRel->setEnabled(hasItem);
                menu.addSeparator();

                // 运行 / 测试
                if (hasItem && !isDir) {
                    menu.addAction(tr("运行测试"), this, [this, path] { runTestsFor(path); });
                    menu.addAction(tr("调试测试"), this, [this, path] {
                        loadFile(path); buildAndRun(true);
                    });
                }
                menu.addSeparator();

                // 管理
                QAction *rename = menu.addAction(tr("重命名…"), QKeySequence(Qt::Key_F2), this, [this, path] { renameTreeItem(path); });
                rename->setEnabled(hasItem);
                QAction *remove = menu.addAction(tr("删除"), QKeySequence::Delete, this, [this, path] { deleteTreeItem(path); });
                remove->setEnabled(hasItem && !isRoot);

                menu.exec(projectTree->viewport()->mapToGlobal(point));
            });
    connect(fileModel, &QFileSystemModel::directoryLoaded, this,
            [this](const QString &path) {
                if (samePath(path, projectRoot))
                    selectFileInTree();
            });
    createExtensionsPage();
    auto *runPage = new QWidget(sidebarPages);
    sidebarPages->addWidget(runPage);
    auto *runLayout = new QVBoxLayout(runPage);
    runLayout->setContentsMargins(12, 18, 12, 12);
    runLayout->addWidget(new QLabel(tr("调试与运行"), runPage));
    auto *runHelp = new QLabel(tr("运行当前 C/C++、Java 或 Python 文件。支持标准输入和停止程序。"), runPage);
    runHelp->setWordWrap(true);
    runLayout->addWidget(runHelp);
    auto *buildRun = new QPushButton(tr("编译并运行 (F5)"), runPage);
    buildRun->setObjectName("buildRunButton");
    runLayout->addWidget(buildRun);
    connect(buildRun, &QPushButton::clicked, this, [this] { buildAndRun(); });
    auto *compilerSettings = new QPushButton(tr("配置开发环境…"), runPage);
    runLayout->addWidget(compilerSettings);
    auto *runtimeMenu = new QMenu(compilerSettings);
    runtimeMenu->addAction(tr("C/C++ 编译器"), this, [this] { devTools->showCompilers(); });
    runtimeMenu->addAction(tr("Java JDK"), this, [this] { devTools->showRuntime("Java"); });
    runtimeMenu->addAction(tr("Python 解释器"), this, [this] { devTools->showRuntime("Python"); });
    compilerSettings->setMenu(runtimeMenu);
    auto *stopRun = new QPushButton(tr("停止运行"), runPage);
    runLayout->addWidget(stopRun);
    connect(stopRun, &QPushButton::clicked, this, [this] { devTools->stop(); });
    auto *debugRun = new QPushButton(tr("调试当前文件 (Shift+F5)"), runPage);
    debugRun->setObjectName("debugRunButton");
    runLayout->addWidget(debugRun);
    connect(debugRun, &QPushButton::clicked, this, [this] { buildAndRun(true); });
    runLayout->addStretch();
    connect(runButton, &QToolButton::clicked, this, [this](bool checked) {
        showSidebarPage(2, checked);
        if (checked && editor && !devTools->busy()) buildAndRun();
    });
    connect(sidebarButton, &QToolButton::clicked, this,
            [this](bool checked) { showSidebarPage(0, checked); });
    connect(extensionsButton, &QToolButton::clicked, this,
            [this](bool checked) { showSidebarPage(1, checked); });
    connect(settingsButton, &QToolButton::clicked, this,
            [this] { openSettings(); });

    auto *mainArea = new QWidget(splitter);
    mainArea->setObjectName("mainArea");
    mainArea->setObjectName("mainArea");
    auto *mainLayout = new QVBoxLayout(mainArea);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    auto *header = new WindowTitleBar(this);
    auto *headerLayout = header->contentLayout();
    auto *brandIcon = new QLabel(header);
    brandIcon->setPixmap(windowIcon().pixmap(QSize(24, 24), devicePixelRatioF()));
    brandIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
    headerLayout->addWidget(brandIcon);
    auto *workspaceLabel = new QLabel(QStringLiteral("Basis Code"), header);
    workspaceLabel->setObjectName("workspaceLabel");
    workspaceLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    headerLayout->addWidget(workspaceLabel);
    headerLayout->addStretch();
    auto menuButton = [header, headerLayout](const QString &text) {
        auto *button = new QToolButton(header);
        button->setText(text);
        button->setPopupMode(QToolButton::InstantPopup);
        headerLayout->addWidget(button);
        return button;
    };
    fileButton = menuButton(tr("文件"));
    editButton = menuButton(tr("编辑"));
    viewButton = menuButton(tr("视图"));
    helpButton = menuButton(tr("帮助"));
    saveButton = new QToolButton(header);
    saveButton->setObjectName("saveButton");
    saveButton->setText(tr("运行"));
    saveButton->setPopupMode(QToolButton::InstantPopup);
    saveButton->setToolTip(tr("运行与调试"));
    saveButton->setCursor(Qt::PointingHandCursor);
    auto *runMenu = new QMenu(saveButton);
    // 启动 / 停止
    runMenu->addAction(tr("启动调试"), QKeySequence(Qt::Key_F5), this, [this] { buildAndRun(true); });
    runMenu->addAction(tr("以非调试模式运行"), QKeySequence("Ctrl+F5"), this, [this] { buildAndRun(false); });
    QAction *stopAct = runMenu->addAction(tr("停止"), QKeySequence("Shift+F5"), this, [this] { devTools->stop(); });
    stopAct->setEnabled(false);
    QAction *restartAct = runMenu->addAction(tr("重启"), QKeySequence("Ctrl+Shift+F5"), this, [this] { buildAndRun(true); });
    restartAct->setEnabled(false);
    runMenu->addSeparator();
    // 配置
    runMenu->addAction(tr("打开配置"), this, [this] { devTools->showCompilers(); });
    runMenu->addAction(tr("添加配置…"), this, [this] { openToolkit("C/C++", QString()); });
    runMenu->addSeparator();
    // 单步调试（当前未接入调试器，处于禁用状态）
    runMenu->addAction(tr("逐过程"), QKeySequence(Qt::Key_F10), this, [this] {})->setEnabled(false);
    runMenu->addAction(tr("单步执行"), QKeySequence(Qt::Key_F11), this, [this] {})->setEnabled(false);
    runMenu->addAction(tr("单步停止"), QKeySequence("Shift+F11"), this, [this] {})->setEnabled(false);
    runMenu->addAction(tr("继续"), QKeySequence(Qt::Key_F5), this, [this] {})->setEnabled(false);
    runMenu->addSeparator();
    // 断点（当前未接入调试器，处于禁用状态）
    runMenu->addAction(tr("切换断点"), QKeySequence(Qt::Key_F9), this, [this] {})->setEnabled(false);
    auto *newBreakpoint = runMenu->addMenu(tr("新建断点"));
    newBreakpoint->addAction(tr("函数断点"), this, [this] {});
    newBreakpoint->addAction(tr("条件断点"), this, [this] {});
    newBreakpoint->setEnabled(false);
    runMenu->addAction(tr("启用所有断点"), this, [this] {})->setEnabled(false);
    runMenu->addAction(tr("禁用所有断点"), this, [this] {})->setEnabled(false);
    runMenu->addAction(tr("删除所有断点"), this, [this] {})->setEnabled(false);
    runMenu->addSeparator();
    runMenu->addAction(tr("安装附加调试器…"), this, [this] { openToolkit("C/C++", QString()); });
    saveButton->setMenu(runMenu);
    headerLayout->addSpacing(10);
    headerLayout->addWidget(saveButton);
    auto *terminalButton = new QToolButton(header);
    terminalButton->setObjectName("headerTerminalButton");
    terminalButton->setText(tr("终端"));
    terminalButton->setPopupMode(QToolButton::InstantPopup);
    terminalButton->setToolTip(tr("打开终端"));
    terminalButton->setAccessibleName(tr("打开终端"));
    terminalButton->setCursor(Qt::PointingHandCursor);
    auto *terminalMenu = new QMenu(terminalButton);
    terminalMenu->addAction(tr("新建终端"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_QuoteLeft),
                            this, [this] { devTools->showTerminal(projectRoot); });
    terminalMenu->addSeparator();
    terminalMenu->addAction(tr("运行活动文件"), this, [this] { buildAndRun(); });
    terminalMenu->addAction(tr("调试当前文件"), QKeySequence("Shift+F5"), this, [this] { buildAndRun(true); });
    terminalMenu->addSeparator();
    terminalMenu->addAction(tr("终止任务"), this, [this] { devTools->stop(); });
    terminalButton->setMenu(terminalMenu);
    headerLayout->addSpacing(10);
    headerLayout->addWidget(terminalButton);
    outerLayout->addWidget(header);
    outerLayout->addWidget(body, 1);
    tabs = new QTabWidget(mainArea);
    tabs->setObjectName("documentTabs");
    tabs->setDocumentMode(true);
    tabs->setTabsClosable(true);
    tabs->setMovable(true);
    tabs->tabBar()->setExpanding(false);
    tabs->setElideMode(Qt::ElideMiddle);
    tabs->setUsesScrollButtons(true);

    // 水平分栏：左侧主编辑器区 + 右侧预览区
    editorSplit = new QSplitter(Qt::Horizontal, mainArea);
    editorSplit->setObjectName("editorSplit");
    editorSplit->setChildrenCollapsible(false);
    editorSplit->addWidget(tabs);

    // 右侧预览面板
    previewPane = new QWidget(editorSplit);
    previewPane->setObjectName("previewPane");
    auto *previewLayout = new QVBoxLayout(previewPane);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(0);
    auto *previewHeader = new QWidget(previewPane);
    previewHeader->setObjectName("previewHeader");
    auto *previewHeaderLayout = new QHBoxLayout(previewHeader);
    previewHeaderLayout->setContentsMargins(10, 6, 6, 6);
    previewHeaderLayout->setSpacing(6);
    previewTitle = new QLabel(tr("预览"), previewHeader);
    previewTitle->setObjectName("previewTitle");
    previewHeaderLayout->addWidget(previewTitle);
    previewHeaderLayout->addStretch();
    auto *closePreviewBtn = new QToolButton(previewHeader);
    closePreviewBtn->setText(tr("×"));
    closePreviewBtn->setObjectName("closePreviewButton");
    closePreviewBtn->setToolTip(tr("关闭预览"));
    closePreviewBtn->setAutoRaise(true);
    closePreviewBtn->setCursor(Qt::PointingHandCursor);
    connect(closePreviewBtn, &QToolButton::clicked, this, &Notepad::closePreview);
    previewHeaderLayout->addWidget(closePreviewBtn);
    previewLayout->addWidget(previewHeader);
    previewEditor = new CodeEditor(previewPane);
    previewEditor->setObjectName("previewEditor");
    previewEditor->setReadOnly(true);
    previewEditor->setFrameShape(QFrame::NoFrame);
    previewEditor->setColorTheme(themeName);
    previewEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    previewLayout->addWidget(previewEditor, 1);
    editorSplit->addWidget(previewPane);
    previewPane->hide();

    auto *codeSplit = new QSplitter(Qt::Vertical, mainArea);
    codeSplit->setObjectName("codeOutputSplitter");
    codeSplit->setChildrenCollapsible(false);
    codeSplit->addWidget(editorSplit);
    mainLayout->addWidget(codeSplit, 1);
    connect(tabs, &QTabWidget::currentChanged, this,
            &Notepad::activateDocument);
    connect(tabs, &QTabWidget::tabCloseRequested, this, &Notepad::closeTab);
    connect(tabs->tabBar(), &QTabBar::tabMoved, this,
            [this] { syncDocuments(); });
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({245, 880});
    pathLabel = new QLabel(this);
    pathLabel->setObjectName("activeFilePath");
    pathLabel->setTextFormat(Qt::PlainText);
    pathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    positionLabel = new QLabel(this);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(pathLabel, 1);
    statusBar()->addPermanentWidget(positionLabel);
}

void Notepad::showSidebarPage(int index, bool visible) {
    sidebarPages->setCurrentIndex(index);
    runButton->setChecked(visible && index == 2);
    sidebarButton->setChecked(visible && index == 0);
    extensionsButton->setChecked(visible && index == 1);
    sidebar->setVisible(visible);
}

void Notepad::createExtensionsPage() {
    auto *market = new QWidget(sidebarPages);
    sidebarPages->addWidget(market);
    auto *layout = new QVBoxLayout(market);
    layout->setContentsMargins(10, 12, 10, 8);
    auto *search = new QLineEdit(market);
    search->setObjectName("extensionSearch");
    search->setPlaceholderText(tr("搜索扩展"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);
    auto *list = new QListWidget(market);
    list->setObjectName("extensionList");
    list->setFrameShape(QFrame::NoFrame);
    list->setIconSize(QSize(36, 36));
    auto addToolkit = [list](const QString &icon, const QString &name, const QString &summary) {
        auto *item = new QListWidgetItem(pluginIcon(icon), name, list);
        item->setData(Qt::UserRole, summary);
    };
    addToolkit("cpp", tr("C/C++"), tr("选择下载 GCC / Clang 编译器，使用内置运行器编译运行 C/C++ 源文件。"));
    addToolkit("java", tr("Java"), tr("配置 JDK，编译运行 Java，并使用 JDB 调试。"));
    addToolkit("python", tr("Python"), tr("配置 Python，运行脚本并使用 PDB 调试。"));
    addToolkit("chatgpt", "ChatGPT", tr("在 Basis Code 中使用 ChatGPT 官网，登录你的现有账号。"));
    addToolkit("deepseek", "DeepSeek", tr("使用 DeepSeek 官方 API，配置密钥后开始聊天。"));
    addToolkit("cpp", "Markdown Preview", tr("实时 Markdown 预览，编辑后自动刷新。"));
    addToolkit("java", "JSON Tools", tr("JSON 校验和格式化，支持撤销。"));
    addToolkit("python", "Whitespace Cleaner", tr("清理行尾空白与多余尾行。"));
    addToolkit("cpp", "Git Changes", tr("异步查看工作区 Git 状态和差异。"));
    layout->addWidget(list);
    connect(search, &QLineEdit::textChanged, list, [list](const QString &text) {
        for (int row = 0; row < list->count(); ++row) {
            auto *item = list->item(row);
            item->setHidden(!item->text().contains(text.trimmed(), Qt::CaseInsensitive));
        }
    });
    auto open = [this](QListWidgetItem *item) {
        openToolkit(item->text(), item->data(Qt::UserRole).toString());
    };
    connect(list, &QListWidget::itemClicked, this, open);
    connect(list, &QListWidget::itemActivated, this, open);
}

void Notepad::openSettings() {
    // 若设置页已在某个标签页中打开，直接聚焦
    for (int i = 0; i < tabs->count(); ++i) {
        if (tabs->widget(i)->property("isSettingsPage").toBool()) {
            tabs->setCurrentIndex(i);
            return;
        }
    }
    auto *page = new QWidget(tabs);
    page->setObjectName("settingsPage");
    page->setProperty("isSettingsPage", true);
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QSettings cfg(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);

    // ── 顶部标题 + 搜索 ──
    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(16, 12, 16, 8);
    auto *title = new QLabel(tr("设置"), page);
    title->setObjectName("settingsTitle");
    headerRow->addWidget(title);
    headerRow->addStretch();
    auto *search = new QLineEdit(page);
    search->setObjectName("settingsSearch");
    search->setPlaceholderText(tr("搜索设置"));
    search->setClearButtonEnabled(true);
    search->setFixedWidth(220);
    headerRow->addWidget(search);
    root->addLayout(headerRow);

    // ── 主体：左侧分类 + 右侧设置项 ──
    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    auto *nav = new QListWidget(page);
    nav->setObjectName("settingsNav");
    nav->setFixedWidth(180);
    nav->setFrameShape(QFrame::NoFrame);
    const QStringList categories = {
        tr("常用设置"), tr("文本编辑器"), tr("工作台"), tr("窗口"),
        tr("聊天"), tr("功能"), tr("应用程序"), tr("安全性"), tr("扩展")};
    for (const QString &c : categories)
        nav->addItem(c);
    body->addWidget(nav);

    auto *contentStack = new QStackedWidget(page);
    contentStack->setObjectName("settingsContent");
    body->addWidget(contentStack, 1);
    root->addLayout(body, 1);

    // 每个分类一个页面；常用设置页承载核心可编辑设置项
    auto makeCategoryPage = [contentStack](const QString &) {
        auto *p = new QWidget(contentStack);
        auto *l = new QVBoxLayout(p);
        l->setContentsMargins(20, 16, 20, 16);
        return p;
    };

    // ── 常用设置页 ──
    auto *common = makeCategoryPage(tr("常用设置"));
    auto *commonLayout = qobject_cast<QVBoxLayout *>(common->layout());
    auto *commonScroll = new QScrollArea(common);
    commonScroll->setWidgetResizable(true);
    commonScroll->setFrameShape(QFrame::NoFrame);
    commonScroll->setObjectName("settingsCommonScroll");
    auto *commonInner = new QWidget;
    commonInner->setObjectName("settingsCommonInner");
    auto *commonInnerLayout = new QVBoxLayout(commonInner);
    commonInnerLayout->setContentsMargins(0, 0, 8, 0);
    commonInnerLayout->setSpacing(18);
    commonScroll->setWidget(commonInner);
    commonLayout->addWidget(commonScroll);

    // 设置项构造辅助：标题 + 描述 + 控件
    auto addSetting = [commonInnerLayout](const QString &titleText,
                                          const QString &descText,
                                          QWidget *control) {
        auto *block = new QVBoxLayout;
        block->setSpacing(4);
        auto *t = new QLabel(titleText);
        t->setObjectName("settingsItemTitle");
        auto *d = new QLabel(descText);
        d->setObjectName("settingsItemDesc");
        d->setWordWrap(true);
        block->addWidget(t);
        block->addWidget(d);
        block->addWidget(control);
        commonInnerLayout->addLayout(block);
    };

    // 0. 颜色主题
    auto *colorTheme = new QComboBox(commonInner);
    colorTheme->setObjectName("settingsCombo");
    colorTheme->addItem(tr("亮色 · 清晰"), "light");
    colorTheme->addItem("Tokyo Night", "tokyo-night");
    colorTheme->addItem("GitHub Dark Default", "github-dark-default");
    int themeIdx = colorTheme->findData(themeName);
    colorTheme->setCurrentIndex(themeIdx < 0 ? 0 : themeIdx);
    addSetting(tr("Workbench: Color Theme"),
               tr("指定工作台使用的颜色主题。"), colorTheme);
    connect(colorTheme, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, colorTheme](int) {
                setColorTheme(colorTheme->currentData().toString());
            });

    // 1. 字体大小
    auto *fontSize = new QSpinBox(commonInner);
    fontSize->setObjectName("settingsSpin");
    fontSize->setRange(6, 72);
    fontSize->setValue(editorFontSize);
    fontSize->setSuffix(" px");
    addSetting(tr("Editor: Font Size"),
               tr("控制字体大小（像素）。"), fontSize);
    connect(fontSize, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int v) {
                editorFontSize = v;
                QSettings s(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
                s.setValue("editor/fontSize", v);
                applyEditorFont();
            });

    // 2. 字体系列
    auto *fontFamily = new QComboBox(commonInner);
    fontFamily->setObjectName("settingsCombo");
    fontFamily->setEditable(true);
    for (const auto &f : {"Cascadia Code", "Cascadia Mono", "Consolas",
                          "Courier New", "JetBrains Mono", "Fira Code",
                          "Source Code Pro", "monospace"})
        fontFamily->addItem(QString::fromLatin1(f));
    fontFamily->setCurrentText(editorFontFamily.isEmpty() ? "Consolas" : editorFontFamily);
    addSetting(tr("Editor: Font Family"),
               tr("控制字体系列。"), fontFamily);
    connect(fontFamily, &QComboBox::currentTextChanged, this,
            [this](const QString &family) {
                editorFontFamily = family;
                QSettings s(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
                s.setValue("editor/fontFamily", family);
                applyEditorFont();
            });

    // 3. 自动换行
    auto *wordWrap = new QComboBox(commonInner);
    wordWrap->setObjectName("settingsCombo");
    wordWrap->addItem(tr("off"));
    wordWrap->addItem(tr("on"));
    wordWrap->addItem(tr("wordWrapColumn"));
    const QString savedWrap = cfg.value("editor/wordWrap", "off").toString();
    wordWrap->setCurrentIndex(savedWrap == "on" ? 1 : savedWrap == "wordWrapColumn" ? 2 : 0);
    addSetting(tr("Editor: Word Wrap"),
               tr("控制换行方式。"), wordWrap);
    connect(wordWrap, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, wordWrap](int) {
                QSettings s(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
                s.setValue("editor/wordWrap", wordWrap->currentText());
                applyWordWrap();
            });

    // 4. 自动保存
    auto *autoSave = new QComboBox(commonInner);
    autoSave->setObjectName("settingsCombo");
    autoSave->addItem(tr("off"));
    autoSave->addItem(tr("afterDelay"));
    autoSave->addItem(tr("onFocusChange"));
    autoSave->setCurrentText(cfg.value("files/autoSave", false).toBool()
                                 ? tr("afterDelay") : tr("off"));
    addSetting(tr("Files: Auto Save"),
               tr("控制具有未保存更改的编辑器的自动保存。"), autoSave);
    connect(autoSave, &QComboBox::currentTextChanged, this,
            [this](const QString &mode) {
                QSettings s(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
                s.setValue("files/autoSave", mode != "off");
            });

    // 5. 保存时格式化
    auto *formatOnSave = new QCheckBox(tr("在保存时格式化文件"), commonInner);
    formatOnSave->setObjectName("settingsCheck");
    formatOnSave->setChecked(cfg.value("editor/formatOnSave", false).toBool());
    addSetting(tr("Editor: Format On Save"),
               tr("保存时设置文件格式。格式化程序必须可用，并且不得关闭编辑器。"), formatOnSave);
    connect(formatOnSave, &QCheckBox::toggled, this, [](bool on) {
        QSettings s(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
        s.setValue("editor/formatOnSave", on);
    });

    // 6. 默认格式化程序
    auto *defaultFormatter = new QComboBox(commonInner);
    defaultFormatter->setObjectName("settingsCombo");
    defaultFormatter->addItem(tr("无"));
    defaultFormatter->addItem("clang-format");
    defaultFormatter->addItem("astyle");
    defaultFormatter->setCurrentText(cfg.value("editor/defaultFormatter", tr("无")).toString());
    addSetting(tr("Editor: Default Formatter"),
               tr("定义一个默认格式化程序，该格式化程序优先于所有其他格式化程序设置。"), defaultFormatter);
    connect(defaultFormatter, &QComboBox::currentTextChanged, this,
            [](const QString &f) {
                QSettings s(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
                s.setValue("editor/defaultFormatter", f);
            });

    commonInnerLayout->addStretch();

    // 其余分类：占位说明
    const QStringList placeholders = {
        tr("文本编辑器相关设置"), tr("工作台相关设置"), tr("窗口相关设置"),
        tr("聊天相关设置"), tr("功能相关设置"), tr("应用程序相关设置"),
        tr("安全性相关设置"), tr("扩展相关设置")};
    for (const QString &hint : placeholders) {
        auto *p = new QWidget(contentStack);
        auto *l = new QVBoxLayout(p);
        l->setContentsMargins(20, 16, 20, 16);
        auto *empty = new QLabel(tr("「%1」下暂无可配置项。").arg(hint), p);
        empty->setObjectName("settingsEmpty");
        empty->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        l->addWidget(empty);
        l->addStretch();
        contentStack->addWidget(p);
    }
    contentStack->insertWidget(0, common);

    connect(nav, &QListWidget::currentRowChanged, contentStack,
            [contentStack](int row) { contentStack->setCurrentIndex(row); });
    nav->setCurrentRow(0);

    // 搜索：过滤导航分类（简单实现：匹配分类名或设置标题）
    connect(search, &QLineEdit::textChanged, this, [nav, search](const QString &text) {
        const QString t = text.trimmed();
        for (int i = 0; i < nav->count(); ++i) {
            auto *item = nav->item(i);
            item->setHidden(!t.isEmpty() && !item->text().contains(t, Qt::CaseInsensitive));
        }
    });

    // 从配置恢复字体设置
    editorFontFamily = cfg.value("editor/fontFamily", QString()).toString();
    editorFontSize = cfg.value("editor/fontSize", 12).toInt();

    // 作为代码区的标签页打开
    tabs->setCurrentIndex(tabs->addTab(page, QIcon(":/icons/settings.svg"), tr("设置")));
}

void Notepad::applyEditorFont() {
    for (int i = 0; i < tabs->count(); ++i) {
        auto *page = qobject_cast<QPlainTextEdit *>(tabs->widget(i));
        if (!page) continue;
        QFont font = page->font();
        if (!editorFontFamily.isEmpty())
            font.setFamily(editorFontFamily);
        font.setPointSize(editorFontSize + zoomSteps);
        page->setFont(font);
        page->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(' ') * 4);
    }
}

void Notepad::applyWordWrap() {
    QSettings s(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
    const bool wrap = s.value("editor/wordWrap", "off").toString() == "on";
    for (int i = 0; i < tabs->count(); ++i) {
        auto *page = qobject_cast<QPlainTextEdit *>(tabs->widget(i));
        if (!page) continue;
        page->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    }
    if (wrapAction) wrapAction->setChecked(wrap);
}

void Notepad::pinToPreview(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("无法预览"), file.errorString());
        return;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    const QString text = stream.readAll();
    previewEditor->setPlainText(text);
    previewEditor->setFilePath(path);
    previewTitle->setText(QFileInfo(path).fileName());
    previewTitle->setToolTip(path);
    previewPane->show();
    // 首次显示时给预览区分配一半宽度
    if (editorSplit->sizes().size() == 2 &&
        editorSplit->sizes()[1] <= 0) {
        const int total = editorSplit->width();
        editorSplit->setSizes({total / 2, total / 2});
    }
}

void Notepad::closePreview() {
    previewPane->hide();
}

void Notepad::openToolkit(const QString &name, const QString &summary) {
    if (name == "ChatGPT" || name == "DeepSeek") { openAgent(name.toLower()); return; }
    for (int i = 0; i < tabs->count(); ++i) {
        if (tabs->widget(i)->property("toolkitName").toString() == name) {
            tabs->setCurrentIndex(i);
            return;
        }
    }
    if (QStringList{"Markdown Preview", "JSON Tools", "Whitespace Cleaner", "Git Changes"}.contains(name)) {
        auto *source = editor;
        if (!source) for (int i = tabs->count() - 1; i >= 0; --i) {
            if (auto *candidate = qobject_cast<QPlainTextEdit *>(tabs->widget(i))) { source = candidate; break; }
        }
        const QString path = source ? source->property("filePath").toString() : QString();
        auto *page = createBuiltinExtension(name, source, path, projectRoot, tabs);
        tabs->setCurrentIndex(tabs->addTab(page, name));
        return;
    }
    auto *details = new QScrollArea(tabs);
    details->setWidgetResizable(true);
    details->setFrameShape(QFrame::NoFrame);
    details->setObjectName("toolkitPage");
    details->setProperty("toolkitName", name);
    auto *surface = new QWidget(details);
    surface->setObjectName("toolkitSurface");
    details->setWidget(surface);
    auto *outer = new QHBoxLayout(surface);
    outer->setContentsMargins(28, 32, 28, 32);
    auto *content = new QWidget(surface);
    content->setMaximumWidth(900);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(22);
    outer->addWidget(content, 1, Qt::AlignTop);
    outer->addStretch();
    auto *header = new QHBoxLayout;
    const QString iconName = name == "C/C++" ? "cpp" : name.toLower();
    auto *icon = new QLabel(content);
    icon->setPixmap(QIcon(":/icons/language-" + iconName + ".svg").pixmap(52, 52));
    header->addWidget(icon);
    header->addSpacing(12);
    auto *titles = new QVBoxLayout;
    auto *heading = new QLabel(name, content);
    heading->setObjectName("toolkitTitle");
    titles->addWidget(heading);
    auto *description = new QLabel(summary.isEmpty() ? tr("选择并管理开发环境，安装所需的运行支持，或使用命令终端。") : summary, content);
    description->setObjectName("toolkitDescription");
    description->setWordWrap(true);
    titles->addWidget(description);
    header->addLayout(titles, 1);
    layout->addLayout(header);
    {
        auto addCard = [content, layout](const QString &title, const QString &text,
                                         const QString &action, bool primary) {
            auto *card = new QFrame(content);
            card->setObjectName("toolkitCard");
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(22, 20, 22, 20);
            cardLayout->setSpacing(12);
            auto *label = new QLabel(title, card);
            label->setObjectName("toolkitSectionTitle");
            cardLayout->addWidget(label);
            auto *body = new QLabel(text, card);
            body->setObjectName("toolkitDescription");
            body->setWordWrap(true);
            cardLayout->addWidget(body);
            auto *button = new QPushButton(action, card);
            button->setProperty("toolkitAction", primary ? "primary" : "secondary");
            button->setCursor(Qt::PointingHandCursor);
            cardLayout->addWidget(button, 0, Qt::AlignLeft);
            layout->addWidget(card);
            return button;
        };
        auto *configurationCard = new QFrame(content);
        configurationCard->setObjectName("toolkitCard");
        auto *configurationLayout = new QVBoxLayout(configurationCard);
        configurationLayout->setContentsMargins(22, 20, 22, 20);
        auto *configurationTitle = new QLabel(name == "C/C++" ? tr("编译器") : tr("开发环境"), configurationCard);
        configurationTitle->setObjectName("toolkitSectionTitle");
        configurationLayout->addWidget(configurationTitle);
        configurationLayout->addWidget(devTools->createToolchainPage(configurationCard, name));
        layout->addWidget(configurationCard);
        auto *runnerButton = addCard(tr("运行支持 · 可选"),
            tr("安装随软件提供的运行组件，即可使用 F5 和调试入口。也可以直接在终端中使用编译器或解释器。"),
            DevTools::runnerInstalled(name) ? tr("打开运行侧栏") : tr("安装运行支持"), false);
        runnerButton->setObjectName("installRunnerButton");
        connect(runnerButton, &QPushButton::clicked, this, [this, name, runnerButton] {
            if (!DevTools::runnerInstalled(name) && !DevTools::installRunner(name)) {
                QMessageBox::warning(this, tr("安装失败"), tr("无法写入运行组件配置。")); return;
            }
        runnerButton->setText(tr("打开运行侧栏"));
        showSidebarPage(2, true);
    });
    }
    layout->addStretch();
    tabs->setCurrentIndex(tabs->addTab(details, name));
}

bool Notepad::openProject(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir() || !info.isReadable())
        return false;
    projectRoot = normalizedPath(path);
    QSettings config(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
    auto recent = config.value("projects/recent").toStringList();
    recent.removeAll(projectRoot); recent.prepend(projectRoot);
    while (recent.size() > 20) recent.removeLast();
    config.setValue("projects/recent", recent);
    const QModelIndex root = fileModel->setRootPath(projectRoot);
    projectTree->setRootIndex(root);
    findChild<QWidget *>("projectHeader")->show();
    recentProjectsPanel->hide();
    projectTree->show();
    projectLabel->setText(QDir(projectRoot).dirName().isEmpty()
                              ? projectRoot
                              : QDir(projectRoot).dirName());
    projectLabel->setToolTip(projectRoot);
    projectLabel->setText(projectDisplayName(projectRoot));
    backToProjects->show();
    refreshWorkspaceFolders();
    projectLabel->show();
    showSidebarPage(0, true);
    updateActions();
    selectFileInTree();
    updateStatus();
    return true;
}

void Notepad::chooseProject() {
    const QString path = QFileDialog::getExistingDirectory(
        this, tr("打开项目文件夹"), projectRoot.isEmpty() ? DevTools::projectsRoot() : projectRoot);
    if (!path.isEmpty() && !openProject(path)) {
        QMessageBox::warning(this, tr("打开失败"), tr("无法读取该文件夹。"));
    }
}

void Notepad::selectFileInTree() {
    if (projectRoot.isEmpty())
        return;
    if (currentFilePath().isEmpty()) {
        projectTree->setCurrentIndex(QModelIndex());
        return;
    }
    const QString relative =
        QDir(projectRoot).relativeFilePath(currentFilePath());
    if (relative == ".." || relative.startsWith("../") ||
        QDir::isAbsolutePath(relative)) {
        projectTree->setCurrentIndex(QModelIndex());
        return;
    }
    const QModelIndex index = fileModel->index(currentFilePath());
    if (!index.isValid())
        return;
    for (QModelIndex parent = index.parent();
         parent.isValid() && parent != projectTree->rootIndex();
         parent = parent.parent()) {
        projectTree->expand(parent);
    }
    projectTree->setCurrentIndex(index);
    projectTree->scrollTo(index);
}
void Notepad::newFileInFolder(const QString &folder) {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("新建文件"), tr("文件名"),
                                               QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    if (name.contains(QRegularExpression(R"([<>:"/\\|?*])")) || name.endsWith('.') || name.endsWith(' ')) {
        QMessageBox::warning(this, tr("名称无效"), tr("请输入合法的文件名。")); return;
    }
    const QString path = QDir(folder).filePath(name);
    if (QFileInfo::exists(path)) { QMessageBox::warning(this, tr("无法创建"), tr("同名文件已存在。")); return; }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("无法创建"), tr("无法创建文件：%1").arg(file.errorString())); return;
    }
    file.close();
    loadFile(path);
}
void Notepad::newFolderInFolder(const QString &folder) {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("新建文件夹"), tr("文件夹名称"),
                                               QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    if (name.contains(QRegularExpression(R"([<>:"/\\|?*])")) || name.endsWith('.') || name.endsWith(' ')) {
        QMessageBox::warning(this, tr("名称无效"), tr("请输入合法的文件夹名称。")); return;
    }
    const QString path = QDir(folder).filePath(name);
    if (QFileInfo::exists(path)) { QMessageBox::warning(this, tr("无法创建"), tr("同名文件夹已存在。")); return; }
    if (!QDir().mkpath(path)) { QMessageBox::warning(this, tr("无法创建"), tr("无法创建文件夹。")); return; }
    const QModelIndex index = fileModel->index(path);
    projectTree->setCurrentIndex(index);
    projectTree->scrollTo(index);
}
void Notepad::deleteTreeItem(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists()) return;
    if (samePath(path, projectRoot)) {
        QMessageBox::information(this, tr("无法删除"), tr("不能删除当前项目根目录。请在最近项目中移除该项目。"));
        return;
    }
    const QString name = info.fileName();
    const auto confirm = QMessageBox::question(this, tr("删除确认"),
        tr("确定要删除“%1”吗？此操作不可撤销。").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (confirm != QMessageBox::Yes) return;
    bool ok = false;
    if (info.isDir()) {
        // 若删除的目录里含当前打开的文件，先关闭对应标签页。
        for (int i = tabs->count() - 1; i >= 0; --i) {
            auto *page = qobject_cast<QPlainTextEdit *>(tabs->widget(i));
            if (!page) continue;
            const QString filePath = page->property("filePath").toString();
            if (filePath.startsWith(normalizedPath(path) + "/")) closeTab(i);
        }
        ok = QDir(path).removeRecursively();
    } else {
        ok = QFile::remove(path);
    }
    if (!ok) QMessageBox::warning(this, tr("删除失败"), tr("文件或文件夹可能正在被其他程序占用。"));
}
void Notepad::renameTreeItem(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists()) return;
    if (samePath(path, projectRoot)) {
        renameProject(path, true);
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("重命名"), tr("新名称"),
                                               QLineEdit::Normal, info.fileName(), &accepted).trimmed();
    if (!accepted || name.isEmpty() || name == info.fileName()) return;
    if (name.contains(QRegularExpression(R"([<>:"/\\|?*])")) || name.endsWith('.') || name.endsWith(' ')) {
        QMessageBox::warning(this, tr("名称无效"), tr("请输入合法的名称。")); return;
    }
    const QString newPath = info.dir().filePath(name);
    if (QFileInfo::exists(newPath)) { QMessageBox::warning(this, tr("无法重命名"), tr("目标名称已经存在。")); return; }
    bool ok = QFile::rename(path, newPath);
    if (!ok) { QMessageBox::warning(this, tr("重命名失败"), tr("文件可能正在被其他程序占用。")); return; }
    // 若被重命名的文件已打开，更新其路径。
    for (int i = 0; i < tabs->count(); ++i) {
        auto *page = qobject_cast<QPlainTextEdit *>(tabs->widget(i));
        if (!page) continue;
        if (samePath(page->property("filePath").toString(), path)) {
            page->setProperty("filePath", newPath);
            tabs->setTabText(i, QFileInfo(newPath).fileName());
        }
    }
    if (info.isDir()) {
        // 目录改名后，其下所有打开文件的路径都要同步。
        for (int i = 0; i < tabs->count(); ++i) {
            auto *page = qobject_cast<QPlainTextEdit *>(tabs->widget(i));
            if (!page) continue;
            const QString fp = page->property("filePath").toString();
            if (fp.startsWith(normalizedPath(path) + "/")) {
                const QString newFp = newPath + fp.mid(normalizedPath(path).length());
                page->setProperty("filePath", newFp);
                tabs->setTabText(i, QFileInfo(newFp).fileName());
            }
        }
    }
    selectFileInTree();
}
void Notepad::copyTreePath(const QString &path) {
    QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
}
void Notepad::revealInExplorer(const QString &path) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath()));
}
void Notepad::openTerminalAt(const QString &path) {
    devTools->showTerminal(path);
}
void Notepad::cutTreeItem(const QString &path) {
    if (samePath(path, projectRoot)) {
        QMessageBox::information(this, tr("无法剪切"), tr("不能剪切当前项目根目录。"));
        return;
    }
    clipboardSource = normalizedPath(path);
    clipboardCut = true;
}
void Notepad::copyTreeItem(const QString &path) {
    if (samePath(path, projectRoot)) {
        QMessageBox::information(this, tr("无法复制"), tr("不能复制当前项目根目录。"));
        return;
    }
    clipboardSource = normalizedPath(path);
    clipboardCut = false;
}
void Notepad::pasteTreeItem(const QString &folder) {
    if (clipboardSource.isEmpty() || !QFileInfo::exists(clipboardSource)) {
        QMessageBox::information(this, tr("粘贴"), tr("剪贴板中没有可粘贴的文件。"));
        return;
    }
    const QString name = QFileInfo(clipboardSource).fileName();
    QString target = QDir(folder).filePath(name);
    if (samePath(target, clipboardSource)) {
        QMessageBox::information(this, tr("粘贴"), tr("源与目标位置相同。"));
        return;
    }
    if (QFileInfo::exists(target)) {
        // 生成「name (1)」样式的不重名目标。
        const QString base = QFileInfo(clipboardSource).completeBaseName();
        const QString suffix = QFileInfo(clipboardSource).suffix();
        int n = 1;
        do {
            target = QDir(folder).filePath(suffix.isEmpty() ? QString("%1 (%2)").arg(base).arg(n)
                                                            : QString("%1 (%2).%3").arg(base).arg(n).arg(suffix));
            ++n;
        } while (QFileInfo::exists(target));
    }
    const QFileInfo src(clipboardSource);
    bool ok = false;
    if (src.isDir()) ok = copyDirRecursive(clipboardSource, target);
    else ok = QFile::copy(clipboardSource, target);
    if (!ok) {
        QMessageBox::warning(this, tr("粘贴失败"), tr("无法复制到目标位置。"));
        return;
    }
    if (clipboardCut) {
        if (src.isDir()) QDir(clipboardSource).removeRecursively();
        else QFile::remove(clipboardSource);
        clipboardSource.clear();
        clipboardCut = false;
    }
}
void Notepad::previewImage(const QString &path) {
    QPixmap pixmap(path);
    if (pixmap.isNull()) {
        QMessageBox::warning(this, tr("无法预览"), tr("无法加载该图片。"));
        return;
    }
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(QFileInfo(path).fileName());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(dialog);
    scroll->setWidgetResizable(true);
    auto *label = new QLabel(scroll);
    label->setPixmap(pixmap);
    label->setAlignment(Qt::AlignCenter);
    scroll->setWidget(label);
    layout->addWidget(scroll);
    dialog->resize(qMin(1000, pixmap.width() + 40), qMin(700, pixmap.height() + 40));
    dialog->open();
}
void Notepad::showTimeline(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists()) return;
    const QDateTime created = info.birthTime();
    const QDateTime modified = info.lastModified();
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("时间线 · %1").arg(info.fileName()));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(10);
    auto addLine = [layout](const QString &k, const QString &v) {
        auto *row = new QHBoxLayout();
        auto *key = new QLabel(k);
        key->setObjectName("timelineKey");
        key->setMinimumWidth(90);
        auto *val = new QLabel(v);
        val->setObjectName("timelineValue");
        val->setWordWrap(true);
        val->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row->addWidget(key, 0, Qt::AlignTop);
        row->addWidget(val, 1);
        layout->addLayout(row);
    };
    addLine(tr("名称"), info.fileName());
    addLine(tr("类型"), info.isDir() ? tr("文件夹") : tr("文件"));
    addLine(tr("完整路径"), QDir::toNativeSeparators(info.absoluteFilePath()));
    addLine(tr("创建时间"), created.isValid() ? created.toString("yyyy-MM-dd HH:mm:ss") : tr("未知"));
    addLine(tr("修改时间"), modified.isValid() ? modified.toString("yyyy-MM-dd HH:mm:ss") : tr("未知"));
    if (info.isFile()) addLine(tr("大小"), tr("%1 字节").arg(info.size()));
    auto *closeBtn = new QPushButton(tr("关闭"), dialog);
    connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    layout->addWidget(closeBtn, 0, Qt::AlignRight);
    dialog->resize(480, dialog->sizeHint().height());
    dialog->open();
}
void Notepad::searchInFolder(const QString &folder) {
    bool accepted = false;
    const QString keyword = QInputDialog::getText(this, tr("在文件夹中查找"),
        tr("搜索关键词（匹配文件名或内容）"), QLineEdit::Normal, QString(), &accepted).trimmed();
    if (!accepted || keyword.isEmpty()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QStringList matches;
    QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QFileInfo info(file);
        if (info.fileName().contains(keyword, Qt::CaseInsensitive)) {
            matches.append(file);
            continue;
        }
        if (info.size() > 2 * 1024 * 1024) continue;
        QFile f(file);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (QString::fromUtf8(f.readAll()).contains(keyword, Qt::CaseInsensitive))
                matches.append(file);
            f.close();
        }
    }
    QApplication::restoreOverrideCursor();
    if (matches.isEmpty()) {
        QMessageBox::information(this, tr("在文件夹中查找"), tr("未找到匹配项。"));
        return;
    }
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("查找结果 · %1 项").arg(matches.size()));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(640, 460);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    auto *list = new QListWidget(dialog);
    for (const QString &m : matches) {
        auto *item = new QListWidgetItem(QDir(folder).relativeFilePath(m));
        item->setData(Qt::UserRole, m);
        item->setToolTip(QDir::toNativeSeparators(m));
        list->addItem(item);
    }
    layout->addWidget(list, 1);
    connect(list, &QListWidget::itemDoubleClicked, this, [this, dialog](QListWidgetItem *item) {
        const QString file = item->data(Qt::UserRole).toString();
        dialog->accept();
        loadFile(file);
    });
    auto *hint = new QLabel(tr("双击打开文件。"), dialog);
    hint->setObjectName("toolchainRowDescription");
    layout->addWidget(hint);
    dialog->open();
}
void Notepad::addToChat(const QString &path) {
    const QFileInfo info(path);
    QString context;
    if (info.isFile() && info.size() <= 512 * 1024) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            context = QString::fromUtf8(f.readAll());
    }
    const QString provider = QSettings(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat)
        .value("agents/default", "deepseek").toString();
    ChatGptPage *page = openAgent(provider);
    if (!page) return;
    QString text;
    if (!context.isEmpty())
        text = tr("请基于以下文件内容回答：\n\n文件：%1\n```\n%2\n```\n\n").arg(QDir::toNativeSeparators(path), context);
    else
        text = tr("我想了解这个路径：%1\n").arg(QDir::toNativeSeparators(path));
    page->setComposerText(text);
}
void Notepad::runTestsFor(const QString &path) {
    if (devTools->busy()) return;
    const QFileInfo info(path);
    const QString source = info.isFile() ? path : QString();
    if (source.isEmpty()) {
        QMessageBox::information(this, tr("运行测试"), tr("请右键点击具体的测试源文件运行。"));
        return;
    }
    // 直接运行该文件（C/C++/Java/Python 均可），复用 buildAndRun 的入口。
    if (editor && samePath(currentFilePath(), source)) {
        buildAndRun();
        return;
    }
    loadFile(source);
    buildAndRun();
}
void Notepad::createActions() {
    auto *fileMenu = new QMenu(this);
    auto *editMenu = new QMenu(this);
    auto *viewMenu = new QMenu(this);
    auto *helpMenu = new QMenu(this);
    fileMenu->setObjectName("fileMenu");
    fileButton->setMenu(fileMenu);
    editButton->setMenu(editMenu);
    viewButton->setMenu(viewMenu);
    helpButton->setMenu(helpMenu);
    auto *newAction = fileMenu->addAction(tr("新建文本文件"));
    newAction->setObjectName("actionNew");
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &Notepad::newDocument);
    auto *newFileAction = fileMenu->addAction(tr("新建文件…"));
    newFileAction->setObjectName("actionNewFile");
    connect(newFileAction, &QAction::triggered, this, [this] {
        if (projectRoot.isEmpty()) return;
        const auto path = QFileDialog::getSaveFileName(this, tr("新建文件"), projectRoot);
        if (path.isEmpty()) return;
        if (QFileInfo::exists(path)) { loadFile(path); return; }
        addDocument();
        if (!saveFile(path)) closeTab(tabs->currentIndex());
    });
    auto *newWindowAction = fileMenu->addAction(tr("新建窗口"));
    newWindowAction->setShortcut(QKeySequence("Ctrl+Shift+N"));
    auto makeWindow = [this] {
        auto *window = new Notepad;
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->show();
        return window;
    };
    connect(newWindowAction, &QAction::triggered, this, [makeWindow] { makeWindow(); });
    fileMenu->addSeparator();
    auto *openAction = fileMenu->addAction(tr("打开文件…"));
    openAction->setObjectName("actionOpen");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &Notepad::openDocument);
    auto *projectAction = fileMenu->addAction(tr("打开文件夹…"));
    projectAction->setObjectName("actionOpenProject");
    projectAction->setShortcut(QKeySequence("Ctrl+K, Ctrl+O"));
    connect(projectAction, &QAction::triggered, this, &Notepad::chooseProject);
    auto *openWorkspaceAction = fileMenu->addAction(tr("从文件打开工作区…"));
    connect(openWorkspaceAction, &QAction::triggered, this, &Notepad::openWorkspace);
    recentMenu = fileMenu->addMenu(tr("打开最近的文件"));
    recentMenu->setObjectName("recentFilesMenu");
    connect(recentMenu, &QMenu::aboutToShow, this, &Notepad::rebuildRecentMenu);
    fileMenu->addSeparator();
    auto *addFolder = fileMenu->addAction(tr("将文件夹添加到工作区…"));
    connect(addFolder, &QAction::triggered, this, [this] {
        const auto folder = QFileDialog::getExistingDirectory(this, tr("将文件夹添加到工作区"), projectRoot);
        if (folder.isEmpty()) return;
        if (workspaceFolders.isEmpty() && !projectRoot.isEmpty()) workspaceFolders.append(projectRoot);
        const QString target = normalizedPath(folder);
        if (!workspaceFolders.contains(target)) workspaceFolders.append(target);
        openProject(target); refreshWorkspaceFolders();
    });
    auto *saveWorkspaceAction = fileMenu->addAction(tr("将工作区另存为…"));
    connect(saveWorkspaceAction, &QAction::triggered, this, &Notepad::saveWorkspace);
    auto *duplicate = fileMenu->addAction(tr("复制工作区"));
    connect(duplicate, &QAction::triggered, this, [this, makeWindow] {
        auto *window = makeWindow();
        if (!projectRoot.isEmpty()) window->openProject(projectRoot);
        window->workspaceFolders = workspaceFolders;
        window->workspaceFile = workspaceFile;
        window->refreshWorkspaceFolders();
    });
    fileMenu->addSeparator();
    auto *saveAction = fileMenu->addAction(tr("保存"));
    saveAction->setObjectName("actionSave");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this] { save(); });
    auto *saveAsAction = fileMenu->addAction(tr("另存为…"));
    saveAsAction->setObjectName("actionSaveAs");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this] { saveAs(); });
    fileMenu->addSeparator();
    auto *saveAllAction = fileMenu->addAction(tr("全部保存"));
    saveAllAction->setObjectName("actionSaveAll");
    saveAllAction->setShortcut(QKeySequence("Ctrl+K, S"));
    connect(saveAllAction, &QAction::triggered, this, [this] { saveAll(); });
    auto *share = fileMenu->addMenu(tr("共享"));
    share->addAction(tr("复制当前文件路径"), this, [this] { QApplication::clipboard()->setText(currentFilePath()); });
    share->addAction(tr("复制当前文件内容"), this, [this] { if (editor) QApplication::clipboard()->setText(editor->toPlainText()); });
    fileMenu->addSeparator();
    auto *autoSaveAction = fileMenu->addAction(tr("自动保存"));
    autoSaveAction->setObjectName("actionAutoSave");
    autoSaveAction->setCheckable(true);
    auto *autoSaveTimer = new QTimer(this);
    autoSaveTimer->setInterval(1200);
    connect(autoSaveAction, &QAction::toggled, this, [autoSaveTimer](bool active) {
        QSettings config(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
        config.setValue("files/autoSave", active);
        if (active) autoSaveTimer->start(); else autoSaveTimer->stop();
    });
    connect(autoSaveTimer, &QTimer::timeout, this, [this, autoSaveTimer, autoSaveAction] {
        autoSaveTimer->stop();
        QWidget *previous = tabs->currentWidget();
        bool success = true;
        for (int index = 0; index < tabs->count(); ++index) {
            auto *page = qobject_cast<QPlainTextEdit *>(tabs->widget(index));
            if (!page || !page->document()->isModified() || page->property("filePath").toString().isEmpty()) continue;
            tabs->setCurrentIndex(index);
            if (!saveFile(page->property("filePath").toString())) { success = false; break; }
        }
        if (previous) tabs->setCurrentWidget(previous);
        if (success && autoSaveAction->isChecked()) autoSaveTimer->start();
        else autoSaveAction->setChecked(false);
    });
    QSettings preferences(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
    autoSaveAction->setChecked(preferences.value("files/autoSave", false).toBool());
    auto *preferencesMenu = fileMenu->addMenu(tr("首选项"));
    preferencesMenu->setObjectName("preferencesMenu");
    fileMenu->addSeparator();
    auto *revert = fileMenu->addAction(tr("还原文件"));
    revert->setObjectName("actionRevert");
    connect(revert, &QAction::triggered, this, &Notepad::revertDocument);
    auto *printAction = fileMenu->addAction(tr("打印…"));
    printAction->setObjectName("actionPrint");
    printAction->setShortcut(QKeySequence::Print);
    connect(printAction, &QAction::triggered, this, &Notepad::printDocument);
    fileMenu->addSeparator();
    auto *closeAction = fileMenu->addAction(tr("关闭编辑器"));
    closeAction->setObjectName("actionClose");
    closeAction->setShortcuts({QKeySequence::Close, QKeySequence("Ctrl+F4")});
    connect(closeAction, &QAction::triggered, this,
            [this] { closeTab(tabs->currentIndex()); });
    auto *closeFolder = fileMenu->addAction(tr("关闭文件夹"));
    closeFolder->setObjectName("actionCloseProject");
    closeFolder->setShortcut(QKeySequence("Ctrl+K, F"));
    connect(closeFolder, &QAction::triggered, this, &Notepad::closeProject);
    auto *closeWindowAction = fileMenu->addAction(tr("关闭窗口"));
    closeWindowAction->setShortcut(QKeySequence("Alt+F4"));
    connect(closeWindowAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction(tr("退出"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::closeAllWindows);

    auto *undoAction = editMenu->addAction(tr("撤销"));
    undoAction->setObjectName("actionUndo");
    undoAction->setShortcuts(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, this, [this] { editor->undo(); });
    auto *redoAction = editMenu->addAction(tr("重做"));
    redoAction->setObjectName("actionRedo");
    redoAction->setShortcuts(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, this, [this] { editor->redo(); });
    editMenu->addSeparator();
    auto *cutAction = editMenu->addAction(tr("剪切"));
    cutAction->setObjectName("actionCut");
    cutAction->setShortcut(QKeySequence::Cut);
    connect(cutAction, &QAction::triggered, this, [this] { editor->cut(); });
    auto *copyAction = editMenu->addAction(tr("复制"));
    copyAction->setObjectName("actionCopy");
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, [this] { editor->copy(); });
    auto *pasteAction = editMenu->addAction(tr("粘贴"));
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this,
            [this] { editor->paste(); });
    auto *selectAllAction = editMenu->addAction(tr("全选"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, this,
            [this] { editor->selectAll(); });

    wrapAction = viewMenu->addAction(tr("自动换行"));
    wrapAction->setObjectName("actionWrap");
    wrapAction->setCheckable(true);
    wrapAction->setChecked(false);
    connect(wrapAction, &QAction::toggled, this, [this](bool enabled) {
        if (editor) {
            editor->setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth
                                            : QPlainTextEdit::NoWrap);
        }
    });
    auto *fontAction = viewMenu->addAction(tr("字体…"));
    connect(fontAction, &QAction::triggered, this, &Notepad::selectFont);
    auto *zoomIn = viewMenu->addAction(tr("放大显示"));
    zoomIn->setObjectName("actionZoomIn");
    zoomIn->setShortcuts({QKeySequence("Ctrl++"), QKeySequence("Ctrl+=")});
    connect(zoomIn, &QAction::triggered, this, [this] { adjustZoom(1); });
    auto *zoomOut = viewMenu->addAction(tr("缩小显示"));
    zoomOut->setObjectName("actionZoomOut");
    zoomOut->setShortcut(QKeySequence("Ctrl+-"));
    connect(zoomOut, &QAction::triggered, this, [this] { adjustZoom(-1); });
    auto *sidebarAction = viewMenu->addAction(tr("显示 / 隐藏侧栏"));
    sidebarAction->setShortcut(QKeySequence("Ctrl+\\"));
    connect(sidebarAction, &QAction::triggered, this, [this] {
        showSidebarPage(sidebarPages->currentIndex(), sidebar->isHidden());
    });
    auto *runAction = viewMenu->addAction(tr("编译并运行"));
    runAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(runAction, &QAction::triggered, this, [this] { buildAndRun(); });
    auto *terminalAction = viewMenu->addAction(tr("终端"));
    terminalAction->setObjectName("actionTerminal");
    terminalAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft));
    connect(terminalAction, &QAction::triggered, this, [this] { devTools->showTerminal(projectRoot); });
    auto *debugAction = viewMenu->addAction(tr("调试当前文件"));
    debugAction->setShortcut(QKeySequence("Shift+F5"));
    connect(debugAction, &QAction::triggered, this, [this] { buildAndRun(true); });
    auto *storageAction = viewMenu->addAction(tr("存储与项目…"));
    connect(storageAction, &QAction::triggered, this, [this] { devTools->showStorage(); });
    auto *themeMenu = viewMenu->addMenu(tr("颜色主题"));
    themeMenu->setObjectName("themeMenu");
    auto *themeGroup = new QActionGroup(this);
    const QList<QPair<QString, QString>> themes{
        {"light", tr("亮色 · 清晰")}, {"tokyo-night", "Tokyo Night"}, {"github-dark-default", "GitHub Dark Default"}};
    for (const auto &theme : themes) {
        auto *action = themeMenu->addAction(theme.second);
        action->setObjectName(theme.first == "light" ? "actionLightTheme" :
            theme.first == "tokyo-night" ? "actionDarkTheme" : "actionGitHubDarkTheme");
        action->setCheckable(true);
        themeGroup->addAction(action);
        action->setChecked(theme.first == themeName);
        connect(action, &QAction::triggered, this, [this, name = theme.first] { setColorTheme(name); });
    }
    auto *agentMenu = preferencesMenu->addMenu(tr("默认 Agent"));
    agentMenu->setObjectName("defaultAgentMenu");
    auto *agentGroup = new QActionGroup(this);
    const QString selectedAgent = QSettings(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat).value("agents/default").toString();
    for (const auto &provider : {QString("chatgpt"), QString("deepseek")}) {
        auto *action = agentMenu->addAction(provider == "chatgpt" ? "ChatGPT" : "DeepSeek");
        action->setObjectName(provider == "chatgpt" ? "actionDefaultChatGPT" : "actionDefaultDeepSeek");
        action->setCheckable(true); action->setChecked(selectedAgent == provider);
        agentGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, provider] {
            QSettings config(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
            config.setValue("agents/default", provider);
            if (auto *chooser = findChild<QWidget *>("defaultAgentChooser")) {
                tabs->removeTab(tabs->indexOf(chooser)); chooser->deleteLater();
            }
            openAgent(provider);
        });
    }
    preferencesMenu->addMenu(themeMenu);
    preferencesMenu->addAction(fontAction);
    preferencesMenu->addAction(storageAction);
    auto *nextTab = viewMenu->addAction(tr("下一个文档"));
    nextTab->setShortcut(QKeySequence("Ctrl+Tab"));
    connect(nextTab, &QAction::triggered, this, [this] {
        tabs->setCurrentIndex((tabs->currentIndex() + 1) % tabs->count());
    });
    auto *previousTab = viewMenu->addAction(tr("上一个文档"));
    previousTab->setShortcut(QKeySequence("Ctrl+Shift+Tab"));
    connect(previousTab, &QAction::triggered, this, [this] {
        tabs->setCurrentIndex((tabs->currentIndex() + tabs->count() - 1) %
                              tabs->count());
    });
    auto *aboutAction = helpMenu->addAction(tr("关于 Basis Code"));
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("关于 Basis Code"),
                           tr("使用 C++ 和 Qt Widgets 构建的代码编辑器。\n文件以 UTF-8 "
                              "编码保存。字体设置只影响显示和打印。"));
    });
    auto *aboutQtAction = helpMenu->addAction(tr("关于 Qt"));
    connect(aboutQtAction, &QAction::triggered, this,
            [this] { QMessageBox::aboutQt(this); });
    documentActions = {saveAction, saveAsAction, printAction, pasteAction,
                       selectAllAction, wrapAction, fontAction};
    tabActions = {closeAction, nextTab, previousTab};
    for (auto *menu : {fileMenu, editMenu, viewMenu, helpMenu}) {
        addActions(menu->actions());
    }
}

QPlainTextEdit *Notepad::addDocument() {
    auto *page = new CodeEditor(tabs);
    page->setObjectName("editor");
    page->setColorTheme(themeName);

    page->setFrameShape(QFrame::NoFrame);
    page->document()->setDocumentMargin(15);
    page->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (const auto &family : {"Cascadia Code", "Cascadia Mono", "Consolas"}) {
        if (QFontDatabase::families().contains(QString::fromLatin1(family))) {
            font.setFamily(QString::fromLatin1(family));
            break;
        }
    }
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(12 + zoomSteps);
    page->setFont(font);
    page->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(' ') * 4);
    page->setProperty("documentName", tr("未命名 %1").arg(++documentNumber));
    page->setProperty("filePath", QString());
    page->document()->setModified(false);
    auto changed = [this] {
        syncDocuments();
        updateActions();
        updateStatus();
    };
    connect(page->document(), &QTextDocument::modificationChanged, this,
            changed);
    connect(page, &QPlainTextEdit::textChanged, this, changed);
    connect(page, &QPlainTextEdit::cursorPositionChanged, this, [this, page] {
        if (page == editor)
            updateStatus();
    });
    connect(page, &QPlainTextEdit::undoAvailable, this,
            [this] { updateActions(); });
    connect(page, &QPlainTextEdit::redoAvailable, this,
            [this] { updateActions(); });
    connect(page, &QPlainTextEdit::copyAvailable, this,
            [this] { updateActions(); });
    int index = tabs->addTab(page, page->property("documentName").toString());
    tabs->setCurrentIndex(index);
    activateDocument(index);
    return page;
}

QString Notepad::currentFilePath() const {
    return editor ? editor->property("filePath").toString() : QString();
}

void Notepad::activateDocument(int index) {
    editor = qobject_cast<QPlainTextEdit *>(tabs->widget(index));
    tabs->setVisible(index >= 0);
    for (auto *action : tabActions)
        action->setEnabled(index >= 0);
    for (auto *action : documentActions)
        action->setEnabled(editor != nullptr);
    if (!editor) {
        setWindowModified(false);
        setWindowFilePath({});
        setWindowTitle(index >= 0 ? tabs->tabText(index) + " - Basis Code"
                                  : QStringLiteral("Basis Code"));
        wrapAction->setChecked(false);
        pathLabel->clear();
        pathLabel->setToolTip({});
        positionLabel->clear();
        updateActions();
        return;
    }
    selectFileInTree();
    const QSignalBlocker blocker(wrapAction);
    wrapAction->setChecked(editor->lineWrapMode() != QPlainTextEdit::NoWrap);
    syncDocuments();
    updateActions();
    updateStatus();
    editor->setFocus();
}

void Notepad::syncDocuments() {
    for (int i = 0; i < tabs->count(); ++i) {
        auto *page = qobject_cast<QPlainTextEdit *>(tabs->widget(i));
        if (!page)
            continue;
        const QString name = page->property("documentName").toString();
        const QString path = page->property("filePath").toString();
        tabs->setTabText(i,
                         name + (page->document()->isModified() ? "  •" : ""));
        tabs->setTabToolTip(i, path.isEmpty() ? tr("尚未保存") : path);
    }
    if (editor) {
        setWindowModified(editor->document()->isModified());
        setWindowFilePath(currentFilePath());
        setWindowTitle(editor->property("documentName").toString() + "[*] - " +
                       QStringLiteral("Basis Code"));
    }
}
void Notepad::updateActions() {
    const bool hasProject = !projectRoot.isEmpty();
    findChild<QAction *>("actionNew")->setEnabled(hasProject);
    findChild<QAction *>("actionNewFile")->setEnabled(hasProject);
    findChild<QAction *>("actionCloseProject")->setEnabled(hasProject);
    findChild<QAction *>("actionRevert")->setEnabled(editor && !currentFilePath().isEmpty());
    bool dirty = false;
    for (int index = 0; index < tabs->count(); ++index)
        if (auto *page = qobject_cast<QPlainTextEdit *>(tabs->widget(index))) dirty |= page->document()->isModified();
    findChild<QAction *>("actionSaveAll")->setEnabled(dirty);
    findChild<QAction *>("actionUndo")
        ->setEnabled(editor && editor->document()->isUndoAvailable());
    findChild<QAction *>("actionRedo")
        ->setEnabled(editor && editor->document()->isRedoAvailable());
    findChild<QAction *>("actionCut")
        ->setEnabled(editor && editor->textCursor().hasSelection());
    findChild<QAction *>("actionCopy")
        ->setEnabled(editor && editor->textCursor().hasSelection());
}

bool Notepad::maybeSave() {
    if (!editor || !editor->document()->isModified())
        return true;
    QMessageBox box(
        QMessageBox::Warning, tr("保存修改"),
        tr("“%1”有尚未保存的修改，是否保存？")
            .arg(editor->property("documentName").toString()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.button(QMessageBox::Save)->setText(tr("保存"));
    box.button(QMessageBox::Discard)->setText(tr("不保存"));
    box.button(QMessageBox::Cancel)->setText(tr("取消"));
    box.setDefaultButton(QMessageBox::Save);
    box.setEscapeButton(QMessageBox::Cancel);
    switch (box.exec()) {
    case QMessageBox::Save:
        return save();
    case QMessageBox::Discard:
        return true;
    default:
        return false;
    }
}

void Notepad::newDocument() {
    if (!projectRoot.isEmpty())
        addDocument();
}

void Notepad::closeTab(int index) {
    if (index < 0 || index >= tabs->count())
        return;
    tabs->setCurrentIndex(index);
    if (!maybeSave())
        return;
    QWidget *page = tabs->widget(index);
    tabs->removeTab(index);
    page->deleteLater();
    syncDocuments();
    selectFileInTree();
}

void Notepad::openDocument() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开文件"), currentFilePath(),
        tr("源代码和文本 (*.cpp *.h *.hpp *.c *.java *.py *.js *.ts *.json *.html *.css *.md "
           "*.txt);;所有文件 (*)"));
    if (!path.isEmpty())
        loadFile(path);
}

bool Notepad::loadFile(const QString &path) {
    const QString target = normalizedPath(path);
    for (int i = 0; i < tabs->count(); ++i) {
        const QString openedPath =
            tabs->widget(i)->property("filePath").toString();
        if (!openedPath.isEmpty() && samePath(openedPath, target)) {
            tabs->setCurrentIndex(i);
            return true;
        }
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("打开失败"), file.errorString());
        return false;
    }
    const QByteArray prefix = file.peek(8192);
    const bool utf16Bom = prefix.startsWith(QByteArray::fromHex("fffe")) ||
                          prefix.startsWith(QByteArray::fromHex("feff"));
    if (prefix.contains('\0') && !utf16Bom) {
        QMessageBox::warning(
            this, tr("无法打开"),
            tr("该文件包含二进制数据，无法作为代码或文本打开。"));
        return false;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    const QString text = stream.readAll();
    if (stream.status() != QTextStream::Ok ||
        file.error() != QFileDevice::NoError) {
        QMessageBox::warning(this, tr("读取失败"), tr("无法完整读取文件。"));
        return false;
    }
    if (!editor || !currentFilePath().isEmpty() ||
        editor->document()->isModified() || !editor->toPlainText().isEmpty()) {
        addDocument();
    }
    editor->setPlainText(text);
    setCurrentFile(target);
    return true;
}

bool Notepad::saveFile(const QString &path) {
    if (!editor)
        return false;
    const QString target = normalizedPath(path);
    for (int i = 0; i < tabs->count(); ++i) {
        QWidget *other = tabs->widget(i);
        const QString otherPath = other->property("filePath").toString();
        if (other != editor && !otherPath.isEmpty() &&
            samePath(otherPath, target)) {
            QMessageBox::warning(
                this, tr("保存失败"),
                tr("该文件已在另一个选项卡打开，请先关闭对应选项卡。"));
            return false;
        }
    }
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("保存失败"), file.errorString());
        return false;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << editor->toPlainText();
    stream.flush();
    if (stream.status() != QTextStream::Ok) {
        file.cancelWriting();
        QMessageBox::warning(this, tr("保存失败"),
                             tr("文件未完整写入，请检查磁盘空间和写入权限。"));
        return false;
    }
    if (!file.commit()) {
        QMessageBox::warning(this, tr("保存失败"), file.errorString());
        return false;
    }
    setCurrentFile(target);
    return true;
}

bool Notepad::save() {
    return currentFilePath().isEmpty() ? saveAs() : saveFile(currentFilePath());
}

bool Notepad::saveAs() {
    QFileDialog dialog(this, tr("另存为"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(tr("源代码和文本 (*.cpp *.h *.hpp *.c *.py *.js *.ts "
                            "*.json *.md *.txt);;所有文件 (*)"));
    dialog.setDefaultSuffix("txt");
    if (!projectRoot.isEmpty())
        dialog.setDirectory(projectRoot);
    if (!currentFilePath().isEmpty())
        dialog.selectFile(currentFilePath());
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return false;
    return saveFile(dialog.selectedFiles().first());
}

void Notepad::setCurrentFile(const QString &path) {
    static_cast<CodeEditor *>(editor)->setFilePath(path);
    editor->setProperty("filePath", path);
    if (!path.isEmpty())
        editor->setProperty("documentName", QFileInfo(path).fileName());
    editor->document()->setModified(false);
    if (!path.isEmpty()) {
        QSettings config(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
        auto recent = config.value("files/recent").toStringList();
        recent.removeAll(path); recent.prepend(path);
        while (recent.size() > 20) recent.removeLast();
        config.setValue("files/recent", recent);
    }
    syncDocuments();
    selectFileInTree();
    updateStatus();
}

void Notepad::updateStatus() {
    if (!editor)
        return;
    QString displayedPath = currentFilePath();
    if (!projectRoot.isEmpty() && !displayedPath.isEmpty()) {
        const QString relative =
            QDir(projectRoot).relativeFilePath(displayedPath);
        if (!relative.startsWith("../") && relative != ".." &&
            !QDir::isAbsolutePath(relative))
            displayedPath = relative;
    }
    pathLabel->setText(displayedPath.isEmpty()
                           ? tr("尚未保存")
                           : QDir::toNativeSeparators(displayedPath));
    pathLabel->setToolTip(currentFilePath());
    const QTextCursor cursor = editor->textCursor();
    positionLabel->setText(
        tr("第 %1 行，第 %2 列   ·   %3 个字符   ·   Tab: 4   ·   UTF-8")
            .arg(cursor.blockNumber() + 1)
            .arg(cursor.positionInBlock() + 1)
            .arg(qMax(0, editor->document()->characterCount() - 1)));
}

void Notepad::selectFont() {
    bool accepted = false;
    const QFont font =
        QFontDialog::getFont(&accepted, editor->font(), this, tr("选择字体"));
    if (accepted) {
        editor->setFont(font);
        editor->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(' ') *
                                   4);
    }
}

void Notepad::printDocument() {
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    dialog.setWindowTitle(tr("打印文档"));
    dialog.setOption(QAbstractPrintDialog::PrintSelection,
                     editor->textCursor().hasSelection());
    if (dialog.exec() == QDialog::Accepted)
        editor->print(&printer);
}

void Notepad::closeEvent(QCloseEvent *event) {
    const int previous = tabs->currentIndex();
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentIndex(i);
        if (!maybeSave()) {
            event->ignore();
            return;
        }
    }
    tabs->setCurrentIndex(previous);
    event->accept();
}

bool Notepad::nativeEvent(const QByteArray &eventType, void *message,
                          qintptr *result) {
#ifdef Q_OS_WIN
    auto *msg = static_cast<MSG *>(message);
    if (msg->message == WM_NCHITTEST && !isMaximized() && !isFullScreen()) {
        RECT frame;
        if (GetWindowRect(msg->hwnd, &frame)) {
            const int x = GET_X_LPARAM(msg->lParam);
            const int y = GET_Y_LPARAM(msg->lParam);
            const int border = qRound(6 * devicePixelRatioF());
            const bool left = x >= frame.left && x < frame.left + border;
            const bool right = x < frame.right && x >= frame.right - border;
            const bool top = y >= frame.top && y < frame.top + border;
            const bool bottom = y < frame.bottom && y >= frame.bottom - border;
            int hit = HTCLIENT;
            if (top && left)
                hit = HTTOPLEFT;
            else if (top && right)
                hit = HTTOPRIGHT;
            else if (bottom && left)
                hit = HTBOTTOMLEFT;
            else if (bottom && right)
                hit = HTBOTTOMRIGHT;
            else if (left)
                hit = HTLEFT;
            else if (right)
                hit = HTRIGHT;
            else if (top)
                hit = HTTOP;
            else if (bottom)
                hit = HTBOTTOM;
            if (hit != HTCLIENT) {
                *result = hit;
                return true;
            }
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void Notepad::buildAndRun(bool debug) {
    showSidebarPage(2, true);
    if (!editor) {
        QMessageBox::information(this, tr("编译并运行"), tr("请先切换到要运行的 C/C++、Java 或 Python 源文件标签页。"));
        return;
    }
    if (devTools->busy()) return;
    if (currentFilePath().isEmpty() || editor->document()->isModified()) {
        if (!save()) return;
    }
    const QString suffix = QFileInfo(currentFilePath()).suffix().toLower();
    const QString language = suffix == "java" ? "Java" : (suffix == "py" || suffix == "pyw") ? "Python" : "C/C++";
    if (!DevTools::runnerInstalled(language)) {
        openToolkit(language, QString());
        statusBar()->showMessage(tr("请安装运行支持，或在终端中使用命令运行。"), 6000);
        return;
    }
    devTools->buildAndRun(currentFilePath(), projectRoot, debug);
}


void Notepad::adjustZoom(int delta) {
    const int next = qBound(-3, zoomSteps + delta, 10);
    const int change = next - zoomSteps;
    if (!change)
        return;
    zoomSteps = next;
    QFont uiFont("Microsoft YaHei UI", 10 + zoomSteps);
    QApplication::setFont(uiFont);
    setFont(uiFont);
    applyTheme();
    for (int i = 0; i < tabs->count(); ++i) {
        auto *page = qobject_cast<QPlainTextEdit *>(tabs->widget(i));
        if (!page)
            continue;
        QFont font = page->font();
        font.setPointSizeF(qMax(6.0, font.pointSizeF() + change));
        page->setFont(font);
        page->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(' ') * 4);
    }
}

void Notepad::openChatGpt() { openAgent("chatgpt"); }
ChatGptPage *Notepad::openAgent(const QString &provider) {
    const QString name = provider == "deepseek" ? "DeepSeek" : "ChatGPT";
    for (int index = 0; index < tabs->count(); ++index) {
        if (tabs->widget(index)->property("toolkitName").toString() == name) {
            tabs->setCurrentIndex(index);
            return static_cast<ChatGptPage *>(tabs->widget(index));
        }
    }
    auto *page = new ChatGptPage(tabs, provider);
    tabs->setCurrentIndex(tabs->addTab(page, pluginIcon(provider), name));
    return page;
}
void Notepad::chooseDefaultAgent() {
    auto *page = new QWidget(tabs);
    page->setObjectName("defaultAgentChooser");
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->addStretch();
    auto *heading = new QLabel(tr("选择默认 Agent"), page);
    heading->setObjectName("toolkitTitle");
    heading->setAlignment(Qt::AlignCenter);
    layout->addWidget(heading);
    for (const auto &provider : {QString("chatgpt"), QString("deepseek")}) {
        const QString name = provider == "chatgpt" ? "ChatGPT" : "DeepSeek";
        auto *button = new QPushButton(pluginIcon(provider),
            provider == "chatgpt" ? tr("ChatGPT · 使用账号登录") : tr("DeepSeek · 配置 API Key"), page);
        button->setObjectName(provider == "chatgpt" ? "chooseChatGPT" : "chooseDeepSeek");
        button->setIconSize(QSize(36, 36));
        button->setMinimumHeight(60);
        button->setMaximumWidth(420);
        layout->addWidget(button, 0, Qt::AlignHCenter);
        connect(button, &QPushButton::clicked, this, [this, provider, page] {
            QSettings config(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
            config.setValue("agents/default", provider);
            if (auto *action = findChild<QAction *>(provider == "chatgpt" ? "actionDefaultChatGPT" : "actionDefaultDeepSeek")) action->setChecked(true);
            const int index = tabs->indexOf(page);
            if (index >= 0) tabs->removeTab(index);
            page->deleteLater();
            openAgent(provider);
        });
    }
    layout->addStretch();
    tabs->setCurrentIndex(tabs->addTab(page, tr("选择默认 Agent")));
}

void Notepad::refreshRecentProjects() {
    auto *list = findChild<QListWidget *>("recentProjects");
    list->clear();
    QSettings settings(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
    const auto projects = settings.value("projects/recent").toStringList();
    for (const auto &path : projects) {
        const QFileInfo info(path);
        auto *item = new QListWidgetItem(list);
        item->setText(projectDisplayName(path));
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        const bool missing = !info.isDir();
        if (missing) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setToolTip(tr("此文件夹已移动或不存在：\n%1").arg(path));
        }

        // 每行右侧嵌入一个「⋯」按钮，点击弹出操作菜单
        auto *row = new QWidget(list);
        row->setObjectName("recentProjectRow");
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 4, 4, 4);
        rowLayout->setSpacing(6);

        auto *iconLabel = new QLabel(row);
        iconLabel->setObjectName("recentProjectIcon");
        iconLabel->setPixmap(style()->standardIcon(QStyle::SP_DirIcon).pixmap(18, 18));
        iconLabel->setFixedSize(18, 18);
        rowLayout->addWidget(iconLabel, 0, Qt::AlignTop);

        auto *label = new QLabel(projectDisplayName(path), row);
        label->setObjectName("recentProjectName");
        label->setTextInteractionFlags(Qt::NoTextInteraction);
        label->setToolTip(path);
        label->setMinimumWidth(0);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        QFont pathFont = label->font();
        pathFont.setPointSizeF(pathFont.pointSizeF() - 1.5);
        auto *subLabel = new QLabel(QDir::toNativeSeparators(path), row);
        subLabel->setObjectName("recentProjectPath");
        subLabel->setFont(pathFont);
        subLabel->setToolTip(path);
        subLabel->setMinimumWidth(0);
        subLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto *textBox = new QVBoxLayout();
        textBox->setContentsMargins(0, 0, 0, 0);
        textBox->setSpacing(1);
        textBox->addWidget(label);
        textBox->addWidget(subLabel);
        rowLayout->addLayout(textBox, 1);

        auto *more = new QToolButton(row);
        more->setObjectName("recentProjectMore");
        more->setText(QString::fromUtf8("⋮"));
        more->setCursor(Qt::PointingHandCursor);
        more->setAutoRaise(true);
        more->setToolTip(tr("更多操作"));
        more->setFixedSize(26, 26);
        if (missing) more->setEnabled(false);
        connect(more, &QToolButton::clicked, this, [this, more, path] {
            QMenu menu(more);
            menu.addAction(tr("打开项目"), this, [this, path] { openProject(path); });
            menu.addAction(tr("修改项目显示名称…"), this, [this, path] { renameProject(path, false); });
            menu.addAction(tr("修改文件夹名称…"), this, [this, path] { renameProject(path, true); });
            menu.addSeparator();
            menu.addAction(tr("从最近项目移除"), this, [this, path] { removeRecentProject(path); });
            menu.exec(more->mapToGlobal(QPoint(0, more->height())));
        });
        rowLayout->addWidget(more);

        item->setSizeHint(QSize(0, 44));
        list->setItemWidget(item, row);
    }
    list->setVisible(!projects.isEmpty());
}

void Notepad::removeRecentProject(const QString &path) {
    QSettings settings(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
    auto projects = settings.value("projects/recent").toStringList();
    projects.removeAll(path);
    settings.setValue("projects/recent", projects);
    refreshRecentProjects();
}

QString Notepad::projectDisplayName(const QString &path) const {
    const QString key = QString::fromLatin1(QCryptographicHash::hash(normalizedPath(path).toUtf8(), QCryptographicHash::Sha256).toHex());
    QSettings settings(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
    return settings.value("projects/names/" + key, QFileInfo(path).fileName()).toString();
}
void Notepad::showRecentProjects() {
    projectClickTimer->stop(); pendingProject.clear();
    refreshRecentProjects();
    projectTree->hide(); projectLabel->hide();
    findChild<QWidget *>("projectHeader")->hide();
    findChild<QListWidget *>("workspaceFolders")->hide();
    recentProjectsPanel->show();
    showSidebarPage(0, true);
}
void Notepad::renameProject(const QString &path, bool folder) {
    if (devTools->busy()) {
        QMessageBox::information(this, tr("项目正在运行"), tr("请先停止运行，再修改项目或文件夹名称。")); return;
    }
    const QFileInfo info(path);
    bool accepted = false;
    const QString name = QInputDialog::getText(this,
        folder ? tr("修改文件夹名称") : tr("修改项目显示名称"),
        folder ? tr("文件夹名称") : tr("项目显示名称"), QLineEdit::Normal,
        folder ? info.fileName() : projectDisplayName(path), &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    QSettings settings(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
    auto keyFor = [](const QString &value) {
        return "projects/names/" + QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
    };
    const QString oldPath = normalizedPath(path);
    if (!folder) settings.setValue(keyFor(oldPath), name);
    else {
        if (!info.isDir() || name == "." || name == ".." || name.contains(QRegularExpression(R"([<>:"/\\|?*])")) ||
            name.endsWith('.') || name.endsWith(' ')) {
            QMessageBox::warning(this, tr("名称无效"), tr("请输入合法的文件夹名称。")); return;
        }
        const QString newPath = info.dir().filePath(name);
        if (QFileInfo::exists(newPath)) { QMessageBox::warning(this, tr("无法重命名"), tr("目标名称已经存在。")); return; }
        const bool current = samePath(oldPath, projectRoot);
        if (current) { projectTree->setRootIndex({}); fileModel->setRootPath(info.dir().absolutePath()); }
        if (!info.dir().rename(info.fileName(), name)) {
            if (current) { fileModel->setRootPath(oldPath); projectTree->setRootIndex(fileModel->index(oldPath)); }
            QMessageBox::warning(this, tr("重命名失败"), tr("文件夹可能正在被其他程序占用。")); return;
        }
        auto recent = settings.value("projects/recent").toStringList();
        for (auto &entry : recent) if (samePath(entry, oldPath)) entry = newPath;
        settings.setValue("projects/recent", recent);
        if (settings.contains(keyFor(oldPath))) {
            settings.setValue(keyFor(normalizedPath(newPath)), settings.value(keyFor(oldPath))); settings.remove(keyFor(oldPath));
        }
        settings.sync();
        for (int index = 0; index < tabs->count(); ++index) {
            auto *page = qobject_cast<QPlainTextEdit *>(tabs->widget(index));
            if (!page) continue;
            const QString file = page->property("filePath").toString();
            const QString relative = QDir(oldPath).relativeFilePath(file);
            if (file.isEmpty() || relative.startsWith("../") || QDir::isAbsolutePath(relative)) continue;
            const QString relocated = QDir(newPath).filePath(relative);
            page->setProperty("filePath", relocated);
            static_cast<CodeEditor *>(page)->setFilePath(relocated);
        }
        for (auto &entry : workspaceFolders) if (samePath(entry, oldPath)) entry = newPath;
        if (current) { projectRoot = newPath; openProject(newPath); }
        syncDocuments(); updateStatus();
    }
    settings.sync();
    if (!projectRoot.isEmpty()) projectLabel->setText(projectDisplayName(projectRoot));
    refreshRecentProjects();
}
void Notepad::applyTheme() {
    QString scaled = baseTheme;
    auto matches = QRegularExpression(R"((\d+)pt)").globalMatch(baseTheme);
    QList<QRegularExpressionMatch> sizes;
    while (matches.hasNext()) sizes.append(matches.next());
    for (auto it = sizes.crbegin(); it != sizes.crend(); ++it)
        scaled.replace(it->capturedStart(), it->capturedLength(), QString::number(it->captured(1).toInt() + zoomSteps) + "pt");
    setStyleSheet(scaled);
    const bool github = themeName == "github-dark-default";
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(github ? "#0d1117" : darkTheme ? "#1a1b26" : "#ffffff"));
    palette.setColor(QPalette::Base, QColor(github ? "#0d1117" : darkTheme ? "#1a1b26" : "#ffffff"));
    palette.setColor(QPalette::WindowText, QColor(github ? "#e6edf3" : darkTheme ? "#c0caf5" : "#26334d"));
    palette.setColor(QPalette::Text, QColor(github ? "#e6edf3" : darkTheme ? "#c0caf5" : "#26334d"));
    palette.setColor(QPalette::ButtonText, QColor(github ? "#e6edf3" : darkTheme ? "#c0caf5" : "#304a72"));
    palette.setColor(QPalette::Button, QColor(github ? "#21262d" : darkTheme ? "#292e42" : "#edf2fa"));
    palette.setColor(QPalette::Highlight, QColor(github ? "#1f3a5a" : darkTheme ? "#33467c" : "#dbe7fb"));
    palette.setColor(QPalette::HighlightedText, QColor(darkTheme ? "#ffffff" : "#26334d"));
    palette.setColor(QPalette::Link, QColor(github ? "#2f81f7" : darkTheme ? "#7aa2f7" : "#426fae"));
    setPalette(palette);
    for (int index = 0; index < tabs->count(); ++index)
        if (auto *page = qobject_cast<QPlainTextEdit *>(tabs->widget(index)))
            static_cast<CodeEditor *>(page)->setColorTheme(themeName);
}
void Notepad::setTheme(bool dark) {
    setColorTheme(dark ? "tokyo-night" : "light");
}
void Notepad::setColorTheme(const QString &name) {
    if (!QStringList{"light", "tokyo-night", "github-dark-default"}.contains(name)) return;
    themeName = name;
    darkTheme = name != "light";
    QFile file(name == "github-dark-default" ? ":/theme-github-dark.qss" : darkTheme ? ":/theme-dark.qss" : ":/theme.qss");
    if (file.open(QIODevice::ReadOnly)) baseTheme = QString::fromUtf8(file.readAll());
    QSettings settings(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
    settings.setValue("ui/theme", name);
    settings.setValue("ui/darkTheme", darkTheme);
    applyTheme();
}

bool Notepad::saveAll() {
    QWidget *previous = tabs->currentWidget();
    for (int index = 0; index < tabs->count(); ++index) {
        auto *page = qobject_cast<QPlainTextEdit *>(tabs->widget(index));
        if (page && page->document()->isModified()) {
            tabs->setCurrentIndex(index);
            if (!save()) { if (previous) tabs->setCurrentWidget(previous); return false; }
        }
    }
    if (previous) tabs->setCurrentWidget(previous);
    return true;
}
void Notepad::closeProject() {
    QWidget *previous = tabs->currentWidget();
    for (int index = 0; index < tabs->count(); ++index) {
        if (!qobject_cast<QPlainTextEdit *>(tabs->widget(index))) continue;
        tabs->setCurrentIndex(index);
        if (!maybeSave()) { if (previous) tabs->setCurrentWidget(previous); return; }
    }
    for (int index = tabs->count() - 1; index >= 0; --index) {
        if (qobject_cast<QPlainTextEdit *>(tabs->widget(index))) {
            auto *page = tabs->widget(index); tabs->removeTab(index); page->deleteLater();
        }
    }
    projectRoot.clear(); workspaceFolders.clear(); workspaceFile.clear();
    fileModel->setRootPath(QString());
    updateActions(); showRecentProjects();
}
void Notepad::rebuildRecentMenu() {
    if (!recentMenu) return;
    recentMenu->clear();
    QSettings settings(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
    for (const auto &path : settings.value("files/recent").toStringList())
        recentMenu->addAction(path, this, [this, path] { loadFile(path); });
    recentMenu->addSeparator();
    for (const auto &path : settings.value("projects/recent").toStringList())
        recentMenu->addAction(projectDisplayName(path), this, [this, path] { openProject(path); });
    recentMenu->addSeparator();
    recentMenu->addAction(tr("清除最近记录"), this, [this] {
        QSettings config(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
        config.remove("files/recent"); config.remove("projects/recent"); refreshRecentProjects();
    });
}
void Notepad::openWorkspace() {
    const auto path = QFileDialog::getOpenFileName(this, tr("从文件打开工作区"), {}, tr("工作区 (*.code-workspace)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonParseError error;
    const auto data = QJsonDocument::fromJson(file.readAll(), &error).object();
    QStringList folders;
    for (const auto &entry : data["folders"].toArray()) {
        const auto value = entry.toObject()["path"].toString();
        const auto folder = QDir(QFileInfo(path).absolutePath()).absoluteFilePath(value);
        if (!value.isEmpty() && QFileInfo(folder).isDir()) folders.append(normalizedPath(folder));
    }
    if (error.error != QJsonParseError::NoError || folders.isEmpty()) {
        QMessageBox::warning(this, tr("无法打开工作区"), tr("工作区文件无效或其中的文件夹不可用。")); return;
    }
    if (openProject(folders.first())) { workspaceFolders = folders; workspaceFile = path; refreshWorkspaceFolders(); }
}
void Notepad::saveWorkspace() {
    if (projectRoot.isEmpty()) return;
    const auto path = QFileDialog::getSaveFileName(this, tr("将工作区另存为"), workspaceFile, tr("工作区 (*.code-workspace)"));
    if (path.isEmpty()) return;
    if (workspaceFolders.isEmpty()) workspaceFolders.append(projectRoot);
    QJsonArray folders;
    for (const auto &folder : workspaceFolders)
        folders.append(QJsonObject{{"path", QDir(QFileInfo(path).absolutePath()).relativeFilePath(folder)}});
    const auto bytes = QJsonDocument(QJsonObject{{"folders", folders}}).toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        QMessageBox::warning(this, tr("保存失败"), file.errorString()); return;
    }
    workspaceFile = path;
}

void Notepad::refreshWorkspaceFolders() {
    auto *list = findChild<QListWidget *>("workspaceFolders");
    list->clear();
    for (const auto &folder : workspaceFolders) {
        auto *item = new QListWidgetItem(projectDisplayName(folder), list);
        item->setData(Qt::UserRole, folder);
        item->setToolTip(folder);
    }
    list->setVisible(workspaceFolders.size() > 1);
}
void Notepad::revertDocument() {
    if (!editor || currentFilePath().isEmpty()) return;
    if (editor->document()->isModified() && QMessageBox::question(this, tr("还原文件"),
        tr("放弃当前修改并重新读取磁盘文件？"), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
    QFile file(currentFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("读取失败"), file.errorString()); return;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    const QString text = stream.readAll();
    if (stream.status() != QTextStream::Ok) { QMessageBox::warning(this, tr("读取失败"), tr("文件内容读取失败。")); return; }
    editor->setPlainText(text);
    editor->document()->setModified(false);
    syncDocuments(); updateActions();
}

bool Notepad::eventFilter(QObject *watched, QEvent *event) {
    if (watched == projectLabel && event->type() == QEvent::MouseButtonDblClick &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton && !projectRoot.isEmpty()) {
        renameProject(projectRoot, false);
        return true;
    }
    if (watched == projectTree && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const QModelIndex index = projectTree->currentIndex();
        if (!index.isValid()) return false;
        const QString path = fileModel->filePath(index);
        const bool isDir = fileModel->isDir(index);
        const QString folder = isDir ? path : QFileInfo(path).absolutePath();
        const bool isRoot = samePath(path, projectRoot);
        const int key = keyEvent->key();
        const auto mods = keyEvent->modifiers();

        // Delete：删除（或回收站，遵循系统）
        if (key == Qt::Key_Delete && !isRoot) {
            deleteTreeItem(path);
            return true;
        }
        // F2：重命名
        if (key == Qt::Key_F2 && (mods & Qt::ControlModifier) == 0) {
            renameTreeItem(path);
            return true;
        }
        // Enter：打开文件/展开目录
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !isDir) {
            loadFile(path);
            return true;
        }
        // Ctrl+C / Ctrl+X / Ctrl+V：复制 / 剪切 / 粘贴
        if (mods & Qt::ControlModifier) {
            if (key == Qt::Key_C && !isRoot) {
                copyTreeItem(path);
                return true;
            }
            if (key == Qt::Key_X && !isRoot) {
                cutTreeItem(path);
                return true;
            }
            if (key == Qt::Key_V) {
                pasteTreeItem(folder);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
