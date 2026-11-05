#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <functional>

// Local file/command toolkit for the AI agent. Every operation is confined to a
// user-selected directory. Approved shell commands run with the user's OS
// permissions; a working directory is not an operating-system sandbox.
class AgentTools {
  public:
    enum class CommandPolicy {
        Any,        // Run any command the model requests.
        Allowlist,  // Only commands whose first token is in the allowlist.
        Confirm     // Ask the user before running each command.
    };
    enum class WritePolicy {
        Direct,      // Overwrite or create without asking.
        Confirm,     // Ask the user before writing.
        NewOnly      // Only create files that do not exist yet.
    };

    explicit AgentTools() = default;

    void setWorkRoot(const QString &path);
    QString workRoot() const { return workRoot_; }
    bool hasWorkRoot() const { return !workRoot_.isEmpty(); }

    void setCommandPolicy(CommandPolicy policy) { commandPolicy_ = policy; }
    CommandPolicy commandPolicy() const { return commandPolicy_; }
    void setWritePolicy(WritePolicy policy) { writePolicy_ = policy; }
    WritePolicy writePolicy() const { return writePolicy_; }

    // Executes a tool call requested by the model. Returns a JSON result object
    // in the format DeepSeek expects ({"role":"tool","tool_call_id":...,"content":...}).
    // If the operation needs user confirmation first, isBlocked is set and the
    // result should not be sent to the model yet.
    struct Result {
        QJsonObject payload;   // tool message to send back to the model
        bool needsConfirm = false;
        QString confirmPrompt; // human-readable prompt for the confirmation dialog
        QString pending;       // serialized pending action, resolved via resolvePending()
    };
    Result run(const QString &toolCallId, const QString &name, const QJsonObject &arguments);

    // After the user confirms a pending action, produce the final tool message.
    QJsonObject resolvePending(const QString &pendingId, const QString &toolCallId, bool approved);

    // The OpenAI/DeepSeek "tools" array describing every callable function.
    static QJsonArray toolDefinitions();

    // Helper: absolute path of a user-supplied path, resolved inside workRoot.
    // Returns empty string if the path escapes the work root or is invalid.
    QString resolve(const QString &path) const;

  private:
    QString workRoot_;
    CommandPolicy commandPolicy_ = CommandPolicy::Confirm;
    WritePolicy writePolicy_ = WritePolicy::Confirm;

    QString listFiles(const QJsonObject &args, QJsonObject &content);
    QString readFile(const QJsonObject &args, QJsonObject &content);
    QString searchFiles(const QJsonObject &args, QJsonObject &content);
    QString writeFile(const QJsonObject &args, QJsonObject &content, bool &needsConfirm,
                      QString &confirmPrompt);
    QString runCommand(const QJsonObject &args, QJsonObject &content, bool &needsConfirm,
                       QString &confirmPrompt);

    static bool allowlistedCommand(const QString &command);
    static QString describeContent(const QJsonObject &content);

    QHash<QString, QJsonObject> pending_actions_;
};
