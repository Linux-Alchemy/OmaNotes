#include "ui/search_palette.hpp"

#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <utility>

namespace omanotes {
SearchPalette::SearchPalette(std::filesystem::path root, QWidget* parent)
    : QDialog(parent), root_(std::move(root)) {
    setObjectName(QStringLiteral("searchPalette"));
    setWindowModality(Qt::WindowModal);
    resize(800, 480);
    auto* layout = new QVBoxLayout(this);
    query_ = new QLineEdit(this);
    query_->setObjectName(QStringLiteral("searchQuery"));
    query_->setAccessibleName(QStringLiteral("Search query"));
    query_->setMaxLength(128);
    layout->addWidget(query_);
    auto* splitter = new QSplitter(this);
    results_ = new QListWidget(splitter);
    results_->setObjectName(QStringLiteral("searchResults"));
    results_->setAccessibleName(QStringLiteral("Search results"));
    preview_ = new QPlainTextEdit(splitter);
    preview_->setObjectName(QStringLiteral("searchPreview"));
    preview_->setAccessibleName(QStringLiteral("Result preview"));
    preview_->setReadOnly(true);
    splitter->addWidget(results_);
    splitter->addWidget(preview_);
    layout->addWidget(splitter, 1);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("searchStatus"));
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Open)->setDefault(false);
    // Plain text, no platform-theme stock icons.
    buttons->button(QDialogButtonBox::Open)->setIcon(QIcon());
    buttons->button(QDialogButtonBox::Cancel)->setIcon(QIcon());
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { chooseCurrent(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(query_, &QLineEdit::textChanged, this, [this] { schedule(); });
    connect(results_, &QListWidget::itemDoubleClicked, this, [this] { chooseCurrent(); });
    connect(results_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && static_cast<std::size_t>(row) < matches_.matches.size()) {
            const auto& match = matches_.matches[static_cast<std::size_t>(row)];
            preview_->setPlainText(match.preview.isEmpty()
                                       ? QStringLiteral("No text preview available")
                                       : match.preview);
        } else {
            preview_->clear();
        }
    });
    query_->installEventFilter(this);
    results_->installEventFilter(this);
    debounce_.setInterval(100);
    debounce_.setSingleShot(true);
    connect(&debounce_, &QTimer::timeout, this, [this] { startSearch(); });
    connect(this, &QDialog::finished, this, [this] {
        ++generation_;
        pending_ = false;
        debounce_.stop();
        cancellation_.request_stop();
    });
}

SearchPalette::~SearchPalette() {
    cancellation_.request_stop();
    if (worker_ != nullptr) {
        worker_->wait();
    }
}

void SearchPalette::begin(SearchKind kind) {
    kind_ = kind;
    setWindowTitle(kind == SearchKind::Files ? QStringLiteral("Find files")
                                             : QStringLiteral("Search text"));
    query_->setPlaceholderText(kind == SearchKind::Files
                                   ? QStringLiteral("Find a Markdown file…")
                                   : QStringLiteral("Search saved Markdown text…"));
    query_->clear();
    show();
    query_->setFocus(Qt::OtherFocusReason);
    schedule();
}

void SearchPalette::schedule() {
    ++generation_;
    cancellation_.request_stop();
    pending_ = true;
    matches_ = {};
    results_->clear();
    preview_->clear();
    status_->setText(QStringLiteral("Searching…"));
    debounce_.start();
}

void SearchPalette::startSearch() {
    if (worker_ != nullptr || !pending_ || !isVisible()) {
        return;
    }
    pending_ = false;
    cancellation_ = std::stop_source{};
    const auto stop = cancellation_.get_token();
    const auto generation = generation_;
    const auto query = query_->text();
    const auto kind = kind_;
    auto result = std::make_shared<SearchResults>();
    worker_ = QThread::create([root = root_, query, kind, stop, result] {
        const WorkspaceSearch search(root);
        *result = kind == SearchKind::Files ? search.findFiles(query, stop)
                                            : search.findText(query, stop);
    });
    worker_->setParent(this);
    connect(worker_, &QThread::finished, this, [this, generation, result] {
        worker_->deleteLater();
        worker_ = nullptr;
        if (generation == generation_ && isVisible() && !result->cancelled) {
            showResults(std::move(*result));
        }
        if (pending_ && !debounce_.isActive()) {
            startSearch();
        }
    });
    worker_->start();
}

void SearchPalette::showResults(SearchResults results) {
    matches_ = std::move(results);
    for (const auto& match : matches_.matches) {
        results_->addItem(kind_ == SearchKind::Files ? match.relativeName
                                                     : QStringLiteral("%1:%2  %3")
                                                           .arg(match.relativeName)
                                                           .arg(match.line + 1)
                                                           .arg(match.preview.left(120)));
    }
    if (!matches_.error.isEmpty()) {
        status_->setText(matches_.error);
    } else {
        status_->setText(
            QStringLiteral("%1 results%2 · ↑/↓ or Ctrl+J/K · Enter opens · Esc cancels")
                .arg(matches_.matches.size())
                .arg(matches_.truncated ? QStringLiteral(" (limit reached)") : QString{}));
    }
    if (results_->count() > 0) {
        results_->setCurrentRow(0);
    }
}

void SearchPalette::chooseCurrent() {
    const auto row = results_->currentRow();
    if (row < 0 || static_cast<std::size_t>(row) >= matches_.matches.size()) {
        return;
    }
    const auto match = matches_.matches[static_cast<std::size_t>(row)];
    accept();
    emit matchChosen(match.path, match.line, match.column);
}

bool SearchPalette::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        const auto& key = *static_cast<QKeyEvent*>(event);
        if (key.key() == Qt::Key_Return || key.key() == Qt::Key_Enter) {
            chooseCurrent();
            return true;
        }
        const bool down = key.key() == Qt::Key_Down ||
                          (key.key() == Qt::Key_J && key.modifiers() == Qt::ControlModifier);
        const bool up = key.key() == Qt::Key_Up ||
                        (key.key() == Qt::Key_K && key.modifiers() == Qt::ControlModifier);
        if ((down || up) && results_->count() > 0) {
            results_->setCurrentRow(
                std::clamp(results_->currentRow() + (down ? 1 : -1), 0, results_->count() - 1));
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
} // namespace omanotes
