#include "session/session_store.hpp"

#include "session/instance_lock.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QStandardPaths>
#include <QString>

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <optional>
#include <system_error>
#include <utility>

namespace omanotes {

namespace {

constexpr mode_t kDirectoryMode = 0700;
constexpr mode_t kFileMode = 0600;
constexpr auto kFileName = "session.json";

QString text(const std::filesystem::path& path) { return QString::fromStdString(path.string()); }

std::unexpected<SessionError> storeFailure(const std::filesystem::path& where, QString message) {
    return std::unexpected(
        SessionError{SessionErrorCode::StoreFailed, text(where), std::move(message)});
}

std::unexpected<SessionError> storeFailure(const std::filesystem::path& where, const char* action,
                                           int errorNumber) {
    return storeFailure(
        where, QStringLiteral("%1: %2").arg(QString::fromLatin1(action),
                                            QString::fromLocal8Bit(std::strerror(errorNumber))));
}

bool isSymlink(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path, error)) && !error;
}

/// Create one directory level owner-only, or tighten an existing one. A
/// symlink in the chain is refused: the store must never be redirected.
std::expected<void, SessionError> ensureDirectory(const std::filesystem::path& directory) {
    if (isSymlink(directory)) {
        return storeFailure(directory, QStringLiteral("State directory is a symlink; refusing"));
    }
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) {
        if (::mkdir(directory.c_str(), kDirectoryMode) != 0 && errno != EEXIST) {
            return storeFailure(directory, "Could not create the state directory", errno);
        }
    }
    struct stat status{};
    if (::lstat(directory.c_str(), &status) != 0) {
        return storeFailure(directory, "Could not inspect the state directory", errno);
    }
    if (!S_ISDIR(status.st_mode)) {
        return storeFailure(directory, QStringLiteral("State path is not a directory"));
    }
    if ((status.st_mode & 07777) != kDirectoryMode &&
        ::chmod(directory.c_str(), kDirectoryMode) != 0) {
        return storeFailure(directory, "Could not make the state directory owner-only", errno);
    }
    return {};
}

/// Remove abandoned temporaries for the session file. A crashed writer
/// leaves one behind; a live concurrent instance has a young one, kept.
void removeStaleTemporaries(const std::filesystem::path& directory) {
    const auto prefix = atomicTemporaryPrefix() + kFileName + "-";
    const auto cutoff = std::filesystem::file_time_type::clock::now() -
                        std::chrono::duration_cast<std::filesystem::file_time_type::duration>(
                            SessionStore::kStaleTemporaryAge);
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (!entry.path().filename().string().starts_with(prefix)) {
            continue;
        }
        std::error_code ignored;
        if (!entry.is_regular_file(ignored) || entry.is_symlink(ignored)) {
            continue;
        }
        const auto written = entry.last_write_time(ignored);
        if (!ignored && written < cutoff) {
            std::filesystem::remove(entry.path(), ignored);
        }
    }
}

/// The newest write among the regular files of a workspace directory and its
/// recovery directory: the last time the program touched it. Directory
/// mtimes are not consulted; they move when a lock file is created.
std::filesystem::file_time_type lastActivityIn(const std::filesystem::path& directory) {
    // No default-constructed file time here: on libstdc++ that is the file
    // clock's epoch, which is in the next century. Nothing readable means
    // "now", so an unreadable directory is kept rather than expired.
    std::optional<std::filesystem::file_time_type> newest;
    const auto consider = [&newest](const std::filesystem::directory_entry& entry) {
        std::error_code ignored;
        if (entry.is_symlink(ignored) || !entry.is_regular_file(ignored)) {
            return;
        }
        const auto written = entry.last_write_time(ignored);
        if (!ignored) {
            newest = newest ? std::max(*newest, written) : written;
        }
    };
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        consider(entry);
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory / "recovery", error)) {
        consider(entry);
    }
    return newest.value_or(std::filesystem::file_time_type::clock::now());
}

/// Only a clean "not there" counts as gone. A permission error or an
/// unreachable mount is not evidence of anything, and the state stays.
bool rootIsGone(const std::filesystem::path& root) {
    std::error_code error;
    const bool present = std::filesystem::exists(root, error);
    return !present && !error;
}

} // namespace

SessionStore::SessionStore(std::filesystem::path sessionsDirectory, const AtomicWriteFaults* faults)
    : sessions_(std::move(sessionsDirectory)), faults_(faults) {}

std::filesystem::path SessionStore::defaultDirectory() {
    const auto state = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
    return std::filesystem::path(QFile::encodeName(state).toStdString()) / "omanotes" / "sessions";
}

std::string SessionStore::workspaceId(const std::filesystem::path& canonicalRoot) {
    const auto digest = QCryptographicHash::hash(
        QByteArray::fromStdString(canonicalRoot.generic_string()), QCryptographicHash::Sha256);
    return digest.left(16).toHex().toStdString();
}

std::filesystem::path SessionStore::directoryFor(const WorkspaceRoot& root) const {
    return sessions_ / workspaceId(root.path());
}

std::filesystem::path SessionStore::fileFor(const WorkspaceRoot& root) const {
    return directoryFor(root) / kFileName;
}

std::filesystem::path SessionStore::lockFileFor(const WorkspaceRoot& root) const {
    return directoryFor(root) / "instance.lock";
}

std::expected<void, SessionError>
SessionStore::ensureDirectoryFor(const WorkspaceRoot& root) const {
    return prepareDirectory(directoryFor(root));
}

std::expected<void, SessionError>
SessionStore::prepareDirectory(const std::filesystem::path& directory) const {
    // Create from the `omanotes` level down; anything above is the desktop's.
    const auto omanotesDirectory = sessions_.parent_path();
    std::error_code error;
    std::filesystem::create_directories(omanotesDirectory.parent_path(), error);
    if (error) {
        return storeFailure(omanotesDirectory.parent_path(),
                            QStringLiteral("Could not create the state directory: %1")
                                .arg(QString::fromStdString(error.message())));
    }
    for (const auto& level : {omanotesDirectory, sessions_, directory}) {
        if (auto ensured = ensureDirectory(level); !ensured) {
            return ensured;
        }
    }
    return {};
}

std::expected<void, SessionError> SessionStore::save(const SessionSnapshot& snapshot) const {
    auto root = WorkspaceRoot::resolve(snapshot.workspaceRoot);
    if (!root) {
        return std::unexpected(SessionError{SessionErrorCode::RootMismatch, "workspaceRoot",
                                            QString::fromStdString(root.error().message)});
    }
    if (auto matches = checkSessionRoot(snapshot, *root); !matches) {
        return matches;
    }
    const auto bytes = serializeSessionSnapshot(snapshot);
    if (static_cast<std::size_t>(bytes.size()) > kSessionMaxBytes) {
        return std::unexpected(SessionError{
            SessionErrorCode::Oversized,
            {},
            QStringLiteral("Session document would exceed %1 bytes").arg(kSessionMaxBytes)});
    }

    const auto directory = directoryFor(*root);
    if (auto prepared = prepareDirectory(directory); !prepared) {
        return prepared;
    }
    const auto file = directory / kFileName;
    if (isSymlink(file)) {
        return storeFailure(file, QStringLiteral("Session file is a symlink; refusing to write"));
    }
    removeStaleTemporaries(directory);

    if (auto replaced = replaceFileAtomically(file, QByteArrayView(bytes), kFileMode, faults_);
        !replaced) {
        return storeFailure(file, QString::fromStdString(replaced.error().message));
    }
    return {};
}

std::expected<std::optional<SessionSnapshot>, SessionError>
SessionStore::load(const WorkspaceRoot& root) const {
    const auto file = fileFor(root);
    if (isSymlink(file)) {
        return storeFailure(file, QStringLiteral("Session file is a symlink; refusing to read"));
    }
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        return std::optional<SessionSnapshot>{};
    }
    auto snapshot = readSessionSnapshot(file);
    if (!snapshot) {
        return std::unexpected(snapshot.error());
    }
    if (auto matches = checkSessionRoot(*snapshot, root); !matches) {
        return std::unexpected(matches.error());
    }
    return std::optional<SessionSnapshot>{std::move(*snapshot)};
}

std::vector<ParkedWorkspace> SessionStore::listSiblings(std::string_view ownId) const {
    std::vector<ParkedWorkspace> found;
    std::error_code error;
    if (isSymlink(sessions_) || !std::filesystem::is_directory(sessions_, error)) {
        return found;
    }
    int scanned = 0;
    for (const auto& entry : std::filesystem::directory_iterator(sessions_, error)) {
        if (++scanned > kSiblingScanLimit) {
            break;
        }
        std::error_code ignored;
        if (entry.is_symlink(ignored) || !entry.is_directory(ignored) ||
            entry.path().filename().string() == ownId) {
            continue;
        }
        const auto file = entry.path() / kFileName;
        if (isSymlink(file)) {
            continue;
        }
        // Metadata only: the snapshot names its root and counts its dirty
        // buffers; no recovery record is opened.
        const auto snapshot = readSessionSnapshot(file);
        if (!snapshot) {
            continue;
        }
        found.push_back({entry.path(), snapshot->workspaceRoot, snapshot->dirtyBufferCount(),
                         !rootIsGone(snapshot->workspaceRoot), lastActivityIn(entry.path())});
    }
    return found;
}

std::vector<SweptWorkspace>
SessionStore::sweepVanishedRoots(std::string_view ownId,
                                 std::filesystem::file_time_type now) const {
    std::vector<SweptWorkspace> swept;
    const auto cutoff = now - std::chrono::duration_cast<std::filesystem::file_time_type::duration>(
                                  kVanishedRootRetention);
    for (const auto& parked : listSiblings(ownId)) {
        if (parked.rootExists || parked.lastActivity >= cutoff) {
            continue;
        }
        // ADR 0012: a live instance may still have this workspace open even
        // though its directory has gone. Its lock says so; leave it alone.
        const auto lock = InstanceLock::acquire(parked.directory / "instance.lock");
        if (!lock.held()) {
            continue;
        }
        std::error_code error;
        std::filesystem::remove_all(parked.directory, error);
        if (error) {
            continue;
        }
        swept.push_back({parked.root, parked.dirtyBuffers});
    }
    return swept;
}

} // namespace omanotes
