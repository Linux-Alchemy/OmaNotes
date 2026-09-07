#ifndef OMANOTES_UI_HELP_OVERLAY_HPP
#define OMANOTES_UI_HELP_OVERLAY_HPP

#include "core/command_registry.hpp"

#include <QDialog>
#include <map>

class QTreeWidget;
namespace omanotes {
class HelpOverlay final : public QDialog {
    Q_OBJECT
  public:
    explicit HelpOverlay(QWidget* parent = nullptr);
    void showCommands(const CommandRegistry& commands, const AppContext& context,
                      const std::map<QString, QString>& shortcuts);
  signals:
    void commandChosen(const QString& id);

  private:
    void chooseCurrent();
    QTreeWidget* list_ = nullptr;
};
} // namespace omanotes
#endif
