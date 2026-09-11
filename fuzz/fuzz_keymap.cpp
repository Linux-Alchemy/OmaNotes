// libFuzzer: a keymap document must be refused or bind only registered
// commands to sequences the policy allows.
#include "app/keymap.hpp"
#include "core/command_registry.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace {

omanotes::CommandRegistry& registry() {
    static omanotes::CommandRegistry commands = [] {
        omanotes::CommandRegistry built;
        for (const auto* id : {"file.save", "file.open", "buffer.show", "buffer.next",
                               "buffer.previous", "buffer.new", "pane.sidebar", "pane.editor",
                               "help.show", "search.files", "search.text"}) {
            const auto added = built.add({QString::fromLatin1(id),
                                          QString::fromLatin1(id),
                                          QStringLiteral("fuzz"),
                                          {},
                                          [](omanotes::AppContext&) {},
                                          {}});
            if (!added) {
                std::abort();
            }
        }
        return built;
    }();
    return commands;
}

} // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
    static QCoreApplication application(*argc, *argv);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto bytes =
        QByteArray(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    const auto values = QJsonDocument::fromJson(bytes).object().toVariantMap();
    const auto keymap = omanotes::Keymap::fromConfig(values, registry());
    if (!keymap) {
        return 0;
    }
    for (const auto& [id, label] : keymap->shortcutLabels()) {
        if (registry().find(id) == nullptr) {
            std::abort();
        }
        const auto sequence = keymap->sequenceFor(id);
        if (sequence.count() > 1) {
            std::abort();
        }
    }
    auto copy = registry();
    if (!keymap->applyTo(copy)) {
        std::abort();
    }
    return 0;
}
