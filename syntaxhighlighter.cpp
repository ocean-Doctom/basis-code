#include "syntaxhighlighter.h"
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTimer>

namespace {
class DelimiterData : public QTextBlockUserData {
  public:
    QString ending;
};
}
SyntaxHighlighter::SyntaxHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {
    connect(document, &QTextDocument::contentsChanged, this, [this] {
        const bool large = largeFileMode();
        if (lastLargeMode == large) return;
        lastLargeMode = large;
        // Reformat after the document finishes its edit; clear old formats on
        // entry and rebuild all state when returning to normal-sized content.
        QTimer::singleShot(0, this, [this] { rehighlight(); });
    });
}
bool SyntaxHighlighter::largeFileMode() const {
    return document() && (document()->characterCount() > 20 * 1024 * 1024 || document()->blockCount() > 100000);
}
void SyntaxHighlighter::setFilePath(const QString &path) {
    const QString extension = QFileInfo(path).suffix().toLower();
    mode.clear();
    if (QStringList{"c", "cc", "cpp", "cxx", "h", "hpp", "hxx"}.contains(extension)) mode = "cpp";
    else if (extension == "java") mode = "java";
    else if (extension == "py" || extension == "pyw") mode = "python";
    else if (QStringList{"js", "jsx", "ts", "tsx", "mjs"}.contains(extension)) mode = "javascript";
    else if (extension == "json") mode = "json";
    else if (QStringList{"html", "htm", "xml", "svg"}.contains(extension)) mode = "html";
    else if (QStringList{"css", "qss"}.contains(extension)) mode = "css";
    else if (extension == "md" || extension == "markdown") mode = "markdown";
    else if (extension == "cmake" || QFileInfo(path).fileName() == "CMakeLists.txt") mode = "cmake";
    QString words;
    if (mode == "cpp") words = "alignas alignof asm auto bool break case catch char char8_t char16_t char32_t class concept const constexpr consteval constinit const_cast continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept nullptr operator private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while";
    else if (mode == "java") words = "abstract assert boolean break byte case catch char class const continue default do double else enum exports extends final finally float for if implements import instanceof int interface long module native new null package permits private protected public record requires return sealed short static strictfp super switch synchronized this throw throws transient true false try var void volatile while yield";
    else if (mode == "python") words = "False None True and as assert async await break class continue def del elif else except finally for from global if import in is lambda nonlocal not or pass raise return try while with yield match case type";
    else if (mode == "javascript") words = "abstract as async await any boolean break case catch class const constructor continue debugger declare default delete do else enum export extends false finally for from function get if implements import in infer instanceof interface let keyof module namespace never new null number of private protected public readonly require return set static string super switch symbol this throw true try type typeof undefined unknown var void while with yield";
    else if (mode == "json") words = "true false null";
    else if (mode == "cmake") words = "if else elseif endif foreach endforeach while endwhile function endfunction macro endmacro set unset option include project cmake_minimum_required find_package add_executable add_library target_link_libraries target_include_directories";
    keywords.clear();
    for (const auto &word : words.split(' ', Qt::SkipEmptyParts)) keywords.insert(word);
    rehighlight();
}
void SyntaxHighlighter::mark(int start, int length, const char *color, bool bold) {
    const QString key = QString::fromLatin1(color) + (bold ? QLatin1Char('b') : QLatin1Char('n'));
    auto cached = formats.constFind(key);
    if (cached != formats.cend()) { setFormat(start, length, cached.value()); return; }
    QTextCharFormat format;
    QString value = QString::fromLatin1(color);
    if (githubTheme) {
        static const QMap<QString, QString> colors{{"#794baa", "#ff7b72"}, {"#718774", "#8b949e"},
            {"#a36136", "#a5d6ff"}, {"#426fae", "#79c0ff"}, {"#397168", "#d2a8ff"}, {"#986296", "#79c0ff"}};
        value = colors.value(value, value);
    } else if (darkTheme) {
        static const QMap<QString, QString> colors{{"#794baa", "#bb9af7"}, {"#718774", "#8b9a75"},
            {"#a36136", "#9ece6a"}, {"#426fae", "#7aa2f7"}, {"#397168", "#73daca"}, {"#986296", "#ff9e64"}};
        value = colors.value(value, value);
    }
    format.setForeground(QColor(value));
    if (bold) format.setFontWeight(QFont::DemiBold);
    formats.insert(key, format);
    setFormat(start, length, format);
}
void SyntaxHighlighter::highlightBlock(const QString &text) {
    setCurrentBlockState(0);
    if (largeFileMode()) return;
    if (mode.isEmpty()) return;
    // Similar to VS Code's long-line tokenization limit: avoid blocking typing
    // on minified/generated data. Carry any open multiline delimiter so later
    // short lines do not accidentally become code inside a comment/string.
    if (text.size() > 20000) {
        const auto *previous = dynamic_cast<DelimiterData *>(currentBlock().previous().userData());
        if (previousBlockState() > 0 && previous && !text.contains(previous->ending)) {
            setCurrentBlockState(previousBlockState());
            auto *data = new DelimiterData; data->ending = previous->ending;
            setCurrentBlockUserData(data);
        }
        return;
    }
    const auto apply = [&](const QString &pattern, const char *color, bool bold = false) {
        static thread_local QHash<QString, QRegularExpression> expressions;
        if (!expressions.contains(pattern)) expressions.insert(pattern, QRegularExpression(pattern));
        auto matches = expressions.value(pattern).globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            mark(match.capturedStart(), match.capturedLength(), color, bold);
        }
    };
    if (mode == "markdown") {
        const QString fence(3, QChar(96));
        if (previousBlockState() == 1 || text.trimmed().startsWith(fence) || text.trimmed().startsWith("~~~")) {
            mark(0, text.size(), "#397168");
            const auto *previous = dynamic_cast<DelimiterData *>(currentBlock().previous().userData());
            QString delimiter = previousBlockState() == 1 && previous ? previous->ending : text.trimmed().left(3);
            const bool closing = previousBlockState() == 1 && text.trimmed().startsWith(delimiter);
            if (!closing) {
                setCurrentBlockState(1);
                auto *data = new DelimiterData; data->ending = delimiter; setCurrentBlockUserData(data);
            }
            return;
        }
        apply("^#{1,6}\\s+.*$", "#426fae", true);
        apply("\\*\\*[^*]+\\*\\*|__[^_]+__", "#794baa", true);
        apply("\\x60[^\\x60]+\\x60", "#a36136");
        apply("\\[[^\\]]+\\]\\([^)]*\\)", "#397168");
        return;
    }
    if (text.isEmpty() && previousBlockState() > 0) {
        setCurrentBlockState(previousBlockState());
        const auto *previous = dynamic_cast<DelimiterData *>(currentBlock().previous().userData());
        auto *data = new DelimiterData;
        data->ending = previous ? previous->ending : "*/";
        setCurrentBlockUserData(data);
        return;
    }
    int position = 0;
    if (mode == "html") {
        apply("</?[\\w:-]+|/?>", "#426fae", true);
        apply("\\b[\\w:-]+(?=\\s*=)", "#794baa");
    }
    const QString lineComment = mode == "python" || mode == "cmake" ? "#" : "//";
    while (position < text.size()) {
        if (previousBlockState() > 0 && position == 0) {
            const auto *data = dynamic_cast<DelimiterData *>(currentBlock().previous().userData());
            const QString ending = data ? data->ending : "*/";
            const int end = text.indexOf(ending);
            const int count = end < 0 ? text.size() : end + ending.size();
            mark(0, count, previousBlockState() == 1 ? "#718774" : "#a36136");
            if (end < 0) {
                setCurrentBlockState(previousBlockState());
                auto *next = new DelimiterData; next->ending = ending; setCurrentBlockUserData(next);
                return;
            }
            position = count;
            continue;
        }
        const bool hashComment = mode == "python" || mode == "cmake";
        if ((hashComment || (mode != "html" && mode != "json" && mode != "css")) &&
            text.mid(position, lineComment.size()) == lineComment) {
            mark(position, text.size() - position, "#718774"); break;
        }
        QString ending;
        int state = 0;
        int openingLength = 0;
        if (mode == "html" && text.mid(position, 4) == "<!--") {
            ending = "-->"; state = 1; openingLength = 4;
        } else if (!hashComment && mode != "json" && text.mid(position, 2) == "/*") {
            ending = "*/"; state = 1; openingLength = 2;
        } else if ((mode == "python" || mode == "java") &&
                   (text.mid(position, 3) == "\"\"\"" || (mode == "python" && text.mid(position, 3) == "'''"))) {
            ending = text.mid(position, 3); state = 2; openingLength = 3;
        } else if (mode == "cpp" && text.mid(position, 2) == "R\"") {
            const int bracket = text.indexOf('(', position + 2);
            if (bracket >= 0 && bracket - position <= 18) {
                ending = ")" + text.mid(position + 2, bracket - position - 2) + "\"";
                state = 2; openingLength = bracket - position + 1;
            }
        } else if (mode == "javascript" && text[position] == QChar(96)) {
            ending = QString(QChar(96)); state = 2; openingLength = 1;
        }
        if (state) {
            int end = text.indexOf(ending, position + openingLength);
            while (state == 2 && end > 0 && text[end - 1] == '\\')
                end = text.indexOf(ending, end + ending.size());
            const int count = end < 0 ? text.size() - position : end + ending.size() - position;
            mark(position, count, state == 1 ? "#718774" : "#a36136");
            if (end < 0) {
                setCurrentBlockState(state);
                auto *data = new DelimiterData; data->ending = ending; setCurrentBlockUserData(data);
                return;
            }
            position += count; continue;
        }
        if (text[position] == '"' || (mode != "json" && text[position] == '\'')) {
            const QChar quote = text[position];
            int end = position + 1;
            while (end < text.size()) {
                if (text[end] == '\\') { end += 2; continue; }
                if (text[end++] == quote) break;
            }
            end = qMin(end, int(text.size()));
            int following = end;
            while (following < text.size() && text[following].isSpace()) ++following;
            mark(position, end - position, mode == "json" && following < text.size() && text[following] == ':' ? "#426fae" : "#a36136");
            position = end; continue;
        }
        if (mode == "cpp" && text[position] == '#' && text.left(position).trimmed().isEmpty()) {
            int end = position + 1;
            while (end < text.size() && (text[end].isLetter() || text[end].isSpace())) ++end;
            mark(position, end - position, "#794baa"); position = end; continue;
        }
        if (mode == "html") { ++position; continue; }
        if (text[position].isDigit() && (position == 0 || (!text[position - 1].isLetterOrNumber() && text[position - 1] != '_'))) {
            static const QRegularExpression number(R"((?:0[xX][0-9a-fA-F]+|0[bB][01]+|\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)[uUlLfF]*)");
            const auto match = number.match(text, position);
            if (match.hasMatch() && match.capturedStart() == position) {
                mark(position, match.capturedLength(), "#986296"); position += match.capturedLength(); continue;
            }
        }
        if (text[position].isLetter() || text[position] == '_') {
            int end = position + 1;
            while (end < text.size() && (text[end].isLetterOrNumber() || text[end] == '_')) ++end;
            const QString token = text.mid(position, end - position);
            int following = end;
            while (following < text.size() && text[following].isSpace()) ++following;
            if (keywords.contains(token)) mark(position, end - position, "#794baa", true);
            else if (mode == "css" && following < text.size() && text[following] == ':') mark(position, end - position, "#426fae");
            else if (following < text.size() && text[following] == '(') mark(position, end - position, "#397168");
            position = end; continue;
        }
        ++position;
    }
}
