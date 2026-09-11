#include "ui/help_overlay.hpp"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace omanotes {
HelpOverlay::HelpOverlay(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("helpOverlay"));
    setWindowTitle(QStringLiteral("OmaNotes commands"));
    setWindowModality(Qt::WindowModal);
    resize(650, 440);
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(
        QStringLiteral(
            "Space starts application commands in Normal mode or the sidebar.\n"
            "Select a command and press Enter, or double-click. Esc returns to your note."),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("helpCommands"));
    list_->setAccessibleName(QStringLiteral("Commands available here"));
    // Two columns: the category said nothing the label did not (Matt's call,
    // 2026-09-10).
    list_->setHeaderLabels({QStringLiteral("Command"), QStringLiteral("Keys")});
    list_->setRootIsDecorated(false);
    layout->addWidget(list_, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    // The platform theme staples a stock icon onto standard buttons; plain
    // text keeps them in the app's own register.
    buttons->button(QDialogButtonBox::Close)->setIcon(QIcon());
    // No accelerator underline either: "Close", not "&Close".
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("Close"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(list_, &QTreeWidget::itemActivated, this, [this] { chooseCurrent(); });
}

void HelpOverlay::showCommands(const CommandRegistry& commands, const AppContext& context,
                               const std::map<QString, QString>& shortcuts) {
    list_->clear();
    for (const auto* command : commands.available(context)) {
        QStringList keys;
        for (const auto& sequence : commands.sequencesFor(command->id)) {
            keys.append(QStringLiteral("Space %1").arg(sequence));
        }
        if (const auto shortcut = shortcuts.find(command->id); shortcut != shortcuts.end()) {
            keys.append(shortcut->second);
        }
        auto* item = new QTreeWidgetItem(list_, {command->label, keys.join(QStringLiteral(" / "))});
        item->setData(0, Qt::UserRole, command->id);
    }
    // Each column hugs its longest entry with a little room to spare; the
    // Keys column does not stretch to the edge (Matt's call, 2026-09-10).
    list_->header()->setStretchLastSection(false);
    list_->resizeColumnToContents(0);
    list_->setColumnWidth(0, list_->columnWidth(0) + 32);
    list_->resizeColumnToContents(1);
    list_->setColumnWidth(1, list_->columnWidth(1) + 24);
    // The dialog is as wide as its two columns, no wider.
    const auto contentWidth =
        list_->columnWidth(0) + list_->columnWidth(1) + 2 * list_->frameWidth();
    const auto margins = layout()->contentsMargins();
    setFixedWidth(contentWidth + margins.left() + margins.right());
    if (list_->topLevelItemCount() > 0) {
        list_->setCurrentItem(list_->topLevelItem(0));
    }
    show();
    list_->setFocus(Qt::OtherFocusReason);
}

void HelpOverlay::chooseCurrent() {
    const auto* item = list_->currentItem();
    if (item == nullptr) {
        return;
    }
    const auto id = item->data(0, Qt::UserRole).toString();
    accept();
    emit commandChosen(id);
}
} // namespace omanotes
