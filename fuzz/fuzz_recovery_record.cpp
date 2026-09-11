// libFuzzer: the recovery record parser must refuse or round-trip, and never
// yield a path that is absolute or climbs.
#include "persistence/recovery_store.hpp"

#include <QByteArray>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto bytes =
        QByteArray(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    const auto parsed = omanotes::parseBufferRecovery(bytes);
    if (!parsed) {
        return 0;
    }
    if (parsed->path && parsed->path->is_absolute()) {
        std::abort();
    }
    // No component may be "." or ".."; a name that merely contains dots,
    // such as "notes../idea.md", is an ordinary relative path.
    if (parsed->path) {
        for (const auto& component : *parsed->path) {
            if (component == "." || component == "..") {
                std::abort();
            }
        }
    }
    const auto again = omanotes::serializeBufferRecovery(*parsed);
    const auto reparsed = omanotes::parseBufferRecovery(again);
    if (!reparsed || !(*reparsed == *parsed)) {
        std::abort();
    }
    return 0;
}
