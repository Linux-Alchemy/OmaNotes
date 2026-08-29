#include "ui/main_window.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QTimer>

#include <algorithm>
#include <span>
#include <string_view>

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

    omanotes::MainWindow window;
    window.show();

    if (smokeTestRequested(argc, argv)) {
        QTimer::singleShot(0, &application, &QCoreApplication::quit);
    }

    return application.exec();
}
