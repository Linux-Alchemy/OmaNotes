#ifndef OMANOTES_WORKSPACE_TEXT_SEARCH_HPP
#define OMANOTES_WORKSPACE_TEXT_SEARCH_HPP

#include <QString>
#include <QStringView>

#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <vector>

namespace omanotes {
enum class SearchKind : std::uint8_t { Files, Text };
struct SearchMatch {
    std::filesystem::path path;
    QString relativeName;
    int line{0};
    int column{0};
    QString preview;
    int score{0};
};
struct SearchResults {
    std::vector<SearchMatch> matches;
    bool truncated{false};
    bool cancelled{false};
    QString error;
};
/// Disk-based, literal case-insensitive search. No process, network or index
/// files. Results and I/O budgets are documented in docs/search.md.
class WorkspaceSearch final {
  public:
    explicit WorkspaceSearch(std::filesystem::path root);
    [[nodiscard]] SearchResults findFiles(QStringView query,
                                          const std::stop_token& stop = {}) const;
    [[nodiscard]] SearchResults findText(QStringView query, const std::stop_token& stop = {}) const;

  private:
    std::filesystem::path root_;
};
} // namespace omanotes
#endif
