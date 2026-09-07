#ifndef OMANOTES_WORKSPACE_FILE_INDEX_HPP
#define OMANOTES_WORKSPACE_FILE_INDEX_HPP

#include <QString>
#include <QStringView>

#include <filesystem>
#include <optional>
#include <stop_token>
#include <vector>

namespace omanotes {
struct IndexedFile {
    std::filesystem::path path;
    QString relativeName;
};
struct FileIndex {
    std::vector<IndexedFile> files;
    bool truncated{false};
    bool cancelled{false};
    QString error;
};
/// Enumerate at most 20,000 entries, without following symlinks or hidden
/// directories. Sorted root-relative names make result ordering reproducible.
[[nodiscard]] FileIndex indexMarkdownFiles(const std::filesystem::path& root,
                                           const std::stop_token& stop = {});
/// Case-insensitive subsequence score; contiguous and filename matches win.
[[nodiscard]] std::optional<int> scoreFile(QStringView name, QStringView query);
} // namespace omanotes
#endif
