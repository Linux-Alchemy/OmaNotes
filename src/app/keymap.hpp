#ifndef OMANOTES_APP_KEYMAP_HPP
#define OMANOTES_APP_KEYMAP_HPP

#include "core/command_registry.hpp"

#include <QKeySequence>
#include <QVariantMap>

#include <expected>
#include <map>
#include <vector>

namespace omanotes {
struct KeymapError {
    QString location;
    QString message;
};

/// A complete, validated candidate. Applying it never partially changes the
/// registry. Configuration names application actions, never code or programs.
class Keymap final {
  public:
    [[nodiscard]] static Keymap defaults(const CommandRegistry& commands);
    [[nodiscard]] static std::expected<Keymap, KeymapError>
    fromConfig(const QVariantMap& values, const CommandRegistry& commands,
               const std::vector<QKeySequence>& editorShortcuts = {});
    [[nodiscard]] static std::expected<Keymap, KeymapError>
    load(const QString& path, const CommandRegistry& commands,
         const std::vector<QKeySequence>& editorShortcuts = {});
    [[nodiscard]] static QString configurationPath();
    [[nodiscard]] QKeySequence sequenceFor(QStringView commandId) const;
    [[nodiscard]] QString commandFor(const QKeySequence& sequence) const;
    [[nodiscard]] std::map<QString, QString> shortcutLabels() const;
    [[nodiscard]] std::expected<void, KeymapError> applyTo(CommandRegistry& commands) const;

  private:
    std::map<QString, QString> leaderBindings_;
    std::map<QString, QKeySequence> shortcuts_;
};
} // namespace omanotes
#endif
