#ifndef OMANOTES_UI_SIDEBAR_HPP
#define OMANOTES_UI_SIDEBAR_HPP

#include <QFrame>

#include <filesystem>
#include <optional>

class QModelIndex;
class QTreeView;

namespace omanotes {

class FileTreeModel;

class Sidebar final : public QFrame {
    Q_OBJECT

  public:
    explicit Sidebar(const std::filesystem::path& root, QWidget* parent = nullptr);

    void focusTree();
    void noteFileCreated(const std::filesystem::path& path);
    /// The file the tree's current row names, if any.
    [[nodiscard]] std::optional<std::filesystem::path> selectedPath() const;
    /// Make `path` the current row, expanding the directories above it.
    /// Does nothing when the path is not in the tree.
    void selectPath(const std::filesystem::path& path);

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
