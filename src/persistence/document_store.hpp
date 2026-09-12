#ifndef OMANOTES_PERSISTENCE_DOCUMENT_STORE_HPP
#define OMANOTES_PERSISTENCE_DOCUMENT_STORE_HPP

#include "persistence/atomic_file_writer.hpp"
#include "workspace/workspace_root.hpp"

#include <QString>

#include <expected>
#include <filesystem>

namespace omanotes {

/// Turns a user's save request into a validated write inside the workspace.
///
/// The store decides *where* a document may go; `AtomicFileWriter` decides how
/// the bytes get there safely.
class DocumentStore final {
  public:
    explicit DocumentStore(WorkspaceRoot root);

    /// Resolve a typed or remembered target against the root without writing.
    [[nodiscard]] std::expected<std::filesystem::path, SaveError>
    resolveTarget(const std::filesystem::path& requested) const;

    /// True when `target` exists and this process may not write it. A plain
    /// `:w` refuses such a file, as Vim's E45 does; `:w!` does not ask,
    /// because the rename-replace needs only a writable directory and the
    /// new file keeps the old one's permissions. The store itself never
    /// refuses on this ground: whether to ask is the caller's, with the bang.
    [[nodiscard]] static bool isReadOnly(const std::filesystem::path& target);

    /// Resolve `requested` and write `text` to it as UTF-8.
    [[nodiscard]] std::expected<std::filesystem::path, SaveError>
    save(const std::filesystem::path& requested, const QString& text,
         const WritePrecondition& precondition = {}) const;

  private:
    WorkspaceRoot root_;
    AtomicFileWriter writer_;
};

} // namespace omanotes

#endif // OMANOTES_PERSISTENCE_DOCUMENT_STORE_HPP
