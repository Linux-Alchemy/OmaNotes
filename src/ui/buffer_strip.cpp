#include "ui/buffer_strip.hpp"

#include <QString>
#include <QToolButton>
#include <QVariant>

namespace omanotes {

BufferStrip::BufferStrip(QWidget* parent) : QTabBar(parent) {
    setObjectName(QStringLiteral("bufferStrip"));
    setAccessibleName(QStringLiteral("Open buffers"));
    setExpanding(false);
    setMovable(false);
    setDrawBase(false);
    setFocusPolicy(Qt::NoFocus);

    connect(this, &QTabBar::currentChanged, this, [this](int index) {
        if (synchronising_ || index < 0) {
            return;
        }
        emit bufferSelected(tabData(index).toUuid());
    });
}

QToolButton* BufferStrip::makeCloseButton() {
    // Our own quiet multiplication glyph instead of the style's stock close
    // icon, so the button is themed like everything else in the strip.
    auto* close = new QToolButton(this);
    close->setObjectName(QStringLiteral("tabCloseButton"));
    close->setText(QStringLiteral("×"));
    close->setAutoRaise(true);
    close->setFocusPolicy(Qt::NoFocus);
    close->setAccessibleName(QStringLiteral("Close buffer"));
    close->setToolTip(QStringLiteral("Close buffer"));
    connect(close, &QToolButton::clicked, this, [this, close] {
        for (int tab = 0; tab < count(); ++tab) {
            if (tabButton(tab, QTabBar::RightSide) == close) {
                emit bufferCloseRequested(tabData(tab).toUuid());
                return;
            }
        }
    });
    return close;
}

void BufferStrip::syncWith(const std::vector<BufferState>& buffers,
                           std::optional<BufferId> activeId) {
    synchronising_ = true;

    while (static_cast<std::size_t>(count()) > buffers.size()) {
        removeTab(count() - 1);
    }
    while (static_cast<std::size_t>(count()) < buffers.size()) {
        const auto tab = addTab(QString{});
        setTabButton(tab, QTabBar::RightSide, makeCloseButton());
    }

    for (std::size_t index = 0; index < buffers.size(); ++index) {
        const auto& buffer = buffers[index];
        const auto tab = static_cast<int>(index);
        const auto label =
            buffer.modified ? QStringLiteral("%1 [+]").arg(buffer.displayName) : buffer.displayName;
        if (tabText(tab) != label) {
            setTabText(tab, label);
        }
        setTabData(tab, QVariant::fromValue(buffer.id));
        setTabToolTip(tab, buffer.path.has_value() ? QString::fromStdString(buffer.path->string())
                                                   : buffer.displayName);
    }

    if (activeId.has_value()) {
        for (int tab = 0; tab < count(); ++tab) {
            if (tabData(tab).toUuid() == *activeId) {
                setCurrentIndex(tab);
                break;
            }
        }
    }

    synchronising_ = false;
}

} // namespace omanotes
