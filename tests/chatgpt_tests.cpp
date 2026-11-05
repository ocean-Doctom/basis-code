#include "../chatgptpage.cpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

struct ChatPageTestAccess {
    static void verifyConnection(ChatGptPage &page) {
        page.findChild<QPushButton *>("agentCheckConnection")->click();
        QElapsedTimer wait; wait.start();
        while (!page.state->login->isEnabled() && wait.elapsed() < 20000) {
            QApplication::processEvents(); QThread::msleep(5);
        }
        const QString result = page.state->status->text();
        std::cout << "Public service connection check: " << result.toUtf8().constData() << '\n';
        require(result.contains(QString::fromUtf8("服务连接正常")), "native Qt service connection succeeds");
    }
    static void verifyDeepSeek(ChatGptPage &page) {
        auto &state = *page.state;
        require(state.directory.endsWith("/basis.deepseek"), "provider credentials are isolated");
        state.draft = "Question";
        const QByteArray first = "data: {\"choices\":[{\"delta\":{\"content\":\"";
        const QByteArray rest = QString::fromUtf8("你好").toUtf8() +
            "\"}}]}\r\n\r\ndata: {\"choices\":[{\"delta\":{\"content\":\"!\"},\"finish_reason\":\"stop\"}]}\n\n";
        state.consumeStreamBytes(first + rest.left(1));
        require(state.answer.isEmpty(), "incomplete SSE frame is buffered");
        state.consumeStreamBytes(rest.mid(1));
        require(state.answer == QString::fromUtf8("你好!"), "split UTF-8 and multiple SSE frames assemble content");
        require(!state.completed, "completion requires DONE marker");
        state.consumeStreamBytes("data: [DONE]\n");
        require(!state.completed, "partial completion frame waits for delimiter");
        state.consumeStreamBytes("\n");
        require(state.completed, "DeepSeek DONE marks complete stream");
        state.activeId = "local-fixture";
        state.records[state.activeId] = QJsonObject{{"access_token", "sk-local-test-key"}, {"history", QJsonArray{}}};
        require(state.saveRecords(), "DeepSeek credentials save with OS encryption");
        QFile stored(state.directory + "/credentials.bin");
        require(stored.open(QIODevice::ReadOnly) && !stored.readAll().contains("sk-local-test-key"), "API Key is never stored as plaintext");
        stored.close();
        state.records = {}; state.loadRecords();
        require(state.account()["access_token"] == "sk-local-test-key", "DeepSeek key restores locally");
        page.findChild<QPushButton *>("agentLogout")->click();
        require(state.account()["access_token"].toString().isEmpty(), "remove key clears local credential");
        state.records = {}; state.loadRecords();
        require(state.account()["access_token"].toString().isEmpty(), "removed key stays removed");
        bool finished = false;
        int ticks = 0;
        QTimer heartbeat;
        QObject::connect(&heartbeat, &QTimer::timeout, &page, [&] { ++ticks; });
        heartbeat.start(10);
        state.runToolJob([uiThread = page.thread()](AgentTools &) {
            const bool worker = QThread::currentThread() != uiThread;
            QThread::msleep(120);
            return QJsonObject{{"worker", worker}};
        }, [&](const QJsonObject &result) { require(result["worker"].toBool(), "tools execute off UI thread"); finished = true; });
        QElapsedTimer wait; wait.start();
        while (!finished && wait.elapsed() < 3000) { QApplication::processEvents(); QThread::msleep(2); }
        require(finished && ticks > 2, "UI heartbeat stays responsive during tools");
        heartbeat.stop();
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        QTemporaryDir directory;
        qputenv("BASIS_CODE_HOME", directory.path().toUtf8());
        QUrlQuery form;
        form.addQueryItem("token", "a+b c&d");
        require(formBytes(form) == "token=a%2Bb%20c%26d", "OAuth form values encode reserved characters");
        const QByteArray secret("test credential record");
        const auto encrypted = protectedBytes(secret, true);
        require(!encrypted.isEmpty() && encrypted != secret, "credentials use OS encryption");
        require(protectedBytes(encrypted, false) == secret, "credentials survive restart decryption");
        require(protectedBytes("invalid bytes", false).isEmpty(), "corrupt credentials rejected");
        require(randomUrlToken() != randomUrlToken(), "independent login state values");
        QJsonObject header{{"alg", "none"}};
        QJsonObject claims{{"iss", "https://auth.openai.com"}, {"aud", "client"},
            {"sub", "account"}, {"exp", double(QDateTime::currentSecsSinceEpoch() + 3600)}, {"nonce", "expected"}};
        auto encode = [](const QJsonObject &object) {
            return QJsonDocument(object).toJson(QJsonDocument::Compact)
                .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        };
        const auto token = encode(header) + "." + encode(claims) + ".forged";
        require(!verifyIdentity(token, {}, "client", "expected", {}), "unsigned ID token rejected");
        header["alg"] = "RS256";
        const auto signedClaim = encode(header) + "." + encode(claims) + ".forged";
        require(!verifyIdentity(signedClaim, {}, "wrong-client", "expected", {}), "wrong audience rejected");
        require(!verifyIdentity(signedClaim, {}, "client", "wrong-nonce", {}), "wrong nonce rejected");
        require(!verifyIdentity(signedClaim, {}, "client", "expected", "other-account"), "account substitution rejected");
#ifdef Q_OS_WIN
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_KEY_HANDLE key = nullptr;
        require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0) >= 0 &&
                    BCryptGenerateKeyPair(algorithm, &key, 2048, 0) >= 0 &&
                    BCryptFinalizeKeyPair(key, 0) >= 0, "generate verification fixture");
        ULONG publicSize = 0;
        BCryptExportKey(key, nullptr, BCRYPT_RSAPUBLIC_BLOB, nullptr, 0, &publicSize, 0);
        QByteArray publicBytes(int(publicSize), Qt::Uninitialized);
        require(BCryptExportKey(key, nullptr, BCRYPT_RSAPUBLIC_BLOB,
            reinterpret_cast<PUCHAR>(publicBytes.data()), publicSize, &publicSize, 0) >= 0, "export public verification key");
        const auto *keyHeader = reinterpret_cast<const BCRYPT_RSAKEY_BLOB *>(publicBytes.constData());
        const QByteArray exponent = publicBytes.mid(sizeof(BCRYPT_RSAKEY_BLOB), keyHeader->cbPublicExp);
        const QByteArray modulus = publicBytes.mid(sizeof(BCRYPT_RSAKEY_BLOB) + keyHeader->cbPublicExp, keyHeader->cbModulus);
        auto base64 = [](const QByteArray &bytes) {
            return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
        };
        header["kid"] = "test-key";
        const QByteArray signingInput = encode(header) + "." + encode(claims);
        auto hash = QCryptographicHash::hash(signingInput, QCryptographicHash::Sha256);
        BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
        ULONG signatureSize = 0;
        BCryptSignHash(key, &padding, reinterpret_cast<PUCHAR>(hash.data()), ULONG(hash.size()),
                       nullptr, 0, &signatureSize, BCRYPT_PAD_PKCS1);
        QByteArray signature(int(signatureSize), Qt::Uninitialized);
        require(BCryptSignHash(key, &padding, reinterpret_cast<PUCHAR>(hash.data()), ULONG(hash.size()),
            reinterpret_cast<PUCHAR>(signature.data()), signatureSize, &signatureSize, BCRYPT_PAD_PKCS1) >= 0, "sign identity fixture");
        const QJsonArray keys{QJsonObject{{"kty", "RSA"}, {"kid", "test-key"},
            {"e", base64(exponent)}, {"n", base64(modulus)}}};
        const auto validToken = signingInput + "." + base64(signature).toLatin1();
        require(verifyIdentity(validToken, keys, "client", "expected", "account"), "valid RS256 identity verified against JWKS");
        signature[0] = char(signature[0] ^ 1);
        require(!verifyIdentity(signingInput + "." + base64(signature).toLatin1(), keys, "client", "expected", "account"),
                "tampered identity signature rejected");
        BCryptDestroyKey(key);
        BCryptCloseAlgorithmProvider(algorithm, 0);
#endif
        ChatGptPage deepseek(nullptr, "deepseek");
        require(deepseek.objectName() == "deepseekPage" && deepseek.property("toolkitName") == "DeepSeek",
                "native DeepSeek plugin uses independent provider");
        require(!deepseek.findChild<QPushButton *>("chatgptSend")->isEnabled(), "DeepSeek requires key before inference");
        ChatPageTestAccess::verifyDeepSeek(deepseek);
        ChatGptPage page;
        if (app.arguments().contains("--network-check")) ChatPageTestAccess::verifyConnection(page);
        require(page.findChild<QScrollArea *>("chatgptMessages"), "native chat message area");
        require(page.findChild<QPlainTextEdit *>("chatgptInput"), "native chat input");
        require(!page.findChild<QPushButton *>("chatgptSend")->isEnabled(), "no unauthenticated inference");
        require(page.findChild<QWidget *>("chatgptWelcome") && page.findChild<QFrame *>("chatgptComposer"),
                "welcome and rounded composer are present");
        QJsonObject record{{"client_id", "test-client"}, {"email", "preview@example.invalid"},
            {"history", QJsonArray{
                QJsonObject{{"role", "user"}, {"content", "How do I run this project?"}},
                QJsonObject{{"role", "assistant"}, {"content", "Open the source file and press **F5** to run it.\\n\\nUse Shift+F5 to start the debugger."}}}}};
        QSaveFile saved(directory.path() + "/extensions/basis.chatgpt/credentials.bin");
        const auto fixture = protectedBytes(QJsonDocument(QJsonObject{
            {"active", "fixture"}, {"accounts", QJsonObject{{"fixture", record}}}}).toJson(), true);
        require(saved.open(QIODevice::WriteOnly) && saved.write(fixture) == fixture.size() && saved.commit(), "save local message fixture");
        ChatGptPage conversation;
        if (qEnvironmentVariableIsSet("BASIS_CONVERSATION_PREVIEW_PATH")) {
            QFile skin("theme-github-dark.qss");
            if (skin.open(QIODevice::ReadOnly)) conversation.setStyleSheet(QString::fromUtf8(skin.readAll()));
            conversation.setFont(QFont("Microsoft YaHei UI", 10));
        }
        conversation.resize(960, 720);
        conversation.show();
        app.processEvents();
        require(!conversation.findChildren<QTextBrowser *>("chatMessageText").isEmpty(), "render existing conversation messages");
        require(!conversation.findChild<QWidget *>("chatgptWelcome")->isVisible(), "conversation hides welcome");
        if (qEnvironmentVariableIsSet("BASIS_CONVERSATION_PREVIEW_PATH")) {
            app.processEvents();
            require(conversation.grab().save(qEnvironmentVariable("BASIS_CONVERSATION_PREVIEW_PATH")), "render conversation layout");
        }
        conversation.findChild<QPushButton *>("chatgptNewChat")->click();
        app.processEvents();
        require(conversation.findChild<QWidget *>("chatgptWelcome")->isVisible(), "new chat restores centered welcome");
        std::cout << "Passed native ChatGPT / DeepSeek, streaming and credential security checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
