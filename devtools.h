#pragma once
#include <QObject>
#include <QString>
#include <QPointer>
class QDialog;
class QMainWindow;
class QProcess;
class QPlainTextEdit;
class QTabWidget;
class QLineEdit;
class QWidget;

class DevTools : public QObject {
    Q_OBJECT
  public:
    explicit DevTools(QMainWindow *window);
    ~DevTools() override;
    static QString dataRoot();
    static QString projectsRoot();
    static QString compilerPath();
    static bool initialize(QString *error = nullptr);
    static bool safeArchiveEntry(QString entry);
    static QString compilerForSource(const QString &cpp, const QString &source);
    void showStorage();
    QWidget *createToolchainPage(QWidget *parent, const QString &language);
    void showTerminal(const QString &project);
    static bool runnerInstalled(const QString &language);
    static bool installRunner(const QString &language);

    void showCompilers();
    void showRuntime(const QString &language);
    static QString runtimePath(const QString &language);
    bool busy() const;
    void buildAndRun(const QString &source, const QString &project, bool debug = false);
    void stop();
  signals:
    void toolchainRequested(const QString &language);
    void projectRequested(const QString &path);
    void activityChanged(bool busy);
    void executionFinished(int code);
  private:
    void log(const QString &text);
    void runExecutable();
    QMainWindow *window;
    QProcess *process;
    QPlainTextEdit *output;
    QLineEdit *input;
    QWidget *dock;
    QTabWidget *outputTabs;
    QWidget *terminal = nullptr;
    QStringList launchArguments;
    bool debugging = false;
    QString activeLanguage;
    QString javaMainClass;
    QString executable;
    QString workingDirectory;
    QString toolBin;
    bool compiling = false;
    bool stopping = false;
};
