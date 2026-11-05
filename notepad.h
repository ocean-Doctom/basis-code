#pragma once
#include <QMainWindow>
#include <QPointer>

class DevTools;
class QAction;
class QPlainTextEdit;
class ChatGptPage;
class CodeEditor;
class QLabel;
class QCloseEvent;
class QTabWidget;
class QTreeView;
class QFileSystemModel;

class QMenu;
class QTimer;
class QFrame;
class QToolButton;
class QSplitter;
class QStackedWidget;

class Notepad : public QMainWindow {
  public:
    explicit Notepad(QWidget *parent = nullptr);
    bool loadFile(const QString &path);
    bool openProject(const QString &path);
    bool saveFile(const QString &path);
    QString currentFilePath() const;

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message,
                     qintptr *result) override;

  private:
    friend struct NotepadFileTestAccess;
    void relocateOpenDocuments(const QString &source, const QString &target);
    void updateWindowCorners();
    QString baseTheme;
    int zoomSteps = 0;
    void adjustZoom(int delta);
    void createWorkspace();
    void createActions();
    void createExtensionsPage();
    void openChatGpt();
    class ChatGptPage *openAgent(const QString &provider);
    void chooseDefaultAgent();
    void refreshRecentProjects();
    void showRecentProjects();
    void renameProject(const QString &path, bool folder);
    void removeRecentProject(const QString &path);
    QString projectDisplayName(const QString &path) const;
    void closeProject();
    bool saveAll();
    void setTheme(bool dark);
    void setColorTheme(const QString &name);
    QString themeName = "light";
    void applyTheme();
    void rebuildRecentMenu();
    void openWorkspace();
    void saveWorkspace();
    void refreshWorkspaceFolders();
    void revertDocument();
    bool darkTheme = false;
    QMenu *recentMenu = nullptr;
    QTimer *projectClickTimer;
    QToolButton *backToProjects;
    QString pendingProject;
    QString workspaceFile;
    QStringList workspaceFolders;

    QWidget *recentProjectsPanel;

    void openToolkit(const QString &name, const QString &summary);
    void showSidebarPage(int index, bool visible);
    void chooseProject();
    void buildAndRun(bool debug = false);
    void selectFileInTree();
    void newFileInFolder(const QString &folder);
    void newFolderInFolder(const QString &folder);
    void deleteTreeItem(const QString &path);
    void renameTreeItem(const QString &path);
    void copyTreePath(const QString &path);
    void revealInExplorer(const QString &path);
    void openTerminalAt(const QString &path);
    void cutTreeItem(const QString &path);
    void copyTreeItem(const QString &path);
    void pasteTreeItem(const QString &folder);
    void previewImage(const QString &path);
    void showTimeline(const QString &path);
    void searchInFolder(const QString &folder, bool todos = false);
    void addToChat(const QString &path);
    void runTestsFor(const QString &path);
    void createSettingsPage();
    void openSettings();
    void applyEditorFont();
    void applyWordWrap();
    void pinToPreview(const QString &path);
    void closePreview();
    QPlainTextEdit *addDocument();
    void activateDocument(int index);
    void syncDocuments();
    void updateActions();
    void closeTab(int index);
    void newDocument();
    void openDocument();
    bool save();
    bool saveAs();
    bool maybeSave();
    void setCurrentFile(const QString &path);
    void updateStatus();
    void selectFont();
    void printDocument();

    QPlainTextEdit *editor = nullptr;
    QPointer<QPlainTextEdit> lastCodeEditor;
    QLabel *positionLabel;
    QTabWidget *tabs;
    QTreeView *projectTree;
    QFileSystemModel *fileModel;
    QLabel *projectLabel;
    QLabel *pathLabel;
    QString projectRoot;

    QFrame *sidebar;
    QStackedWidget *sidebarPages;
    DevTools *devTools;
    QToolButton *runButton;
    QToolButton *extensionsButton;
    QToolButton *settingsButton;
    QToolButton *sidebarButton;
    QToolButton *fileButton;
    QToolButton *editButton;
    QToolButton *viewButton;
    QToolButton *helpButton;
    QToolButton *saveButton;
    QSplitter *splitter;
    QList<QAction *> tabActions;
    QList<QAction *> documentActions;
    QAction *wrapAction = nullptr;
    int documentNumber = 0;
    QString clipboardSource;
    bool clipboardCut = false;
    QString editorFontFamily;
    int editorFontSize = 12;
    QSplitter *editorSplit = nullptr;
    QWidget *previewPane = nullptr;
    CodeEditor *previewEditor = nullptr;
    QLabel *previewTitle = nullptr;
};
