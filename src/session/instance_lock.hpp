#ifndef OMANOTES_SESSION_INSTANCE_LOCK_HPP
#define OMANOTES_SESSION_INSTANCE_LOCK_HPP

#include <QString>

#include <cstdint>
#include <filesystem>

namespace omanotes {

/// One advisory lock per workspace, held for the life of the process that
/// took it (ADR 0012). Whoever holds it is the instance that may adopt
/// recovery records; every other instance on the same root leaves records
/// alone, so two windows never write the same record file.
///
/// The lock is `flock(2)` on `<sessions>/<workspace-id>/instance.lock`. The
/// kernel drops it when the descriptor closes, including on a crash, so a
/// dead instance never holds it and the file itself is never deleted: an
/// empty lock file is harmless, a deleted one is a race.
class InstanceLock final {
  public:
    enum class State : std::uint8_t {
        /// This process holds the lock.
        Held,
        /// Another live process holds it.
        HeldElsewhere,
        /// The lock could not be taken for a reason other than contention;
        /// `error()` says why. Callers treat this like HeldElsewhere.
        Failed
    };

    /// Take the lock at `file`, creating it owner-only if needed. The final
    /// path component is never followed through a symlink.
    [[nodiscard]] static InstanceLock acquire(const std::filesystem::path& file);

    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;
    InstanceLock(InstanceLock&& other) noexcept;
    InstanceLock& operator=(InstanceLock&& other) noexcept;
    ~InstanceLock();

    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] bool held() const noexcept;
    [[nodiscard]] const QString& error() const noexcept;

  private:
    InstanceLock(int descriptor, State state, QString error);
    void release() noexcept;

    int descriptor_{-1};
    State state_{State::Failed};
    QString error_;
};

} // namespace omanotes

#endif // OMANOTES_SESSION_INSTANCE_LOCK_HPP
