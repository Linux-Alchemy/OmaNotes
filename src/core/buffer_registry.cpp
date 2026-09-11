#include "core/buffer_registry.hpp"

#include <QByteArray>
#include <QFile>

#include <algorithm>
#include <system_error>
#include <utility>

namespace omanotes {

namespace {

/// Resolve `path` to one stable identity so a symlink, a relative route, and an
/// absolute route to the same file share a buffer. Falls back to lexical
/// normalisation when the filesystem cannot answer.
std::filesystem::path canonicalIdentity(const std::filesystem::path& path) {
    std::error_code error;
    auto resolved = std::filesystem::weakly_canonical(path, error);
    if (error) {
        return path.lexically_normal();
    }
    return resolved;
}

QString fileDisplayName(const std::filesystem::path& path) {
    return QFile::decodeName(QByteArray::fromStdString(path.filename().native()));
}

} // namespace

BufferId BufferRegistry::createScratch() {
    const auto id = QUuid::createUuid();
    buffers_.push_back(BufferState{id, std::nullopt, scratchDisplayName(), false});
    activeId_ = id;
    return id;
}

std::expected<BufferId, BufferError> BufferRegistry::open(const std::filesystem::path& path) {
    if (path.empty()) {
        return std::unexpected(BufferError{BufferErrorCode::InvalidPath, "No file was named"});
    }

    const auto identity = canonicalIdentity(path);
    const auto existing = std::ranges::find_if(
        buffers_, [&identity](const BufferState& buffer) { return buffer.path == identity; });
    if (existing != buffers_.end()) {
        activeId_ = existing->id;
        return existing->id;
    }

    const auto id = QUuid::createUuid();
    buffers_.push_back(BufferState{id, identity, fileDisplayName(identity), false});
    activeId_ = id;
    return id;
}

bool BufferRegistry::activate(BufferId id) {
    if (find(id) == nullptr) {
        return false;
    }
    activeId_ = id;
    return true;
}

std::expected<void, BufferError> BufferRegistry::close(BufferId id) { return remove(id, false); }

std::expected<void, BufferError> BufferRegistry::closeDiscardingChanges(BufferId id) {
    return remove(id, true);
}

const std::vector<BufferState>& BufferRegistry::buffers() const noexcept { return buffers_; }

std::optional<BufferId> BufferRegistry::activeId() const noexcept { return activeId_; }

const BufferState* BufferRegistry::find(BufferId id) const noexcept {
    const auto match =
        std::ranges::find_if(buffers_, [id](const BufferState& buffer) { return buffer.id == id; });
    return match == buffers_.end() ? nullptr : &*match;
}

std::optional<BufferId> BufferRegistry::findByPath(const std::filesystem::path& path) const {
    if (path.empty()) {
        return std::nullopt;
    }

    const auto identity = canonicalIdentity(path);
    const auto match = std::ranges::find_if(
        buffers_, [&identity](const BufferState& buffer) { return buffer.path == identity; });
    if (match == buffers_.end()) {
        return std::nullopt;
    }
    return match->id;
}

std::optional<BufferId> BufferRegistry::findByExactPath(const std::filesystem::path& path) const {
    if (path.empty()) {
        return std::nullopt;
    }
    const auto match = std::ranges::find_if(
        buffers_, [&path](const BufferState& buffer) { return buffer.path == path; });
    if (match == buffers_.end()) {
        return std::nullopt;
    }
    return match->id;
}

std::optional<std::size_t> BufferRegistry::indexOf(BufferId id) const noexcept {
    const auto match =
        std::ranges::find_if(buffers_, [id](const BufferState& buffer) { return buffer.id == id; });
    if (match == buffers_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(buffers_.begin(), match));
}

std::size_t BufferRegistry::count() const noexcept { return buffers_.size(); }

std::expected<void, BufferError> BufferRegistry::assignPath(BufferId id,
                                                            const std::filesystem::path& path) {
    if (path.empty()) {
        return std::unexpected(BufferError{BufferErrorCode::InvalidPath, "No file was named"});
    }

    const auto match =
        std::ranges::find_if(buffers_, [id](const BufferState& buffer) { return buffer.id == id; });
    if (match == buffers_.end()) {
        return std::unexpected(BufferError{BufferErrorCode::NotFound, "That buffer is not open"});
    }

    const auto identity = canonicalIdentity(path);
    const auto clash = std::ranges::find_if(buffers_, [&identity, id](const BufferState& buffer) {
        return buffer.id != id && buffer.path == identity;
    });
    if (clash != buffers_.end()) {
        return std::unexpected(
            BufferError{BufferErrorCode::AlreadyOpen, "That file is already open in another tab"});
    }

    match->path = identity;
    match->displayName = fileDisplayName(identity);
    return {};
}

bool BufferRegistry::setModified(BufferId id, bool modified) {
    const auto match =
        std::ranges::find_if(buffers_, [id](const BufferState& buffer) { return buffer.id == id; });
    if (match == buffers_.end()) {
        return false;
    }
    match->modified = modified;
    return true;
}

std::optional<BufferId> BufferRegistry::activateNext() { return activateOffset(1); }

std::optional<BufferId> BufferRegistry::activatePrevious() { return activateOffset(-1); }

std::expected<void, BufferError> BufferRegistry::remove(BufferId id, bool discardChanges) {
    const auto position = indexOf(id);
    if (!position.has_value()) {
        return std::unexpected(BufferError{BufferErrorCode::NotFound, "That buffer is not open"});
    }

    const auto index = *position;
    if (buffers_[index].modified && !discardChanges) {
        return std::unexpected(
            BufferError{BufferErrorCode::Modified, "Buffer has unsaved changes"});
    }

    const auto wasActive = activeId_.has_value() && *activeId_ == id;
    buffers_.erase(buffers_.begin() + static_cast<std::ptrdiff_t>(index));

    if (buffers_.empty()) {
        // A window without a buffer has nowhere to type; Vim keeps an unnamed
        // buffer alive for the same reason.
        createScratch();
        return {};
    }

    if (wasActive) {
        const auto successor = std::min(index, buffers_.size() - 1);
        activeId_ = buffers_[successor].id;
    }
    return {};
}

std::optional<BufferId> BufferRegistry::activateOffset(std::ptrdiff_t offset) {
    if (buffers_.empty()) {
        return std::nullopt;
    }

    const auto size = static_cast<std::ptrdiff_t>(buffers_.size());
    std::ptrdiff_t current = 0;
    if (activeId_.has_value()) {
        if (const auto position = indexOf(*activeId_); position.has_value()) {
            current = static_cast<std::ptrdiff_t>(*position);
        }
    }

    const auto next = ((current + offset) % size + size) % size;
    activeId_ = buffers_[static_cast<std::size_t>(next)].id;
    return activeId_;
}

} // namespace omanotes
