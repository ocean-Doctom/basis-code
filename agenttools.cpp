#include "agenttools.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace {
constexpr qint64 kMaxReadBytes = 512 * 1024;      // 512 KiB read cap
constexpr int kMaxListEntries = 500;
constexpr int kMaxSearchResults = 100;
constexpr qint64 kMaxWriteBytes = 1024 * 1024;    // 1 MiB write cap

QString norm(const QString &path) {
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}
}  // namespace

void AgentTools::setWorkRoot(const QString &path) {
    workRoot_ = QFileInfo(path).isDir() ? QFileInfo(path).canonicalFilePath() : QString();
    pending_actions_.clear();
}

QString AgentTools::resolve(const QString &path) const {
    if (workRoot_.isEmpty() || path.isEmpty()) return {};
    const QDir root(workRoot_);
    const QString clean = norm(path);
    if (clean.isEmpty()) return {};
    // Absolute path must live inside the work root.
    const QString abs = root.absoluteFilePath(clean);
    const QString rel = root.relativeFilePath(abs);
    if (rel == ".." || rel.startsWith("../") || rel.startsWith("..\\") || QDir::isAbsolutePath(rel)) return {};
    // Check existing ancestors too: junctions/symlinks can leave the root even
    // when the lexical path is inside it. New file names inherit this check.
    QFileInfo ancestor(abs);
    while (!ancestor.exists()) {
        if (ancestor.isSymLink()) return {};
        const QString parent = ancestor.absolutePath();
        if (parent == ancestor.absoluteFilePath()) return {};
        ancestor.setFile(parent);
    }
    const QString canonical = ancestor.canonicalFilePath();
    if (canonical.isEmpty()) return {};
    const QString actualRelative = root.relativeFilePath(canonical);
    if (actualRelative == ".." || actualRelative.startsWith("../") ||
        QDir::isAbsolutePath(actualRelative)) return {};
    return QDir::cleanPath(abs);
}

QString AgentTools::listFiles(const QJsonObject &args, QJsonObject &content) {
    const QString rel = args["path"].toString(".");
    const int depth = args["depth"].toInt(2);
    const QString base = resolve(rel);
    if (base.isEmpty()) {
        content["error"] = "path is outside the working directory";
        return "list_files";
    }
    if (!QFileInfo(base).isDir()) {
        content["error"] = "path is not a directory";
        return "list_files";
    }
    const int maxDepth = qBound(1, depth, 6);
    QJsonArray entries;
    QDirIterator it(base, QDir::NoDotAndDotDot | QDir::AllEntries, QDirIterator::Subdirectories);
    int count = 0;
    const QDir root(workRoot_);
    while (it.hasNext() && count < kMaxListEntries) {
        it.next();
        const QString absolute = it.filePath();
        const QString relPath = root.relativeFilePath(absolute);
        const int slashCount = relPath.count('/');
        if (slashCount >= maxDepth) continue;  // depth 0 == the dir itself
        QJsonObject entry;
        entry["name"] = it.fileName();
        entry["path"] = relPath;
        entry["type"] = it.fileInfo().isDir() ? "dir" : "file";
        if (!it.fileInfo().isDir()) entry["size"] = double(it.fileInfo().size());
        entries.append(entry);
        ++count;
    }
    content["entries"] = entries;
    content["count"] = entries.size();
    content["truncated"] = count >= kMaxListEntries;
    return "list_files";
}

QString AgentTools::readFile(const QJsonObject &args, QJsonObject &content) {
    const QString rel = args["path"].toString();
    const QString path = resolve(rel);
    if (path.isEmpty()) {
        content["error"] = "path is outside the working directory";
        return "read_file";
    }
    QFile file(path);
    if (!file.exists()) {
        content["error"] = "file not found";
        return "read_file";
    }
    if (!file.open(QIODevice::ReadOnly)) {
        content["error"] = "cannot open file: " + file.errorString();
        return "read_file";
    }
    const qint64 size = file.size();
    if (size > kMaxReadBytes) {
        content["error"] = "file too large";
        content["size"] = double(size);
        file.close();
        return "read_file";
    }
    const QByteArray data = file.readAll();
    file.close();
    content["path"] = QDir(workRoot_).relativeFilePath(path);
    content["size"] = double(data.size());
    content["content"] = QString::fromUtf8(data);
    return "read_file";
}

QString AgentTools::searchFiles(const QJsonObject &args, QJsonObject &content) {
    const QString pattern = args["pattern"].toString();
    const QString rel = args["path"].toString(".");
    const bool contentSearch = args["content"].toBool(false);
    const QString base = resolve(rel);
    if (base.isEmpty()) {
        content["error"] = "path is outside the working directory";
        return "search_files";
    }
    if (pattern.isEmpty()) {
        content["error"] = "pattern is required";
        return "search_files";
    }
    QRegularExpression regex(pattern,
                             QRegularExpression::CaseInsensitiveOption);
    if (!regex.isValid()) {
        content["error"] = "invalid regex pattern";
        return "search_files";
    }
    const QDir root(workRoot_);
    QJsonArray results;
    QDirIterator it(base, QDir::NoDotAndDotDot | QDir::Files, QDirIterator::Subdirectories);
    int count = 0;
    while (it.hasNext() && count < kMaxSearchResults) {
        it.next();
        const QString absolute = it.filePath();
        const QString relPath = root.relativeFilePath(absolute);
        if (contentSearch) {
            QFile file(absolute);
            if (file.size() > kMaxReadBytes) continue;
            if (file.open(QIODevice::ReadOnly)) {
                const QString text = QString::fromUtf8(file.readAll());
                file.close();
                if (!regex.match(text).hasMatch()) continue;
                QJsonObject entry;
                entry["path"] = relPath;
                entry["match"] = regex.match(text).captured().left(200);
                results.append(entry);
                ++count;
            }
        } else {
            if (regex.match(it.fileName()).hasMatch()) {
                QJsonObject entry;
                entry["path"] = relPath;
                results.append(entry);
                ++count;
            }
        }
    }
    content["results"] = results;
    content["count"] = results.size();
    content["truncated"] = count >= kMaxSearchResults;
    return "search_files";
}

QString AgentTools::writeFile(const QJsonObject &args, QJsonObject &content, bool &needsConfirm,
                              QString &confirmPrompt) {
    const QString rel = args["path"].toString();
    const QString text = args["content"].toString();
    const QString path = resolve(rel);
    if (path.isEmpty()) {
        content["error"] = "path is outside the working directory";
        return "write_file";
    }
    if (text.toUtf8().size() > kMaxWriteBytes) {
        content["error"] = "content too large";
        return "write_file";
    }
    const bool exists = QFileInfo::exists(path);
    if (writePolicy_ == WritePolicy::NewOnly && exists) {
        content["error"] = "file already exists (write policy forbids overwrite)";
        return "write_file";
    }
    if (writePolicy_ == WritePolicy::Confirm) {
        needsConfirm = true;
        confirmPrompt = QStringLiteral("写入文件：\n%1\n\n（%2 字节，%3）")
                            .arg(QDir::toNativeSeparators(path), QString::number(text.toUtf8().size()),
                                 exists ? QStringLiteral("覆盖已有文件") : QStringLiteral("新建文件"));
        return "write_file";
    }
    // Direct write.
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        content["error"] = "cannot write file: " + file.errorString();
        return "write_file";
    }
    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        content["error"] = "write failed";
        return "write_file";
    }
    content["path"] = QDir(workRoot_).relativeFilePath(path);
    content["written"] = double(bytes.size());
    content["action"] = exists ? "overwritten" : "created";
    return "write_file";
}

QString AgentTools::runCommand(const QJsonObject &args, QJsonObject &content, bool &needsConfirm,
                               QString &confirmPrompt) {
    const QString command = args["command"].toString().trimmed();
    if (command.isEmpty()) {
        content["error"] = "command is required";
        return "run_command";
    }
    const QString cwdArg = args["cwd"].toString(".");
    const QString cwd = resolve(cwdArg);
    if (cwd.isEmpty() || !QFileInfo(cwd).isDir()) {
        content["error"] = "working directory is outside the work root";
        return "run_command";
    }
    if (commandPolicy_ == CommandPolicy::Allowlist && !allowlistedCommand(command)) {
        content["error"] = "command not in allowlist";
        return "run_command";
    }
    if (commandPolicy_ == CommandPolicy::Confirm) {
        needsConfirm = true;
        confirmPrompt = QStringLiteral("执行命令：\n%1\n\n（工作目录：%2）")
                            .arg(command, QDir::toNativeSeparators(cwd));
        return "run_command";
    }

    // Execute synchronously with a timeout. Reuse the PowerShell encoded-command
    // approach already used by the terminal pane so output stays UTF-8 safe.
    QProcess process;
    QByteArray standardOutput, standardError;
    auto collect = [&] {
        const auto out = process.readAllStandardOutput();
        const auto err = process.readAllStandardError();
        standardOutput += out.left(qMax(0, int(kMaxReadBytes) - int(standardOutput.size())));
        standardError += err.left(qMax(0, int(kMaxReadBytes) - int(standardError.size())));
    };
    QObject::connect(&process, &QProcess::readyReadStandardOutput, &process, collect);
    QObject::connect(&process, &QProcess::readyReadStandardError, &process, collect);
    process.setWorkingDirectory(cwd);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("PYTHONIOENCODING", "utf-8");
    environment.insert("PYTHONUTF8", "1");
    process.setProcessEnvironment(environment);
#ifdef Q_OS_WIN
    const QString program = QStandardPaths::findExecutable("powershell.exe",
        {qEnvironmentVariable("SystemRoot") + "/System32/WindowsPowerShell/v1.0"});
    const QString session =
        "[Console]::InputEncoding = New-Object System.Text.UTF8Encoding($false); "
        "[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false); "
        "$OutputEncoding = [Console]::OutputEncoding; try {\n" + command +
        "\n} finally { [Console]::Error.WriteLine('__BASIS_DONE__') }";
    QByteArray utf16;
    for (const auto character : session) {
        utf16.append(char(character.unicode() & 255));
        utf16.append(char(character.unicode() >> 8));
    }
    process.start(program, {"-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand",
                            QString::fromLatin1(utf16.toBase64())});
#else
    process.start("/bin/sh", {"-c", command});
#endif
    if (!process.waitForStarted(5000)) {
        content["error"] = "failed to start process";
        return "run_command";
    }
    if (!process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished(2000);
        content["error"] = "command timed out (30s)";
        collect();
        content["stdout"] = QString::fromUtf8(standardOutput);
        content["stderr"] = QString::fromUtf8(standardError);
        return "run_command";
    }
    collect();
    content["stdout"] = QString::fromUtf8(standardOutput);
    content["stderr"] = QString::fromUtf8(standardError);
    content["exitCode"] = process.exitCode();
    return "run_command";
}

bool AgentTools::allowlistedCommand(const QString &command) {
    // Reject shell composition rather than approving only its first command.
    if (command.contains(QRegularExpression(R"([\r\n;&|<>`$])"))) return false;
    const QString first = command.section(QRegularExpression("\\s+"), 0, 0).toLower();
    static const QSet<QString> allowed = {
        "ls", "dir", "pwd", "cd", "cat", "type", "git", "g++", "gcc", "clang++", "clang",
        "python", "python3", "javac", "java", "cmake", "make", "echo", "tree", "find",
        "where", "which"};
    return allowed.contains(first);
}

QString AgentTools::describeContent(const QJsonObject &content) {
    return QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact));
}

AgentTools::Result AgentTools::run(const QString &toolCallId, const QString &name,
                                   const QJsonObject &arguments) {
    Result result;
    QJsonObject content;
    bool needsConfirm = false;
    QString confirmPrompt;
    QString toolName = name;
    if (name == "list_files") toolName = listFiles(arguments, content);
    else if (name == "read_file") toolName = readFile(arguments, content);
    else if (name == "search_files") toolName = searchFiles(arguments, content);
    else if (name == "write_file") toolName = writeFile(arguments, content, needsConfirm, confirmPrompt);
    else if (name == "run_command") toolName = runCommand(arguments, content, needsConfirm, confirmPrompt);
    else content["error"] = "unknown tool: " + name;

    if (needsConfirm) {
        // Serialize the pending action so it can be resolved after user approval.
        QJsonObject pending;
        pending["name"] = toolName;
        pending["arguments"] = arguments;
        result.needsConfirm = true;
        result.confirmPrompt = confirmPrompt;
        result.pending = QUuid::createUuid().toString(QUuid::WithoutBraces);
        pending_actions_.insert(result.pending, pending);
        result.payload = QJsonObject{{"role", "tool"}, {"tool_call_id", toolCallId},
                                     {"content", QStringLiteral("(pending user confirmation)")}};
        return result;
    }

    result.needsConfirm = false;
    result.payload = QJsonObject{{"role", "tool"}, {"tool_call_id", toolCallId},
                                 {"content", describeContent(content)}};
    return result;
}

QJsonObject AgentTools::resolvePending(const QString &pendingId, const QString &toolCallId,
                                       bool approved) {
    QJsonObject toolMessage{{"role", "tool"}, {"tool_call_id", toolCallId}};
    const QJsonObject action = pending_actions_.value(pendingId);
    pending_actions_.remove(pendingId);
    if (!approved || action.isEmpty()) {
        toolMessage["content"] = QStringLiteral("(action declined by user)");
        return toolMessage;
    }
    // Re-run the tool in "forced" mode by temporarily lowering policy to Direct/Any.
    const QString name = action["name"].toString();
    const QJsonObject arguments = action["arguments"].toObject();
    QJsonObject content;
    if (name == "write_file") {
        const auto oldWrite = writePolicy_;
        writePolicy_ = WritePolicy::Direct;
        bool nc = false; QString cp;
        writeFile(arguments, content, nc, cp);
        writePolicy_ = oldWrite;
    } else if (name == "run_command") {
        const auto oldCmd = commandPolicy_;
        commandPolicy_ = CommandPolicy::Any;
        bool nc = false; QString cp;
        runCommand(arguments, content, nc, cp);
        commandPolicy_ = oldCmd;
    }
    toolMessage["content"] = describeContent(content);
    return toolMessage;
}

QJsonArray AgentTools::toolDefinitions() {
    // Helper lambdas keep the deeply nested schema readable and avoid brace-parsing issues.
    auto prop = [](const QString &type, const QString &desc) {
        QJsonObject o;
        o["type"] = type;
        o["description"] = desc;
        return o;
    };
    auto tool = [&](const QString &name, const QString &desc,
                    const QJsonObject &properties, const QJsonArray &required) {
        QJsonObject params;
        params["type"] = "object";
        params["properties"] = properties;
        params["required"] = required;
        QJsonObject fn;
        fn["name"] = name;
        fn["description"] = desc;
        fn["parameters"] = params;
        QJsonObject entry;
        entry["type"] = "function";
        entry["function"] = fn;
        return entry;
    };

    QJsonArray tools;

    {
        QJsonObject props;
        props["path"] = prop("string", "Relative path from the working directory. Defaults to '.'");
        props["depth"] = prop("integer", "Maximum directory depth to recurse (1-6). Default 2.");
        tools.append(tool("list_files",
            "List files and directories inside the working directory. Use this to explore the project structure.",
            props, QJsonArray{}));
    }
    {
        QJsonObject props;
        props["path"] = prop("string", "Path to the file, relative to the working directory.");
        tools.append(tool("read_file", "Read the contents of a file inside the working directory.",
            props, QJsonArray{"path"}));
    }
    {
        QJsonObject props;
        props["pattern"] = prop("string", "Regular expression to match file names or content.");
        props["path"] = prop("string", "Directory to search, relative to the working directory. Defaults to '.'");
        props["content"] = prop("boolean", "If true, search inside file contents instead of file names.");
        tools.append(tool("search_files",
            "Search for files by name (regex) or by content inside the working directory.",
            props, QJsonArray{"pattern"}));
    }
    {
        QJsonObject props;
        props["path"] = prop("string", "Path to the file, relative to the working directory.");
        props["content"] = prop("string", "Full text content to write to the file.");
        tools.append(tool("write_file", "Create or overwrite a file inside the working directory.",
            props, QJsonArray{"path", "content"}));
    }
    {
        QJsonObject props;
        props["command"] = prop("string", "The command line to execute.");
        props["cwd"] = prop("string", "Working directory, relative to the working directory. Defaults to '.'");
        tools.append(tool("run_command", "Run a shell command inside the working directory and return its output.",
            props, QJsonArray{"command"}));
    }
    return tools;
}
