#include "projectsearch.h"
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
#include <atomic>
#include <memory>
namespace {
struct Hit { QString path; int line; QString text; };
struct Scan { QList<Hit> hits; int visited = 0; bool limited = false; };
Scan search(const QString &folder, const QString &keyword, const std::shared_ptr<std::atomic_bool> &cancel) {
    Scan result;
    QStringList folders{folder}; qint64 bytes = 0;
    QElapsedTimer time; time.start();
    const QSet<QString> excluded{".git", ".svn", "node_modules", "build", ".venv", "venv", "__pycache__"};
    while (!folders.isEmpty() && !cancel->load()) {
        const QString next = folders.takeLast();
        QDirIterator entries(next, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks);
        while (entries.hasNext()) {
            entries.next(); const auto entry = entries.fileInfo();
            if (cancel->load()) return result;
            if (++result.visited > 5000 || time.elapsed() > 3000 || bytes > 20 * 1024 * 1024 || result.hits.size() >= 500) {
                result.limited = true; return result;
            }
            const QString relative = QDir(folder).relativeFilePath(entry.canonicalFilePath());
            if (entry.canonicalFilePath().isEmpty() || relative == ".." || relative.startsWith("../") || QDir::isAbsolutePath(relative)) continue;
            if (entry.isDir()) { if (!excluded.contains(entry.fileName())) folders.append(entry.absoluteFilePath()); continue; }
            if (!entry.isFile()) continue;
            if (entry.fileName().contains(keyword, Qt::CaseInsensitive)) {
                result.hits.append({entry.absoluteFilePath(), 0, entry.fileName()}); continue;
            }
            if (entry.size() > 512 * 1024) continue;
            QFile file(entry.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) continue;
            const auto data = file.read(512 * 1024); bytes += data.size();
            if (data.contains('\0')) continue;
            const auto lines = QString::fromUtf8(data).split('\n');
            for (int i = 0; i < lines.size(); ++i) {
                if (lines[i].contains(keyword, Qt::CaseInsensitive)) {
                    result.hits.append({entry.absoluteFilePath(), i + 1, lines[i].trimmed().left(180)});
                    if (result.hits.size() >= 500) { result.limited = true; return result; }
                }
            }
        }
    }
    return result;
}
}
QWidget *createProjectSearch(const QString &root, const QString &initial,
                            std::function<void(const QString &, int)> open, QWidget *parent) {
    auto *page = new QWidget(parent); page->setObjectName("projectSearchPage");
    page->setProperty("toolkitName", "Project Search");
    auto *layout = new QVBoxLayout(page);
    auto *input = new QLineEdit(page); input->setObjectName("projectSearchInput");
    input->setPlaceholderText(QObject::tr("搜索文件名与文本内容（不区分大小写）")); layout->addWidget(input);
    auto *description = new QLabel(QObject::tr("%1\n跳过 .git、build、node_modules、虚拟环境和链接。扫描上限：3 秒、5000 项、20 MiB、500 个结果。").arg(root), page);
    description->setWordWrap(true); layout->addWidget(description);
    auto *status = new QLabel(page); status->setObjectName("projectSearchStatus"); layout->addWidget(status);
    auto *results = new QTreeWidget(page); results->setObjectName("projectSearchResults");
    results->setHeaderLabels({QObject::tr("文件"), QObject::tr("行"), QObject::tr("匹配内容")});
    results->setRootIsDecorated(false); layout->addWidget(results, 1);
    auto *cancelButton = new QPushButton(QObject::tr("取消搜索"), page); layout->addWidget(cancelButton);
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
    QObject::connect(delay, &QTimer::timeout, page, [=] {
        const QString keyword = input->text().trimmed();
        if (keyword.isEmpty()) { status->setText(QObject::tr("请输入搜索内容。")); return; }
        if (!QFileInfo(root).isDir()) { status->setText(QObject::tr("请先打开项目文件夹。")); return; }
        auto token = std::make_shared<std::atomic_bool>(false); *active = token;
        auto found = std::make_shared<Scan>(); status->setText(QObject::tr("搜索中…"));
        auto *worker = QThread::create([=] { *found = search(root, keyword, token); });
        QObject::connect(worker, &QThread::finished, page, [=] {
            if (token->load() || *active != token) return;
            results->clear();
            for (const auto &hit : found->hits) {
                auto *item = new QTreeWidgetItem(results, {QDir(root).relativeFilePath(hit.path), QString::number(hit.line), hit.text});
                item->setData(0, Qt::UserRole, hit.path); item->setData(1, Qt::UserRole, hit.line);
            }
            results->resizeColumnToContents(0);
            status->setText(QObject::tr("%1 个结果，扫描 %2 项%3").arg(found->hits.size()).arg(found->visited)
                .arg(found->limited ? QObject::tr("；达到扫描上限，结果不完整。") : QString()));
        });
        QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater); worker->start();
    });
    QObject::connect(results, &QTreeWidget::itemActivated, page, [open](QTreeWidgetItem *item, int) {
        if (open) open(item->data(0, Qt::UserRole).toString(), item->data(1, Qt::UserRole).toInt());
    });
    input->setText(initial);
    return page;
}
