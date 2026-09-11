// libFuzzer: the session snapshot parser must refuse or round-trip, never
// crash, never accept a document outside its limits.
#include "session/session_snapshot.hpp"

#include <QByteArray>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto bytes =
        QByteArray(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    const auto parsed = omanotes::parseSessionSnapshot(bytes);
    if (!parsed) {
        return 0;
    }
    if (parsed->buffers.size() > omanotes::kSessionMaxBuffers ||
        parsed->window.width > omanotes::kSessionMaxDimension) {
        std::abort();
    }
    const auto again = omanotes::serializeSessionSnapshot(*parsed);
    const auto reparsed = omanotes::parseSessionSnapshot(again);
    if (!reparsed || !(*reparsed == *parsed)) {
        std::abort();
    }
    return 0;
}
