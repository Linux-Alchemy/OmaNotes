#include "editor/save_command.hpp"

#include <QStringList>

#include <utility>

namespace omanotes {

SaveCommand::SaveCommand(Handler handler, QObject* parent)
    : KTextEditor::Command({QStringLiteral("w"), QStringLiteral("write")}, parent),
      handler_(std::move(handler)) {}

bool SaveCommand::exec(KTextEditor::View* /*view*/, const QString& command, QString& message,
                       const KTextEditor::Range& /*range*/) {
    if (!handler_) {
        message = QStringLiteral("Saving is not available");
        return false;
    }

    // `command` arrives as the whole line, so `w notes/idea.md` carries its
    // argument with it.
    const auto separator = command.indexOf(QLatin1Char(' '));
    const auto argument = separator < 0 ? QString{} : command.mid(separator + 1).trimmed();

    const auto failure = handler_(argument);
    if (!failure.isEmpty()) {
        message = failure;
        return false;
    }
    return true;
}

bool SaveCommand::help(KTextEditor::View* /*view*/, const QString& /*command*/, QString& message) {
    message = QStringLiteral("w [path] — write the current buffer inside the workspace");
    return true;
}

} // namespace omanotes
