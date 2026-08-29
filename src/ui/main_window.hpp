#ifndef OMANOTES_UI_MAIN_WINDOW_HPP
#define OMANOTES_UI_MAIN_WINDOW_HPP

#include <QMainWindow>

namespace omanotes {

class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(QWidget* parent = nullptr);
};

} // namespace omanotes

#endif // OMANOTES_UI_MAIN_WINDOW_HPP
