#include "workspace/file_watcher.hpp"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <algorithm>
#include <system_error>
#include <utility>

namespace omanotes {

namespace {

QString toQString(const std::filesystem::path& path) {
    return QFile::decodeName(QByteArray::fromStdString(path.native()));
}

std::filesystem::path toPath(const QString& path) {
    return std::filesystem::path(QFile::encodeName(path).toStdString());
}

} // namespace

FileWatcher::FileWatcher(std::chrono::milliseconds quietPeriod, QObject* parent) : QObject(parent) {
    quiet_.setSingleShot(true);
    quiet_.setInterval(quietPeriod);
    connect(&quiet_, &QTimer::timeout, this, [this] { flush(); });
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this,
            [this](const QString& path) { noteFileActivity(path); });
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString& path) { noteDirectoryActivity(path); });
}

void FileWatcher::watch(const std::filesystem::path& file) {
    if (!files_.insert(file).second) {
        return;
    }
    watcher_.addPath(toQString(file.parent_path()));
    rearm(file);
}

void FileWatcher::unwatch(const std::filesystem::path& file) {
    if (files_.erase(file) == 0) {
        return;
    }
    pending_.erase(file);
    watcher_.removePath(toQString(file));

    const auto directory = file.parent_path();
    const auto siblingStillWatched = std::ranges::any_of(
        files_, [&directory](const auto& other) { return other.parent_path() == directory; });
    if (!siblingStillWatched) {
        watcher_.removePath(toQString(directory));
    }
}

bool FileWatcher::isWatching(const std::filesystem::path& file) const {
    return files_.contains(file);
}

void FileWatcher::noteFileActivity(const QString& path) {
    const auto file = toPath(path);
    if (!files_.contains(file)) {
        return;
    }
    pending_.insert(file);
    quiet_.start();
}

void FileWatcher::noteDirectoryActivity(const QString& path) {
    const auto directory = toPath(path);
    auto anyPending = false;
    for (const auto& file : files_) {
        if (file.parent_path() == directory) {
            pending_.insert(file);
            anyPending = true;
        }
    }
    if (anyPending) {
        quiet_.start();
    }
}

void FileWatcher::rearm(const std::filesystem::path& file) {
    // A rename-replace or a deletion silently drops the file watch; putting
    // it back whenever the file is present again keeps in-place writes
    // visible after an external editor has swapped the inode.
    std::error_code error;
    if (!std::filesystem::exists(file, error) || error) {
        return;
    }
    const auto name = toQString(file);
    if (!watcher_.files().contains(name)) {
        watcher_.addPath(name);
    }
}

void FileWatcher::flush() {
    auto burst = std::exchange(pending_, {});
    for (const auto& file : burst) {
        if (!files_.contains(file)) {
            continue;
        }
        rearm(file);
        emit fileChanged(file);
    }
}

} // namespace omanotes
