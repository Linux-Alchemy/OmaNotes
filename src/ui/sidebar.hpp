#ifndef OMANOTES_UI_SIDEBAR_HPP
#define OMANOTES_UI_SIDEBAR_HPP

#include <QFrame>

#include <filesystem>

class QModelIndex;
class QTreeView;

namespace omanotes {

class FileTreeModel;

class Sidebar final : public QFrame {
    Q_OBJECT

  public:
    explicit Sidebar(const std::filesystem::path& root, QWidget* parent = nullptr);

    void focusTree();

  signals:
    void fileActivated(const std::filesystem::path& path);
    void editorFocusRequested();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void activate(const QModelIndex& index);
    void moveSelection(bool down);
    void moveLeft();
    void moveRight();

    FileTreeModel* model_;
    QTreeView* tree_;
};

} // namespace omanotes

#endif // OMANOTES_UI_SIDEBAR_HPP
