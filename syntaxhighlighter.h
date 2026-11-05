#pragma once
#include <QSyntaxHighlighter>
#include <QSet>
#include <QHash>
#include <QTextCharFormat>
class SyntaxHighlighter : public QSyntaxHighlighter {
  public:
    explicit SyntaxHighlighter(QTextDocument *document);
    void setColorTheme(const QString &name) { darkTheme = name != "light"; githubTheme = name == "github-dark-default"; formats.clear(); rehighlight(); }
    void setFilePath(const QString &path);
  protected:
    void highlightBlock(const QString &text) override;
  private:
    void mark(int start, int length, const char *color, bool bold = false);
    bool darkTheme = false;
    bool githubTheme = false;
    QString mode;
    QSet<QString> keywords;
    QHash<QString, QTextCharFormat> formats;
};
