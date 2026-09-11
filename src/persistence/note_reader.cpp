#include "persistence/note_reader.hpp"

#include <QFile>
#include <QIODevice>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace omanotes {

namespace {

QString nameOf(const std::filesystem::path& path) {
    return QString::fromStdString(path.filename().string());
}

std::unexpected<NoteReadError> refuse(NoteReadErrorCode code, QString message) {
    return std::unexpected(NoteReadError{code, std::move(message)});
}

QString limitText(qint64 limit) {
    return QStringLiteral("%1 MiB").arg(limit / (qint64{1024} * 1024));
}

} // namespace

std::expected<QByteArray, NoteReadError> readNoteFile(const std::filesystem::path& path,
                                                      qint64 limit) {
    const auto name = nameOf(path);
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0) {
        const int reason = errno;
        if (reason == ENOENT) {
            return refuse(NoteReadErrorCode::Missing,
                          QStringLiteral("%1 does not exist").arg(name));
        }
        if (reason == ELOOP) {
            return refuse(NoteReadErrorCode::NotRegular,
                          QStringLiteral("%1 is a symlink now; refusing to read it").arg(name));
        }
        return refuse(NoteReadErrorCode::Unreadable,
                      QStringLiteral("Could not read %1: %2")
                          .arg(name, QString::fromLocal8Bit(std::strerror(reason))));
    }

    struct stat status{};
    if (::fstat(descriptor, &status) != 0) {
        const int reason = errno;
        ::close(descriptor);
        return refuse(NoteReadErrorCode::Unreadable,
                      QStringLiteral("Could not inspect %1: %2")
                          .arg(name, QString::fromLocal8Bit(std::strerror(reason))));
    }
    if (!S_ISREG(status.st_mode)) {
        ::close(descriptor);
        return refuse(NoteReadErrorCode::NotRegular,
                      QStringLiteral("%1 is not a regular file; refusing to read it").arg(name));
    }
    if (status.st_size > limit) {
        ::close(descriptor);
        return refuse(NoteReadErrorCode::Oversized,
                      QStringLiteral("%1 is larger than %2; refusing to read it")
                          .arg(name, limitText(limit)));
    }

    QFile file;
    if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor);
        return refuse(NoteReadErrorCode::Unreadable,
                      QStringLiteral("Could not read %1: %2").arg(name, file.errorString()));
    }
    QByteArray bytes;
    bytes.reserve(static_cast<qsizetype>(std::min<qint64>(status.st_size, limit)));
    // Read past the limit by one chunk at most, so a file that grew between
    // the size check and here is caught rather than trusted.
    while (!file.atEnd() && bytes.size() <= limit) {
        const auto chunk = file.read(65536);
        if (chunk.isEmpty()) {
            if (file.atEnd()) {
                break;
            }
            return refuse(NoteReadErrorCode::Unreadable,
                          QStringLiteral("Could not read %1: %2").arg(name, file.errorString()));
        }
        bytes.append(chunk);
    }
    if (file.error() != QFileDevice::NoError) {
        return refuse(NoteReadErrorCode::Unreadable,
                      QStringLiteral("Could not read %1: %2").arg(name, file.errorString()));
    }
    if (bytes.size() > limit || !file.atEnd()) {
        return refuse(NoteReadErrorCode::Oversized,
                      QStringLiteral("%1 is larger than %2; refusing to read it")
                          .arg(name, limitText(limit)));
    }
    return bytes;
}

} // namespace omanotes
