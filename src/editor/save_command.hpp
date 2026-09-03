#ifndef OMANOTES_EDITOR_SAVE_COMMAND_HPP
#define OMANOTES_EDITOR_SAVE_COMMAND_HPP

#include <KTextEditor/Command>

#include <QString>

#include <functional>

namespace omanotes {

/// Bridges the editor's `:` command line to the application's save path.
///
/// KTextEditor's part registers no `w` command of its own, so `:w` and
/// `:w <path>` are ours to define and route through the atomic writer rather
/// than any editor-internal write.
class SaveCommand final : public KTextEditor::Command {
  public:
    /// Performs the save. Returns an empty string on success, or the message to
    /// show the user on failure.
    using Handler = std::function<QString(const QString& argument)>;

    explicit SaveCommand(Handler handler, QObject* parent = nullptr);

    bool exec(KTextEditor::View* view, const QString& command, QString& message,
              const KTextEditor::Range& range = KTextEditor::Range::invalid()) override;
    bool help(KTextEditor::View* view, const QString& command, QString& message) override;

  private:
    Handler handler_;
};

} // namespace omanotes

#endif // OMANOTES_EDITOR_SAVE_COMMAND_HPP
