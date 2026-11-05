#pragma once
#include <QMainWindow>

class QAction;
class QPlainTextEdit;
class QLabel;
class QCloseEvent;
class QTabWidget;
class QTreeView;
class QFileSystemModel;

class QFrame;
class QToolButton;
class QSplitter;

class Notepad : public QMainWindow {
  public:
    explicit Notepad(QWidget *parent = nullptr);
    bool loadFile(const QString &path);
    bool openProject(const QString &path);
    bool saveFile(const QString &path);
    QString currentFilePath() const;

  protected:
    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message,
                     qintptr *result) override;

  private:
    void createWorkspace();
    void createActions();
    void chooseProject();
    void selectFileInTree();
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
    QLabel *positionLabel;
    QTabWidget *tabs;
    QTreeView *projectTree;
    QFileSystemModel *fileModel;
    QLabel *projectLabel;
    QLabel *pathLabel;
    QString projectRoot;

    QFrame *sidebar;
    QToolButton *settingsButton;
    QToolButton *sidebarButton;
    QToolButton *fileButton;
    QToolButton *editButton;
    QToolButton *viewButton;
    QToolButton *helpButton;
    QToolButton *saveButton;
    QSplitter *splitter;
    QList<QAction *> documentActions;
    QAction *wrapAction;
    int documentNumber = 0;
};
