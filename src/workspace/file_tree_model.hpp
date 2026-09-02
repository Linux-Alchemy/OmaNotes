#ifndef OMANOTES_WORKSPACE_FILE_TREE_MODEL_HPP
#define OMANOTES_WORKSPACE_FILE_TREE_MODEL_HPP

#include "workspace/workspace_root.hpp"

#include <QAbstractItemModel>

#include <filesystem>
#include <memory>
#include <vector>

namespace omanotes {

class FileTreeModel final : public QAbstractItemModel {
  public:
    explicit FileTreeModel(const std::filesystem::path& root, QObject* parent = nullptr);
    ~FileTreeModel() override;

    void setShowAllFiles(bool enabled);
    [[nodiscard]] std::filesystem::path pathForIndex(const QModelIndex& index) const;
    [[nodiscard]] bool isDirectory(const QModelIndex& index) const;

    [[nodiscard]] QModelIndex index(int row, int column,
                                    const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] bool hasChildren(const QModelIndex& parent = {}) const override;
    [[nodiscard]] bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

  private:
    struct Node;

    [[nodiscard]] Node* nodeForIndex(const QModelIndex& index) const noexcept;
    [[nodiscard]] std::vector<std::unique_ptr<Node>> enumerate(Node& parent) const;
    void resetTree();

    WorkspaceRoot workspace_;
    std::unique_ptr<Node> root_;
    bool showAllFiles_ = false;
};

} // namespace omanotes

#endif // OMANOTES_WORKSPACE_FILE_TREE_MODEL_HPP
