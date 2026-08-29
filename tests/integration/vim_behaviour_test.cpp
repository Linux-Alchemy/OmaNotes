#include "editor/ktext_editor_adapter.hpp"

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <QApplication>
#include <QCoreApplication>
#include <QVBoxLayout>
#include <QtTest>

#include <memory>

namespace {

class VimHarness final {
  public:
    VimHarness()
        : layout_(&host_), adapter_(std::make_unique<omanotes::KTextEditorAdapter>(&host_)),
          view_(qobject_cast<KTextEditor::View*>(adapter_->widget())) {
        layout_.setContentsMargins(0, 0, 0, 0);
        layout_.addWidget(adapter_->widget());
        host_.resize(800, 500);
        host_.show();
        view_->setFocus();
        QCoreApplication::processEvents();
    }

    [[nodiscard]] KTextEditor::Document* document() const { return view_->document(); }
    [[nodiscard]] KTextEditor::View* view() const { return view_; }
    [[nodiscard]] QString text() const { return adapter_->text(); }
    [[nodiscard]] QString modeName() const { return adapter_->modeName(); }

    void setText(const QString& text) {
        adapter_->setText(text);
        document()->setModified(false);
        view_->setCursorPosition(KTextEditor::Cursor(0, 0));
        view_->setFocus();
        QCoreApplication::processEvents();
    }

    void type(QStringView keys) {
        for (const auto character : keys) {
            QTest::keyClicks(eventTarget(), QString(character));
            QCoreApplication::processEvents();
        }
    }

    void press(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QTest::keyClick(eventTarget(), key, modifiers);
        QCoreApplication::processEvents();
    }

  private:
    [[nodiscard]] QWidget* eventTarget() const {
        if (auto* focused = QApplication::focusWidget(); focused != nullptr) {
            return focused;
        }
        return view_;
    }

    QWidget host_;
    QVBoxLayout layout_;
    std::unique_ptr<omanotes::KTextEditorAdapter> adapter_;
    KTextEditor::View* view_;
};

} // namespace

class VimBehaviourTest final : public QObject {
    Q_OBJECT

  private slots:
    void supportsMotionsAndCounts();
    void supportsOperatorsAndTextObjects();
    void supportsVisualModes();
    void supportsRegistersPasteUndoAndRepeat();
    void preservesUnnamedRegisterAfterBlackHoleDelete();
    void keepsUnnamedRegisterIndependentFromNamedYank();
    void supportsSearchAndMarks();
    void supportsCommandModeAndMappings();
};

void VimBehaviourTest::supportsMotionsAndCounts() {
    VimHarness vim;
    vim.setText(QStringLiteral("one two three\nfour five six\nseven eight nine"));

    vim.type(QStringLiteral("2w"));
    QCOMPARE(vim.view()->cursorPosition(), KTextEditor::Cursor(0, 8));
    vim.type(QStringLiteral("2j"));
    QCOMPARE(vim.view()->cursorPosition(), KTextEditor::Cursor(2, 8));
    vim.type(QStringLiteral("0e"));
    QCOMPARE(vim.view()->cursorPosition(), KTextEditor::Cursor(2, 4));
    vim.type(QStringLiteral("b$"));
    QCOMPARE(vim.view()->cursorPosition(), KTextEditor::Cursor(2, 15));
    vim.type(QStringLiteral("gg^"));
    QCOMPARE(vim.view()->cursorPosition(), KTextEditor::Cursor(0, 0));
    vim.type(QStringLiteral("G"));
    QCOMPARE(vim.view()->cursorPosition().line(), 2);

    vim.setText(QString(40, QLatin1Char('\n')));
    vim.view()->setCursorPosition(KTextEditor::Cursor(39, 0));
    vim.press(Qt::Key_B, Qt::ControlModifier);
    QVERIFY(vim.view()->cursorPosition().line() < 39);
    const auto pageUpLine = vim.view()->cursorPosition().line();
    vim.press(Qt::Key_F, Qt::ControlModifier);
    QVERIFY(vim.view()->cursorPosition().line() > pageUpLine);
}

void VimBehaviourTest::supportsOperatorsAndTextObjects() {
    VimHarness vim;
    vim.setText(QStringLiteral("one two three"));

    vim.type(QStringLiteral("dw"));
    QCOMPARE(vim.text(), QStringLiteral("two three"));
    vim.type(QStringLiteral("ciwONE"));
    vim.press(Qt::Key_Escape);
    QCOMPARE(vim.text(), QStringLiteral("ONE three"));
    vim.type(QStringLiteral("A!"));
    vim.press(Qt::Key_Escape);
    QCOMPARE(vim.text(), QStringLiteral("ONE three!"));
    vim.type(QStringLiteral("0rX"));
    QCOMPARE(vim.text(), QStringLiteral("XNE three!"));

    vim.setText(QStringLiteral("alpha \"quoted words\" omega"));
    vim.type(QStringLiteral("f\"ldi\""));
    QCOMPARE(vim.text(), QStringLiteral("alpha \"\" omega"));

    vim.setText(QStringLiteral("first\nsecond\nthird"));
    vim.type(QStringLiteral("ddyyP"));
    QCOMPARE(vim.text(), QStringLiteral("second\nsecond\nthird"));
}

void VimBehaviourTest::supportsVisualModes() {
    VimHarness vim;
    vim.setText(QStringLiteral("alpha\nbeta\ngamma"));

    vim.type(QStringLiteral("v"));
    QVERIFY(vim.modeName().contains(QStringLiteral("VISUAL"), Qt::CaseInsensitive));
    vim.press(Qt::Key_Escape);
    vim.type(QStringLiteral("V"));
    QVERIFY(vim.modeName().contains(QStringLiteral("VISUAL LINE"), Qt::CaseInsensitive));
    vim.press(Qt::Key_Escape);
    vim.press(Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(vim.view()->viewMode(), KTextEditor::View::ViModeVisualBlock);
    vim.press(Qt::Key_Escape);
    QVERIFY(vim.modeName().contains(QStringLiteral("NORMAL"), Qt::CaseInsensitive));
}

void VimBehaviourTest::supportsRegistersPasteUndoAndRepeat() {
    VimHarness vim;
    vim.setText(QStringLiteral("one\ntwo\nthree"));

    vim.type(QStringLiteral("\"ayyjddG\"ap"));
    QCOMPARE(vim.text(), QStringLiteral("one\nthree\none"));
    vim.type(QStringLiteral("u"));
    QCOMPARE(vim.text(), QStringLiteral("one\nthree"));
    vim.press(Qt::Key_R, Qt::ControlModifier);
    QCOMPARE(vim.text(), QStringLiteral("one\nthree\none"));

    vim.setText(QStringLiteral("one two three"));
    vim.type(QStringLiteral("dw."));
    QCOMPARE(vim.text(), QStringLiteral("three"));
}

void VimBehaviourTest::preservesUnnamedRegisterAfterBlackHoleDelete() {
    VimHarness vim;
    vim.setText(QStringLiteral("one\ntwo\nthree"));

    vim.type(QStringLiteral("yyj\"_ddGp"));
    QCOMPARE(vim.text(), QStringLiteral("one\nthree\none"));
}

void VimBehaviourTest::keepsUnnamedRegisterIndependentFromNamedYank() {
    VimHarness vim;
    vim.setText(QStringLiteral("seed\none\ntwo"));

    vim.type(QStringLiteral("yyj\"ayyGp"));
    QCOMPARE(vim.text(), QStringLiteral("seed\none\ntwo\nseed"));
}

void VimBehaviourTest::supportsSearchAndMarks() {
    VimHarness vim;
    vim.setText(QStringLiteral("alpha beta alpha\nbeta alpha"));

    vim.type(QStringLiteral("/beta"));
    vim.press(Qt::Key_Return);
    QCOMPARE(vim.view()->cursorPosition(), KTextEditor::Cursor(0, 6));
    vim.type(QStringLiteral("n"));
    QCOMPARE(vim.view()->cursorPosition(), KTextEditor::Cursor(1, 0));
    vim.type(QStringLiteral("N"));
    QCOMPARE(vim.view()->cursorPosition(), KTextEditor::Cursor(0, 6));
    vim.type(QStringLiteral("?alpha"));
    vim.press(Qt::Key_Return);
    QCOMPARE(vim.view()->cursorPosition(), KTextEditor::Cursor(0, 0));

    vim.type(QStringLiteral("maG$`a"));
    QCOMPARE(vim.view()->cursorPosition(), KTextEditor::Cursor(0, 0));
}

void VimBehaviourTest::supportsCommandModeAndMappings() {
    VimHarness vim;
    vim.setText(QStringLiteral("old old\nold"));

    vim.type(QStringLiteral(":%s/old/new/g"));
    vim.press(Qt::Key_Return);
    QCOMPARE(vim.text(), QStringLiteral("new new\nnew"));

    vim.type(QStringLiteral(":nmap ,w dw"));
    vim.press(Qt::Key_Return);
    vim.setText(QStringLiteral("one two"));
    vim.type(QStringLiteral(",w"));
    QCOMPARE(vim.text(), QStringLiteral("two"));
    vim.type(QStringLiteral(":nunmap ,w"));
    vim.press(Qt::Key_Return);
}

QTEST_MAIN(VimBehaviourTest)
#include "vim_behaviour_test.moc"
