#ifndef OMANOTES_UI_MARKDOWN_VIEW_HPP
#define OMANOTES_UI_MARKDOWN_VIEW_HPP

#include <QStringList>
#include <QTextBrowser>
#include <QUrl>

#include <cstdint>
#include <expected>
#include <filesystem>

namespace omanotes {

/// ADR 0009's resource rules for one render: where the note lives, and what
/// an image is allowed to cost. Path judgement is the workspace root's.
struct ResourcePolicy {
    std::filesystem::path root;
    std::filesystem::path noteDirectory;
    qint64 maxImageBytes = 10LL * 1024 * 1024;
    int maxImageEdge = 8192;
};

enum class LinkActionKind : std::uint8_t { OpenExternal, OpenNote, ScrollToAnchor, Refuse };

struct LinkAction {
    LinkActionKind kind;
    QUrl external;
    std::filesystem::path note;
    QString anchor;
    QString reason;
};

/// ADR 0009's scheme allowlist as a pure decision: http/https go to the
/// browser, a relative Markdown path inside the root opens as a note, a
/// bare fragment scrolls, and everything else is refused with the reason.
[[nodiscard]] LinkAction classifyLink(const QUrl& url, const ResourcePolicy& policy);

/// Resolve an image reference to a readable raster file inside the root,
/// or say exactly why not. Dimension checks happen at load time; this
/// answers scheme, path, format, and file-size questions.
[[nodiscard]] std::expected<std::filesystem::path, QString>
resolveImageSource(const QUrl& url, const ResourcePolicy& policy);

/// The reading view: a projection of a note's current text, never an editor.
/// It performs no network access, reads nothing outside the workspace root,
/// and leaves the source document untouched.
class MarkdownView final : public QTextBrowser {
    Q_OBJECT

  public:
    explicit MarkdownView(QWidget* parent = nullptr);

    void render(QStringView markdown, const ResourcePolicy& policy);
    /// Why each resource in the last render was refused; empty when all loaded.
    [[nodiscard]] const QStringList& refusedResources() const noexcept;

  signals:
    void noteLinkActivated(const std::filesystem::path& path);
    void externalLinkOpened(const QString& url);
    void linkRefused(const QString& reason);
    /// The hovered link's real target, or empty when the pointer leaves it.
    void linkTargetChanged(const QString& target);

  protected:
    QVariant loadResource(int type, const QUrl& url) override;

  private:
    ResourcePolicy policy_;
    QStringList refusals_;
};

} // namespace omanotes

#endif // OMANOTES_UI_MARKDOWN_VIEW_HPP
