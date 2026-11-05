#include "devtools.h"
#include <QApplication>
#include <QMainWindow>
#include <QTemporaryDir>
#include <QSettings>
#include <QDialog>
#include <QComboBox>
#include <QToolButton>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include <QFile>
#include <iostream>
#include <stdexcept>

static void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
template <typename Predicate>
static void waitFor(QApplication &app, Predicate ready, int timeout = 60000) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < timeout) {
        app.processEvents();
        QThread::msleep(10);
    }
    check(ready(), "operation timed out");
}
static void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "open fixture");
    check(file.write(bytes) == bytes.size(), "write fixture");
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    qputenv("BASIS_CODE_HOME", temporary.path().toUtf8());
    QMainWindow window;
    DevTools tools(&window);
    try {
        const bool download = app.arguments().contains("--download");
        const bool pythonOnly = app.arguments().contains("--local-python");
        if (!download) {
            QSettings config(DevTools::dataRoot() + "/settings.ini", QSettings::IniFormat);
            config.setValue("toolchain/python", qEnvironmentVariable("BASIS_TEST_PYTHON"));
            config.setValue("toolchain/java", qEnvironmentVariable("BASIS_TEST_JAVAC"));
            config.sync();
        }
        for (const QString language : download ? QStringList{"Python", "Java"} : QStringList{}) {
            auto *dialog = tools.createToolchainPage(&window, language);
            window.setCentralWidget(dialog);
            window.show();
            auto *install = dialog->findChild<QToolButton *>("install" + language);
            auto *versions = dialog->findChild<QComboBox *>("compilerVersions");
            auto *download = dialog->findChild<QPushButton *>("downloadToolchain");
            auto *status = dialog->findChild<QLabel *>("toolchainStatus");
            install->click();
            waitFor(app, [&] { return install->isEnabled(); });
            std::cout << language.toStdString() << " versions: " << versions->count() << std::endl;
            check(versions->count() > 0, "publisher versions available");
            download->click();
            waitFor(app, [&] { return install->isEnabled(); }, 600000);
            std::cout << status->text().toStdString() << std::endl;
            check(QFile::exists(DevTools::runtimePath(language)), "download and installation completed");
            std::cout << language.toStdString() << " installed" << std::endl;
            dialog->hide();
            app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        auto *output = window.findChild<QPlainTextEdit *>("runOutput");
        auto *input = window.findChild<QLineEdit *>("runInput");
        auto send = [&](const QString &command) {
            input->setText(command);
            QMetaObject::invokeMethod(input, "returnPressed");
        };
        int exitCode = -999;
        QObject::connect(&tools, &DevTools::executionFinished, &window, [&](int code) { exitCode = code; });
        auto run = [&](const QString &path, bool debug = false) {
            exitCode = -999;
            tools.buildAndRun(path, temporary.path(), debug);
        };
        auto finish = [&] {
            waitFor(app, [&] { return exitCode != -999; });
            std::cout << output->toPlainText().toStdString() << std::endl;
            check(exitCode == 0, "program completed successfully");
        };
        auto contains = [&](const QString &text) {
            waitFor(app, [&] { return output->toPlainText().contains(text); });
        };
        const QString python = temporary.filePath("hello space.py");
        write(python, QString::fromUtf8("value = 24\nprint('PYTHON_OK_' + str(value), flush=True)\nprint('中文输出😀', flush=True)\n").toUtf8());
        run(python); finish();
        check(output->toPlainText().contains("PYTHON_OK_24"), "Python output");
        check(output->toPlainText().contains(QString::fromUtf8("中文输出😀")), "Python UTF-8 output includes Chinese and astral Unicode");
        run(python, true); contains("(Pdb)");
        send("n"); contains("print(");
        send("p value"); contains("24\n");
        send("c"); contains("PYTHON_OK_24");
        send("q"); finish();
        if (pythonOnly) {
            std::cout << "Passed local Python execution and debugger checks" << std::endl;
            return 0;
        }
        const QString java = temporary.filePath("BasisJava.java");
        write(java, "public class BasisJava {\n public static void main(String[] args) {\n int value = 24;\n System.out.println(\"JAVA_OK_\" + value);\n }\n}\n");
        run(java); finish();
        check(output->toPlainText().contains("JAVA_OK_24"), "Java output");
        // JDB messages are localized by the selected JDK; thread prompts and
        // our source text remain stable across English/Chinese installations.
        run(java, true); contains("main[1]");
        send("next"); contains("System.out.println");
        send("locals"); contains("value = 24");
        send("cont"); finish();
        check(output->toPlainText().contains("JAVA_OK_24"), "Java debugger ran program");
        write(java, "invalid java\n");
        run(java);
        waitFor(app, [&] { return exitCode != -999; });
        check(exitCode != 0 && !output->toPlainText().contains("JAVA_OK_24"), "Java compile error prevents stale run");
        std::cout << (download ? "Passed downloads, installation, execution and debugger checks" :
                     "Passed local Python/Java execution, debugger and compile-failure checks (no downloads)") << std::endl;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << std::endl;
        if (auto *output = window.findChild<QPlainTextEdit *>("runOutput"))
            std::cerr << output->toPlainText().toStdString() << std::endl;
        return 1;
    }
}
