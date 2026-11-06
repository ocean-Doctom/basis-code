#include "codeeditor.h"
#include <QApplication>
#include <QImage>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextLayout>
#include <iostream>
#include <stdexcept>

static void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        CodeEditor editor;
        editor.resize(600, 300);
        editor.setLineWrapMode(QPlainTextEdit::NoWrap);
        editor.show();
        app.processEvents();
        auto *gutter = editor.findChild<QWidget *>("lineNumberArea");
        check(gutter && gutter->isVisible(), "line number area visible");
        const int singleDigitWidth = gutter->width();
        QStringList lines;
        for (int i = 1; i <= 120; ++i)
            lines.append(QString("line %1 ").arg(i) + QString(200, 'x'));
        editor.setPlainText(lines.join('\n'));
        app.processEvents();
        check(editor.blockCount() == 120 && gutter->width() > singleDigitWidth,
              "gutter grows for three digit line numbers");
        check(editor.viewport()->geometry().left() >= gutter->geometry().right(),
              "line numbers do not overlap editor text");
        const auto beforeScroll = gutter->grab().toImage();
        editor.verticalScrollBar()->setValue(50);
        app.processEvents();
        check(gutter->grab().toImage() != beforeScroll,
              "visible line numbers change on vertical scroll");
        const auto beforeHorizontalScroll = gutter->grab().toImage();
        editor.horizontalScrollBar()->setValue(100);
        app.processEvents();
        check(gutter->grab().toImage() == beforeHorizontalScroll,
              "line numbers stay fixed on horizontal scroll");
        const int originalWidth = gutter->width();
        auto font = editor.font();
        font.setPointSize(24);
        editor.setFont(font);
        app.processEvents();
        check(gutter->width() > originalWidth, "gutter follows font size");
        editor.setLineWrapMode(QPlainTextEdit::WidgetWidth);
        editor.resize(360, 220);
        app.processEvents();
        check(editor.blockCount() == 120 && gutter->height() == editor.viewport()->height(),
              "wrapping keeps logical line count and resized gutter height");
        editor.setPlainText("single line");
        app.processEvents();
        check(gutter->width() < originalWidth * 2,
              "gutter shrinks after deleting lines");
        auto colorAt = [&](int block, int offset) {
            const auto formats = editor.document()->findBlockByNumber(block).layout()->formats();
            for (const auto &range : formats)
                if (offset >= range.start && offset < range.start + range.length)
                    return range.format.foreground().color().name();
            return QString();
        };
        for (const auto &suffix : {"cpp", "java", "py", "js", "ts"}) {
            editor.setFilePath(QString("example.") + suffix);
            editor.setPlainText(QString(suffix) == "py" ? "def greet():\n    return 42"
                                                       : "class Example { return 42; }");
            app.processEvents();
            check(colorAt(0, 0) == "#794baa", "language keyword highlighted");
        }
        editor.setFilePath("example.cpp");
        editor.setPlainText("/* comment\n\nstill comment */ int value = 42;\nconst char *s = \"// string\";");
        app.processEvents();
        check(colorAt(2, 0) == "#718774" && colorAt(2, 17) == "#794baa",
              "multiline comment crosses blank lines and ends before keyword");
        editor.setFilePath("example.py");
        editor.setPlainText("text = \"\"\"hello\n\nworld\"\"\"\n# comment");
        app.processEvents();
        check(colorAt(2, 0) == "#a36136" && colorAt(3, 0) == "#718774",
              "Python triple quoted strings and comments");
        editor.setFilePath("example.json");
        editor.setPlainText("{\"key\": true}");
        app.processEvents();
        check(colorAt(0, 1) == "#426fae" && colorAt(0, 8) == "#794baa",
              "JSON property keys and literals");
        for (const auto &example : QList<QPair<QString, QString>>{
                 {"example.html", "<div title=\"hello\">"},
                 {"example.css", "color: red;"},
                 {"example.md", "# Heading"}}) {
            editor.setFilePath(example.first);
            editor.setPlainText(example.second);
            app.processEvents();
            check(colorAt(0, 0) == "#426fae", "markup and stylesheet highlighting");
        }
        editor.setFilePath("CMakeLists.txt");
        editor.setPlainText("project(example)");
        app.processEvents();
        check(colorAt(0, 0) == "#794baa", "CMake keyword highlighted");
        editor.setColorTheme("github-dark-default");
        editor.setFilePath("example.cpp");
        editor.setPlainText("return 42;");
        app.processEvents();
        check(colorAt(0, 0) == "#ff7b72" && colorAt(0, 7) == "#79c0ff",
              "GitHub Dark Default token colors applied");
        editor.setColorTheme("light");
        editor.setPlainText("/*\n" + QString(21000, 'x') + "\nstill a comment\n*/\nreturn 1;");
        app.processEvents();
        check(colorAt(2, 0) == "#718774", "long line preserves multiline comment state");
        check(colorAt(4, 0) == "#794baa", "highlighting resumes after multiline comment closes");
        editor.setPlainText(QString("return 1;" + QString(10000, ' ' ) + "\n").repeated(2100));
        app.processEvents();
        check(editor.largeFileMode() && colorAt(0, 0).isEmpty(), "large documents pause token formatting");
        editor.setPlainText("return 1;"); app.processEvents();
        check(!editor.largeFileMode() && colorAt(0, 0) == "#794baa", "shrinking document restores highlighting");
        editor.setPlainText(QString("x\n").repeated(100001)); app.processEvents();
        check(editor.largeFileMode(), "large line counts also activate the mode");
        editor.setPlainText("return 1;"); app.processEvents();
        check(!editor.largeFileMode() && colorAt(0, 0) == "#794baa", "line-count downgrade also restores normal formatting");
        editor.setFilePath("example.txt");
        app.processEvents();
        check(colorAt(0, 1).isEmpty(), "plain text clears previous highlighting");
        auto matchAt = [&](const QString &text, int position, const QString &path = "match.cpp") {
            editor.setFilePath(path); editor.setPlainText(text);
            auto cursor = editor.textCursor(); cursor.setPosition(position); editor.setTextCursor(cursor);
            app.processEvents();
            return editor.extraSelections();
        };
        const QString brackets = "call(a[0], \")\")";
        auto matches = matchAt(brackets, 5);
        check(matches.size() == 2 && matches[0].cursor.selectionStart() == 4 &&
              matches[1].cursor.selectionStart() == brackets.size() - 1,
              "pair matching respects nested brackets and ignores quoted closing bracket");
        editor.jumpToMatchingBracket();
        check(editor.textCursor().position() == brackets.size(), "jump reaches closing bracket edge");
        editor.jumpToMatchingBracket();
        check(editor.textCursor().position() == 5 && !editor.document()->isModified(),
              "reverse jump returns to opening edge without editing document");
        matches = matchAt("call(\n/* ) */\nvalue[0])", 5);
        check(matches.size() == 2 && matches[1].cursor.selectionStart() == editor.toPlainText().size() - 1,
              "matching crosses blocks and ignores multiline comment brackets");
        matches = matchAt("call(R\"tag(])tag\")", 5);
        check(matches.size() == 2 && matches[1].cursor.selectionStart() == editor.toPlainText().size() - 1,
              "C++ raw-string brackets do not affect matching");
        matches = matchAt("call(\"\"\"\n) ]\n\"\"\")", 5, "match.py");
        check(matches.size() == 2 && matches[1].cursor.selectionStart() == editor.toPlainText().size() - 1,
              "Python triple-quoted brackets do not affect matching");
        matches = matchAt("([)]", 1);
        check(matches.size() == 1 && matches[0].cursor.selectionStart() == 0, "malformed nesting highlights only unmatched source");
        matches = matchAt("test(/[)]/.source, value)", 5, "match.js");
        check(matches.size() == 2 && matches[1].cursor.selectionStart() == editor.toPlainText().size() - 1,
              "JavaScript regex character-class brackets do not affect call matching");
        matches = matchAt("call(a / (b + c))", 5, "match.js");
        check(matches.size() == 2 && matches[1].cursor.selectionStart() == editor.toPlainText().size() - 1,
              "JavaScript division remains code with nested parentheses");
        check(matchAt("\"(\"", 2).isEmpty(), "cursor inside string does not match brackets");
        check(matchAt("// (", 4).isEmpty(), "cursor inside line comment does not match brackets");
        check(matchAt("(\n" + QString("x\n").repeated(300) + ")", 1).isEmpty(),
              "matching stops at block budget instead of scanning whole document");
        check(matchAt("(\n" + QString(12000, ' ') + "\n" + QString(12000, ' ') + "\n)", 1).isEmpty(),
              "matching stops at distance budget without marking an uncertain mismatch");
        check(matchAt("(" + QString(21000, ' ') + ")", 1).isEmpty(), "long skipped lines do not trigger bracket scans");
        check(matchAt("(value)", 1, "plain.txt").isEmpty(), "unsupported plain text does not reuse code bracket cache");
        std::cout << "Passed line number checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
