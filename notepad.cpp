#include "notepad.h"
#include "windowtitlebar.h"
#include <QAbstractButton>
#include <QAction>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QFrame>
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
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

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

Notepad::Notepad(QWidget *parent) : QMainWindow(parent) {
    setWindowIcon(QIcon(":/icons/basis-code.ico"));
    setWindowFlag(Qt::FramelessWindowHint);
    QFont uiFont = font();
#ifdef Q_OS_WIN
    uiFont.setFamily("Microsoft YaHei UI");
#endif
    uiFont.setPointSize(10);
    setFont(uiFont);
    resize(1180, 780);
    setMinimumSize(720, 480);
    QFile theme(":/theme.qss");
    if (theme.open(QIODevice::ReadOnly)) {
        setStyleSheet(QString::fromUtf8(theme.readAll()));
    }
    createWorkspace();
    createActions();
    addDocument();
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
    auto *folderButton =
        railButton("folder", tr("打开项目文件夹"), "openProjectButton");
    auto *newButton =
        railButton("new-file", tr("新建文件 (Ctrl+N)"), "newDocumentButton");
    auto *railSave =
        railButton("save", tr("保存当前文件 (Ctrl+S)"), "railSaveButton");
    railLayout->addStretch();
    settingsButton = railButton("settings", tr("设置"), "settingsButton");
    settingsButton->setPopupMode(QToolButton::InstantPopup);
    rootLayout->addWidget(rail);

    splitter = new QSplitter(Qt::Horizontal, root);
    splitter->setObjectName("workspaceSplitter");
    splitter->setChildrenCollapsible(false);
    rootLayout->addWidget(splitter, 1);
    sidebar = new QFrame(splitter);
    sidebar->setObjectName("sidebar");
    sidebar->setMinimumWidth(190);
    sidebar->setMaximumWidth(420);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(12, 18, 12, 12);
    sideLayout->setSpacing(12);
    projectLabel = new QLabel(sidebar);
    projectLabel->hide();
    projectLabel->setObjectName("projectName");
    projectLabel->setTextFormat(Qt::PlainText);
    sideLayout->addWidget(projectLabel);
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
    for (int column = 1; column < fileModel->columnCount(); ++column)
        projectTree->hideColumn(column);
    projectTree->hide();
    sideLayout->addWidget(projectTree, 1);
    connect(projectTree, &QTreeView::doubleClicked, this,
            [this](const QModelIndex &index) {
                if (!fileModel->isDir(index))
                    loadFile(fileModel->filePath(index));
            });
    connect(fileModel, &QFileSystemModel::directoryLoaded, this,
            [this](const QString &path) {
                if (samePath(path, projectRoot))
                    selectFileInTree();
            });
    connect(sidebarButton, &QToolButton::toggled, sidebar,
            &QWidget::setVisible);
    connect(folderButton, &QToolButton::clicked, this, &Notepad::chooseProject);
    connect(newButton, &QToolButton::clicked, this, &Notepad::newDocument);
    connect(railSave, &QToolButton::clicked, this, [this] { save(); });

    auto *mainArea = new QWidget(splitter);
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
    headerLayout->addSpacing(10);
    headerLayout->addWidget(saveButton);
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
    auto *addTabButton = new QToolButton(tabs);
    addTabButton->setText("＋");
    addTabButton->setObjectName("addTabButton");
    addTabButton->setToolTip(tr("新建文件 (Ctrl+N)"));
    tabs->setCornerWidget(addTabButton, Qt::TopRightCorner);
    connect(addTabButton, &QToolButton::clicked, this, &Notepad::newDocument);
    mainLayout->addWidget(tabs, 1);
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

bool Notepad::openProject(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir() || !info.isReadable())
        return false;
    projectRoot = normalizedPath(path);
    const QModelIndex root = fileModel->setRootPath(projectRoot);
    projectTree->setRootIndex(root);
    projectTree->show();
    projectLabel->setText(QDir(projectRoot).dirName().isEmpty()
                              ? projectRoot
                              : QDir(projectRoot).dirName());
    projectLabel->setToolTip(projectRoot);
    projectLabel->show();
    sidebarButton->setChecked(true);
    selectFileInTree();
    updateStatus();
    return true;
}

void Notepad::chooseProject() {
    const QString path = QFileDialog::getExistingDirectory(
        this, tr("打开项目文件夹"), projectRoot);
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
void Notepad::createActions() {
    auto *fileMenu = new QMenu(this);
    auto *editMenu = new QMenu(this);
    auto *viewMenu = new QMenu(this);
    auto *helpMenu = new QMenu(this);
    fileButton->setMenu(fileMenu);
    editButton->setMenu(editMenu);
    viewButton->setMenu(viewMenu);
    helpButton->setMenu(helpMenu);
    auto *newAction = fileMenu->addAction(tr("新建文档"));
    newAction->setObjectName("actionNew");
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &Notepad::newDocument);
    auto *openAction = fileMenu->addAction(tr("打开文件…"));
    openAction->setObjectName("actionOpen");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &Notepad::openDocument);
    auto *projectAction = fileMenu->addAction(tr("打开文件夹…"));
    projectAction->setObjectName("actionOpenProject");
    projectAction->setShortcut(QKeySequence("Ctrl+K"));
    connect(projectAction, &QAction::triggered, this, &Notepad::chooseProject);
    auto *saveAction = fileMenu->addAction(tr("保存"));
    saveAction->setObjectName("actionSave");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this] { save(); });
    saveButton->setDefaultAction(saveAction);
    auto *saveAsAction = fileMenu->addAction(tr("另存为…"));
    saveAsAction->setObjectName("actionSaveAs");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this] { saveAs(); });
    fileMenu->addSeparator();
    auto *printAction = fileMenu->addAction(tr("打印…"));
    printAction->setObjectName("actionPrint");
    printAction->setShortcut(QKeySequence::Print);
    connect(printAction, &QAction::triggered, this, &Notepad::printDocument);
    fileMenu->addSeparator();
    auto *closeAction = fileMenu->addAction(tr("关闭当前文档"));
    closeAction->setObjectName("actionClose");
    closeAction->setShortcut(QKeySequence::Close);
    connect(closeAction, &QAction::triggered, this,
            [this] { closeTab(tabs->currentIndex()); });
    auto *quitAction = fileMenu->addAction(tr("退出"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

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
    auto *sidebarAction = viewMenu->addAction(tr("显示 / 隐藏侧栏"));
    sidebarAction->setShortcut(QKeySequence("Ctrl+\\"));
    connect(sidebarAction, &QAction::triggered, sidebarButton,
            &QToolButton::click);
    auto *settingsMenu = new QMenu(settingsButton);
    settingsMenu->addAction(fontAction);
    settingsMenu->addAction(wrapAction);
    settingsMenu->addAction(sidebarAction);
    settingsButton->setMenu(settingsMenu);
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
    documentActions = {saveAction,  saveAsAction,    printAction, closeAction,
                       pasteAction, selectAllAction, wrapAction,  fontAction,
                       nextTab,     previousTab};
    for (auto *menu : {fileMenu, editMenu, viewMenu, helpMenu}) {
        addActions(menu->actions());
    }
}

QPlainTextEdit *Notepad::addDocument() {
    auto *page = new QPlainTextEdit(tabs);
    page->setObjectName("editor");

    page->setFrameShape(QFrame::NoFrame);
    page->document()->setDocumentMargin(15);
    page->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QStringList installed = QFontDatabase::families();
    for (const QString &family :
         {QStringLiteral("Cascadia Code"), QStringLiteral("Cascadia Mono"),
          QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono")}) {
        if (installed.contains(family, Qt::CaseInsensitive)) {
            font.setFamily(family);
            break;
        }
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSizeF(12);
    font.setKerning(false);
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
    tabs->setVisible(editor != nullptr);
    for (auto *action : documentActions)
        action->setEnabled(editor != nullptr);
    findChild<QToolButton *>("railSaveButton")->setEnabled(editor != nullptr);
    if (!editor) {
        setWindowModified(false);
        setWindowFilePath({});
        setWindowTitle(QStringLiteral("Basis Code"));
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

void Notepad::newDocument() { addDocument(); }

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
        tr("源代码和文本 (*.cpp *.h *.hpp *.c *.py *.js *.ts *.json *.md "
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
    if (!currentFilePath().isEmpty())
        dialog.selectFile(currentFilePath());
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return false;
    return saveFile(dialog.selectedFiles().first());
}

void Notepad::setCurrentFile(const QString &path) {
    editor->setProperty("filePath", path);
    if (!path.isEmpty())
        editor->setProperty("documentName", QFileInfo(path).fileName());
    editor->document()->setModified(false);
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
            .arg(editor->toPlainText().toUcs4().size()));
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
