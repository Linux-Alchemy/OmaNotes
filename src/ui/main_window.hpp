#ifndef OMANOTES_UI_MAIN_WINDOW_HPP
#define OMANOTES_UI_MAIN_WINDOW_HPP

#include "app/launch_request.hpp"

#include <QMainWindow>

#include <filesystem>
#include <memory>
#include <optional>

class QLabel;
class QTabBar;

namespace omanotes {

class EditorAdapter;
class PrefixRouter;
class Sidebar;

class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(LaunchRequest launchRequest, QWidget* parent = nullptr);
    ~MainWindow() override;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void loadMarkdownFile(const std::filesystem::path& path);
    void refreshEditorStatus();

    LaunchRequest launchRequest_;
    std::unique_ptr<EditorAdapter> editor_;
    std::unique_ptr<PrefixRouter> prefixRouter_;
    Sidebar* sidebar_ = nullptr;
    QTabBar* bufferStrip_ = nullptr;
    QLabel* statusArea_ = nullptr;
    std::optional<std::filesystem::path> currentFile_;
};

} // namespace omanotes

#endif // OMANOTES_UI_MAIN_WINDOW_HPP
