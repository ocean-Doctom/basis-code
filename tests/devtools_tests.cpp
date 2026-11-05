#include "devtools.h"
#include "terminalpane.h"
#include <QProcess>
#include <QApplication>
#include <QMainWindow>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QDir>
#include <QSettings>
#include <QPlainTextEdit>
#include <QEventLoop>
#include <QTimer>
#include <QLineEdit>
#include <QElapsedTimer>
#include <QThread>
#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QListWidget>
#include <iostream>
#include <stdexcept>
static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
struct TerminalPaneTestAccess {
    static void buffering(QWidget *parent, const QString &directory) {
        TerminalPane pane(parent);
        pane.start(directory);
        pane.consumeErrorText(QString(3000000, 'e'));
        check(pane.errorBuffer.size() < 65536 && pane.output->document()->characterCount() < 1000000,
              "terminal stderr without newline cannot grow its parser or display indefinitely");
        pane.consumeErrorText("\n  indented error\n__BASIS_CWD__" + directory + "\n");
        check(pane.currentDirectory == directory && pane.output->toPlainText().endsWith("  indented error\n"),
              "terminal retains stderr spacing and recognizes directory record after large output");
    }
};
struct DevToolsTestAccess {
    static void outputLimits(DevTools &tools) {
        tools.log(QString(3000000, 'x') + "RECENT_OUTPUT");
        check(tools.output->document()->characterCount() < 1000000 &&
              tools.output->toPlainText().endsWith("RECENT_OUTPUT") &&
              tools.output->toPlainText().startsWith("["), "huge unbroken output is bounded and retains latest text with notice");
        for (int i = 0; i < 20; ++i) tools.log(QString(20000, 'y'));
        tools.log(QString::fromUtf8("\n中文😀LATEST\n"));
        check(tools.output->document()->characterCount() < 1000000 &&
              tools.output->toPlainText().endsWith(QString::fromUtf8("中文😀LATEST\n")),
              "repeated streaming output stays bounded and preserves recent Unicode");
        tools.output->clear();
    }
    static QMetaObject::Connection stopAfterCompile(DevTools &tools, bool &observed, bool &wasBusy) {
        return QObject::connect(tools.process, &QProcess::finished, &tools, [&tools, &observed, &wasBusy](int code, QProcess::ExitStatus exit) {
            if (!observed && tools.compiling && code == 0 && exit == QProcess::NormalExit) {
                observed = true; wasBusy = tools.busy(); tools.stop();
            }
        });
    }
    static void diagnostics(DevTools &tools) {
        tools.compiling = true;
        tools.consumeProcessText("C:/sample.cpp:12:3: er", true);
        check(tools.problemsList->count() == 0, "partial diagnostic is buffered");
        tools.consumeProcessText("ror: bad token\n", true);
        check(tools.problemsList->count() == 1, "split diagnostic reassembles once");
        bool navigated = false;
        auto connection = QObject::connect(&tools, &DevTools::diagnosticRequested, &tools, [&](const QString &path, int line, int col) {
            navigated = path == "C:/sample.cpp" && line == 12 && col == 3;
        });
        tools.problemsList->itemActivated(tools.problemsList->item(0)); QObject::disconnect(connection);
        check(navigated, "problem activation reports exact file line column");
        tools.consumeProcessText("Example.java:4: error: unfinished", false);
        tools.flushDiagnostics();
        check(tools.problemsList->count() == 2 && tools.problemsList->item(1)->data(Qt::UserRole + 3).toInt() == 1, "unterminated Java diagnostics get line and default column");
        tools.consumeProcessText(" 12 | int error = 0;\n1 error generated.\n", true);
        check(tools.problemsList->count() == 2, "source excerpts and summary text do not duplicate problems");
        tools.consumeProcessText("C:/sample.cpp:1:2: fatal error: missing header\n", true);
        check(tools.problemsList->count() == 3, "fatal compiler error retains source coordinates");
        tools.compiling = false;
        tools.consumeProcessText("normal output: zero errors\n", false);
        check(tools.problemsList->count() == 3, "program output is not a compiler problem");
        tools.problemsList->clear(); tools.problemCount = 0; tools.refreshProblemsBadge(); tools.output->clear();
    }
};
static void write(const QString &path, const QByteArray &data) {
    QFile file(path); check(file.open(QIODevice::WriteOnly), "open fixture"); check(file.write(data)==data.size(), "write fixture");
}
int main(int argc, char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);
    try {
        QTemporaryDir temp; check(temp.isValid(), "temporary workspace");
        qputenv("BASIS_CODE_HOME",temp.filePath("data").toUtf8());
        check(DevTools::initialize(),"initialize storage");
        check(QFileInfo::exists(DevTools::dataRoot()+"/extensions/basis.cpp/manifest.json"),"local extension manifest");
        check(QDir(DevTools::projectsRoot()).exists(),"project storage created");
        check(!DevTools::safeArchiveEntry("../evil") && !DevTools::safeArchiveEntry("C:/evil") && !DevTools::safeArchiveEntry("a/../../evil") && !DevTools::safeArchiveEntry("\\\\host/share"), "reject archive traversal");
        check(DevTools::safeArchiveEntry("mingw64/bin/g++.exe"),"allow normal archive member");
        const auto compiler=QString::fromLocal8Bit(qgetenv("BASIS_TEST_COMPILER"));
        check(QFileInfo::exists(compiler),"test compiler exists");
        QSettings config(DevTools::dataRoot()+"/settings.ini",QSettings::IniFormat); config.setValue("toolchain/compiler",compiler); config.sync();
        QMainWindow window; DevTools tools(&window);
        TerminalPaneTestAccess::buffering(&window, temp.path());
        DevToolsTestAccess::outputLimits(tools);
        DevToolsTestAccess::diagnostics(tools);
        auto *output=window.findChild<QPlainTextEdit *>("runOutput"); check(output,"output panel exists");
        auto execute=[&](const QString &source, bool stop=false, bool feedInput=false) {
            QEventLoop loop; int result=-999;
            auto connection=QObject::connect(&tools,&DevTools::executionFinished,&loop,[&](int code){result=code;loop.quit();});
            QTimer timeout; timeout.setSingleShot(true); QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit); timeout.start(30000);
            tools.buildAndRun(source,temp.path());
            QTimer feeding;
            if(feedInput) {
                QObject::connect(&feeding,&QTimer::timeout,&loop,[&]{
                    auto *input=window.findChild<QLineEdit *>("runInput");
                    if(input && input->isEnabled()) { input->setText("24"); QMetaObject::invokeMethod(input,"returnPressed"); feeding.stop(); }
                }); feeding.start(20);
            }
            QTimer stopping;
            if(stop) { QObject::connect(&stopping,&QTimer::timeout,&loop,[&]{ if(output->toPlainText().contains("READY")) tools.stop(); }); stopping.start(20); }
            loop.exec(); QObject::disconnect(connection); check(result!=-999,"compile/run completed before timeout"); check(!tools.busy(),"process released"); return result;
        };
        auto *toolchainPage = tools.createToolchainPage(&window, "C/C++");
        window.setCentralWidget(toolchainPage);
        window.show();
        for (const bool clang : {true, false}) {
            auto *button = window.findChild<QToolButton *>(clang ? "installClang" : "installMinGW");
            check(button && button->menu() && button->menu()->actions().size() == 1, "compiler split button");
            const QString driver = clang ? "clang++.exe" : "g++.exe";
            const QString path = clang ? temp.filePath(driver) : QFileInfo(compiler).absoluteFilePath();
            if (clang) check(QFile::copy(compiler, path), "create local selection fixture");
            bool correctFilter = false;
            QTimer::singleShot(0, [&] {
                auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                if (!dialog) return;
                correctFilter = dialog->selectedNameFilter().contains(driver);
                dialog->selectFile(path);
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            });
            button->menu()->actions().first()->trigger();
            check(correctFilter && DevTools::compilerPath() == QFileInfo(path).absoluteFilePath(),
                  "local compiler menu filters and saves the chosen driver");
        }
        if (app.arguments().contains("--network")) {
            toolchainPage->show();
            auto *versions=window.findChild<QComboBox *>("compilerVersions");
            for (const auto *id : {"installMinGW", "installClang"}) {
                auto *fetch = window.findChild<QToolButton *>(id);
                check(versions && fetch && fetch->menu() && !fetch->menu()->actions().isEmpty(),
                      "each installer has a local compiler menu");
                fetch->click();
                QElapsedTimer timer; timer.start();
                while(!fetch->isEnabled() && timer.elapsed()<65000) {app.processEvents();QThread::msleep(10);}
                check(versions->count()>0,"real publisher releases loaded through Qt HTTPS");
                std::cout << "Loaded " << versions->count() << " compiler packages\n";
            }
        }
        toolchainPage->hide(); app.processEvents();
        check(!DevTools::runnerInstalled("C/C++") && DevTools::installRunner("C/C++") &&
                  DevTools::runnerInstalled("C/C++"), "optional bundled runner installation");
        tools.showTerminal(temp.path());
        auto *terminalInput = window.findChild<QLineEdit *>("terminalCommand");
        auto *terminalOutput = window.findChild<QPlainTextEdit *>("terminalOutput");
        QElapsedTimer terminalReady;
        terminalReady.start();
        while (!terminalInput->isEnabled() && terminalReady.elapsed() < 4000) {
            app.processEvents(); QThread::msleep(10);
        }
        check(terminalInput->isEnabled(), "interactive terminal starts");
        terminalInput->setText("echo BASIS_TERMINAL_OK");
        QMetaObject::invokeMethod(terminalInput, "returnPressed");
        terminalReady.restart();
        while (!terminalOutput->toPlainText().contains("BASIS_TERMINAL_OK") && terminalReady.elapsed() < 4000) {
            app.processEvents(); QThread::msleep(10);
        }
        check(terminalOutput->toPlainText().contains("BASIS_TERMINAL_OK"), "terminal executes real commands");
        auto *terminalProcess = terminalInput->parentWidget()->findChild<QProcess *>();
        terminalReady.restart();
        while (terminalProcess->state() != QProcess::NotRunning && terminalReady.elapsed() < 5000) {
            app.processEvents(); QThread::msleep(10);
        }
        const auto cpp=temp.filePath(QString::fromUtf8("空 格.cpp"));
        write(cpp,"#include <iostream>\nint main(){std::cout<<\"BASIS_CPP_OK\";return 0;}\n");
        const QString terminalExecutable = temp.filePath("terminal-program.exe");
        terminalInput->setText(QString("g++ \"%1\" -o \"%2\"; if ($LASTEXITCODE -eq 0) { & \"%2\" }").arg(cpp, terminalExecutable));
        QMetaObject::invokeMethod(terminalInput, "returnPressed");
        terminalReady.restart();
        while ((!QFileInfo::exists(terminalExecutable) || !terminalOutput->toPlainText().contains("BASIS_CPP_OK")) &&
               terminalReady.elapsed() < 20000) {
            app.processEvents(); QThread::msleep(10);
        }
        if (!QFileInfo::exists(terminalExecutable) || !terminalOutput->toPlainText().contains("BASIS_CPP_OK")) std::cerr << terminalOutput->toPlainText().toStdString() << std::endl;
        check(QFileInfo::exists(terminalExecutable) && terminalOutput->toPlainText().contains("BASIS_CPP_OK"),
              "terminal compiles and runs independently from the IDE runner");
        check(execute(cpp)==0 && output->toPlainText().contains("BASIS_CPP_OK"),"C++ compile and run with Unicode spaces path");
        bool handoffObserved = false, handoffBusy = false;
        const auto handoffConnection = DevToolsTestAccess::stopAfterCompile(tools, handoffObserved, handoffBusy);
        const int stoppedHandoff = execute(cpp); QObject::disconnect(handoffConnection);
        check(handoffObserved && handoffBusy && stoppedHandoff == -1 && !output->toPlainText().contains("BASIS_CPP_OK"), "stop in compile/launch handoff completes without starting target");
        const auto c=temp.filePath("hello.c");
        write(c,"#include <stdio.h>\nint main(void){puts(\"BASIS_C_OK zero errors\");return 0;}\n");
        check(execute(c)==0 && output->toPlainText().contains("BASIS_C_OK"),"C selects gcc and runs");
        check(window.findChild<QListWidget *>("problemsList")->count() == 0, "actual normal error-word output is not reported as a problem");
        write(c,"#include <stdio.h>\nint main(void){for(int i=0;i<3000000;++i)putchar('x');puts(\"BASIS_OUTPUT_TAIL\");return 0;}\n");
        check(execute(c)==0 && output->document()->characterCount() < 1000000 &&
              output->toPlainText().contains("BASIS_OUTPUT_TAIL"), "real program with huge unbroken output finishes and retains tail in bounded view");
        write(cpp,"#include <iostream>\nint main(){int n;std::cin>>n;std::cout<<\"INPUT_\"<<(n*2);return 0;}\n");
        check(execute(cpp,false,true)==0 && output->toPlainText().contains("INPUT_48"),"program standard input");
        write(cpp,"this is invalid C++\n");
        check(execute(cpp)!=0 && !output->toPlainText().contains("BASIS_CPP_OK"),"compile errors never run stale executable");
        check(window.findChild<QListWidget *>("problemsList")->count() > 0, "real compiler errors appear in problems panel");
        write(cpp,"#include <iostream>\n#include <thread>\n#include <chrono>\nint main(){std::cout<<\"READY\"<<std::endl;for(;;)std::this_thread::sleep_for(std::chrono::milliseconds(50));}\n");
        execute(cpp,true); check(output->toPlainText().contains(QString::fromUtf8("已停止")),"stop running process");
        std::cout<<"Passed storage and real C/C++ compilation checks\n";
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
