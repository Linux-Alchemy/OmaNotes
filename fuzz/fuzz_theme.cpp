// libFuzzer: whatever colors.toml says, the palette that comes out is
// complete and readable.
#include "ui/theme_adapter.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

struct Sandbox {
    QTemporaryDir directory;
    omanotes::ThemeSources sources;

    Sandbox() {
        const std::filesystem::path base = directory.path().toStdString();
        sources = {base / "state" / "omarchy" / "current", base / "config" / "omarchy",
                   base / "config" / "omanotes"};
        std::filesystem::create_directories(sources.stateDir / "theme");
        std::filesystem::create_directories(sources.configDir);
    }
};

Sandbox& sandbox() {
    static Sandbox instance;
    return instance;
}

void writeBytes(const std::filesystem::path& path, const std::uint8_t* data, std::size_t size) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
}

} // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
    static QCoreApplication application(*argc, *argv);
    (void)sandbox();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // The first byte picks which file receives the input; the rest is the file.
    const auto& box = sandbox();
    if (size == 0) {
        return 0;
    }
    const auto target = data[0] % 3;
    const auto* body = data + 1;
    const auto length = size - 1;
    writeBytes(box.sources.stateDir / "theme" / "colors.toml", target == 0 ? body : nullptr,
               target == 0 ? length : 0);
    writeBytes(box.sources.configDir / "shell.toml", target == 1 ? body : nullptr,
               target == 1 ? length : 0);
    writeBytes(box.sources.stateDir / "theme.name", target == 2 ? body : nullptr,
               target == 2 ? length : 0);

    const omanotes::ThemeAdapter adapter(box.sources);
    const auto& palette = adapter.currentPalette();
    if (!palette.background.isValid() || !palette.text.isValid() ||
        omanotes::contrastRatio(palette.text, palette.background) < 3.0 ||
        omanotes::contrastRatio(palette.selectedText, palette.selection) < 3.0 ||
        !(palette.baseFontPointSize >= 6.0 && palette.baseFontPointSize <= 32.0)) {
        std::abort();
    }
    return 0;
}
