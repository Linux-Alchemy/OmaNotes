#include "ui/markdown_view.hpp"

#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include <filesystem>

namespace {

std::filesystem::path fixtureSource() {
    return std::filesystem::path(OMANOTES_FIXTURE_DIR) / "markdown";
}

QString readFixture(const std::filesystem::path& path) {
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

void writeFile(const std::filesystem::path& path, const QByteArray& contents) {
    QFile file(QString::fromStdString(path.string()));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

/// A disposable workspace holding a copy of the committed fixtures, so
/// runtime-generated cases (symlinks, oversized files) never touch the repo.
class FixtureWorkspace final {
  public:
    FixtureWorkspace() {
        root_ = std::filesystem::canonical(temporary_.path().toStdString());
        std::filesystem::copy(fixtureSource(), root_, std::filesystem::copy_options::recursive);
    }

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }
    [[nodiscard]] omanotes::ResourcePolicy policy() const {
        omanotes::ResourcePolicy policy;
        policy.root = root_;
        policy.noteDirectory = root_;
        return policy;
    }

  private:
    QTemporaryDir temporary_;
    std::filesystem::path root_;
};

} // namespace

class MarkdownRenderTest final : public QObject {
    Q_OBJECT

  private slots:
    void rendersSupportedBasicsWithoutRefusals();
    void treatsRawHtmlAsInertText();
    void refusesRemoteImagesWithoutFetching();
    void refusesTraversalAndAbsoluteImagePaths();
    void classifiesLinkSchemesPerPolicy();
    void refusesSymlinkEscape();
    void refusesOversizedImages();
    void survivesMalformedInput();
    void survivesPathologicalText();
    void rendersDeterministically();
};

void MarkdownRenderTest::rendersSupportedBasicsWithoutRefusals() {
    FixtureWorkspace workspace;
    omanotes::MarkdownView view;
    view.render(readFixture(workspace.root() / "supported-basics.md"), workspace.policy());

    const auto text = view.toPlainText();
    QVERIFY(text.contains(QStringLiteral("Supported basics")));
    QVERIFY(text.contains(QStringLiteral("First ordered item")));
    QVERIFY(text.contains(QStringLiteral("A restrained blockquote")));
    QVERIFY(text.contains(QStringLiteral("int main()")));
    QVERIFY2(view.refusedResources().isEmpty(),
             qPrintable(view.refusedResources().join(QStringLiteral("; "))));
}

void MarkdownRenderTest::treatsRawHtmlAsInertText() {
    FixtureWorkspace workspace;
    omanotes::MarkdownView view;
    view.render(readFixture(workspace.root() / "hostile-raw-html.md"), workspace.policy());

    const auto text = view.toPlainText();
    // The script's source text is visible as text, not swallowed as markup,
    // and rendering continued past every attempt.
    QVERIFY(text.contains(QStringLiteral("alert('executed')")));
    QVERIFY(text.contains(QStringLiteral("overlay attempt")));
    QVERIFY(text.contains(QStringLiteral("rendering continued")));
    // Nothing in the rendered document became an image or link resource.
    QVERIFY(view.refusedResources().isEmpty());
}

void MarkdownRenderTest::refusesRemoteImagesWithoutFetching() {
    FixtureWorkspace workspace;
    omanotes::MarkdownView view;
    view.render(readFixture(workspace.root() / "hostile-remote-resources.md"), workspace.policy());

    QVERIFY(view.toPlainText().contains(QStringLiteral("still be visible")));
    QCOMPARE(view.refusedResources().size(), 5);
    for (const auto& reason : view.refusedResources()) {
        QVERIFY2(reason.contains(QStringLiteral("scheme")) ||
                     reason.contains(QStringLiteral("remote")),
                 qPrintable(reason));
    }
}

void MarkdownRenderTest::refusesTraversalAndAbsoluteImagePaths() {
    FixtureWorkspace workspace;
    omanotes::MarkdownView view;
    view.render(readFixture(workspace.root() / "hostile-traversal.md"), workspace.policy());

    QVERIFY(view.toPlainText().contains(QStringLiteral("still be visible")));
    // Every image in the fixture must have been refused; none may resolve.
    QVERIFY(view.refusedResources().size() >= 6);

    // A target that genuinely exists outside the root is still refused.
    const auto outside = workspace.root().parent_path() / "outside-target.png";
    QFile::copy(QString::fromStdString((workspace.root() / "assets/tiny.png").string()),
                QString::fromStdString(outside.string()));
    auto policy = workspace.policy();
    const auto resolved =
        omanotes::resolveImageSource(QUrl(QStringLiteral("../outside-target.png")), policy);
    QVERIFY(!resolved.has_value());
    QFile::remove(QString::fromStdString(outside.string()));
}

void MarkdownRenderTest::classifiesLinkSchemesPerPolicy() {
    FixtureWorkspace workspace;
    writeFile(workspace.root() / "other-note.md", "# other\n");
    const auto policy = workspace.policy();

    using omanotes::classifyLink;
    using omanotes::LinkActionKind;

    QCOMPARE(classifyLink(QUrl(QStringLiteral("https://omarchy.org")), policy).kind,
             LinkActionKind::OpenExternal);
    QCOMPARE(classifyLink(QUrl(QStringLiteral("http://example.org")), policy).kind,
             LinkActionKind::OpenExternal);

    const auto note = classifyLink(QUrl(QStringLiteral("other-note.md")), policy);
    QCOMPARE(note.kind, LinkActionKind::OpenNote);
    QCOMPARE(note.note, workspace.root() / "other-note.md");

    for (const auto* refused :
         {"javascript:alert('js')", "JaVaScRiPt:alert('case')",
          "data:text/html,<script>alert('data')</script>", "vscode://payload/open",
          "ssh://evil.example/", "magnet:?xt=urn:btih:payload", "mailto:someone@example.com",
          "file:///etc/passwd", "//evil.example/x", "/etc/passwd", "../outside.md",
          "missing-note.md", "not-a-note.txt"}) {
        const auto action = classifyLink(QUrl(QString::fromUtf8(refused)), policy);
        QVERIFY2(action.kind == LinkActionKind::Refuse, refused);
        QVERIFY2(!action.reason.isEmpty(), refused);
    }

    QCOMPARE(classifyLink(QUrl(QStringLiteral("#heading")), policy).kind,
             LinkActionKind::ScrollToAnchor);
}

void MarkdownRenderTest::refusesSymlinkEscape() {
    FixtureWorkspace workspace;
    const auto secret = workspace.root().parent_path() / "escape-secret.png";
    QFile::copy(QString::fromStdString((workspace.root() / "assets/tiny.png").string()),
                QString::fromStdString(secret.string()));
    std::filesystem::create_symlink(secret, workspace.root() / "assets/escape.png");

    omanotes::MarkdownView view;
    view.render(QStringLiteral("![escapes](assets/escape.png)"), workspace.policy());
    QCOMPARE(view.refusedResources().size(), 1);
    QFile::remove(QString::fromStdString(secret.string()));
}

void MarkdownRenderTest::refusesOversizedImages() {
    FixtureWorkspace workspace;

    // Larger than the byte cap: refused before any decode.
    writeFile(workspace.root() / "big-file.png", QByteArray(qsizetype{11} * 1024 * 1024, 'x'));
    // Within the byte cap, beyond the edge cap: refused from the header.
    QImage wide(9000, 8, QImage::Format_RGB32);
    wide.fill(Qt::white);
    QVERIFY(wide.save(QString::fromStdString((workspace.root() / "wide.png").string())));

    omanotes::MarkdownView view;
    view.render(QStringLiteral("![big](big-file.png)\n\n![wide](wide.png)"), workspace.policy());
    QCOMPARE(view.refusedResources().size(), 2);
    QVERIFY(view.refusedResources().first().contains(QStringLiteral("MiB")));
    QVERIFY(view.refusedResources().last().contains(QStringLiteral("px")));
}

void MarkdownRenderTest::survivesMalformedInput() {
    FixtureWorkspace workspace;
    omanotes::MarkdownView view;
    view.render(readFixture(workspace.root() / "malformed.md"), workspace.policy());
    QVERIFY(view.toPlainText().contains(QStringLiteral("Final line")));
}

void MarkdownRenderTest::survivesPathologicalText() {
    FixtureWorkspace workspace;
    QString hostile;
    hostile += QString(qsizetype{2} * 1024 * 1024, u'a');
    hostile += QStringLiteral("\n\n");
    for (int depth = 0; depth < 200; ++depth) {
        hostile += QStringLiteral(">");
    }
    hostile += QStringLiteral(" deep quote\n\n");
    for (int row = 0; row < 2000; ++row) {
        hostile += QStringLiteral("| a | b | c |\n");
    }
    hostile += QStringLiteral("\nfinal marker\n");

    omanotes::MarkdownView view;
    view.render(hostile, workspace.policy());
    QVERIFY(view.toPlainText().contains(QStringLiteral("final marker")));
}

void MarkdownRenderTest::rendersDeterministically() {
    FixtureWorkspace workspace;
    const auto markdown = readFixture(workspace.root() / "supported-basics.md");
    omanotes::MarkdownView first;
    first.render(markdown, workspace.policy());
    omanotes::MarkdownView second;
    second.render(markdown, workspace.policy());
    QCOMPARE(first.toPlainText(), second.toPlainText());
    QCOMPARE(first.document()->toHtml(), second.document()->toHtml());
    // Rendering never touched the source.
    QCOMPARE(readFixture(workspace.root() / "supported-basics.md"), markdown);
}

QTEST_MAIN(MarkdownRenderTest)
#include "markdown_render_test.moc"
