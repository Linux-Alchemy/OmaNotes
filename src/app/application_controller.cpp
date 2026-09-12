#include "app/application_controller.hpp"

#include "ui/main_window.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <system_error>
#include <utility>
#include <vector>

namespace omanotes {

namespace {

QString displayRoot(const std::filesystem::path& root) {
    auto text = QFile::decodeName(QByteArray::fromStdString(root.native()));
    const auto home = QDir::homePath();
    if (!home.isEmpty() && text.startsWith(home + QLatin1Char('/'))) {
        return QLatin1Char('~') + text.mid(home.size());
    }
    return text;
}

QString bufferCount(std::size_t count) {
    return QStringLiteral("%1 %2").arg(count).arg(count == 1 ? QStringLiteral("buffer")
                                                             : QStringLiteral("buffers"));
}

/// A file-clock instant as a calendar day, for "kept until".
QString dayOf(std::filesystem::file_time_type when) {
    const auto system =
        std::chrono::system_clock::now() + (when - std::filesystem::file_time_type::clock::now());
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
    return QDateTime::fromSecsSinceEpoch(seconds).date().toString(QStringLiteral("d MMM"));
}

} // namespace

ApplicationController::ApplicationController(MainWindow& window, LaunchRequest request,
                                             const std::filesystem::path& sessionsDirectory,
                                             QObject* parent)
    : QObject(parent), window_(window), request_(std::move(request)), sessions_(sessionsDirectory),
      recovery_(RecoveryStore::directoryBeside(sessionsDirectory /
                                               SessionStore::workspaceId(request_.root))) {
    debounce_.setSingleShot(true);
    debounce_.setInterval(kDebounce);
    connect(&debounce_, &QTimer::timeout, this, [this] { checkpointNow(); });
}

ApplicationController::~ApplicationController() = default;

std::optional<WorkspaceRoot> ApplicationController::resolveRoot() const {
    auto root = WorkspaceRoot::resolve(request_.root);
    if (!root) {
        return std::nullopt;
    }
    return *root;
}

void ApplicationController::start() {
    if (started_) {
        return;
    }
    started_ = true;
    const auto root = resolveRoot();
    QStringList status;

    // ADR 0012: only the instance holding the workspace's lock may read or
    // take over recovery records. Any other outcome, contention or failure,
    // means "leave every record alone".
    if (root) {
        if (const auto prepared = sessions_.ensureDirectoryFor(*root); !prepared) {
            status << QStringLiteral("Session directory unavailable (%1); unsaved work from "
                                     "earlier sessions is not adopted")
                          .arg(prepared.error().describe());
        } else {
            lock_.emplace(InstanceLock::acquire(sessions_.lockFileFor(*root)));
            switch (lock_->state()) {
            case InstanceLock::State::Held:
                break;
            case InstanceLock::State::HeldElsewhere:
                status << QStringLiteral("Another OmaNotes has this workspace open; its unsaved "
                                         "work stays with it");
                break;
            case InstanceLock::State::Failed:
                status << QStringLiteral("Could not take the workspace lock (%1); unsaved work "
                                         "from earlier sessions is not adopted")
                              .arg(lock_->error());
                break;
            }
        }
    }
    const bool adoptRecords = holdsInstanceLock();
    if (root) {
        if (const auto wide = wideRootNotice(
                root->path(),
                std::filesystem::path(QFile::encodeName(QDir::homePath()).toStdString()));
            wide) {
            status << QString::fromStdString(*wide);
        }
    }

    if (root && !request_.bypassRestore) {
        const auto loaded = sessions_.load(*root);
        if (!loaded) {
            const auto parked = recovery_.list();
            const auto kept = parked && !parked->empty()
                                  ? QStringLiteral("; %1 recovery record(s) kept in %2")
                                        .arg(parked->size())
                                        .arg(QString::fromStdString(recovery_.directory().string()))
                                  : QString();
            status << QStringLiteral("Session state ignored (%1); starting clean%2")
                          .arg(loaded.error().describe(), kept);
        } else if (loaded->has_value()) {
            const auto initial = window_.untouchedInitialBuffer();
            lastReport_ = restorer_.restore(**loaded, *root, recovery_, window_, adoptRecords);
            recoveryIds_ = lastReport_.recoveryIds;
            if (initial && (lastReport_.restored > 0 || lastReport_.recovered > 0)) {
                window_.closeIfUntouched(*initial);
            }
        }
    }

    if (root && !request_.bypassRestore && adoptRecords) {
        // Records nothing references are unsaved text with lost bookkeeping;
        // they come back as dirty buffers rather than being tidied away.
        // Only the lock holder does this: to anyone else, an unreferenced
        // record may be another live window's text.
        if (const auto present = recovery_.list(); present) {
            std::vector<RecoveryId> orphans;
            for (const auto& id : *present) {
                const auto referenced = std::ranges::any_of(
                    recoveryIds_, [&id](const auto& entry) { return entry.second == id; });
                if (!referenced) {
                    orphans.push_back(id);
                }
            }
            if (!orphans.empty()) {
                const auto extra =
                    restorer_.restoreUnreferenced(orphans, *root, recovery_, window_);
                for (const auto& [bufferId, recordId] : extra.recoveryIds) {
                    recoveryIds_.emplace(bufferId, recordId);
                }
                lastReport_.recovered += extra.recovered;
                lastReport_.skipped.insert(lastReport_.skipped.end(), extra.skipped.begin(),
                                           extra.skipped.end());
            }
        }
    }

    // The file named on the command line opens or comes to the front last,
    // whatever the session had to say about focus.
    if (request_.requestedFile) {
        window_.focusRequestedFile(*request_.requestedFile);
    }

    if (const auto summary = lastReport_.summary(); !summary.isEmpty()) {
        status << summary;
    }
    if (root) {
        // ADR 0015: state for a root that has been gone longer than the
        // retention period goes now, and is announced if it held text.
        // Geometry-only state goes quietly; there was nothing in it to lose.
        for (const auto& gone :
             sessions_.sweepVanishedRoots(SessionStore::workspaceId(root->path()),
                                          std::filesystem::file_time_type::clock::now())) {
            if (gone.dirtyBuffers > 0) {
                status << QStringLiteral(
                              "Removed unsaved work from %1 (%2): that directory has been gone "
                              "for %3 days")
                              .arg(displayRoot(gone.root), bufferCount(gone.dirtyBuffers))
                              .arg(SessionStore::kVanishedRootRetention.count());
            }
        }
    }
    if (const auto notice = parkedWorkNotice(); !notice.isEmpty()) {
        status << notice;
    }
    if (!status.isEmpty()) {
        window_.showStatus(status.join(QStringLiteral(" · ")));
    }

    connect(&window_, &MainWindow::sessionStateChanged, this, [this] { scheduleCheckpoint(); });
    connect(&window_, &MainWindow::bufferResolved, this,
            [this](BufferId id) { releaseRecord(id); });
    connect(&window_, &MainWindow::windowDeactivated, this, [this] { checkpointNow(); });
    connect(&window_, &MainWindow::aboutToClose, this, [this] { checkpointNow(); });
}

void ApplicationController::scheduleCheckpoint() { debounce_.start(); }

void ApplicationController::releaseRecord(BufferId id) {
    const auto found = recoveryIds_.find(id);
    if (found == recoveryIds_.end()) {
        return;
    }
    if (const auto removed = recovery_.remove(found->second); !removed) {
        window_.showStatus(removed.error().describe());
        return;
    }
    recoveryIds_.erase(found);
    scheduleCheckpoint();
}

void ApplicationController::checkpointNow() {
    debounce_.stop();
    const auto root = resolveRoot();
    if (!root) {
        return;
    }
    auto snapshot = window_.captureSnapshot();
    QStringList problems;
    std::map<BufferId, RecoveryId> live;
    for (auto& buffer : snapshot.buffers) {
        const auto existing = recoveryIds_.find(buffer.id);
        if (!buffer.modified) {
            if (existing != recoveryIds_.end()) {
                // Edited back to clean, or saved through a route that did not
                // announce itself: the record has nothing left to protect.
                if (const auto removed = recovery_.remove(existing->second); !removed) {
                    problems << removed.error().describe();
                }
            }
            continue;
        }
        const auto record = window_.dirtyRecord(buffer.id);
        if (!record) {
            buffer.modified = false;
            continue;
        }
        const auto id = recovery_.checkpoint(*record, existing != recoveryIds_.end()
                                                          ? std::optional(existing->second)
                                                          : std::nullopt);
        if (!id) {
            problems << QStringLiteral("%1 not protected: %2")
                            .arg(buffer.path ? QString::fromStdString(buffer.path->generic_string())
                                             : scratchDisplayName(),
                                 id.error().message);
            // The snapshot must not claim a record that does not exist.
            buffer.modified = false;
            continue;
        }
        buffer.recovery = *id;
        live.emplace(buffer.id, *id);
    }
    recoveryIds_ = std::move(live);

    if (!request_.bypassRestore) {
        if (const auto saved = sessions_.save(snapshot); !saved) {
            problems << QStringLiteral("Session not saved: %1").arg(saved.error().describe());
        }
    }
    if (!problems.isEmpty()) {
        window_.showStatus(problems.join(QStringLiteral(" · ")));
    }
}

const RestoreReport& ApplicationController::lastReport() const noexcept { return lastReport_; }

const std::map<BufferId, RecoveryId>& ApplicationController::recoveryIds() const noexcept {
    return recoveryIds_;
}

bool ApplicationController::holdsInstanceLock() const noexcept {
    return lock_.has_value() && lock_->held();
}

QString ApplicationController::parkedWorkNotice() const {
    // The root can have vanished since launch; the notice is a courtesy and
    // must not be the thing that brings the window down.
    const auto root = resolveRoot();
    if (!root) {
        return {};
    }
    QStringList waiting;
    QStringList gone;
    const auto retention = std::chrono::duration_cast<std::filesystem::file_time_type::duration>(
        SessionStore::kVanishedRootRetention);
    for (const auto& parked : sessions_.listSiblings(SessionStore::workspaceId(root->path()))) {
        if (parked.dirtyBuffers == 0) {
            continue;
        }
        if (parked.rootExists) {
            waiting << QStringLiteral("%1 (%2)").arg(displayRoot(parked.root),
                                                     bufferCount(parked.dirtyBuffers));
        } else {
            // "Open it to recover" would point at a door that no longer
            // exists. Say where the text is and how long it will be there.
            gone << QStringLiteral("Unsaved work from %1 (%2); that directory is gone. Records "
                                   "kept until %3 in %4")
                        .arg(displayRoot(parked.root), bufferCount(parked.dirtyBuffers),
                             dayOf(parked.lastActivity + retention), displayRoot(parked.directory));
        }
    }
    QStringList parts;
    if (!waiting.isEmpty()) {
        parts << QStringLiteral("Unsaved work waiting in %1. Open it to recover.")
                     .arg(waiting.join(QStringLiteral(", ")));
    }
    parts << gone;
    return parts.join(QStringLiteral(" · "));
}

} // namespace omanotes
