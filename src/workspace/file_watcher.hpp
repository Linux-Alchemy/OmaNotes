#ifndef OMANOTES_WORKSPACE_FILE_WATCHER_HPP
#define OMANOTES_WORKSPACE_FILE_WATCHER_HPP

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

#include <chrono>
#include <filesystem>
#include <set>

namespace omanotes {

/// Reports external changes to specific files, one report per burst.
///
/// Both the file and its directory are watched: an in-place write shows up on
/// the file, while an editor that saves by rename-replace (Neovim's default)
/// swaps the inode out from under a file watch and only the directory notices.
/// Events are coalesced so an agent writing a note twenty times in a second
/// produces one `fileChanged`, carrying the final content, not twenty reloads.
class FileWatcher final : public QObject {
    Q_OBJECT

  public:
    static constexpr std::chrono::milliseconds kDefaultQuietPeriod{150};

    explicit FileWatcher(std::chrono::milliseconds quietPeriod = kDefaultQuietPeriod,
                         QObject* parent = nullptr);

    void watch(const std::filesystem::path& file);
    void unwatch(const std::filesystem::path& file);
    [[nodiscard]] bool isWatching(const std::filesystem::path& file) const;

  signals:
    /// The file changed, was replaced, or was removed, and the disk has been
    /// quiet for the configured period since.
    void fileChanged(const std::filesystem::path& file);

  private:
    void noteFileActivity(const QString& path);
    void noteDirectoryActivity(const QString& path);
    void rearm(const std::filesystem::path& file);
    void flush();

    QFileSystemWatcher watcher_;
    QTimer quiet_;
    std::set<std::filesystem::path> files_;
    std::set<std::filesystem::path> pending_;
};

} // namespace omanotes

#endif // OMANOTES_WORKSPACE_FILE_WATCHER_HPP
