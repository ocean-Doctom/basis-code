#pragma once
#include <QPlainTextEdit>

class LineNumberArea;
class SyntaxHighlighter;

class CodeEditor : public QPlainTextEdit {
  public:
    explicit CodeEditor(QWidget *parent = nullptr);
    int lineNumberAreaWidth() const;
    void setDarkTheme(bool dark);
    void setColorTheme(const QString &name);
    void setFilePath(const QString &path);
    bool largeFileMode() const;

  protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

  private:
    friend class LineNumberArea;
    void updateLineNumberAreaWidth();
    void paintLineNumbers(QPaintEvent *event);
    bool darkTheme = false;
    bool githubTheme = false;
    SyntaxHighlighter *highlighter;
    QWidget *lineNumberArea = nullptr;
};
