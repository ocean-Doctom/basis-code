#pragma once
#include <QWidget>
#include <QStringConverter>
class QProcess;
class QPlainTextEdit;
class QLineEdit;
class QLabel;
class TerminalPane : public QWidget {
  public:
    explicit TerminalPane(QWidget *parent = nullptr);
    ~TerminalPane() override;
    void start(const QString &directory);
  private:
    friend struct TerminalPaneTestAccess;
    void consumeErrorText(const QString &text);
    void stopShell();
    void execute(const QString &text);
    QString errorBuffer;
    void append(const QString &text);
    QProcess *shell;
    QPlainTextEdit *output;
    QLineEdit *command;
    QLabel *location;
    QString currentDirectory;
    QStringDecoder stdoutDecoder{QStringDecoder::Utf8};
    QStringDecoder stderrDecoder{QStringDecoder::Utf8};
    void *job = nullptr;
};
