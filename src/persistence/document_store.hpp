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
