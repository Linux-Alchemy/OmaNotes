#include "persistence/document_store.hpp"

#include <QByteArray>
#include <QByteArrayView>

#include <cctype>
#include <system_error>
#include <utility>

namespace omanotes {

namespace {

bool isMarkdown(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    for (auto& character : extension) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return extension == ".md";
}

} // namespace

DocumentStore::DocumentStore(WorkspaceRoot root) : root_(std::move(root)) {}

std::expected<std::filesystem::path, SaveError>
DocumentStore::resolveTarget(const std::filesystem::path& requested) const {
    if (requested.empty() || !requested.has_filename()) {
        return std::unexpected(SaveError{SaveErrorCode::InvalidTarget, "No file was named"});
    }

    auto joined =
        (requested.is_absolute() ? requested : root_.path() / requested).lexically_normal();
    if (!isMarkdown(joined)) {
        // Say what would have worked: a refusal the user has to guess their way
        // out of is only a slower failure.
        auto suggestion = requested;
        suggestion.replace_extension(".md");
        return std::unexpected(
            SaveError{SaveErrorCode::InvalidTarget,
                      "Only Markdown (.md) files can be saved; try " + suggestion.string()});
    }

    // Directories are never created implicitly: `:w notes/idea.md` fails in Vim
    // when `notes` does not exist, and silently creating trees is how a typo
    // becomes a new folder.
    std::error_code error;
    if (!std::filesystem::is_directory(joined.parent_path(), error) || error) {
        return std::unexpected(SaveError{SaveErrorCode::InvalidTarget,
                                         "No such directory: " + joined.parent_path().string()});
    }

    return joined;
}

std::expected<std::filesystem::path, SaveError>
DocumentStore::save(const std::filesystem::path& requested, const QString& text,
                    const WritePrecondition& precondition) const {
    const auto target = resolveTarget(requested);
    if (!target) {
        return std::unexpected(target.error());
    }

    const auto encoded = text.toUtf8();
    return writer_.write(*target, QByteArrayView(encoded), root_, precondition);
}

} // namespace omanotes
