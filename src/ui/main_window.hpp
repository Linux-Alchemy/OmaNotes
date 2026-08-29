#ifndef OMANOTES_UI_MAIN_WINDOW_HPP
#define OMANOTES_UI_MAIN_WINDOW_HPP

#include <QMainWindow>

#include <memory>

namespace omanotes {

class EditorAdapter;
class PrefixRouter;

class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    std::unique_ptr<EditorAdapter> editor_;
    std::unique_ptr<PrefixRouter> prefixRouter_;
};

} // namespace omanotes

#endif // OMANOTES_UI_MAIN_WINDOW_HPP
