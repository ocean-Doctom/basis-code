#include "devtools.h"
#include "terminalpane.h"
#include <QTabWidget>
#include <QMainWindow>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSplitter>
#include <QVersionNumber>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QLabel>
#include <QComboBox>
#include <QListWidget>
#include <QColor>
#include <QAbstractItemView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QSettings>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QFrame>
#include <QProcess>
#include <QProcessEnvironment>
#include <QCryptographicHash>
#include <QPointer>
#include <QTimer>
#include <QUuid>
#include <QRegularExpression>
#include <memory>
#include <algorithm>

static QString configPath() { return DevTools::dataRoot() + "/settings.ini"; }
static QString setting(const QString &key) {
    return QSettings(configPath(), QSettings::IniFormat).value(key).toString();
}
static void setSetting(const QString &key, const QVariant &value) {
    QSettings config(configPath(), QSettings::IniFormat);
    config.setValue(key, value);
    config.sync();
}
QString DevTools::dataRoot() {
    const auto overrideRoot = qEnvironmentVariable("BASIS_CODE_HOME");
    return overrideRoot.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : QDir(overrideRoot).absolutePath();
}
QString DevTools::projectsRoot() {
    const auto custom = setting("storage/projects");
    if (custom.isEmpty() && !qEnvironmentVariableIsEmpty("BASIS_CODE_HOME")) return dataRoot()+"/projects";
    return custom.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                 + "/Basis Code/Projects" : custom;
}
QString DevTools::compilerPath() { return setting("toolchain/compiler"); }
QString DevTools::runtimePath(const QString &language) {
    return setting("toolchain/" + language.toLower());
}
bool DevTools::initialize(QString *error) {
    for (const auto &path : {dataRoot(), projectsRoot(), dataRoot()+"/extensions/basis.cpp",
                            dataRoot()+"/toolchains", dataRoot()+"/downloads", dataRoot()+"/cache/builds"}) {
        if (!QDir().mkpath(path)) {
            if (error) *error = QStringLiteral("无法创建目录：") + path;
            return false;
        }
    }
    QSaveFile manifest(dataRoot()+"/extensions/basis.cpp/manifest.json");
    const QByteArray data = R"({"id":"basis.cpp","name":"C/C++ 工具包","version":"0.1.0","components":["compiler","runner"]})";
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write(data) != data.size() || !manifest.commit()) {
        if (error) *error = QStringLiteral("无法保存 C/C++ 扩展清单");
        return false;
    }
    for (const QString language : {"java", "python", "chatgpt", "deepseek"}) {
        const QString directory = dataRoot() + "/extensions/basis." + language;
        if (!QDir().mkpath(directory)) return false;
        QSaveFile file(directory + "/manifest.json");
        const QByteArray bytes = QJsonDocument(QJsonObject{
            {"id", "basis." + language}, {"version", "0.1.0"},
            {"components", (language == "chatgpt" || language == "deepseek") ? QJsonArray{"chat"} : QJsonArray{"runtime", "runner", "debugger"}}}).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return false;
    }
    return true;
}
bool DevTools::safeArchiveEntry(QString entry) {
    entry.replace('\\', '/');
    if (entry.startsWith('/') || entry.contains(':')) return false;
    for (const auto &part : entry.split('/')) if (part == "..") return false;
    return true;
}
QString DevTools::compilerForSource(const QString &cpp, const QString &source) {
    if (QFileInfo(source).suffix().compare("c", Qt::CaseInsensitive) != 0) return cpp;
    QString name = QFileInfo(cpp).fileName();
    if (name.contains("clang++")) name.replace("clang++", "clang");
    else if (name.contains("g++")) name.replace("g++", "gcc");
    else return {};
    return QFileInfo(cpp).dir().filePath(name);
}

void DevTools::showStorage() {
    auto *dialog = new QDialog(window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("存储与项目"));
    dialog->resize(680, 480);
    auto *layout = new QVBoxLayout(dialog);
    auto addFolder = [&](const QString &title, const QString &path) {
        auto *label = new QLabel(title + "\n" + QDir::toNativeSeparators(path), dialog);
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setWordWrap(true);
        layout->addWidget(label);
        auto *open = new QPushButton(tr("打开目录"), dialog);
        layout->addWidget(open);
        connect(open, &QPushButton::clicked, dialog, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
    };
    addFolder(tr("默认项目位置"), projectsRoot());
    auto *choose = new QPushButton(tr("更改默认项目位置…"), dialog);
    layout->addWidget(choose);
    connect(choose, &QPushButton::clicked, dialog, [this, dialog] {
        const auto path = QFileDialog::getExistingDirectory(dialog, tr("选择默认项目位置"), projectsRoot());
        if (!path.isEmpty()) { setSetting("storage/projects", path); dialog->close(); showStorage(); }
    });
    addFolder(tr("应用数据（extensions 插件 / toolchains 编译器 / downloads 下载 / cache 缓存）"), dataRoot());
    auto *create = new QPushButton(tr("新建 C++ 项目…"), dialog);
    layout->addWidget(create);
    connect(create, &QPushButton::clicked, dialog, [this, dialog] {
        bool ok = false;
        const auto name = QInputDialog::getText(dialog, tr("新建项目"), tr("项目名称"), QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        if (name == "." || name == ".." || name.contains(QRegularExpression(R"([<>:"/\\|?*])")) || name.endsWith('.') || name.endsWith(' ')) {
            QMessageBox::warning(dialog, tr("名称无效"), tr("请输入合法的单个文件夹名称。")); return;
        }
        const auto path = QDir(projectsRoot()).filePath(name);
        if (QFileInfo::exists(path) || !QDir().mkpath(path)) {
            QMessageBox::warning(dialog, tr("创建失败"), tr("目录已存在或无法创建。")); return;
        }
        QSaveFile file(path + "/main.cpp");
        const QByteArray code = "#include <iostream>\n\nint main() {\n    std::cout << \"Hello, Basis Code!\\n\";\n    return 0;\n}\n";
        if (!file.open(QIODevice::WriteOnly) || file.write(code) != code.size() || !file.commit()) {
            QMessageBox::warning(dialog, tr("创建失败"), file.errorString()); return;
        }
        emit projectRequested(path); dialog->close();
    });
    layout->addWidget(new QLabel(tr("最近打开的项目（双击打开）"), dialog));
    auto *recent = new QListWidget(dialog);
    recent->addItems(QSettings(configPath(), QSettings::IniFormat).value("projects/recent").toStringList());
    layout->addWidget(recent);
    connect(recent, &QListWidget::itemDoubleClicked, dialog, [this, dialog](QListWidgetItem *item) {
        emit projectRequested(item->text()); dialog->close();
    });
    dialog->show();
}

// Downloads use the publishers linked by mingw-w64.org, not third-party mirrors.
class ToolchainPage : public QWidget {
  public:
    explicit ToolchainPage(QWidget *parent, const QString &language = {}) : QWidget(parent), runtime(language) {
        setObjectName("toolchainConfiguration");
        setProperty("language", runtime.isEmpty() ? "C/C++" : runtime);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(14);

        // 辅助：创建一张分组卡片，返回卡片内部的垂直布局。
        auto makeCard = [this, layout](const QString &title) -> QVBoxLayout * {
            auto *card = new QFrame(this);
            card->setObjectName("toolchainCard");
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(16, 14, 16, 14);
            cardLayout->setSpacing(0);
            auto *titleLabel = new QLabel(title, card);
            titleLabel->setObjectName("toolchainCardTitle");
            cardLayout->addWidget(titleLabel);
            cardLayout->addSpacing(6);
            layout->addWidget(card);
            return cardLayout;
        };
        // 辅助：在卡片内追加一行「左标题/说明 + 右侧控件」，行间带细分隔线。
        auto addRow = [this](QVBoxLayout *cardLayout, const QString &title, const QString &description, QWidget *right) {
            auto *sep = new QFrame(this);
            sep->setObjectName("toolchainRowSeparator");
            sep->setFrameShape(QFrame::HLine);
            sep->setFrameShadow(QFrame::Plain);
            cardLayout->addWidget(sep);
            auto *row = new QWidget(this);
            row->setObjectName("toolchainRow");
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 10, 0, 10);
            rowLayout->setSpacing(12);
            auto *textBox = new QWidget(row);
            auto *textLayout = new QVBoxLayout(textBox);
            textLayout->setContentsMargins(0, 0, 0, 0);
            textLayout->setSpacing(2);
            auto *titleLabel = new QLabel(title, textBox);
            titleLabel->setObjectName("toolchainRowTitle");
            titleLabel->setTextFormat(Qt::PlainText);
            textLayout->addWidget(titleLabel);
            if (!description.isEmpty()) {
                auto *descLabel = new QLabel(description, textBox);
                descLabel->setObjectName("toolchainRowDescription");
                descLabel->setTextFormat(Qt::PlainText);
                descLabel->setWordWrap(true);
                textLayout->addWidget(descLabel);
            }
            rowLayout->addWidget(textBox, 1);
            if (right) {
                right->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
                rowLayout->addWidget(right, 0, Qt::AlignVCenter);
            }
            cardLayout->addWidget(row);
            return right;
        };

        // === 分组一：安装工具 ===
        auto *installCard = makeCard(tr("安装工具"));
        sources = new QLabel(tr("<a href='https://www.mingw-w64.org/downloads/'>MinGW-w64 官网</a> · "
                                "<a href='https://github.com/mstorsjo/llvm-mingw/releases'>LLVM-MinGW 发布页</a>"), this);
        sources->setObjectName("toolchainSources");
        if (!runtime.isEmpty())
            sources->setText(runtime == "Java"
                ? "<a href='https://adoptium.net/temurin/releases/'>Eclipse Temurin JDK 21 · Windows x64</a>"
                : "<a href='https://docs.python.org/3/using/windows.html#the-nuget-org-packages'>Python 官方 NuGet 发行包 · Windows x64</a>");
        sources->setOpenExternalLinks(true);
        addRow(installCard, tr("下载来源"), tr("官方发行版说明与下载地址"), sources);

        // 安装按钮行：左侧说明，右侧「安装 MinGW / 安装 Clang」（带下拉）。
        installRow = new QWidget(this);
        installRow->setObjectName("toolchainInstallRow");
        auto *buttons = new QHBoxLayout(installRow);
        buttons->setContentsMargins(0, 0, 0, 0);
        buttons->setSpacing(8);
        for (const bool clang : runtime.isEmpty() ? QList<bool>{false, true} : QList<bool>{false}) {
            const QString family = runtime.isEmpty() ? (clang ? "Clang" : "MinGW") : runtime;
            auto *button = new QToolButton(installRow);
            button->setObjectName(runtime.isEmpty() ? (clang ? "installClang" : "installMinGW") : "install" + runtime);
            button->setProperty("compilerInstall", true);
            button->setText(tr("安装 %1").arg(family));
            button->setToolTip(clang ? tr("获取 LLVM-MinGW 版本") : tr("获取 WinLibs GCC 版本"));
            button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            button->setPopupMode(QToolButton::MenuButtonPopup);
            auto *menu = new QMenu(button);
            menu->addAction(tr("选择电脑中已经安装的 %1…").arg(family), this, [this, clang, family] {
                const QString driver = runtime.isEmpty() ? (clang ? "clang++.exe" : "g++.exe") : runtime == "Java" ? "javac.exe" : "python.exe";
                const QString path = QFileDialog::getOpenFileName(this,
                    tr("选择电脑中已经安装的 %1").arg(family), {},
                    tr("%1 编译器 (%2 *-%2)").arg(family, driver));
                if (path.isEmpty()) return;
                const auto name = QFileInfo(path).fileName().toLower();
                if (name != driver && !name.endsWith("-" + driver)) {
                    status->setText(tr("请选择 %1 或对应的带平台前缀的编译器。").arg(driver));
                    return;
                }
                selectCompiler(path);
            });
            button->setMenu(menu);
            connect(button, &QToolButton::clicked, this, [this, clang] {
                clangSelected = clang;
                fetchVersions();
            });
            buttons->addWidget(button);
        }
        addRow(installCard, tr("安装编译器"), tr("在线下载并安装，或从本机选择已安装的编译器"), installRow);

        versions = new QComboBox(this);
        versions->setObjectName("compilerVersions");
        versions->setMinimumWidth(220);
        addRow(installCard, tr("选择版本"), tr("从维护者发布的最新版本中选择一个"), versions);

        download = new QPushButton(tr("下载并安装"), this);
        download->setObjectName("downloadToolchain");
        download->setCursor(Qt::PointingHandCursor);
        download->setEnabled(false);
        addRow(installCard, tr("开始安装"), tr("下载所选版本并解压安装到本地工具链目录"), download);

        // 进度区（下载/解压时显示）。
        progressRow = new QWidget(this);
        progressRow->setObjectName("toolchainProgressRow");
        auto *progressLayout = new QVBoxLayout(progressRow);
        progressLayout->setContentsMargins(0, 0, 0, 0);
        progressLayout->setSpacing(6);
        status = new QLabel(tr("选择安装方式开始。"), progressRow);
        status->setObjectName("toolchainStatus");
        status->setTextFormat(Qt::PlainText);
        status->setWordWrap(true);
        progressLayout->addWidget(status);
        progress = new QProgressBar(progressRow);
        progress->setObjectName("toolchainProgress");
        progress->setRange(0, 100);
        progress->setTextVisible(false);
        progressLayout->addWidget(progress);
        cancel = new QPushButton(tr("取消下载 / 解压"), progressRow);
        cancel->setObjectName("cancelToolchain");
        cancel->setCursor(Qt::PointingHandCursor);
        cancel->setEnabled(false);
        progressLayout->addWidget(cancel);
        installCard->addWidget(progressRow);

        // === 分组二：已安装 ===
        auto *installedCard = makeCard(tr("已安装"));
        current = new QLabel(tr("尚未设置"), this);
        current->setObjectName("toolchainCurrent");
        current->setTextFormat(Qt::PlainText);
        current->setWordWrap(true);
        addRow(installedCard, tr("当前工具"), QString(), current);

        installed = new QComboBox(this);
        installed->setObjectName("toolchainInstalled");
        installed->setMinimumWidth(220);
        addRow(installedCard, tr("选择已安装工具"), tr("从本机或已下载的工具链中选择"), installed);

        use = new QPushButton(tr("使用选中的工具"), this);
        use->setObjectName("useToolchain");
        use->setCursor(Qt::PointingHandCursor);
        addRow(installedCard, tr("应用"), tr("将选中的工具设为当前编译器"), use);

        layout->addStretch();
        refreshInstalled();
        connect(download, &QPushButton::clicked, this, [this] { startDownload(); });
        connect(cancel, &QPushButton::clicked, this, [this] { abort(); });
        connect(use, &QPushButton::clicked, this, [this] { selectCompiler(installed->currentData().toString()); });
    }
    ~ToolchainPage() override { abort(); }
  private:
    QString runtime;
    QNetworkAccessManager network;
    QPointer<QNetworkReply> reply;
    QProcess extractor;
    QFile file;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    QWidget *installRow, *progressRow;
    bool clangSelected = false;
    QComboBox *versions, *installed;
    QPushButton *download, *cancel, *use;
    QProgressBar *progress;
    QLabel *sources, *status, *current;
    QString staging, archive, destination;
    bool cancelled = false;
    void lock(bool active) {
        installRow->setEnabled(!active);
        versions->setEnabled(!active); download->setEnabled(!active && versions->count()); cancel->setEnabled(active);
    }
    void fail(const QString &message) {
        file.close(); progress->setRange(0,100); status->setText(message); lock(false);
        if (!archive.isEmpty()) QFile::remove(archive + ".part");
        if (!staging.isEmpty() && extractor.state() == QProcess::NotRunning && QFileInfo(staging).absolutePath() == DevTools::dataRoot()+"/toolchains") {
            QDir(staging).removeRecursively(); staging.clear();
        }
    }
    void abort() {
        cancelled = true;
        progress->setRange(0,100);
        if (reply) { reply->disconnect(this); reply->abort(); reply->deleteLater(); reply = nullptr; }
        if (extractor.state() != QProcess::NotRunning) { extractor.disconnect(this); extractor.kill(); extractor.waitForFinished(3000); }
        file.close();
        if (!archive.isEmpty()) QFile::remove(archive + ".part");
        // staging always uses a unique direct child of our toolchains directory.
        if (!staging.isEmpty() && QFileInfo(staging).absolutePath() == DevTools::dataRoot()+"/toolchains") QDir(staging).removeRecursively();
        staging.clear(); status->setText(tr("已取消。")); lock(false);
    }
    QNetworkReply *get(const QUrl &url) {
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "Basis-Code");
        request.setTransferTimeout(60000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        return network.get(request);
    }
    void fetchVersions() {
        lock(true); versions->clear(); status->setText(tr("正在获取维护者发布的版本…"));
        if (!runtime.isEmpty()) { fetchRuntimeVersions(); return; }
        const QString repo = clangSelected ? "mstorsjo/llvm-mingw" : "brechtsanders/winlibs_mingw";
        reply = get(QUrl("https://api.github.com/repos/"+repo+"/releases?per_page=10"));
        connect(reply, &QNetworkReply::finished, this, [this] {
            auto *result = reply.data(); reply = nullptr;
            const auto data = result->readAll(); const auto error = result->error(); result->deleteLater();
            if (error != QNetworkReply::NoError) { fail(tr("获取版本失败，请检查网络或稍后重试。")); return; }
            const auto releases = QJsonDocument::fromJson(data).array();
            for (const auto &r : releases) {
                const auto release = r.toObject();
                if (release["draft"].toBool() || release["prerelease"].toBool()) continue;
                for (const auto &a : release["assets"].toArray()) {
                    const auto asset = a.toObject(); const auto name = asset["name"].toString();
                    const bool match = !clangSelected
                        ? name.startsWith("winlibs-x86_64-") && name.contains("ucrt") && !name.contains("llvm")
                        : name.startsWith("llvm-mingw-") && name.contains("ucrt-x86_64");
                    if (match && name.endsWith(".zip")) versions->addItem(name, asset.toVariantMap());
                }
            }
            status->setText(versions->count() ? tr("请选择版本，再点击下载。") : tr("未找到兼容的 Windows x64 ZIP 包，请查看发布页或选择本机编译器。"));
            lock(false);
        });
    }
    QString driverName() const {
        return runtime == "Java" ? "javac.exe" : "python.exe";
    }
    QString selectedPath() const {
        return runtime.isEmpty() ? DevTools::compilerPath() : DevTools::runtimePath(runtime);
    }
    void selectCompiler(const QString &path) {
        if (path.isEmpty() || !QFileInfo(path).isExecutable()) {
            status->setText(tr("工具文件不存在或不可执行。")); return;
        }
        if (runtime == "Java" && (!QFileInfo::exists(QFileInfo(path).dir().filePath("java.exe")) ||
                                   !QFileInfo::exists(QFileInfo(path).dir().filePath("jdb.exe")))) {
            status->setText(tr("请选择包含 javac.exe、java.exe 和 jdb.exe 的完整 JDK。")); return;
        }
        setSetting(runtime.isEmpty() ? "toolchain/compiler" : "toolchain/" + runtime.toLower(),
                   QFileInfo(path).absoluteFilePath());
        refreshInstalled();
        status->setText(tr("已设为当前工具。"));
    }
    void refreshInstalled() {
        current->setText(tr("当前工具：") + selectedPath()); installed->clear();
        if (QFileInfo::exists(selectedPath())) installed->addItem(selectedPath(), selectedPath());
        const QStringList filters = runtime.isEmpty() ? QStringList{"g++.exe", "clang++.exe"} : QStringList{driverName()};
        QDirIterator it(DevTools::dataRoot()+"/toolchains", filters, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const auto path = it.next();
            if (!path.contains("/.install-")) installed->addItem(path, path);
        }
    }
    void fetchRuntimeVersions() {
        const QString url = runtime == "Java"
            ? "https://api.adoptium.net/v3/assets/latest/21/hotspot?architecture=x64&image_type=jdk&os=windows&vendor=eclipse"
            : "https://api.nuget.org/v3-flatcontainer/python/index.json";
        reply = get(QUrl(url));
        connect(reply, &QNetworkReply::finished, this, [this] {
            auto *result = reply.data(); reply = nullptr;
            const auto bytes = result->readAll();
            const auto error = result->error(); result->deleteLater();
            if (error != QNetworkReply::NoError) { fail(tr("获取版本失败，请检查网络。")); return; }
            const auto document = QJsonDocument::fromJson(bytes);
            if (runtime == "Java") {
                for (const auto &entry : document.array()) {
                    const auto package = entry.toObject()["binary"].toObject()["package"].toObject();
                    const auto name = package["name"].toString();
                    if (!name.endsWith(".zip")) continue;
                    QJsonObject asset{{"name", name}, {"browser_download_url", package["link"]},
                                      {"digest", "sha256:" + package["checksum"].toString()}};
                    versions->addItem(name, asset.toVariantMap());
                }
            } else {
                QList<QVersionNumber> releases;
                for (const auto &entry : document.object()["versions"].toArray()) {
                    const auto version = entry.toString();
                    if (QRegularExpression("^3\\.\\d+\\.\\d+$").match(version).hasMatch())
                        releases.append(QVersionNumber::fromString(version));
                }
                std::sort(releases.begin(), releases.end(), [](const auto &a, const auto &b) { return a > b; });
                for (const auto &release : releases.mid(0, 12)) {
                    const auto version = release.toString();
                    const auto name = "python." + version + ".nupkg";
                    QJsonObject asset{{"name", name}, {"browser_download_url",
                        "https://api.nuget.org/v3-flatcontainer/python/" + version + "/" + name}};
                    versions->addItem("Python " + version, asset.toVariantMap());
                }
            }
            status->setText(versions->count() ? tr("请选择版本后下载安装。") : tr("未找到可用版本。"));
            lock(false);
        });
    }
    void startDownload() {
        const auto asset = QJsonObject::fromVariantMap(versions->currentData().toMap());
        const auto name = asset["name"].toString(); const QUrl url(asset["browser_download_url"].toString());
        if (!DevTools::safeArchiveEntry(name) || name.contains('/') || url.scheme() != "https" || (url.host() != "github.com" && url.host() != "api.nuget.org")) { fail(tr("下载地址无效。")); return; }
        archive = DevTools::dataRoot()+"/downloads/"+name;
        file.setFileName(archive+".part");
        if (!file.open(QIODevice::WriteOnly)) { fail(file.errorString()); return; }
        cancelled = false; hash.reset(); lock(true); status->setText(tr("下载中：")+name);
        reply = get(url);
        connect(reply, &QNetworkReply::readyRead, this, [this] {
            const auto bytes = reply->readAll(); hash.addData(bytes);
            if (file.write(bytes) != bytes.size()) { reply->abort(); }
        });
        connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
            progress->setRange(0, total > 0 ? 100 : 0);
            if (total > 0) progress->setValue(int(received*100/total));
        });
        connect(reply, &QNetworkReply::finished, this, [this, asset] {
            auto *result = reply.data(); reply = nullptr;
            const auto remaining = result->readAll(); hash.addData(remaining);
            const bool written = file.write(remaining) == remaining.size() && file.flush(); file.close();
            const bool success = result->error() == QNetworkReply::NoError; result->deleteLater();
            if (!success || !written || cancelled) { fail(tr("下载未完成，请重试。")); return; }
            const auto digest = asset["digest"].toString();
            if (digest.startsWith("sha256:") && digest.mid(7).toLatin1().toLower() != hash.result().toHex()) { fail(tr("SHA-256 校验失败。")); return; }
            QFile::remove(archive);
            if (!QFile::rename(archive+".part", archive)) { fail(tr("保存下载文件失败。")); return; }
            staging = DevTools::dataRoot()+"/toolchains/.install-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
            destination = DevTools::dataRoot()+"/toolchains/"+QFileInfo(archive).completeBaseName();
            if (QFileInfo::exists(destination)) { fail(tr("此版本已经安装，请从已安装列表选择。")); return; }
            if (!QDir().mkpath(staging)) { fail(tr("无法创建解压目录。")); return; }
            status->setText(tr("正在检查压缩包目录…"));
            extractor.disconnect(this);
            connect(&extractor, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
                if (e == QProcess::FailedToStart) fail(tr("无法运行 Windows tar.exe。"));
            });
            connect(&extractor, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
                if (code || exit != QProcess::NormalExit) { fail(tr("无法读取压缩包。")); return; }
                const auto listing = QString::fromUtf8(extractor.readAllStandardOutput()).split('\n');
                for (const auto &line : listing) if (!DevTools::safeArchiveEntry(line.trimmed())) { fail(tr("压缩包包含无效路径。")); return; }
                extract();
            });
            extractor.start("tar.exe", {"-tf", archive});
        });
    }
    void extract() {
        extractor.disconnect(this); status->setText(tr("正在解压安装，可能需要几分钟…")); progress->setRange(0,0);
        connect(&extractor, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) fail(tr("无法启动 Windows tar.exe 解压工具。"));
        });
        connect(&extractor, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
            progress->setRange(0,100);
            if (code || exit != QProcess::NormalExit) { fail(tr("解压失败：")+QString::fromLocal8Bit(extractor.readAllStandardError())); return; }
            QDirIterator find(staging, runtime.isEmpty() ? QStringList{"g++.exe", "clang++.exe"} : QStringList{driverName()}, QDir::Files, QDirIterator::Subdirectories);
            if (!find.hasNext()) { fail(tr("安装包中未找到 C++ 编译器。")); return; }
            const auto relative = QDir(staging).relativeFilePath(find.next());
            if (!QDir().rename(staging, destination)) { fail(tr("安装目录提交失败。")); return; }
            staging.clear(); selectCompiler(destination+"/"+relative); progress->setValue(100); lock(false);
            status->setText(tr("安装完成，已选择此开发工具。"));
        });
        extractor.start("tar.exe", {"-xf", archive, "-C", staging});
    }
};
QWidget *DevTools::createToolchainPage(QWidget *parent, const QString &language) {
    return new ToolchainPage(parent, language == "C/C++" ? QString() : language);
}
void DevTools::showRuntime(const QString &language) { emit toolchainRequested(language); }
void DevTools::showCompilers() { emit toolchainRequested("C/C++"); }
bool DevTools::runnerInstalled(const QString &language) {
    const QString id = language == "C/C++" ? "cpp" : language.toLower();
    return QFileInfo::exists(dataRoot() + "/extensions/basis." + id + "/runner.json");
}
bool DevTools::installRunner(const QString &language) {
    const QString id = language == "C/C++" ? "cpp" : language.toLower();
    const QString directory = dataRoot() + "/extensions/basis." + id;
    if (!QDir().mkpath(directory)) return false;
    const auto data = QJsonDocument(QJsonObject{{"id", "basis." + id + ".runner"},
        {"version", "1.0"}, {"provider", "Basis Code built-in"}}).toJson();
    QSaveFile file(directory + "/runner.json");
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
void DevTools::showTerminal(const QString &project) {
    dock->show();
    static_cast<TerminalPane *>(terminal)->start(project.isEmpty() ? projectsRoot() : project);
    outputTabs->setCurrentWidget(terminal);
}

DevTools::DevTools(QMainWindow *parent) : QObject(parent), window(parent) {
    QString error;
    if (!initialize(&error)) QTimer::singleShot(0, this, [this,error] { QMessageBox::warning(window, tr("存储初始化失败"), error); });
    dock = new QWidget(window); dock->setObjectName("runOutputDock");
    auto *dockLayout = new QVBoxLayout(dock);
    dockLayout->setContentsMargins(0, 0, 0, 0);
    outputTabs = new QTabWidget(dock);
    outputTabs->setObjectName("outputTabs");
    outputTabs->setDocumentMode(true);
    outputTabs->setTabsClosable(false);
    outputTabs->setMovable(true);

    // ── 标签 1：问题 ──
    auto *problemsPage = new QWidget(outputTabs);
    problemsPage->setObjectName("problemsPage");
    auto *problemsLayout = new QVBoxLayout(problemsPage);
    problemsLayout->setContentsMargins(0, 0, 0, 0);
    auto *filterRow = new QHBoxLayout;
    auto *filterEdit = new QLineEdit(problemsPage);
    filterEdit->setObjectName("problemsFilter");
    filterEdit->setPlaceholderText(tr("筛选器 (例如 text、**/*.ts、**/node_modules/**)"));
    filterEdit->setClearButtonEnabled(true);
    filterRow->addWidget(filterEdit);
    problemsLayout->addLayout(filterRow);
    problemsList = new QListWidget(problemsPage);
    problemsList->setObjectName("problemsList");
    problemsList->setSelectionMode(QAbstractItemView::NoSelection);
    problemsList->setWordWrap(true);
    problemsLayout->addWidget(problemsList);
    auto *problemsEmpty = new QLabel(tr("尚无问题被检测到。\n运行或编译后，此处将显示错误与警告。"), problemsList->viewport());
    problemsEmpty->setObjectName("problemsEmpty");
    problemsEmpty->setAlignment(Qt::AlignCenter);
    problemsEmpty->setGeometry(problemsList->viewport()->rect());
    problemsEmpty->show();
    connect(filterEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int i = 0; i < problemsList->count(); ++i) {
            auto *item = problemsList->item(i);
            item->setHidden(!text.isEmpty() && !item->text().contains(text, Qt::CaseInsensitive));
        }
    });
    outputTabs->addTab(problemsPage, tr("问题"));

    // ── 标签 2：输出 ──
    auto *outPage = new QWidget(outputTabs);
    outPage->setObjectName("runOutputBody");
    auto *outLayout = new QVBoxLayout(outPage);
    outLayout->setContentsMargins(0, 0, 0, 0);
    output = new QPlainTextEdit(outPage);
    output->setObjectName("runOutput");
    output->setReadOnly(true);
    output->setMaximumBlockCount(10000);
    outLayout->addWidget(output);
    outputTabs->addTab(outPage, tr("输出"));

    // ── 标签 3：调试控制台 ──
    auto *debugPage = new QWidget(outputTabs);
    debugPage->setObjectName("debugConsolePage");
    auto *debugLayout = new QVBoxLayout(debugPage);
    debugLayout->setContentsMargins(8, 8, 8, 8);
    auto *debugRow = new QHBoxLayout;
    const QList<QPair<QString, QString>> commands = {
        {tr("继续"), "continue"}, {tr("单步"), "next"}, {tr("进入"), "step"}, {tr("变量"), "locals"}};
    for (const auto &item : commands) {
        auto *button = new QPushButton(item.first, debugPage);
        button->setObjectName("debug" + item.second);
        debugRow->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, command = item.second] {
            if (!debugging || compiling || process->state() != QProcess::Running) return;
            QString value = command;
            if (activeLanguage == "Python") {
                if (command == "continue") value = "c";
                if (command == "next") value = "n";
                if (command == "step") value = "s";
                if (command == "locals") value = "p locals()";
            } else if (activeLanguage == "Java" && command == "continue") {
                value = "cont";
            } else if (activeLanguage == "C/C++" && command == "locals") {
                value = "info locals";
            }
            process->write(value.toUtf8() + "\n");
        });
    }
    debugRow->addStretch();
    debugLayout->addLayout(debugRow);
    auto *debugHint = new QLabel(tr("调试命令会直接发送到调试器。"), debugPage);
    debugHint->setObjectName("debugHint");
    debugLayout->addWidget(debugHint);
    auto *row = new QHBoxLayout;
    input = new QLineEdit(debugPage); input->setObjectName("runInput"); input->setPlaceholderText(tr("调试命令 / 程序标准输入，按回车发送")); input->setEnabled(false); row->addWidget(input);
    auto *eofButton = new QPushButton(tr("结束输入"), debugPage); row->addWidget(eofButton);
    connect(eofButton, &QPushButton::clicked, this, [this] { if (!compiling) process->closeWriteChannel(); });
    auto *stopButton = new QPushButton(tr("停止"), debugPage); row->addWidget(stopButton); debugLayout->addLayout(row);
    debugLayout->addStretch();
    outputTabs->addTab(debugPage, tr("调试控制台"));

    // ── 标签 4：终端 ──
    terminal = new TerminalPane(outputTabs);
    outputTabs->addTab(terminal, tr("终端"));

    // ── 标签 5：端口 ──
    auto *portsPage = new QWidget(outputTabs);
    portsPage->setObjectName("portsPage");
    auto *portsLayout = new QVBoxLayout(portsPage);
    auto *portsEmpty = new QLabel(tr("尚无已转发的端口。\n在调试会话中转发端口后，此处将显示相关信息。"), portsPage);
    portsEmpty->setAlignment(Qt::AlignCenter);
    portsLayout->addWidget(portsEmpty);
    outputTabs->addTab(portsPage, tr("端口"));

    dockLayout->addWidget(outputTabs);
    connect(outputTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (outputTabs->widget(index) == terminal) static_cast<TerminalPane *>(terminal)->start(projectsRoot());
    });

    // ── 右上角：面板操作 + 关闭 ──
    auto *panelCorner = new QWidget(outputTabs);
    panelCorner->setObjectName("panelCorner");
    auto *cornerLayout = new QHBoxLayout(panelCorner);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(2);
    auto *closePanel = new QToolButton(panelCorner);
    closePanel->setText(tr("×"));
    closePanel->setObjectName("closePanelButton");
    closePanel->setToolTip(tr("关闭面板"));
    closePanel->setAutoRaise(true);
    closePanel->setCursor(Qt::PointingHandCursor);
    connect(closePanel, &QToolButton::clicked, dock, &QWidget::hide);
    cornerLayout->addWidget(closePanel);
    outputTabs->setCornerWidget(panelCorner, Qt::TopRightCorner);

    if (auto *split = window->findChild<QSplitter *>("codeOutputSplitter")) {
        split->addWidget(dock);
        split->setStretchFactor(0, 3);
        split->setStretchFactor(1, 1);
        split->setSizes({520, 220});
    }
    dock->setMinimumHeight(160);
    dock->hide();
    process = new QProcess(this);
    connect(stopButton, &QPushButton::clicked, this, &DevTools::stop);
    connect(input, &QLineEdit::returnPressed, this, [this] {
        if (process->state() == QProcess::Running && !compiling) {
            process->write(input->text().toUtf8()+"\n"); input->clear();
        }
    });
    connect(process, &QProcess::started, this, [this] {
        if (debugging && !compiling && activeLanguage == "Java")
            process->write(("stop in " + javaMainClass + ".main\nrun\n").toUtf8());
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [this] { log(QString::fromLocal8Bit(process->readAllStandardOutput())); });
    connect(process, &QProcess::readyReadStandardError, this, [this] { log(QString::fromLocal8Bit(process->readAllStandardError())); });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { log(tr("\n启动失败：")+process->errorString()); compiling=false; input->setEnabled(false); emit activityChanged(false); emit executionFinished(-1); }
    });
    connect(process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (compiling && !stopping && code == 0 && status == QProcess::NormalExit) {
            compiling=false; QTimer::singleShot(0, this, &DevTools::runExecutable); return;
        }
        log(stopping ? tr("\n已停止。\n") : tr("\n进程结束，退出码：%1\n").arg(code));
        compiling=false; input->setEnabled(false); emit activityChanged(false); emit executionFinished(code);
    });
}
DevTools::~DevTools() { process->disconnect(this); stop(); process->waitForFinished(3000); }
bool DevTools::busy() const { return process->state() != QProcess::NotRunning || compiling; }
void DevTools::log(const QString &text) {
    auto cursor = output->textCursor(); cursor.movePosition(QTextCursor::End); cursor.insertText(text); output->setTextCursor(cursor); output->ensureCursorVisible();
    // 解析编译错误/警告，更新「问题」面板
    const QStringList lines = text.split('\n');
    for (const QString &line : lines)
        if (!line.trimmed().isEmpty())
            updateProblems(line);
}

void DevTools::updateProblems(const QString &line) {
    if (!problemsList) return;
    // 常见编译错误/警告模式：文件:行:列: 错误/警告: 描述
    const QRegularExpression re(
        R"(^(.+?):(\d+):(\d+):\s*(错误|警告|error|warning|Error|Warning):?\s*(.*)$)");
    const auto m = re.match(line);
    if (m.hasMatch()) {
        const QString file = m.captured(1).trimmed();
        const QString kind = m.captured(4);
        const QString msg = m.captured(5).trimmed();
        const bool isError = kind.contains("错误") || kind.compare("error", Qt::CaseInsensitive) == 0;
        auto *item = new QListWidgetItem(
            tr("%1 · %2").arg(isError ? tr("错误") : tr("警告"), msg));
        item->setToolTip(file);
        item->setData(Qt::UserRole, file + ":" + m.captured(2) + ":" + m.captured(3));
        item->setForeground(isError ? QColor(0xc6, 0x2b, 0x2b) : QColor(0xb0, 0x7a, 0x00));
        problemsList->addItem(item);
        ++problemCount;
    } else {
        // 宽松匹配：含 "error" 或 "错误" 的普通行也记入
        const QString t = line.trimmed();
        const bool hasError = t.contains("error", Qt::CaseInsensitive) || t.contains("错误");
        const bool hasWarn = t.contains("warning", Qt::CaseInsensitive) || t.contains("警告");
        if (hasError || hasWarn) {
            auto *item = new QListWidgetItem(t);
            item->setForeground(hasError ? QColor(0xc6, 0x2b, 0x2b) : QColor(0xb0, 0x7a, 0x00));
            problemsList->addItem(item);
            ++problemCount;
        }
    }
    refreshProblemsBadge();
}

void DevTools::refreshProblemsBadge() {
    outputTabs->setTabText(0, problemCount > 0 ? tr("问题 (%1)").arg(problemCount) : tr("问题"));
}
void DevTools::stop() {
    stopping = true;
    if (process->state() == QProcess::NotRunning)
        return;
    if (debugging && activeLanguage == "Java") {
        process->write("exit\n");
        QTimer::singleShot(1000, process, [this] {
            if (stopping && process->state() != QProcess::NotRunning)
                process->kill();
        });
    } else {
        process->kill();
    }
}

void DevTools::buildAndRun(const QString &source, const QString &project, bool debug) {
    if (busy())
        return;
    dock->show();
    outputTabs->setCurrentIndex(1); // 输出
    output->clear();
    if (problemsList) { problemsList->clear(); problemCount = 0; refreshProblemsBadge(); }
    const QString suffix = QFileInfo(source).suffix().toLower();
    const bool java = suffix == "java";
    const bool python = suffix == "py" || suffix == "pyw";
    const bool cpp = QStringList{"c", "cpp", "cc", "cxx"}.contains(suffix);
    if (!java && !python && !cpp) {
        log(tr("请打开 C/C++、Java 或 Python 源文件。"));
        return;
    }
    activeLanguage = java ? "Java" : python ? "Python" : "C/C++";
    debugging = debug;
    const QString tool = cpp ? compilerForSource(compilerPath(), source) : runtimePath(activeLanguage);
    if (!QFileInfo(tool).isExecutable()) {
        log(tr("请先配置 %1 开发工具。").arg(activeLanguage));
        if (cpp) showCompilers(); else showRuntime(activeLanguage);
        return;
    }
    const QString build = dataRoot() + "/cache/builds/" +
        QString::fromLatin1(QCryptographicHash::hash(
            QFileInfo(source).absoluteFilePath().toUtf8(), QCryptographicHash::Sha256).toHex());
    if (!QDir().mkpath(build)) {
        log(tr("无法创建构建目录。"));
        return;
    }
    workingDirectory = project.isEmpty() ? QFileInfo(source).absolutePath() : project;
    toolBin = QFileInfo(tool).absolutePath();
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("PATH", toolBin + ";" + environment.value("PATH"));
    environment.insert("PYTHONIOENCODING", "utf-8");
    environment.insert("PYTHONUTF8", "1");
    process->setProcessEnvironment(environment);
    process->setWorkingDirectory(workingDirectory);
    launchArguments.clear();
    stopping = false;
    compiling = false;
    input->setEnabled(false);

    QStringList compileArguments;
    if (python) {
        executable = tool;
        launchArguments = debug ? QStringList{"-u", "-m", "pdb", QFileInfo(source).absoluteFilePath()}
                                : QStringList{"-u", QFileInfo(source).absoluteFilePath()};
        if (debug) log(tr("PDB 命令：b 行号 设置断点，n 单步，s 进入，c 继续，p 变量，q 退出。\n"));
        emit activityChanged(true);
        runExecutable();
        return;
    }
    if (java) {
        QFile file(source);
        if (!file.open(QIODevice::ReadOnly)) { log(file.errorString()); return; }
        QString text = QString::fromUtf8(file.readAll());
        text.remove(QRegularExpression("/\\*.*?\\*/", QRegularExpression::DotMatchesEverythingOption));
        text.remove(QRegularExpression("//[^\n]*"));
        const auto package = QRegularExpression(
            R"((?:^|\n)\s*package\s+([\w.]+)\s*;)").match(text);
        javaMainClass = (package.hasMatch() ? package.captured(1) + "." : QString()) +
                        QFileInfo(source).completeBaseName();
        executable = toolBin + (debug ? "/jdb.exe" : "/java.exe");
        if (!QFileInfo::exists(executable)) { log(tr("所选 JDK 缺少 java.exe 或 jdb.exe。")); return; }
        launchArguments = {"-classpath", build, javaMainClass};
        if (debug)
            launchArguments = {"-sourcepath", workingDirectory, "-classpath", build, javaMainClass};
        compileArguments = {"-g", "-encoding", "UTF-8", "-d", build,
                            "-sourcepath", workingDirectory, QFileInfo(source).absoluteFilePath()};
        if (debug) log(tr("JDB 命令：stop at 类名:行号 设置断点，next 单步，step 进入，cont 继续，locals 查看变量，exit 退出。\n"));
    } else {
        executable = build + "/program.exe";
        if (debug) {
            executable = toolBin + "/gdb.exe";
            if (!QFileInfo::exists(executable)) { log(tr("当前工具链没有 gdb.exe，请安装包含 GDB 的工具链。")); return; }
            launchArguments = {"--quiet", build + "/program.exe"};
        }
        compileArguments = {suffix == "c" ? "-std=c17" : "-std=c++17", "-g", "-Wall",
                            QFileInfo(source).absoluteFilePath(), "-o", build + "/program.exe"};
    }
    log(tr("编译：%1\n源文件：%2\n").arg(tool, source));
    compiling = true;
    emit activityChanged(true);
    process->start(tool, compileArguments);
}

void DevTools::runExecutable() {
    if (stopping) { compiling = false; emit activityChanged(false); return; }
    log(tr("\n%1：%2\n").arg(debugging ? tr("调试") : tr("运行"), executable));
    input->setPlaceholderText(debugging ? tr("输入调试命令，按回车发送") : tr("程序标准输入，按回车发送"));
    input->setEnabled(true);
    process->start(executable, launchArguments);
}
