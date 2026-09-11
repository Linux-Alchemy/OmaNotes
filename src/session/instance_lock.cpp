#include "session/instance_lock.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace omanotes {

namespace {

constexpr mode_t kLockMode = 0600;

QString describe(const char* action, int errorNumber) {
    return QStringLiteral("%1: %2").arg(QString::fromLatin1(action),
                                        QString::fromLocal8Bit(std::strerror(errorNumber)));
}

} // namespace

InstanceLock InstanceLock::acquire(const std::filesystem::path& file) {
    const int descriptor =
        ::open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, kLockMode);
    if (descriptor < 0) {
        return {-1, State::Failed, describe("Could not open the instance lock", errno)};
    }
    struct stat status{};
    if (::fstat(descriptor, &status) != 0) {
        const int reason = errno;
        ::close(descriptor);
        return {-1, State::Failed, describe("Could not inspect the instance lock", reason)};
    }
    if (!S_ISREG(status.st_mode)) {
        ::close(descriptor);
        return {-1, State::Failed, QStringLiteral("The instance lock is not a regular file")};
    }
    if (::flock(descriptor, LOCK_EX | LOCK_NB) == 0) {
        return {descriptor, State::Held, {}};
    }
    const int reason = errno;
    ::close(descriptor);
    if (reason == EWOULDBLOCK) {
        return {-1, State::HeldElsewhere, {}};
    }
    return {-1, State::Failed, describe("Could not lock the instance lock", reason)};
}

InstanceLock::InstanceLock(int descriptor, State state, QString error)
    : descriptor_(descriptor), state_(state), error_(std::move(error)) {}

InstanceLock::InstanceLock(InstanceLock&& other) noexcept
    : descriptor_(std::exchange(other.descriptor_, -1)),
      state_(std::exchange(other.state_, State::Failed)), error_(std::move(other.error_)) {}

InstanceLock& InstanceLock::operator=(InstanceLock&& other) noexcept {
    if (this != &other) {
        release();
        descriptor_ = std::exchange(other.descriptor_, -1);
        state_ = std::exchange(other.state_, State::Failed);
        error_ = std::move(other.error_);
    }
    return *this;
}

InstanceLock::~InstanceLock() { release(); }

void InstanceLock::release() noexcept {
    if (descriptor_ >= 0) {
        // Closing drops the flock; nothing else is needed and the file stays.
        ::close(descriptor_);
        descriptor_ = -1;
    }
    state_ = State::Failed;
}

InstanceLock::State InstanceLock::state() const noexcept { return state_; }

bool InstanceLock::held() const noexcept { return state_ == State::Held; }

const QString& InstanceLock::error() const noexcept { return error_; }

} // namespace omanotes
