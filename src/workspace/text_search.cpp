#include "workspace/text_search.hpp"
#include "workspace/file_index.hpp"
#include "workspace/workspace_root.hpp"

#include <QFile>
#include <QStringDecoder>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace omanotes {
namespace {
constexpr qint64 kFileBytes = 4LL * 1024 * 1024;
constexpr qint64 kTotalBytes = 64LL * 1024 * 1024;
constexpr std::size_t kResults = 200;

std::optional<QString> readText(const std::filesystem::path& path, const WorkspaceRoot& root,
                                qint64& remaining, const std::stop_token& stop) {
    const auto resolved = root.resolveFile(path);
    if (!resolved) {
        return std::nullopt;
    }
    const int descriptor =
        ::open(resolved->c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0) {
        return std::nullopt;
    }
    struct stat status{};
    if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size > kFileBytes) {
        ::close(descriptor);
        return std::nullopt;
    }
    QFile file;
    if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor);
        return std::nullopt;
    }
    QByteArray bytes;
    while (!file.atEnd() && bytes.size() <= kFileBytes && remaining > 0) {
        if (stop.stop_requested()) {
            return std::nullopt;
        }
        const auto chunk = file.read(std::min(qint64{65536}, remaining));
        if (chunk.isEmpty() && !file.atEnd()) {
            return std::nullopt;
        }
        remaining -= chunk.size();
        bytes.append(chunk);
    }
    if (!file.atEnd() || bytes.size() > kFileBytes || file.error() != QFileDevice::NoError ||
        bytes.contains('\0')) {
        return std::nullopt;
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder(bytes);
    return decoder.hasError() ? std::nullopt : std::optional<QString>{text};
}
} // namespace

WorkspaceSearch::WorkspaceSearch(std::filesystem::path root) : root_(std::move(root)) {}

SearchResults WorkspaceSearch::findFiles(QStringView query, const std::stop_token& stop) const {
    const auto index = indexMarkdownFiles(root_, stop);
    SearchResults result{{}, index.truncated, index.cancelled, index.error};
    for (const auto& file : index.files) {
        if (stop.stop_requested()) {
            result.cancelled = true;
            return result;
        }
        if (const auto score = scoreFile(file.relativeName, query); score.has_value()) {
            result.matches.push_back({file.path, file.relativeName, -1, 0, {}, *score});
        }
    }
    std::ranges::sort(result.matches, [](const SearchMatch& left, const SearchMatch& right) {
        return left.score != right.score ? left.score > right.score
                                         : left.relativeName < right.relativeName;
    });
    if (result.matches.size() > kResults) {
        result.matches.resize(kResults);
        result.truncated = true;
    }
    // The palette preview uses the same bounded read policy as text search.
    const auto root = WorkspaceRoot::resolve(root_);
    qint64 remaining = kTotalBytes;
    if (root) {
        for (auto& match : result.matches) {
            if (const auto text = readText(match.path, *root, remaining, stop)) {
                match.preview = text->left(2000);
            }
            if (stop.stop_requested()) {
                result.cancelled = true;
                break;
            }
        }
    }
    return result;
}

SearchResults WorkspaceSearch::findText(QStringView query, const std::stop_token& stop) const {
    SearchResults result;
    if (query.isEmpty() || query.size() > 128) {
        return result;
    }
    const auto index = indexMarkdownFiles(root_, stop);
    result = {{}, index.truncated, index.cancelled, index.error};
    const auto root = WorkspaceRoot::resolve(root_);
    if (!root) {
        result.error = QString::fromStdString(root.error().message);
        return result;
    }
    qint64 remaining = kTotalBytes;
    for (const auto& file : index.files) {
        if (stop.stop_requested()) {
            result.cancelled = true;
            break;
        }
        if (remaining <= 0 || result.matches.size() >= kResults) {
            result.truncated = true;
            break;
        }
        const auto text = readText(file.path, *root, remaining, stop);
        if (!text) {
            continue;
        }
        const QStringView view(*text);
        qsizetype offset = 0;
        int lineNumber = 0;
        while (offset < view.size()) {
            if (stop.stop_requested()) {
                result.cancelled = true;
                return result;
            }
            auto end = view.indexOf(QLatin1Char('\n'), offset);
            if (end < 0) {
                end = view.size();
            }
            const auto line = view.sliced(offset, end - offset);
            if (line.size() <= 4096) {
                const auto column = line.indexOf(query, 0, Qt::CaseInsensitive);
                if (column >= 0) {
                    result.matches.push_back({file.path, file.relativeName, lineNumber,
                                              static_cast<int>(column), line.toString(), 0});
                    if (result.matches.size() >= kResults) {
                        result.truncated = true;
                        return result;
                    }
                }
            }
            offset = end + 1;
            ++lineNumber;
        }
    }
    return result;
}
} // namespace omanotes
