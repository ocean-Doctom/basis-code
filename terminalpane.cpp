#include "terminalpane.h"
#include "devtools.h"
#include <QProcess>
#include <QProcessEnvironment>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFontDatabase>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

TerminalPane::TerminalPane(QWidget *parent) : QWidget(parent) {
    setObjectName("terminalPane");
    auto *layout = new QVBoxLayout(this);
    auto *toolbar = new QHBoxLayout;
    location = new QLabel(this);
    location->setObjectName("terminalDirectory");
    location->setTextInteractionFlags(Qt::TextSelectableByMouse);
    toolbar->addWidget(location, 1);
    auto *restart = new QPushButton(tr("重启终端"), this);
    toolbar->addWidget(restart);
    layout->addLayout(toolbar);
    output = new QPlainTextEdit(this);
    output->setObjectName("terminalOutput");
    output->setReadOnly(true);
    output->setMaximumBlockCount(15000);
    output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(output, 1);
    command = new QLineEdit(this);
    command->setObjectName("terminalCommand");
    command->setPlaceholderText(tr("输入命令，按 Enter 执行"));
    layout->addWidget(command);
    shell = new QProcess(this);
#ifdef Q_OS_WIN
    shell->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
#endif
    connect(shell, &QProcess::started, this, [this] {
#ifdef Q_OS_WIN
        HANDLE handle = CreateJobObjectW(nullptr, nullptr);
        if (handle) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(shell->processId()));
            const bool assigned = SetInformationJobObject(handle, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) &&
                                  process && AssignProcessToJobObject(handle, process);
            if (process) CloseHandle(process);
            if (assigned) job = handle; else CloseHandle(handle);
        }

#endif

    });
    connect(shell, &QProcess::readyReadStandardOutput, this, [this] {
        append(stdoutDecoder(shell->readAllStandardOutput()));
    });
    connect(shell, &QProcess::readyReadStandardError, this, [this] {
        errorBuffer += stderrDecoder(shell->readAllStandardError());
        int end;
        while ((end = errorBuffer.indexOf('\n')) >= 0) {
            const QString line = errorBuffer.left(end).trimmed();
            errorBuffer.remove(0, end + 1);
            if (line.startsWith("__BASIS_CWD__")) {
                const auto folder = line.mid(13);
                if (QFileInfo(folder).isDir()) currentDirectory = folder;
            } else append(line + "\n");
        }
    });
    connect(shell, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) append(tr("\n无法启动终端：") + shell->errorString());
    });
    connect(shell, &QProcess::finished, this, [this] {
        if (!errorBuffer.isEmpty()) { append(errorBuffer); errorBuffer.clear(); }
        command->setEnabled(true);
        location->setText("PowerShell · " + QDir::toNativeSeparators(currentDirectory));
        append("\nPS " + currentDirectory + "> ");
    });
    connect(command, &QLineEdit::returnPressed, this, [this] {
        const QString text = command->text();
        command->clear();
        append(text + "\n");
        if (shell->state() == QProcess::Running) shell->write(text.toUtf8() + "\n");
        else execute(text);
    });
    connect(restart, &QPushButton::clicked, this, [this] {
        stopShell();
        const QString directory = currentDirectory;
        currentDirectory.clear();
        start(directory);
    });
}
TerminalPane::~TerminalPane() {
    shell->disconnect(this);
    stopShell();
}
void TerminalPane::append(const QString &text) {
    auto cursor = output->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    output->setTextCursor(cursor);
    output->ensureCursorVisible();
}
void TerminalPane::stopShell() {
#ifdef Q_OS_WIN
    if (job) { CloseHandle(static_cast<HANDLE>(job)); job = nullptr; }
#endif
    if (shell->state() != QProcess::NotRunning) {
        shell->kill();
        shell->waitForFinished(2000);
    }
}
void TerminalPane::start(const QString &directory) {
    if (!currentDirectory.isEmpty()) { command->setFocus(); return; }
    currentDirectory = QFileInfo(directory).isDir() ? QDir(directory).absolutePath() : DevTools::projectsRoot();
    location->setText("PowerShell · " + QDir::toNativeSeparators(currentDirectory));
    command->setEnabled(true);
    command->setFocus();
    append("PS " + currentDirectory + "> ");
}
void TerminalPane::execute(const QString &text) {
    stopShell();
    shell->setWorkingDirectory(currentDirectory);
    auto environment = QProcessEnvironment::systemEnvironment();
    QStringList binaries;
    for (const auto &path : {DevTools::compilerPath(), DevTools::runtimePath("Java"), DevTools::runtimePath("Python")})
        if (QFileInfo::exists(path)) binaries.append(QFileInfo(path).absolutePath());
    if (!binaries.isEmpty()) environment.insert("PATH", binaries.join(QDir::listSeparator()) + QDir::listSeparator() + environment.value("PATH"));
    environment.insert("PYTHONIOENCODING", "utf-8");
    environment.insert("PYTHONUTF8", "1");
    shell->setProcessEnvironment(environment);
    stdoutDecoder.resetState(); stderrDecoder.resetState(); errorBuffer.clear();
#ifdef Q_OS_WIN
    const QString program = QStandardPaths::findExecutable("powershell.exe",
        {qEnvironmentVariable("SystemRoot") + "/System32/WindowsPowerShell/v1.0"});
    const QString session =
        "[Console]::InputEncoding = New-Object System.Text.UTF8Encoding($false); "
        "[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false); "
        "$OutputEncoding = [Console]::OutputEncoding; try {\n" + text +
        "\n} finally { [Console]::Error.WriteLine('__BASIS_CWD__' + (Get-Location).Path) }";
    QByteArray utf16;
    for (const auto character : session) {
        utf16.append(char(character.unicode() & 255));
        utf16.append(char(character.unicode() >> 8));
    }
    shell->start(program, {"-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand", QString::fromLatin1(utf16.toBase64())});
#else
    shell->start("/bin/sh", {"-c", text});
#endif
}
