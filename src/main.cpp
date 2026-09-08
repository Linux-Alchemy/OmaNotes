#include "app/launch_request.hpp"
#include "ui/main_window.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QTimer>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

[[nodiscard]] bool smokeTestRequested(int argc, char* argv[]) {
    const auto arguments = std::span(argv, static_cast<std::size_t>(argc));
    return std::ranges::any_of(arguments, [](const char* argument) {
        return std::string_view{argument} == "--smoke-test";
    });
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Omanotes"));
    QCoreApplication::setOrganizationName(QStringLiteral("Omanotes"));
    // The Wayland app_id, which Omarchy's bar shows for the focused window;
    // without this it falls back to the binary name, lowercase. Task 8.2's
    // package must ship a matching OmaNotes.desktop file.
    QGuiApplication::setDesktopFileName(QStringLiteral("OmaNotes"));

    std::vector<std::string_view> launchArguments;
    launchArguments.reserve(static_cast<std::size_t>(std::max(0, argc - 1)));
    for (int index = 1; index < argc; ++index) {
        const auto argument = std::string_view(argv[index]);
        if (argument != "--smoke-test") {
            launchArguments.push_back(argument);
        }
    }

    std::error_code currentDirectoryError;
    const auto currentDirectory = std::filesystem::current_path(currentDirectoryError);
    if (currentDirectoryError) {
        std::cerr << "Cannot resolve current directory: " << currentDirectoryError.message()
                  << '\n';
        return 2;
    }

    auto launchRequest = omanotes::resolveLaunchRequest(launchArguments, currentDirectory);
    if (!launchRequest) {
        std::cerr << launchRequest.error().message << '\n';
        return 2;
    }

    omanotes::MainWindow window(std::move(*launchRequest));
    window.show();

    if (smokeTestRequested(argc, argv)) {
        QTimer::singleShot(0, &application, &QCoreApplication::quit);
    }

    return application.exec();
}
