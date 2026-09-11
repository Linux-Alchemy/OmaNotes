// libFuzzer: link and image classification is total and never yields a path
// outside the root.
#include "ui/markdown_view.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QUrl>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

struct Sandbox {
    QTemporaryDir directory;
    std::filesystem::path root;
    omanotes::ResourcePolicy policy;

    Sandbox() {
        root = std::filesystem::canonical(directory.path().toStdString());
        std::filesystem::create_directories(root / "sub");
        std::ofstream(root / "real.md") << "# real\n";
        std::ofstream(root / "sub" / "deep.md") << "# deep\n";
        std::ofstream(root / "pic.png") << "\x89PNG";
        policy.root = root;
        policy.noteDirectory = root / "sub";
    }
};

Sandbox& sandbox() {
    static Sandbox instance;
    return instance;
}

bool inside(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    std::error_code error;
    const auto canonical = std::filesystem::canonical(candidate, error);
    return !error && canonical.string().rfind(root.string() + "/", 0) == 0;
}

} // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
    static QCoreApplication application(*argc, *argv);
    (void)sandbox();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto& box = sandbox();
    const auto text =
        QString::fromUtf8(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    const QUrl url(text);

    const auto action = omanotes::classifyLink(url, box.policy);
    switch (action.kind) {
    case omanotes::LinkActionKind::OpenExternal:
        if (action.external.scheme() != QStringLiteral("http") &&
            action.external.scheme() != QStringLiteral("https")) {
            std::abort();
        }
        break;
    case omanotes::LinkActionKind::OpenNote:
        if (!inside(action.note, box.root) || action.note.extension() != ".md") {
            std::abort();
        }
        break;
    case omanotes::LinkActionKind::ScrollToAnchor:
    case omanotes::LinkActionKind::Refuse:
        break;
    }

    const auto image = omanotes::resolveImageSource(url, box.policy);
    if (image && !inside(*image, box.root)) {
        std::abort();
    }
    return 0;
}
