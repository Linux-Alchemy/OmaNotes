#include "workspace/file_index.hpp"
#include "workspace/workspace_root.hpp"

#include <QFile>

#include <algorithm>
#include <system_error>

namespace omanotes {
FileIndex indexMarkdownFiles(const std::filesystem::path& rootPath, const std::stop_token& stop) {
    FileIndex result;
    const auto root = WorkspaceRoot::resolve(rootPath);
    if (!root) {
        result.error = QString::fromStdString(root.error().message);
        return result;
    }
    std::vector<std::filesystem::path> pending{root->path()};
    std::size_t inspected = 0;
    while (!pending.empty()) {
        if (stop.stop_requested()) {
            result.cancelled = true;
            break;
        }
        const auto directory = pending.back();
        pending.pop_back();
        if (!root->contains(directory)) {
            continue;
        }
        std::error_code error;
        std::vector<std::filesystem::directory_entry> entries;
        for (std::filesystem::directory_iterator entry(directory, error), end;
             !error && entry != end; entry.increment(error)) {
            if (stop.stop_requested()) {
                result.cancelled = true;
                return result;
            }
            if (++inspected > 20000) {
                // Do not return an arbitrary filesystem-order subset of this
                // directory when its enumeration exceeds the budget.
                result.truncated = true;
                entries.clear();
                pending.clear();
                break;
            }
            entries.push_back(*entry);
        }
        std::ranges::sort(entries, {}, &std::filesystem::directory_entry::path);
        for (const auto& entry : entries) {
            const auto status = entry.symlink_status(error);
            if (error || std::filesystem::is_symlink(status)) {
                continue;
            }
            const auto name =
                QFile::decodeName(QByteArray::fromStdString(entry.path().filename().native()));
            if (std::filesystem::is_directory(status)) {
                if (!name.startsWith(QLatin1Char('.'))) {
                    pending.push_back(entry.path());
                }
            } else if (std::filesystem::is_regular_file(status) &&
                       name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive) &&
                       root->contains(entry.path())) {
                result.files.push_back(
                    {entry.path(), QFile::decodeName(QByteArray::fromStdString(
                                       entry.path().lexically_relative(root->path()).native()))});
            }
        }
    }
    std::ranges::sort(result.files, {}, &IndexedFile::relativeName);
    return result;
}

std::optional<int> scoreFile(QStringView name, QStringView query) {
    if (query.isEmpty()) {
        return 0;
    }
    if (query.size() > 128 || name.size() > 4096) {
        return std::nullopt;
    }
    const auto folded = name.toString().toCaseFolded();
    const auto needle = query.toString().toCaseFolded();
    qsizetype position = 0;
    qsizetype previous = -2;
    int score = 0;
    for (const auto character : needle) {
        const auto found = folded.indexOf(character, position);
        if (found < 0) {
            return std::nullopt;
        }
        score += 10;
        if (found == previous + 1) {
            score += 15;
        }
        if (found == 0 || folded.at(found - 1) == QLatin1Char('/')) {
            score += 20;
        }
        previous = found;
        position = found + 1;
    }
    const auto filename = folded.sliced(folded.lastIndexOf(QLatin1Char('/')) + 1);
    if (filename.startsWith(needle)) {
        score += 100;
    }
    return score - static_cast<int>(folded.size());
}
} // namespace omanotes
