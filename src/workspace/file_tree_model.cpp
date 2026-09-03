#include "workspace/file_tree_model.hpp"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <algorithm>
#include <stdexcept>
#include <system_error>

namespace omanotes {

struct FileTreeModel::Node {
    std::filesystem::path path;
    Node* parent = nullptr;
    bool directory = false;
    bool fetched = false;
    std::vector<std::unique_ptr<Node>> children;
};

namespace {

WorkspaceRoot checkedWorkspace(const std::filesystem::path& root) {
    auto resolved = WorkspaceRoot::resolve(root);
    if (!resolved) {
        throw std::invalid_argument(resolved.error().message);
    }
    return std::move(*resolved);
}

QString displayName(const std::filesystem::path& path) {
    return QFile::decodeName(QByteArray::fromStdString(path.filename().native()));
}

bool isHidden(const std::filesystem::path& path) {
    const auto name = path.filename().native();
    return !name.empty() && name.front() == '.';
}

bool isMarkdown(const std::filesystem::path& path) {
    return displayName(path.extension()).compare(QStringLiteral(".md"), Qt::CaseInsensitive) == 0;
}

} // namespace

FileTreeModel::FileTreeModel(const std::filesystem::path& root, QObject* parent)
    : QAbstractItemModel(parent), workspace_(checkedWorkspace(root)),
      root_(std::make_unique<Node>(Node{workspace_.path(), nullptr, true, false, {}})) {}

FileTreeModel::~FileTreeModel() = default;

void FileTreeModel::setShowAllFiles(bool enabled) {
    if (showAllFiles_ == enabled) {
        return;
    }
    beginResetModel();
    showAllFiles_ = enabled;
    resetTree();
    endResetModel();
}

std::filesystem::path FileTreeModel::pathForIndex(const QModelIndex& index) const {
    if (!index.isValid()) {
        return {};
    }
    return nodeForIndex(index)->path;
}

bool FileTreeModel::isDirectory(const QModelIndex& index) const {
    return index.isValid() && nodeForIndex(index)->directory;
}

QModelIndex FileTreeModel::index(int row, int column, const QModelIndex& parentIndex) const {
    if (row < 0 || column != 0) {
        return {};
    }
    auto* parentNode = nodeForIndex(parentIndex);
    const auto childRow = static_cast<std::size_t>(row);
    if (childRow >= parentNode->children.size()) {
        return {};
    }
    return createIndex(row, column, parentNode->children[childRow].get());
}

QModelIndex FileTreeModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) {
        return {};
    }
    auto* parentNode = nodeForIndex(child)->parent;
    if (parentNode == nullptr || parentNode == root_.get()) {
        return {};
    }
    auto* grandparent = parentNode->parent;
    const auto found = std::ranges::find_if(grandparent->children,
                                            [parentNode](const std::unique_ptr<Node>& candidate) {
                                                return candidate.get() == parentNode;
                                            });
    if (found == grandparent->children.end()) {
        return {};
    }
    const auto row = static_cast<int>(std::distance(grandparent->children.begin(), found));
    return createIndex(row, 0, parentNode);
}

int FileTreeModel::rowCount(const QModelIndex& parentIndex) const {
    if (parentIndex.column() > 0) {
        return 0;
    }
    return static_cast<int>(nodeForIndex(parentIndex)->children.size());
}

int FileTreeModel::columnCount(const QModelIndex& parentIndex) const {
    Q_UNUSED(parentIndex)
    return 1;
}

QVariant FileTreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) {
        return {};
    }
    const auto* node = nodeForIndex(index);
    if (role == Qt::DisplayRole) {
        return displayName(node->path);
    }
    if (role == Qt::ToolTipRole) {
        return QFile::decodeName(QByteArray::fromStdString(node->path.native()));
    }
    return {};
}

Qt::ItemFlags FileTreeModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool FileTreeModel::hasChildren(const QModelIndex& parentIndex) const {
    return nodeForIndex(parentIndex)->directory;
}

bool FileTreeModel::canFetchMore(const QModelIndex& parentIndex) const {
    const auto* node = nodeForIndex(parentIndex);
    return node->directory && !node->fetched;
}

void FileTreeModel::fetchMore(const QModelIndex& parentIndex) {
    auto* node = nodeForIndex(parentIndex);
    if (!node->directory || node->fetched) {
        return;
    }

    auto children = enumerate(*node);
    node->fetched = true;
    if (children.empty()) {
        return;
    }

    beginInsertRows(parentIndex, 0, static_cast<int>(children.size()) - 1);
    node->children = std::move(children);
    endInsertRows();
}

FileTreeModel::Node* FileTreeModel::nodeForIndex(const QModelIndex& index) const noexcept {
    if (!index.isValid()) {
        return root_.get();
    }
    return static_cast<Node*>(index.internalPointer());
}

std::vector<std::unique_ptr<FileTreeModel::Node>> FileTreeModel::enumerate(Node& parentNode) const {
    std::vector<std::unique_ptr<Node>> children;
    std::error_code error;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::directory_iterator entry(parentNode.path, options, error), end;
         !error && entry != end; entry.increment(error)) {
        const auto& path = entry->path();
        if (isHidden(path)) {
            continue;
        }

        std::error_code statusError;
        const auto status = entry->symlink_status(statusError);
        if (statusError || std::filesystem::is_symlink(status)) {
            continue;
        }
        const auto directory = std::filesystem::is_directory(status);
        if (!directory &&
            (!std::filesystem::is_regular_file(status) || (!showAllFiles_ && !isMarkdown(path)))) {
            continue;
        }

        const auto canonical = std::filesystem::canonical(path, statusError);
        if (statusError || !workspace_.contains(canonical)) {
            continue;
        }
        children.push_back(
            std::make_unique<Node>(Node{canonical, &parentNode, directory, false, {}}));
    }

    std::ranges::sort(children, [](const auto& left, const auto& right) {
        if (left->directory != right->directory) {
            return left->directory;
        }
        return QString::localeAwareCompare(displayName(left->path), displayName(right->path)) < 0;
    });
    return children;
}

void FileTreeModel::resetTree() {
    root_->children.clear();
    root_->fetched = false;
}

} // namespace omanotes
