#ifndef OMANOTES_UI_SEARCH_PALETTE_HPP
#define OMANOTES_UI_SEARCH_PALETTE_HPP

#include "workspace/text_search.hpp"

#include <QDialog>
#include <QTimer>

#include <cstdint>
#include <filesystem>
#include <stop_token>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QThread;

namespace omanotes {
class SearchPalette final : public QDialog {
    Q_OBJECT
  public:
    explicit SearchPalette(std::filesystem::path root, QWidget* parent = nullptr);
    ~SearchPalette() override;
    void begin(SearchKind kind);
  signals:
    void matchChosen(const std::filesystem::path& path, int line, int column);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void schedule();
    void startSearch();
    void chooseCurrent();
    void showResults(SearchResults results);
    std::filesystem::path root_;
    SearchKind kind_{SearchKind::Files};
    QLineEdit* query_ = nullptr;
    QListWidget* results_ = nullptr;
    QPlainTextEdit* preview_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer debounce_;
    QThread* worker_ = nullptr;
    std::stop_source cancellation_;
    std::uint64_t generation_{0};
    bool pending_{false};
    SearchResults matches_;
};
} // namespace omanotes
#endif
