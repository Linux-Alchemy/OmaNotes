#include "ui/markdown_view.hpp"

#include "workspace/workspace_root.hpp"

#include <QDesktopServices>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>

#include <system_error>
#include <utility>

namespace omanotes {

namespace {

/// The raster allowlist from ADR 0009. SVG is deliberately absent.
bool hasAllowedImageExtension(const std::filesystem::path& path) {
    const auto extension = QString::fromStdString(path.extension().string()).toLower();
    return extension == QStringLiteral(".png") || extension == QStringLiteral(".jpg") ||
           extension == QStringLiteral(".jpeg") || extension == QStringLiteral(".gif") ||
           extension == QStringLiteral(".webp");
}

bool isMarkdownPath(const std::filesystem::path& path) {
    return QString::fromStdString(path.extension().string())
               .compare(QStringLiteral(".md"), Qt::CaseInsensitive) == 0;
}

QString describe(const QUrl& url) {
    const auto text = url.toDisplayString();
    return text.size() > 120 ? text.left(117) + QStringLiteral("...") : text;
}

/// What a refused image shows instead of nothing: a small, obviously
/// deliberate placeholder rather than a broken-icon mystery.
QImage placeholderImage() {
    QImage image(12, 12, QImage::Format_ARGB32);
    image.fill(Qt::lightGray);
    QPainter painter(&image);
    painter.setPen(Qt::darkGray);
    painter.drawRect(0, 0, 11, 11);
    painter.drawLine(0, 0, 11, 11);
    painter.drawLine(11, 0, 0, 11);
    return image;
}

} // namespace

LinkAction classifyLink(const QUrl& url, const ResourcePolicy& policy) {
    const auto scheme = url.scheme().toLower();

    if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) {
        return {LinkActionKind::OpenExternal, url, {}, {}, {}};
    }
    if (!scheme.isEmpty()) {
        return {LinkActionKind::Refuse,
                {},
                {},
                {},
                QStringLiteral("Refused link: the '%1' scheme is not allowed").arg(scheme)};
    }
    if (!url.authority().isEmpty()) {
        return {LinkActionKind::Refuse,
                {},
                {},
                {},
                QStringLiteral("Refused link: remote target %1").arg(describe(url))};
    }
    if (url.path().isEmpty() && url.hasFragment()) {
        return {LinkActionKind::ScrollToAnchor, {}, {}, url.fragment(), {}};
    }

    const std::filesystem::path relative = url.path().toStdString();
    if (relative.empty() || relative.is_absolute()) {
        return {LinkActionKind::Refuse,
                {},
                {},
                {},
                QStringLiteral("Refused link: only workspace-relative paths open (%1)")
                    .arg(describe(url))};
    }
    if (!isMarkdownPath(relative)) {
        return {LinkActionKind::Refuse,
                {},
                {},
                {},
                QStringLiteral("Refused link: only Markdown notes open from a note (%1)")
                    .arg(describe(url))};
    }

    auto workspace = WorkspaceRoot::resolve(policy.root);
    if (!workspace) {
        return {
            LinkActionKind::Refuse, {}, {}, {}, QString::fromStdString(workspace.error().message)};
    }
    auto resolved = workspace->resolveFile(policy.noteDirectory / relative);
    if (!resolved) {
        return {
            LinkActionKind::Refuse, {}, {}, {}, QString::fromStdString(resolved.error().message)};
    }
    return {LinkActionKind::OpenNote, {}, *std::move(resolved), {}, {}};
}

std::expected<std::filesystem::path, QString> resolveImageSource(const QUrl& url,
                                                                 const ResourcePolicy& policy) {
    const auto scheme = url.scheme().toLower();
    if (!scheme.isEmpty()) {
        return std::unexpected(
            QStringLiteral("image %1: the '%2' scheme is not allowed").arg(describe(url), scheme));
    }
    if (!url.authority().isEmpty()) {
        return std::unexpected(
            QStringLiteral("image %1: remote images are not fetched").arg(describe(url)));
    }

    const std::filesystem::path relative = url.path().toStdString();
    if (relative.empty() || relative.is_absolute()) {
        return std::unexpected(
            QStringLiteral("image %1: only workspace-relative paths load").arg(describe(url)));
    }
    if (!hasAllowedImageExtension(relative)) {
        return std::unexpected(
            QStringLiteral("image %1: not an allowed raster format").arg(describe(url)));
    }

    auto workspace = WorkspaceRoot::resolve(policy.root);
    if (!workspace) {
        return std::unexpected(QString::fromStdString(workspace.error().message));
    }
    auto resolved = workspace->resolveFile(policy.noteDirectory / relative);
    if (!resolved) {
        return std::unexpected(QString::fromStdString(resolved.error().message));
    }

    std::error_code sizeError;
    const auto bytes = std::filesystem::file_size(*resolved, sizeError);
    if (sizeError) {
        return std::unexpected(
            QStringLiteral("image %1: size could not be read").arg(describe(url)));
    }
    if (std::cmp_greater(bytes, policy.maxImageBytes)) {
        return std::unexpected(QStringLiteral("image %1: larger than the %2 MiB limit")
                                   .arg(describe(url))
                                   .arg(policy.maxImageBytes / (1024LL * 1024)));
    }
    return *std::move(resolved);
}

MarkdownView::MarkdownView(QWidget* parent) : QTextBrowser(parent) {
    setObjectName(QStringLiteral("readingView"));
    setAccessibleName(QStringLiteral("Reading view"));
    setOpenLinks(false);
    setOpenExternalLinks(false);
    setFrameShape(QFrame::NoFrame);
    document()->setDocumentMargin(24);
    // Restrained reading: the system face, one point up. Colours stay with
    // the palette until Task 6.2 brings the Omarchy theme.
    auto readingFont = font();
    readingFont.setPointSizeF(readingFont.pointSizeF() + 1.0);
    setFont(readingFont);

    connect(this, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        auto action = classifyLink(url, policy_);
        switch (action.kind) {
        case LinkActionKind::OpenExternal:
            if (QDesktopServices::openUrl(action.external)) {
                emit externalLinkOpened(action.external.toDisplayString());
            } else {
                emit linkRefused(QStringLiteral("The browser could not be asked to open %1")
                                     .arg(describe(action.external)));
            }
            return;
        case LinkActionKind::OpenNote:
            emit noteLinkActivated(action.note);
            return;
        case LinkActionKind::ScrollToAnchor:
            scrollToAnchor(action.anchor);
            return;
        case LinkActionKind::Refuse:
            emit linkRefused(action.reason);
            return;
        }
    });
    connect(this, &QTextBrowser::highlighted, this,
            [this](const QUrl& url) { emit linkTargetChanged(url.toDisplayString()); });
}

void MarkdownView::render(QStringView markdown, const ResourcePolicy& policy) {
    policy_ = policy;
    refusals_.clear();
    document()->setMarkdown(markdown.toString(),
                            QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) |
                                QTextDocument::MarkdownNoHTML);
    // A touch of air between lines; the projection is ours to restyle.
    QTextCursor cursor(document());
    cursor.select(QTextCursor::Document);
    QTextBlockFormat spacing;
    spacing.setLineHeight(130, QTextBlockFormat::ProportionalHeight);
    cursor.mergeBlockFormat(spacing);

    // Layout loads images lazily, but the policy verdicts must not be:
    // request every image now, so each is loaded or refused up front.
    for (auto block = document()->begin(); block != document()->end(); block = block.next()) {
        for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment) {
            const auto format = fragment.fragment().charFormat();
            if (format.isImageFormat()) {
                (void)document()->resource(QTextDocument::ImageResource,
                                           QUrl(format.toImageFormat().name()));
            }
        }
    }
    // Replacing the document's content pushes the widget's own cursor to the
    // end of the new text, and the next layout scrolls to keep it visible;
    // on a fresh launch that layout is deferred until the view is first
    // shown, so the first note opened for reading landed at the bottom. A
    // freshly rendered note opens at the top.
    moveCursor(QTextCursor::Start);
    verticalScrollBar()->setValue(0);
}

const QStringList& MarkdownView::refusedResources() const noexcept { return refusals_; }

QVariant MarkdownView::loadResource(int type, const QUrl& url) {
    // Never fall through to QTextBrowser's own file loader: every resource
    // request is answered here, with policy or with a placeholder.
    if (type != QTextDocument::ImageResource) {
        return {};
    }
    auto resolved = resolveImageSource(url, policy_);
    if (!resolved) {
        // Layout may ask for the same resource more than once; one refusal
        // per reason keeps the record honest.
        if (!refusals_.contains(resolved.error())) {
            refusals_.push_back(resolved.error());
        }
        return placeholderImage();
    }

    QImageReader reader(QString::fromStdString(resolved->string()));
    const auto dimensions = reader.size();
    if (dimensions.isValid() &&
        (dimensions.width() > policy_.maxImageEdge || dimensions.height() > policy_.maxImageEdge)) {
        refusals_.push_back(QStringLiteral("image %1: %2x%3 exceeds the %4px limit")
                                .arg(describe(url))
                                .arg(dimensions.width())
                                .arg(dimensions.height())
                                .arg(policy_.maxImageEdge));
        return placeholderImage();
    }
    const auto image = reader.read();
    if (image.isNull() || image.width() > policy_.maxImageEdge ||
        image.height() > policy_.maxImageEdge) {
        refusals_.push_back(
            QStringLiteral("image %1: could not be decoded safely").arg(describe(url)));
        return placeholderImage();
    }
    return image;
}

} // namespace omanotes
