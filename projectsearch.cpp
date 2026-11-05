#include "projectsearch.h"
#include "filesystempaths.h"
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QThread>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QSet>
#include <QRegularExpression>
#include <QHeaderView>
#include <atomic>
#include <memory>
namespace {
struct Hit { QString path; int line; QString text; };
struct Scan { QList<Hit> hits; int visited = 0; int unread = 0; bool limited = false; };
Scan search(const QString &folder, const QString &keyword, bool todos, const std::shared_ptr<std::atomic_bool> &cancel) {
    Scan result;
    const QString realRoot = resolvedFileSystemPath(folder);
    if (realRoot.isEmpty()) return result;
    const QDir rootDirectory(realRoot);
    QStringList folders{folder}; qint64 bytes = 0;
    QElapsedTimer time; time.start();
    const QSet<QString> excluded{".git", ".svn", "node_modules", "build", ".venv", "venv", "__pycache__"};
    while (!folders.isEmpty() && !cancel->load()) {
        const QString next = folders.takeLast();
        QDirIterator entries(next, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks);
        while (entries.hasNext()) {
            entries.next(); const auto entry = entries.fileInfo();
            if (cancel->load()) return result;
            if (entry.isSymLink() || entry.isJunction()) continue;
            if (++result.visited > 5000 || time.elapsed() > 3000 || bytes > 20 * 1024 * 1024 || result.hits.size() >= 500) {
                result.limited = true; return result;
            }
            const QString resolved = resolvedFileSystemPath(entry.absoluteFilePath());
            const QString relative = rootDirectory.relativeFilePath(resolved);
            if (resolved.isEmpty() || relative == ".." || relative.startsWith("../") || QDir::isAbsolutePath(relative)) continue;
            if (entry.isDir()) { if (!excluded.contains(entry.fileName())) folders.append(entry.absoluteFilePath()); continue; }
            if (!entry.isFile()) continue;
            if (!todos && entry.fileName().contains(keyword, Qt::CaseInsensitive)) {
                result.hits.append({entry.absoluteFilePath(), 0, entry.fileName()}); continue;
            }
            if (entry.size() > 512 * 1024) { ++result.unread; continue; }
            QFile file(entry.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) { ++result.unread; continue; }
            const auto data = file.read(512 * 1024); bytes += data.size();
            if (data.contains('\0')) continue;
            const auto lines = QString::fromUtf8(data).split('\n');
            static const QRegularExpression tags(QStringLiteral(R"(\b(?:TODO|FIXME|BUG|HACK|XXX)\b)"), QRegularExpression::CaseInsensitiveOption);
            for (int i = 0; i < lines.size(); ++i) {
                if (lines[i].contains(keyword, Qt::CaseInsensitive) && (!todos || tags.match(lines[i]).hasMatch())) {
                    result.hits.append({entry.absoluteFilePath(), i + 1, lines[i].trimmed().left(180)});
                    if (result.hits.size() >= 500) { result.limited = true; return result; }
                }
            }
        }
    }
    return result;
}
}
static QWidget *createSearchPage(const QString &root, const QString &initial,
                            std::function<void(const QString &, int)> open, QWidget *parent, bool todos) {
    auto *page = new QWidget(parent); page->setObjectName(todos ? "todoTreePage" : "projectSearchPage");
    page->setProperty("toolkitName", todos ? "TODO Tree" : "Project Search");
    page->setProperty("searchRoot", root);
    auto *layout = new QVBoxLayout(page);
    auto *input = new QLineEdit(page); input->setObjectName("projectSearchInput");
    input->setPlaceholderText(todos ? QObject::tr("筛选待办内容，可留空显示全部标记") : QObject::tr("搜索文件名与文本内容（不区分大小写）")); layout->addWidget(input);
    if (todos) {
        auto *heading = new QLabel(QObject::tr("TODO Tree · TODO / FIXME / BUG / HACK / XXX\n扫描文件中的文本标记，包含普通文本；点击结果跳转源码。"), page);
        heading->setWordWrap(true); layout->insertWidget(0, heading);
    }
    auto *description = new QLabel(QObject::tr("项目：%1\n跳过依赖、构建目录和链接；过大或无法读取的文件会在结果中提示。").arg(root), page);
    description->setToolTip(QObject::tr("扫描上限：3 秒、5000 项、总计 20 MiB、单文件 512 KiB、500 个结果。达到上限时会提示结果不完整。"));
    description->setWordWrap(true); layout->addWidget(description);
    auto *status = new QLabel(page); status->setObjectName("projectSearchStatus"); layout->addWidget(status);
    auto *results = new QTreeWidget(page); results->setObjectName("projectSearchResults");
    results->setHeaderLabels({QObject::tr("文件"), QObject::tr("行"), QObject::tr("匹配内容")});
    results->setUniformRowHeights(true);
    results->setStyleSheet("QTreeWidget::item { padding: 5px 8px; }");
    results->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    results->header()->setStretchLastSection(true);
    results->setRootIsDecorated(false); layout->addWidget(results, 1);
    auto *controls = new QHBoxLayout;
    auto *refresh = new QPushButton(QObject::tr("刷新"), page); refresh->setObjectName("projectSearchRefresh"); controls->addWidget(refresh);
    auto *cancelButton = new QPushButton(QObject::tr("取消搜索"), page); cancelButton->setObjectName("projectSearchCancel"); controls->addWidget(cancelButton);
    layout->addLayout(controls);
    auto active = std::make_shared<std::shared_ptr<std::atomic_bool>>();
    auto *delay = new QTimer(page); delay->setSingleShot(true); delay->setInterval(300);
    QObject::connect(page, &QObject::destroyed, [active] { if (*active) (*active)->store(true); });
    QObject::connect(cancelButton, &QPushButton::clicked, page, [active, status, delay] {
        delay->stop();
        if (*active) (*active)->store(true);
        status->setText(QObject::tr("已取消。"));
    });
    QObject::connect(input, &QLineEdit::textChanged, page, [=] {
        if (*active) (*active)->store(true);
        results->clear(); delay->start();
    });
    QObject::connect(refresh, &QPushButton::clicked, page, [=] {
        if (*active) (*active)->store(true);
        results->clear(); delay->start(0);
    });
    QObject::connect(delay, &QTimer::timeout, page, [=] {
        const QString keyword = input->text().trimmed();
        if (keyword.isEmpty() && !todos) { status->setText(QObject::tr("请输入搜索内容。")); return; }
        if (!QFileInfo(root).isDir()) { status->setText(QObject::tr("请先打开项目文件夹。")); return; }
        auto token = std::make_shared<std::atomic_bool>(false); *active = token;
        auto found = std::make_shared<Scan>(); status->setText(QObject::tr("搜索中…"));
        auto *worker = QThread::create([=] { *found = search(root, keyword, todos, token); });
        QObject::connect(worker, &QThread::finished, page, [=] {
            if (token->load() || *active != token) return;
            results->clear();
            for (const auto &hit : found->hits) {
                auto *item = new QTreeWidgetItem(results, {QDir(root).relativeFilePath(hit.path), hit.line > 0 ? QString::number(hit.line) : QString(QChar(0x2014)), hit.text});
                item->setData(0, Qt::UserRole, hit.path); item->setData(1, Qt::UserRole, hit.line);
            }
            results->resizeColumnToContents(0);
            results->setColumnWidth(0, results->columnWidth(0) + 16);
            status->setText(QObject::tr("%1 个结果，扫描 %2 项%3%4").arg(found->hits.size()).arg(found->visited)
                .arg(found->limited ? QObject::tr("；达到扫描上限，结果不完整。") : QString())
                .arg(found->unread ? QObject::tr("；%1 个文件因过大或无读取权限而跳过内容。").arg(found->unread) : QString()));
        });
        QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater); worker->start();
    });
    QObject::connect(results, &QTreeWidget::itemActivated, page, [open](QTreeWidgetItem *item, int) {
        if (open) open(item->data(0, Qt::UserRole).toString(), item->data(1, Qt::UserRole).toInt());
    });
    input->setText(initial);
    if (todos) delay->start(0);
    return page;
}

QWidget *createProjectSearch(const QString &root, const QString &initial,
                            std::function<void(const QString &, int)> open, QWidget *parent) {
    return createSearchPage(root, initial, std::move(open), parent, false);
}
QWidget *createTodoTree(const QString &root, std::function<void(const QString &, int)> open, QWidget *parent) {
    return createSearchPage(root, {}, std::move(open), parent, true);
}
