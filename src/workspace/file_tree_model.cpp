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

    std::ranges::sort(children, orderBefore);
    return children;
}

bool FileTreeModel::orderBefore(const std::unique_ptr<Node>& left,
                                const std::unique_ptr<Node>& right) {
    if (left->directory != right->directory) {
        return left->directory;
    }
    return QString::localeAwareCompare(displayName(left->path), displayName(right->path)) < 0;
}

FileTreeModel::Node*
FileTreeModel::findFetchedNode(const std::filesystem::path& path) const noexcept {
    Node* current = root_.get();
    if (!current->fetched) {
        return nullptr;
    }
    if (current->path == path) {
        return current;
    }

    const auto relative = path.lexically_relative(current->path);
    if (relative.empty() || relative.begin()->string() == "..") {
        return nullptr;
    }

    for (const auto& part : relative) {
        const auto next = std::ranges::find_if(current->children, [&part](const auto& child) {
            return child->path.filename() == part;
        });
        if (next == current->children.end() || !(*next)->fetched) {
            return nullptr;
        }
        current = next->get();
    }
    return current;
}

QModelIndex FileTreeModel::indexForNode(Node* node) const {
    if (node == nullptr || node == root_.get() || node->parent == nullptr) {
        return {};
    }
    const auto& siblings = node->parent->children;
    const auto found =
        std::ranges::find_if(siblings, [node](const auto& child) { return child.get() == node; });
    if (found == siblings.end()) {
        return {};
    }
    return createIndex(static_cast<int>(std::distance(siblings.begin(), found)), 0, node);
}

QModelIndex FileTreeModel::indexForPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error || !workspace_.contains(canonical)) {
        return {};
    }
    const auto relative = canonical.lexically_relative(root_->path);
    if (relative.empty() || relative.begin()->string() == "..") {
        return {};
    }
    QModelIndex current;
    auto walked = root_->path;
    for (const auto& part : relative) {
        walked /= part;
        if (canFetchMore(current)) {
            fetchMore(current);
        }
        QModelIndex next;
        for (int row = 0; row < rowCount(current); ++row) {
            const auto candidate = index(row, 0, current);
            if (pathForIndex(candidate) == walked) {
                next = candidate;
                break;
            }
        }
        if (!next.isValid()) {
            return {};
        }
        current = next;
    }
    return current;
}

void FileTreeModel::noteFileCreated(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error || !workspace_.contains(canonical) || isHidden(canonical)) {
        return;
    }
    if (!showAllFiles_ && !isMarkdown(canonical)) {
        return;
    }

    // An unopened directory has no children yet; it will list the file itself
    // when the user expands it.
    auto* parentNode = findFetchedNode(canonical.parent_path());
    if (parentNode == nullptr) {
        return;
    }
    const auto present = std::ranges::any_of(
        parentNode->children, [&canonical](const auto& child) { return child->path == canonical; });
    if (present) {
        return;
    }

    auto node = std::make_unique<Node>(Node{canonical, parentNode, false, false, {}});
    const auto where = std::ranges::lower_bound(parentNode->children, node, orderBefore);
    const auto row = static_cast<int>(std::distance(parentNode->children.begin(), where));

    beginInsertRows(indexForNode(parentNode), row, row);
    parentNode->children.insert(where, std::move(node));
    endInsertRows();
}

void FileTreeModel::resetTree() {
    root_->children.clear();
    root_->fetched = false;
}

} // namespace omanotes
