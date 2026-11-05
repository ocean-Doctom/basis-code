#include "chatgptpage.h"
#include "agenttools.h"
#include "devtools.h"
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLockFile>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QScrollBar>
#include <QShortcut>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextBrowser>
#include <QScrollArea>
#include <QFrame>
#include <QToolButton>
#include <QWidgetAction>
#include <QMenu>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QResizeEvent>
#include <QtMath>
#include <QPlainTextEdit>
#include <QInputDialog>
#include <QRegularExpression>
#include <QThread>
#include <QNetworkProxyFactory>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>
#include <functional>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#endif

namespace {
QByteArray formBytes(const QUrlQuery &form) {
    QByteArray bytes;
    for (const auto &item : form.queryItems(QUrl::FullyDecoded)) {
        if (!bytes.isEmpty()) bytes += '&';
        bytes += QUrl::toPercentEncoding(item.first) + '=' + QUrl::toPercentEncoding(item.second);
    }
    return bytes;
}
QByteArray randomUrlToken() {
    QByteArray bytes(32, Qt::Uninitialized);
    for (int i = 0; i < bytes.size(); ++i)
        bytes[i] = char(QRandomGenerator::system()->generate() & 255);
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
QByteArray protectedBytes(const QByteArray &bytes, bool encrypt) {
#ifdef Q_OS_WIN
    DATA_BLOB input{DWORD(bytes.size()), reinterpret_cast<BYTE *>(const_cast<char *>(bytes.constData()))};
    DATA_BLOB output{};
    const bool ok = encrypt
        ? CryptProtectData(&input, L"Basis Code ChatGPT", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)
        : CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    if (!ok) return {};
    const QByteArray result(reinterpret_cast<const char *>(output.pbData), int(output.cbData));
    LocalFree(output.pbData);
    return result;
#else
    Q_UNUSED(bytes); Q_UNUSED(encrypt);
    return {};
#endif
}
QJsonObject tokenClaims(const QByteArray &jwt) {
    const auto pieces = jwt.split('.');
    return pieces.size() == 3 ? QJsonDocument::fromJson(QByteArray::fromBase64(pieces[1], QByteArray::Base64UrlEncoding)).object() : QJsonObject{};
}
bool verifyIdentity(const QByteArray &jwt, const QJsonArray &keys, const QString &client,
                    const QString &nonce, const QString &subject) {
    const auto pieces = jwt.split('.');
    if (pieces.size() != 3) return false;
    const auto header = QJsonDocument::fromJson(QByteArray::fromBase64(pieces[0], QByteArray::Base64UrlEncoding)).object();
    const auto claims = tokenClaims(jwt);
    const auto audience = claims["aud"];
    bool rightAudience = audience.toString() == client;
    for (const auto &item : audience.toArray()) rightAudience |= item.toString() == client;
    if (header["alg"].toString() != "RS256" || !rightAudience ||
        (claims.contains("azp") && claims["azp"].toString() != client) ||
        claims["iss"].toString() != "https://auth.openai.com" ||
        claims["exp"].toDouble() <= QDateTime::currentSecsSinceEpoch() ||
        claims["sub"].toString().isEmpty() ||
        (!nonce.isEmpty() && claims["nonce"].toString() != nonce) ||
        (!subject.isEmpty() && claims["sub"].toString() != subject))
        return false;
#ifdef Q_OS_WIN
    for (const auto &item : keys) {
        const auto key = item.toObject();
        if (key["kid"] != header["kid"] || key["kty"].toString() != "RSA") continue;
        const auto modulus = QByteArray::fromBase64(key["n"].toString().toLatin1(), QByteArray::Base64UrlEncoding);
        const auto exponent = QByteArray::fromBase64(key["e"].toString().toLatin1(), QByteArray::Base64UrlEncoding);
        if (modulus.isEmpty() || exponent.isEmpty()) return false;
        BCRYPT_RSAKEY_BLOB keyHeader{BCRYPT_RSAPUBLIC_MAGIC, ULONG(modulus.size() * 8),
                                   ULONG(exponent.size()), ULONG(modulus.size()), 0, 0};
        QByteArray blob(reinterpret_cast<const char *>(&keyHeader), sizeof(keyHeader));
        blob += exponent; blob += modulus;
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_KEY_HANDLE handle = nullptr;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0) < 0) return false;
        const auto imported = BCryptImportKeyPair(algorithm, nullptr, BCRYPT_RSAPUBLIC_BLOB, &handle,
            reinterpret_cast<PUCHAR>(blob.data()), ULONG(blob.size()), 0);
        bool valid = false;
        if (imported >= 0) {
            auto digest = QCryptographicHash::hash(pieces[0] + "." + pieces[1], QCryptographicHash::Sha256);
            auto signature = QByteArray::fromBase64(pieces[2], QByteArray::Base64UrlEncoding);
            BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
            valid = BCryptVerifySignature(handle, &padding, reinterpret_cast<PUCHAR>(digest.data()), ULONG(digest.size()),
                reinterpret_cast<PUCHAR>(signature.data()), ULONG(signature.size()), BCRYPT_PAD_PKCS1) >= 0;
            BCryptDestroyKey(handle);
        }
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return valid;
    }
#else
    Q_UNUSED(keys);
#endif
    return false;
}
}

class MessageText : public QTextBrowser {
  public:
    explicit MessageText(QWidget *parent) : QTextBrowser(parent) {
        setObjectName("chatMessageText");
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setOpenLinks(false);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        document()->setDocumentMargin(0);
        connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
                [this] { updateHeight(); });
        connect(this, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
            if (url.scheme() == "https" || url.scheme() == "http") QDesktopServices::openUrl(url);
        });
    }
  protected:
    void resizeEvent(QResizeEvent *event) override {
        QTextBrowser::resizeEvent(event);
        document()->setTextWidth(viewport()->width());
        updateHeight();
    }
  private:
    void updateHeight() {
        const int target = qMax(fontMetrics().height() + 2, qCeil(document()->size().height()) + 3);
        if (height() != target) setFixedHeight(target);
    }
};
class ChatMessage : public QWidget {
  public:
    ChatMessage(bool user, QWidget *parent) : QWidget(parent), user(user) {
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(0, 5, 0, 15);
        if (user) row->addStretch(1);
        auto *bubble = new QFrame(this);
        bubble->setObjectName(user ? "chatUserBubble" : "chatAssistantBubble");
        if (user) bubble->setMaximumWidth(610);
        auto *layout = new QVBoxLayout(bubble);
        layout->setContentsMargins(user ? 18 : 0, user ? 13 : 4, user ? 18 : 0, user ? 13 : 4);
        text = new MessageText(bubble);
        layout->addWidget(text);
        row->addWidget(bubble, user ? 4 : 1);
    }
    void setContent(const QString &value) {
        if (value == content) return;
        content = value;
        if (user) text->setPlainText(value); else text->setMarkdown(value);
    }
  private:
    bool user;
    QString content;
    MessageText *text;
};

struct ChatGptPage::State {
    ChatGptPage *page;
    QString provider = "chatgpt";
    bool deepSeek() const { return provider == "deepseek"; }
    QString agentName() const { return deepSeek() ? "DeepSeek" : "ChatGPT"; }
    QNetworkAccessManager network;
    QTcpServer listener;
    QPointer<QNetworkReply> response;
    QTimer loginTimeout;
    QTimer streamRefresh;
    QComboBox *accounts;
    QComboBox *models;
    QPushButton *login;
    QPushButton *send;
    QLabel *status;
    QScrollArea *transcript;
    QVBoxLayout *messagesLayout;
    QList<ChatMessage *> messages;
    QVBoxLayout *columnLayout;
    QWidget *welcome;
    QToolButton *accountButton;
    QPushButton *stop;
    QPlainTextEdit *composer;
    QJsonObject records;
    QString activeId;
    QString directory;
    QString hostId;
    QString clientId;
    QString callbackUri;
    QByteArray oauthState, verifier, nonce;
    QByteArray streamBuffer;
    QJsonArray history;
    QString draft, answer;
    bool completed = false;
    bool changingAccounts = false;
    std::unique_ptr<QLockFile> credentialLock;

    // --- AI agent (function calling) state ---
    AgentTools tools;
    QLabel *workDirLabel = nullptr;
    QComboBox *commandPolicy = nullptr;
    QComboBox *writePolicy = nullptr;
    QJsonArray agentMessages;   // conversation history for the agent loop
    QByteArray agentStreamBuffer;
    QString agentAnswer;
    QString activeToolCallId;
    QString pendingActionId;
    QPointer<QNetworkReply> agentReply;
    int agentIterations = 0;
    bool agentCancelled = false;
    bool toolRunning = false;

    void runToolJob(std::function<QJsonObject(AgentTools &)> operation,
                    std::function<void(const QJsonObject &)> done) {
        toolRunning = true;
        stop->show();
        auto copy = std::make_shared<AgentTools>(tools);
        auto result = std::make_shared<QJsonObject>();
        auto *worker = QThread::create([copy, result, operation] { *result = operation(*copy); });
        QObject::connect(worker, &QThread::finished, page, [this, copy, result, done] {
            toolRunning = false;
            tools = *copy;
            if (agentCancelled) { busy(false); status->setText(QObject::tr("智能体已停止。")); return; }
            done(*result);
        });
        QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        worker->start();
    }

    explicit State(ChatGptPage *owner) : page(owner) {
        streamRefresh.setSingleShot(true);
        streamRefresh.setInterval(50);
        streamRefresh.setTimerType(Qt::PreciseTimer);
        QObject::connect(&streamRefresh, &QTimer::timeout, page, [this] { display(); });
    }
    QJsonObject account() const { return records[activeId].toObject(); }
    void busy(bool value) {
        login->setEnabled(!value);
        accounts->setEnabled(!value);
        send->setEnabled(!value && models->count() > 0);
        composer->setReadOnly(value);
        models->setEnabled(!value && models->count() > 0);
        stop->setVisible(value && (response || agentReply || toolRunning));
        accountButton->setEnabled(!value);
    }
    bool lockCredentials() {
        credentialLock = std::make_unique<QLockFile>(directory + "/credentials.lock");
        if (credentialLock->tryLock(0)) return true;
        status->setText(QObject::tr("另一个窗口正在更新登录，请稍后重试。"));
        credentialLock.reset();
        return false;
    }
    bool saveRecords() {
        QJsonObject root{{"active", activeId}, {"accounts", records}};
        const auto bytes = protectedBytes(QJsonDocument(root).toJson(QJsonDocument::Compact), true);
        QSaveFile file(directory + "/credentials.bin");
        const bool ok = !bytes.isEmpty() && file.open(QIODevice::WriteOnly) &&
                        file.write(bytes) == bytes.size() && file.commit();
        if (!ok) status->setText(QObject::tr("无法安全保存登录信息，请检查应用数据目录。"));
        return ok;
    }
    void loadRecords() {
        QFile file(directory + "/credentials.bin");
        if (!file.open(QIODevice::ReadOnly)) return;
        const auto root = QJsonDocument::fromJson(protectedBytes(file.readAll(), false)).object();
        records = root["accounts"].toObject();
        activeId = root["active"].toString();
    }
    void request(const QUrl &url, const QByteArray &body,
                 std::function<void(QJsonObject, int)> result, bool form = false,
                 const QByteArray &bearer = {}) {
        QNetworkRequest request(url);
        request.setTransferTimeout(45000);
        request.setRawHeader("User-Agent", "Basis-Code");
        if (!bearer.isEmpty()) request.setRawHeader("Authorization", "Bearer " + bearer);
        request.setHeader(QNetworkRequest::ContentTypeHeader, form ? "application/x-www-form-urlencoded" : "application/json");
        auto *reply = body.isNull() ? network.get(request) : network.post(request, body);
        QObject::connect(reply, &QNetworkReply::finished, page, [this, reply, result] {
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto object = QJsonDocument::fromJson(reply->readAll()).object();
            const QString failure = reply->error() == QNetworkReply::NoError ? QString() :
                QObject::tr("请求 %1 失败：HTTP %2，网络错误 %3（%4）。请检查系统代理、网络连通性及账号授权。")
                    .arg(reply->url().host()).arg(code).arg(int(reply->error())).arg(reply->errorString());
            reply->deleteLater();
            result(object, code);
            if (!failure.isEmpty()) status->setText(failure);
        });
    }
    void display() {
        streamRefresh.stop();
        QJsonArray entries = history;
        if (!draft.isEmpty()) {
            entries.append(QJsonObject{{"role", "user"}, {"content", draft}});
            entries.append(QJsonObject{{"role", "assistant"}, {"content", answer.isEmpty() ? QString(QChar(0x2026)) : answer}});
        }
        const bool hasConversation = !entries.isEmpty();
        welcome->setVisible(!hasConversation);
        transcript->setVisible(hasConversation);
        columnLayout->setStretch(0, hasConversation ? 0 : 1);
        columnLayout->setStretch(columnLayout->count() - 1, hasConversation ? 0 : 2);
        const bool follow = transcript->verticalScrollBar()->maximum() - transcript->verticalScrollBar()->value() < 45;
        while (messages.size() > entries.size()) {
            auto *message = messages.takeLast();
            messagesLayout->removeWidget(message);
            delete message;
        }
        for (int index = 0; index < entries.size(); ++index) {
            const auto item = entries[index].toObject();
            if (index >= messages.size()) {
                auto *message = new ChatMessage(item["role"].toString() == "user", transcript->widget());
                messages.append(message);
                messagesLayout->insertWidget(messagesLayout->count() - 1, message);
            }
            messages[index]->setContent(item["content"].toString());
        }
        if (follow) QTimer::singleShot(0, page, [this] {
            transcript->verticalScrollBar()->setValue(transcript->verticalScrollBar()->maximum());
        });
    }
    void accountList() {
        changingAccounts = true;
        accounts->clear();
        for (auto it = records.begin(); it != records.end(); ++it) {
            const auto entry = it.value().toObject();
            accounts->addItem(entry["email"].toString() + " · " + entry["client_id"].toString().right(8), it.key());
        }
        accounts->setCurrentIndex(accounts->findData(activeId));
        changingAccounts = false;
        accountButton->setText(account()["access_token"].toString().isEmpty() ? (deepSeek() ? QObject::tr("配置密钥") : QObject::tr("登录")) : QObject::tr("账户"));
        history = account()["history"].toArray();
        draft.clear(); answer.clear(); display();
    }
    void loadModels() {
        models->clear();
        const auto access = account()["access_token"].toString().toUtf8();
        if (access.isEmpty()) { busy(false); status->setText(deepSeek() ? QObject::tr("首次使用请配置 DeepSeek API Key。") : QObject::tr("首次使用请登录 ChatGPT。")); return; }
        busy(true);
        status->setText(QObject::tr("正在获取可用模型…"));
        request(QUrl(deepSeek() ? "https://api.deepseek.com/models" : "https://api.openai.com/v1/models"), {}, [this](const QJsonObject &object, int code) {
            if (code == 200) {
                for (const auto &entry : object[deepSeek() ? "data" : "models"].toArray()) {
                    const auto model = entry.toObject();
                    if (deepSeek()) models->addItem(model["name"].toString(model["id"].toString()), model["id"].toString());
                    else if (model["visibility"].toString() == "list")
                        models->addItem(model["display_name"].toString(), model["slug"].toString());
                }
            }
            status->setText(code == 200 && models->count() ? QObject::tr("已登录，可以开始聊天。")
                : (deepSeek() ? QObject::tr("无法获取模型，请检查 API Key 与网络后重新配置。") : QObject::tr("暂时无法获取模型，请检查账号授权与网络后重新登录。")));
            busy(false);
        }, false, access);
    }
    void ensureSession(std::function<void()> next) {
        if (deepSeek()) {
            if (!account()["access_token"].toString().isEmpty()) next();
            else { status->setText(QObject::tr("请先配置 DeepSeek API Key。")); busy(false); }
            return;
        }
        if (account()["expires_at"].toDouble() > QDateTime::currentSecsSinceEpoch() + 90) { next(); return; }
        if (account()["refresh_token"].toString().isEmpty() || !lockCredentials()) {
            status->setText(QObject::tr("请登录 ChatGPT 以继续。")); busy(false); return;
        }
        // Reload under the cross-process lock before using a rotating refresh token.
        const QString chosen = activeId;
        loadRecords(); activeId = chosen;
        auto saved = account();
        if (saved["expires_at"].toDouble() > QDateTime::currentSecsSinceEpoch() + 90) {
            credentialLock.reset(); next(); return;
        }
        QUrlQuery form;
        form.addQueryItem("grant_type", "refresh_token");
        form.addQueryItem("client_id", saved["client_id"].toString());
        form.addQueryItem("refresh_token", saved["refresh_token"].toString());
        form.addQueryItem("resource", "https://api.openai.com/v1");
        busy(true);
        request(QUrl("https://auth.openai.com/api/accounts/oauth/token"), formBytes(form),
            [this, saved, next](const QJsonObject &tokens, int code) mutable {
                if (code != 200 || tokens["access_token"].toString().isEmpty() || tokens["refresh_token"].toString().isEmpty()) {
                    credentialLock.reset(); busy(false);
                    status->setText(QObject::tr("登录续期失败，请重新登录 ChatGPT。")); return;
                }
                saved["access_token"] = tokens["access_token"];
                saved["refresh_token"] = tokens["refresh_token"];
                saved["expires_at"] = double(QDateTime::currentSecsSinceEpoch()) + tokens["expires_in"].toDouble();
                if (tokens.contains("scope")) {
                    const auto granted = tokens["scope"].toString().split(' ');
                    if (!granted.contains("chatgpt.tokens.use.direct") || !granted.contains("resource.invoke")) {
                        credentialLock.reset(); busy(false);
                        status->setText(QObject::tr("ChatGPT 使用授权已变化，请重新登录。")); return;
                    }
                    saved["scope"] = tokens["scope"];
                }
                // Keep the previously validated ID token as a returning-login hint.
                // The renewed access token is issued by the authenticated TLS token endpoint.
                records[activeId] = saved;
                const bool stored = saveRecords();
                credentialLock.reset();
                if (stored) next(); else busy(false);
            }, true);
    }
    void finishLogin(const QJsonObject &tokens, const QString &issued) {
        const auto jwt = tokens["id_token"].toString().toUtf8();
        request(QUrl("https://auth.openai.com/.well-known/jwks.json"), {}, [this, tokens, jwt, issued](const QJsonObject &jwks, int code) {
            const QString originalSubject = clientId == "dynamic_agent_client" ? QString() : account()["subject"].toString();
            const auto scopes = tokens["scope"].toString().split(' ');
            if (code != 200 || !verifyIdentity(jwt, jwks["keys"].toArray(), issued, QString::fromLatin1(nonce), originalSubject) ||
                !scopes.contains("chatgpt.tokens.use.direct") || !scopes.contains("resource.invoke") ||
                tokens["access_token"].toString().isEmpty()) {
                credentialLock.reset(); busy(false);
                status->setText(QObject::tr("登录身份或 ChatGPT 使用授权未通过验证，请重新登录。")); return;
            }
            const auto identity = tokenClaims(jwt);
            const QString id = QString::fromLatin1(QCryptographicHash::hash(
                (issued + ":" + identity["sub"].toString()).toUtf8(), QCryptographicHash::Sha256).toHex());
            QJsonObject record = records[id].toObject();
            record["email"] = identity["email"];
            record["subject"] = identity["sub"];
            record["client_id"] = issued;
            record["id_token"] = tokens["id_token"];
            record["access_token"] = tokens["access_token"];
            record["refresh_token"] = tokens["refresh_token"];
            record["scope"] = tokens["scope"];
            record["expires_at"] = double(QDateTime::currentSecsSinceEpoch()) + tokens["expires_in"].toDouble();
            records[id] = record; activeId = id;
            const bool stored = saveRecords();
            credentialLock.reset();
            if (!stored) { busy(false); return; }
            accountList(); loadModels();
        });
    }
    void loginAccount(bool another) {
        if (deepSeek()) { configureApiKey(); return; }
        if (listener.isListening()) return;
        if (!lockCredentials()) return;
        const QString selected = activeId;
        loadRecords(); activeId = selected;
        if (!listener.listen(QHostAddress::LocalHost, 0)) {
            credentialLock.reset(); status->setText(QObject::tr("无法启动本地登录回调。")); return;
        }
        clientId = another || activeId.isEmpty() ? "dynamic_agent_client" : account()["client_id"].toString();
        if (clientId.isEmpty()) clientId = "dynamic_agent_client";
        oauthState = randomUrlToken(); nonce = randomUrlToken(); verifier = randomUrlToken();
        callbackUri = "http://127.0.0.1:" + QString::number(listener.serverPort()) + "/auth/callback";
        QUrl url("https://auth.openai.com/api/accounts/authorize");
        QUrlQuery query;
        query.addQueryItem("client_id", clientId);
        if (clientId == "dynamic_agent_client") query.addQueryItem("agent_name_hint", "Basis Code");
        else if (!account()["id_token"].toString().isEmpty()) query.addQueryItem("id_token_hint", account()["id_token"].toString());
        query.addQueryItem("ext_agent_host_id", hostId);
        query.addQueryItem("response_type", "code");
        query.addQueryItem("redirect_uri", callbackUri);
        query.addQueryItem("scope", "openid profile email offline_access resource.invoke chatgpt.tokens.use.direct");
        query.addQueryItem("resource", "https://api.openai.com/v1");
        query.addQueryItem("state", QString::fromLatin1(oauthState));
        query.addQueryItem("nonce", QString::fromLatin1(nonce));
        query.addQueryItem("code_challenge_method", "S256");
        query.addQueryItem("code_challenge", QString::fromLatin1(QCryptographicHash::hash(verifier, QCryptographicHash::Sha256)
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
        url.setQuery(query);
        busy(true);
        status->setText(QObject::tr("请在浏览器完成首次登录和授权。"));
        loginTimeout.start(300000);
        if (!QDesktopServices::openUrl(url)) {
            listener.close(); loginTimeout.stop(); credentialLock.reset(); busy(false);
            status->setText(QObject::tr("无法打开系统浏览器。"));
        }
    }
    void handleConnection() {
        while (listener.hasPendingConnections()) {
            auto *socket = listener.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, page, [this, socket, buffer] {
                *buffer += socket->readAll();
                if (buffer->size() > 16384) { socket->disconnectFromHost(); return; }
                if (!buffer->contains("\r\n\r\n")) return;
                const auto pieces = buffer->split(' ');
                if (pieces.size() < 2) { socket->disconnectFromHost(); return; }
                const QUrl received("http://127.0.0.1" + QString::fromUtf8(pieces[1]));
                const QUrlQuery query(received);
                if (received.path() != "/auth/callback" || query.queryItemValue("state") != QString::fromLatin1(oauthState) ||
                    !listener.isListening()) {
                    socket->write("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nInvalid callback");
                    socket->disconnectFromHost(); return;
                }
                listener.close(); loginTimeout.stop();
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
                              "<h2>Basis Code</h2><p>You can return to Basis Code.</p>");
                socket->disconnectFromHost();
                const QString code = query.queryItemValue("code");
                const QString issued = query.queryItemValue("client_id", QUrl::FullyDecoded).isEmpty()
                    ? clientId : query.queryItemValue("client_id", QUrl::FullyDecoded);
                if (query.hasQueryItem("error") || code.isEmpty() || issued == "dynamic_agent_client" ||
                    (clientId != "dynamic_agent_client" && issued != clientId)) {
                    credentialLock.reset(); busy(false); status->setText(QObject::tr("登录未获授权，请重试。")); return;
                }
                QUrlQuery form;
                form.addQueryItem("grant_type", "authorization_code");
                form.addQueryItem("client_id", issued);
                form.addQueryItem("code", code);
                form.addQueryItem("code_verifier", QString::fromLatin1(verifier));
                form.addQueryItem("redirect_uri", callbackUri);
                form.addQueryItem("resource", "https://api.openai.com/v1");
                request(QUrl("https://auth.openai.com/api/accounts/oauth/token"), formBytes(form),
                    [this, issued](const QJsonObject &tokens, int result) {
                        if (result == 200) finishLogin(tokens, issued);
                        else { credentialLock.reset(); busy(false); status->setText(QObject::tr("登录交换失败，请重新授权。")); }
                    }, true);
            });
        }
    }
    void configureApiKey() {
        bool accepted = false;
        const auto key = QInputDialog::getText(page, QObject::tr("配置 DeepSeek API Key"),
            QObject::tr("输入在 DeepSeek 官方平台创建的 API Key"), QLineEdit::Password, {}, &accepted).trimmed();
        if (!accepted || key.isEmpty()) return;
        if (key.contains(QRegularExpression("\\s"))) {
            status->setText(QObject::tr("API Key 中不能包含空白字符。")); return;
        }
        if (!lockCredentials()) return;
        loadRecords();
        const QString id = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
        auto record = records[id].toObject();
        record["client_id"] = "api-" + id.right(8);
        record["email"] = "DeepSeek API";
        record["access_token"] = key;
        records[id] = record; activeId = id;
        const bool saved = saveRecords();
        credentialLock.reset();
        if (saved) { accountList(); loadModels(); }
    }
    void consumeStream() {
        if (!response) return;
        consumeStreamBytes(response->readAll());
    }
    void scheduleStreamDisplay() {
        if (!streamRefresh.isActive()) streamRefresh.start();
    }
    void consumeStreamBytes(const QByteArray &bytes) {
        streamBuffer += bytes;
        streamBuffer.replace("\r\n", "\n");
        int boundary;
        while ((boundary = streamBuffer.indexOf("\n\n")) >= 0) {
            const auto frame = streamBuffer.left(boundary);
            streamBuffer.remove(0, boundary + 2);
            QByteArray data;
            for (const auto &line : frame.split('\n'))
                if (line.startsWith("data:")) data += line.mid(5).trimmed();
            if (deepSeek() && data == "[DONE]") { completed = true; continue; }
            const auto event = QJsonDocument::fromJson(data).object();
            if (deepSeek()) {
                for (const auto &entry : event["choices"].toArray()) {
                    const auto choice = entry.toObject();
                    const auto delta = choice["delta"].toObject();
                    if (!delta["reasoning_content"].toString().isEmpty()) status->setText(QObject::tr("DeepSeek 正在思考…"));
                    answer += delta["content"].toString();
                    if (!delta["content"].toString().isEmpty()) scheduleStreamDisplay();
                }
                continue;
            }
            const QString type = event["type"].toString();
            if (type == "response.output_text.delta" || type == "response.refusal.delta") { answer += event["delta"].toString(); scheduleStreamDisplay(); }
            else if (type == "response.completed") completed = true;
            else if (type == "response.failed" || type == "error")
                status->setText(QObject::tr("请求未完成，请检查账号配额或稍后重试。"));
        }
    }
    void sendMessage() {
        if (response || composer->toPlainText().trimmed().isEmpty()) return;
        busy(true);
        ensureSession([this] {
            draft = composer->toPlainText().trimmed();
            if (draft.isEmpty()) { busy(false); return; }
            // DeepSeek agent mode: use function calling when a work directory is chosen.
            if (deepSeek() && tools.hasWorkRoot()) { startAgentTurn(); return; }
            answer.clear(); completed = false; streamBuffer.clear();
            QJsonArray input = history;
            input.append(QJsonObject{{"role", "user"}, {"content", draft}});
            QJsonObject payload{{"model", models->currentData().toString()}, {"input", input},
                                {"store", false}, {"stream", true}};
            if (deepSeek()) payload = QJsonObject{{"model", models->currentData().toString()},
                {"messages", input}, {"stream", true}, {"thinking", QJsonObject{{"type", "disabled"}}}};
            QNetworkRequest request(QUrl(deepSeek() ? "https://api.deepseek.com/chat/completions" : "https://api.openai.com/v1/responses"));
            request.setTransferTimeout(120000);
            request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            request.setRawHeader("Authorization", "Bearer " + account()["access_token"].toString().toUtf8());
            response = network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
            stop->show();
            status->setText(QObject::tr("正在回复…")); display();
            QObject::connect(response, &QNetworkReply::readyRead, page, [this] { consumeStream(); });
            QObject::connect(response, &QNetworkReply::finished, page, [this] {
                consumeStream();
                auto *reply = response.data(); response = nullptr;
                const int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const bool success = completed && reply->error() == QNetworkReply::NoError;
                reply->deleteLater();
                if (success) {
                    history.append(QJsonObject{{"role", "user"}, {"content", draft}});
                    history.append(QJsonObject{{"role", "assistant"}, {"content", answer}});
                    composer->clear(); draft.clear(); answer.clear();
                    if (lockCredentials()) {
                        const QString chosen = activeId; loadRecords(); activeId = chosen;
                        auto record = account(); record["history"] = history; records[activeId] = record;
                        saveRecords(); credentialLock.reset();
                    }
                    status->setText(QObject::tr("回复完成。"));
                } else {
                    status->setText(httpCode == 401 ? (deepSeek() ? QObject::tr("API Key 无效，请重新配置。") : QObject::tr("登录授权已失效，请重新登录后重试。"))
                        : httpCode == 403 ? QObject::tr("当前账号未获此模型的使用权限，请检查授权。")
                        : QObject::tr("回复中断，可保留输入后重试。"));
                }
                display();
                busy(false);
            });
        });
    }
    // --- AI agent (function calling) loop ---
    void startAgentTurn() {
        agentMessages = history;
        agentMessages.append(QJsonObject{{"role", "user"}, {"content", draft}});
        agentIterations = 0;
        agentCancelled = false;
        answer.clear();
        display();
        continueAgentTurn();
    }
    void continueAgentTurn() {
        if (agentCancelled) { busy(false); return; }
        if (++agentIterations > 20) {
            finishAgentTurn(QStringLiteral("智能体循环次数过多，已停止。"));
            return;
        }
        QJsonObject payload{{"model", models->currentData().toString()},
                            {"messages", agentMessages},
                            {"tools", AgentTools::toolDefinitions()},
                            {"stream", false}};
        QNetworkRequest request(QUrl("https://api.deepseek.com/chat/completions"));
        request.setTransferTimeout(120000);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", "Bearer " + account()["access_token"].toString().toUtf8());
        agentReply = network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        stop->show();
        status->setText(QObject::tr("智能体思考中…"));
        QObject::connect(agentReply, &QNetworkReply::finished, page, [this] {
            auto *reply = agentReply.data(); agentReply = nullptr;
            const int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
            reply->deleteLater();
            if (agentCancelled) { busy(false); status->setText(QObject::tr("智能体已停止。")); return; }
            if (httpCode != 200) {
                finishAgentTurn(httpCode == 401 ? QObject::tr("API Key 无效。") : QObject::tr("请求失败（%1）。").arg(httpCode));
                return;
            }
            const auto choices = object["choices"].toArray();
            if (choices.isEmpty()) { busy(false); status->setText(QObject::tr("服务未返回有效回复，请重试。")); return; }
            const auto choice = choices.first().toObject();
            const auto message = choice["message"].toObject();
            const auto toolCalls = message["tool_calls"].toArray();
            // Append the assistant message (with any tool_calls) to history.
            agentMessages.append(message);
            if (!toolCalls.isEmpty()) {
                handleToolCalls(toolCalls);
                return;
            }
            // Final answer.
            const QString content = message["content"].toString();
            finishAgentTurn(content.isEmpty() ? QStringLiteral("(无回复)") : content);
        });
    }
    void handleToolCalls(const QJsonArray &toolCalls) {
        // Process tool calls sequentially (simplest correct approach).
        struct Pending { QJsonObject call; };
        QList<QJsonObject> calls;
        for (const auto &item : toolCalls) calls.append(item.toObject());
        processNextToolCall(calls, 0);
    }
    void processNextToolCall(const QList<QJsonObject> &calls, int index) {
        if (agentCancelled) { busy(false); return; }
        if (index >= calls.size()) { continueAgentTurn(); return; }
        const auto call = calls[index];
        const QString callId = call["id"].toString();
        const auto fn = call["function"].toObject();
        const QString name = fn["name"].toString();
        const QJsonObject arguments = QJsonDocument::fromJson(fn["arguments"].toString().toUtf8()).object();
        status->setText(QObject::tr("智能体正在调用工具：%1…").arg(name));
        // Ask for permissions on the UI thread; execute approved work on a
        // private worker so filesystem scans and compilers cannot freeze typing.
        AgentTools probe = tools;
        probe.setCommandPolicy(AgentTools::CommandPolicy::Confirm);
        probe.setWritePolicy(AgentTools::WritePolicy::Confirm);
        const bool mustConfirm = (name == "run_command" && tools.commandPolicy() == AgentTools::CommandPolicy::Confirm) ||
                                 (name == "write_file" && tools.writePolicy() == AgentTools::WritePolicy::Confirm);
        if (!mustConfirm) {
            runToolJob([callId, name, arguments](AgentTools &workerTools) {
                return workerTools.run(callId, name, arguments).payload;
            }, [this, calls, index](const QJsonObject &message) {
                agentMessages.append(message); processNextToolCall(calls, index + 1);
            });
            return;
        }
        const auto result = probe.run(callId, name, arguments);
        if (result.needsConfirm) {
            pendingActionId = result.pending;
            activeToolCallId = callId;
            // Ask the user via a modal dialog.
            const auto choice = QMessageBox::question(page, QObject::tr("智能体请求确认"),
                result.confirmPrompt, QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            const bool approved = choice == QMessageBox::Yes;
            probe.setCommandPolicy(tools.commandPolicy());
            probe.setWritePolicy(tools.writePolicy());
            tools = probe;
            const auto pending = pendingActionId;
            runToolJob([pending, callId, approved](AgentTools &workerTools) {
                return workerTools.resolvePending(pending, callId, approved);
            }, [this, calls, index](const QJsonObject &message) {
                agentMessages.append(message); processNextToolCall(calls, index + 1);
            });
            return;
        }
        agentMessages.append(result.payload);
        processNextToolCall(calls, index + 1);
    }
    void finishAgentTurn(const QString &content) {
        agentAnswer = content;
        history.append(QJsonObject{{"role", "user"}, {"content", draft}});
        history.append(QJsonObject{{"role", "assistant"}, {"content", content}});
        composer->clear();
        if (lockCredentials()) {
            const QString chosen = activeId; loadRecords(); activeId = chosen;
            auto record = account(); record["history"] = history; records[activeId] = record;
            saveRecords(); credentialLock.reset();
        }
        answer = content;
        draft.clear();
        display();
        status->setText(QObject::tr("智能体回复完成。"));
        busy(false);
    }
};

ChatGptPage::ChatGptPage(QWidget *parent, const QString &provider) : QWidget(parent), state(std::make_unique<State>(this)) {
    QNetworkProxyFactory::setUseSystemConfiguration(true);
    state->provider = provider;
    setObjectName(state->deepSeek() ? "deepseekPage" : "chatgptPage");
    setProperty("toolkitName", state->agentName());
    state->directory = DevTools::dataRoot() + "/extensions/basis." + provider;
    QDir().mkpath(state->directory);
    QFile host(state->directory + "/host-id");
    if (host.open(QIODevice::ReadOnly)) state->hostId = QString::fromUtf8(host.readAll()).trimmed();
    if (state->hostId.isEmpty()) {
        state->hostId = "urn:uuid:" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        QSaveFile file(host.fileName());
        if (file.open(QIODevice::WriteOnly)) { file.write(state->hostId.toUtf8()); file.commit(); }
    }
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 14);
    layout->setSpacing(8);
    auto *header = new QHBoxLayout;
    state->models = new QComboBox(this);
    state->models->setObjectName("chatgptModels");
    state->models->setPlaceholderText(state->agentName());
    state->models->setMinimumWidth(150);
    state->models->setMaximumWidth(330);
    header->addWidget(state->models);
    auto *clear = new QPushButton(QString(QChar(0xff0b)), this);
    clear->setObjectName("chatgptNewChat");
    clear->setFixedSize(34, 34);
    clear->setToolTip(tr("新对话"));
    clear->setAccessibleName(tr("新对话"));
    header->addWidget(clear);
    header->addStretch();
    state->accountButton = new QToolButton(this);
    state->accountButton->setObjectName("chatgptAccountButton");
    state->accountButton->setText(tr("登录"));
    state->accountButton->setPopupMode(QToolButton::InstantPopup);
    auto *accountMenu = new QMenu(state->accountButton);
    auto *accountPanel = new QWidget(accountMenu);
    auto *accountLayout = new QVBoxLayout(accountPanel);
    accountLayout->setContentsMargins(14, 14, 14, 14);
    accountLayout->setSpacing(10);
    state->accounts = new QComboBox(accountPanel);
    state->accounts->setObjectName("chatgptAccounts");
    state->accounts->setMinimumWidth(240);
    state->accounts->setPlaceholderText(tr("ChatGPT 账户"));
    accountLayout->addWidget(state->accounts);
    state->login = new QPushButton(state->deepSeek() ? tr("配置 API Key") : "Continue with ChatGPT", accountPanel);
    accountLayout->addWidget(state->login);
    auto *other = new QPushButton(state->deepSeek() ? tr("添加其他密钥") : tr("添加其他账号"), accountPanel);
    accountLayout->addWidget(other);
    auto *logout = new QPushButton(state->deepSeek() ? tr("移除本机密钥") : tr("退出账号"), accountPanel);
    logout->setObjectName("agentLogout");
    accountLayout->addWidget(logout);
    auto *diagnose = new QPushButton(tr("检查服务连接"), accountPanel);
    diagnose->setObjectName("agentCheckConnection");
    accountLayout->addWidget(diagnose);
    connect(diagnose, &QPushButton::clicked, this, [this] {
        state->busy(true);
        state->status->setText(tr("正在检查服务连接（不发送密钥或聊天内容）…"));
        const QUrl url(state->deepSeek() ? "https://api.deepseek.com/models" :
                       "https://auth.openai.com/.well-known/openid-configuration");
        QNetworkRequest request(url); request.setTransferTimeout(15000);
        auto *reply = state->network.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            state->busy(false);
            if (code == 200 || (state->deepSeek() && code == 401))
                state->status->setText(tr("服务连接正常（HTTP %1）。此检查未验证账号登录或模型使用权限。").arg(code));
            else state->status->setText(tr("%1 连接检查失败：HTTP %2，Qt 网络错误 %3（%4）。请检查系统代理或服务访问限制。")
                .arg(reply->url().host()).arg(code).arg(int(reply->error())).arg(reply->errorString()));
            reply->deleteLater();
        });
    });
    // AI agent controls (DeepSeek only).
    if (state->deepSeek()) {
        auto *agentSep = new QFrame(accountPanel);
        agentSep->setFrameShape(QFrame::HLine);
        agentSep->setObjectName("agentSeparator");
        accountLayout->addWidget(agentSep);
        auto *agentTitle = new QLabel(tr("AI 智能体（本地工具）"), accountPanel);
        agentTitle->setObjectName("agentTitle");
        accountLayout->addWidget(agentTitle);
        auto *chooseDir = new QPushButton(tr("选择工作目录…"), accountPanel);
        chooseDir->setObjectName("agentChooseDir");
        accountLayout->addWidget(chooseDir);
        state->workDirLabel = new QLabel(tr("未选择（仅对话）"), accountPanel);
        state->workDirLabel->setObjectName("agentWorkDir");
        state->workDirLabel->setWordWrap(true);
        state->workDirLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        accountLayout->addWidget(state->workDirLabel);
        auto *cmdLabel = new QLabel(tr("执行命令权限"), accountPanel);
        accountLayout->addWidget(cmdLabel);
        state->commandPolicy = new QComboBox(accountPanel);
        state->commandPolicy->addItem(tr("每次确认"), int(AgentTools::CommandPolicy::Confirm));
        state->commandPolicy->addItem(tr("仅白名单命令"), int(AgentTools::CommandPolicy::Allowlist));
        state->commandPolicy->addItem(tr("允许任意命令"), int(AgentTools::CommandPolicy::Any));
        accountLayout->addWidget(state->commandPolicy);
        auto *writeLabel = new QLabel(tr("写文件权限"), accountPanel);
        accountLayout->addWidget(writeLabel);
        state->writePolicy = new QComboBox(accountPanel);
        state->writePolicy->addItem(tr("写前确认"), int(AgentTools::WritePolicy::Confirm));
        state->writePolicy->addItem(tr("只能新建文件"), int(AgentTools::WritePolicy::NewOnly));
        state->writePolicy->addItem(tr("直接写入"), int(AgentTools::WritePolicy::Direct));
        accountLayout->addWidget(state->writePolicy);
        connect(chooseDir, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getExistingDirectory(this, tr("选择 AI 智能体工作目录"),
                state->tools.hasWorkRoot() ? state->tools.workRoot() : DevTools::projectsRoot());
            if (path.isEmpty()) return;
            state->tools.setWorkRoot(path);
            state->workDirLabel->setText(QDir::toNativeSeparators(path));
        });
        connect(state->commandPolicy, &QComboBox::currentIndexChanged, this, [this](int) {
            state->tools.setCommandPolicy(AgentTools::CommandPolicy(state->commandPolicy->currentData().toInt()));
        });
        connect(state->writePolicy, &QComboBox::currentIndexChanged, this, [this](int) {
            state->tools.setWritePolicy(AgentTools::WritePolicy(state->writePolicy->currentData().toInt()));
        });
    }
    auto *accountAction = new QWidgetAction(accountMenu);
    accountAction->setDefaultWidget(accountPanel);
    accountMenu->addAction(accountAction);
    state->accountButton->setMenu(accountMenu);
    header->addWidget(state->accountButton);
    layout->addLayout(header);
    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->addStretch(1);
    auto *column = new QWidget(this);
    column->setObjectName("chatgptColumn");
    column->setMaximumWidth(790);
    column->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    state->columnLayout = new QVBoxLayout(column);
    state->columnLayout->setContentsMargins(12, 0, 12, 0);
    state->columnLayout->setSpacing(12);
    state->columnLayout->addStretch(1);
    state->welcome = new QLabel(tr("有什么可以帮忙的？"), column);
    state->welcome->setObjectName("chatgptWelcome");
    static_cast<QLabel *>(state->welcome)->setAlignment(Qt::AlignCenter);
    state->columnLayout->addWidget(state->welcome);
    state->transcript = new QScrollArea(column);
    state->transcript->setObjectName("chatgptMessages");
    state->transcript->setFrameShape(QFrame::NoFrame);
    state->transcript->setWidgetResizable(true);
    state->transcript->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *messages = new QWidget(state->transcript);
    messages->setObjectName("chatgptMessageList");
    state->messagesLayout = new QVBoxLayout(messages);
    state->messagesLayout->setContentsMargins(0, 14, 10, 0);
    state->messagesLayout->setSpacing(6);
    state->messagesLayout->addStretch();
    state->transcript->setWidget(messages);
    state->columnLayout->addWidget(state->transcript, 1);
    auto *composerBox = new QFrame(column);
    composerBox->setObjectName("chatgptComposer");
    auto *composerLayout = new QVBoxLayout(composerBox);
    composerLayout->setContentsMargins(16, 14, 12, 10);
    composerLayout->setSpacing(6);
    state->composer = new QPlainTextEdit(composerBox);
    state->composer->setObjectName("chatgptInput");
    state->composer->setPlaceholderText(tr("询问任何问题"));
    state->composer->setFrameShape(QFrame::NoFrame);
    state->composer->setFixedHeight(62);
    composerLayout->addWidget(state->composer);
    auto *actions = new QHBoxLayout;
    auto *hint = new QLabel(tr("Ctrl+Enter 发送"), composerBox);
    hint->setObjectName("chatgptInputHint");
    actions->addWidget(hint);
    actions->addStretch();
    state->stop = new QPushButton(QString(QChar(0x25a0)), composerBox);
    auto *stop = state->stop;
    stop->setObjectName("chatgptStop");
    stop->setFixedSize(36, 36);
    stop->setToolTip(tr("停止生成"));
    stop->setAccessibleName(tr("停止生成"));
    actions->addWidget(stop);
    state->send = new QPushButton(QString(QChar(0x2191)), composerBox);
    state->send->setObjectName("chatgptSend");
    state->send->setFixedSize(36, 36);
    state->send->setToolTip(tr("发送"));
    state->send->setAccessibleName(tr("发送"));
    actions->addWidget(state->send);
    composerLayout->addLayout(actions);
    state->columnLayout->addWidget(composerBox);
    state->status = new QLabel(column);
    state->status->setObjectName("chatgptStatus");
    state->status->setWordWrap(true);
    state->status->setAlignment(Qt::AlignCenter);
    state->columnLayout->addWidget(state->status);
    state->columnLayout->addStretch(2);
    body->addWidget(column, 8);
    body->addStretch(1);
    layout->addLayout(body, 1);
    connect(state->login, &QPushButton::clicked, this, [this] { state->loginAccount(false); });
    connect(other, &QPushButton::clicked, this, [this] { if (state->login->isEnabled()) state->loginAccount(true); });
    connect(state->send, &QPushButton::clicked, this, [this] { state->sendMessage(); });
    auto *shortcut = new QShortcut(QKeySequence("Ctrl+Return"), this);
    connect(shortcut, &QShortcut::activated, state->send, &QPushButton::click);
    connect(stop, &QPushButton::clicked, this, [this] {
        state->agentCancelled = true;
        if (state->response) state->response->abort();
        if (state->agentReply) state->agentReply->abort();
        state->display();
        if (state->toolRunning) state->status->setText(tr("已请求停止，等待当前工具结束（命令最多 30 秒）。"));
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        if (state->response || !state->login->isEnabled()) return;
        state->history = {}; state->draft.clear(); state->answer.clear(); state->display();
        if (!state->activeId.isEmpty() && state->lockCredentials()) {
            const QString selected = state->activeId;
            state->loadRecords(); state->activeId = selected;
            auto account = state->account();
            account["history"] = QJsonArray{};
            state->records[state->activeId] = account;
            state->saveRecords(); state->credentialLock.reset();
        }
    });
    connect(state->accounts, &QComboBox::currentIndexChanged, this, [this] {
        if (state->changingAccounts) return;
        state->activeId = state->accounts->currentData().toString();
        state->accountList();
        state->ensureSession([this] { state->loadModels(); });
    });
    connect(logout, &QPushButton::clicked, this, [this] {
        if (state->response || !state->login->isEnabled() || state->activeId.isEmpty() || !state->lockCredentials()) return;
        if (state->deepSeek()) {
            const auto original = state->account();
            auto record = original;
            record.remove("access_token");
            state->records[state->activeId] = record;
            if (!state->saveRecords()) {
                state->records[state->activeId] = original;
                state->credentialLock.reset(); return;
            }
            state->credentialLock.reset();
            state->models->clear(); state->accountList(); state->busy(false);
            state->status->setText(tr("已移除本机 API Key。"));
            return;
        }
        QUrlQuery form;
        form.addQueryItem("client_id", state->account()["client_id"].toString());
        form.addQueryItem("token", state->account()["refresh_token"].toString());
        form.addQueryItem("token_type_hint", "refresh_token");
        state->busy(true);
        state->request(QUrl("https://auth.openai.com/api/accounts/oauth/revoke"), formBytes(form),
            [this](QJsonObject, int code) {
                auto record = state->account();
                for (const auto &key : {"access_token", "refresh_token", "id_token", "expires_at"}) record.remove(key);
                state->records[state->activeId] = record; state->saveRecords(); state->credentialLock.reset();
                state->models->clear(); state->busy(false);
                state->status->setText(code == 200 ? tr("已退出账号。") : tr("已退出本机账号，远程撤销未确认，可在 ChatGPT 设置中断开连接。"));
            }, true);
    });
    state->loginTimeout.setSingleShot(true);
    connect(&state->loginTimeout, &QTimer::timeout, this, [this] {
        state->listener.close(); state->credentialLock.reset(); state->busy(false); state->status->setText(tr("登录超时，请重试。"));
    });
    connect(&state->listener, &QTcpServer::newConnection, this, [this] { state->handleConnection(); });
    state->loadRecords(); state->accountList(); state->busy(false);
    if (!state->account()["access_token"].toString().isEmpty())
        QTimer::singleShot(0, this, [this] { state->ensureSession([this] { state->loadModels(); }); });
    else state->status->setText(state->deepSeek() ? tr("首次使用请配置 DeepSeek API Key。") : tr("首次使用请登录 ChatGPT。"));
}
ChatGptPage::~ChatGptPage() {
    if (state->response) { state->response->disconnect(this); state->response->abort(); }
    for (auto *reply : state->network.findChildren<QNetworkReply *>()) reply->disconnect(this);
}
void ChatGptPage::setComposerText(const QString &text) {
    if (state->composer) {
        state->composer->setPlainText(text);
        state->composer->setFocus();
    }
}
