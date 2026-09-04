#include "app/prefix_router.hpp"
#include "core/command_registry.hpp"

#include <QKeyEvent>
#include <QSignalSpy>
#include <QtTest>

#include <tuple>
#include <utility>

namespace {

QKeyEvent keyEvent(Qt::Key key, const QString& text = {},
                   Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    return {QEvent::KeyPress, key, modifiers, text};
}

/// A registry holding `?` and `b d`, with the router resolving against it.
omanotes::CommandRegistry bindings() {
    omanotes::CommandRegistry registry;
    for (const auto& [id, sequence] :
         {std::pair{QStringLiteral("help.show"), QStringLiteral("?")},
          std::pair{QStringLiteral("buffer.close"), QStringLiteral("b d")}}) {
        omanotes::CommandDescriptor descriptor;
        descriptor.id = id;
        descriptor.label = id;
        descriptor.category = QStringLiteral("test");
        descriptor.execute = [](omanotes::AppContext&) {};
        std::ignore = registry.add(descriptor);
        std::ignore = registry.bind(omanotes::LeaderSequence{sequence}, id);
    }
    return registry;
}

void resolveAgainst(omanotes::PrefixRouter& router, const omanotes::CommandRegistry& registry) {
    router.setResolver([&registry](const QString& sequence) { return registry.lookup(sequence); });
}

} // namespace

class PrefixRouterTest final : public QObject {
    Q_OBJECT

  private slots:
    void spaceCandidateRoutesKnownSequence();
    void spaceCandidateLeavesOrdinaryInputAndControlBAlone();
    void modifierKeyDoesNotCancelPendingPrefix();
    void unknownSequenceCancelsWithFeedback();
    void escapeCancelsPendingPrefix();
    void controlBCandidateConsumesPageBackward();
    void multiKeySequenceReportsEachStep();
    void unknownContinuationCancelsWithTheWholeSequence();
    void withoutAResolverEveryKeyIsUnknown();
    void secondSpaceIsAKeyInItsOwnRight();
};

void PrefixRouterTest::spaceCandidateRoutesKnownSequence() {
    const auto registry = bindings();
    omanotes::PrefixRouter router(omanotes::LeaderKey::Space);
    resolveAgainst(router, registry);
    QSignalSpy feedbackSpy(&router, &omanotes::PrefixRouter::feedbackChanged);
    QSignalSpy acceptedSpy(&router, &omanotes::PrefixRouter::sequenceAccepted);
    auto space = keyEvent(Qt::Key_Space, QStringLiteral(" "));
    auto help = keyEvent(Qt::Key_Question, QStringLiteral("?"), Qt::ShiftModifier);

    QVERIFY(router.route(space, omanotes::EditorMode::Normal));
    QVERIFY(router.isPending());
    QCOMPARE(feedbackSpy.last().first().toString(), QStringLiteral("Space …"));
    QVERIFY(router.route(help, omanotes::EditorMode::Normal));
    QVERIFY(!router.isPending());
    QCOMPARE(acceptedSpy.count(), 1);
    QCOMPARE(acceptedSpy.first().at(0).toString(), QStringLiteral("help.show"));
    QCOMPARE(acceptedSpy.first().at(1).toString(), QStringLiteral("Space+?"));
}

void PrefixRouterTest::spaceCandidateLeavesOrdinaryInputAndControlBAlone() {
    omanotes::PrefixRouter router(omanotes::LeaderKey::Space);
    auto letter = keyEvent(Qt::Key_X, QStringLiteral("x"));
    auto insertSpace = keyEvent(Qt::Key_Space, QStringLiteral(" "));
    auto controlB = keyEvent(Qt::Key_B, QString{}, Qt::ControlModifier);

    QVERIFY(!router.route(letter, omanotes::EditorMode::Normal));
    QVERIFY(!router.route(insertSpace, omanotes::EditorMode::Insert));
    QVERIFY(!router.route(insertSpace, omanotes::EditorMode::Visual));
    QVERIFY(!router.route(controlB, omanotes::EditorMode::Normal));
}

void PrefixRouterTest::modifierKeyDoesNotCancelPendingPrefix() {
    const auto registry = bindings();
    omanotes::PrefixRouter router(omanotes::LeaderKey::Space);
    resolveAgainst(router, registry);
    auto space = keyEvent(Qt::Key_Space, QStringLiteral(" "));
    auto shift = keyEvent(Qt::Key_Shift, {}, Qt::ShiftModifier);
    auto help = keyEvent(Qt::Key_Question, QStringLiteral("?"), Qt::ShiftModifier);

    QVERIFY(router.route(space, omanotes::EditorMode::Normal));
    QVERIFY(!router.route(shift, omanotes::EditorMode::Normal));
    QVERIFY(router.isPending());
    QVERIFY(router.route(help, omanotes::EditorMode::Normal));
    QVERIFY(!router.isPending());
}

void PrefixRouterTest::unknownSequenceCancelsWithFeedback() {
    const auto registry = bindings();
    omanotes::PrefixRouter router(omanotes::LeaderKey::Space);
    resolveAgainst(router, registry);
    QSignalSpy feedbackSpy(&router, &omanotes::PrefixRouter::feedbackChanged);
    auto space = keyEvent(Qt::Key_Space, QStringLiteral(" "));
    auto unknown = keyEvent(Qt::Key_X, QStringLiteral("x"));

    QVERIFY(router.route(space, omanotes::EditorMode::Normal));
    QVERIFY(router.route(unknown, omanotes::EditorMode::Normal));
    QVERIFY(!router.isPending());
    QCOMPARE(feedbackSpy.last().first().toString(),
             QStringLiteral("Unknown application command: Space+x"));
    QVERIFY(!router.route(unknown, omanotes::EditorMode::Normal));
}

void PrefixRouterTest::escapeCancelsPendingPrefix() {
    omanotes::PrefixRouter router(omanotes::LeaderKey::Space);
    QSignalSpy feedbackSpy(&router, &omanotes::PrefixRouter::feedbackChanged);
    auto space = keyEvent(Qt::Key_Space, QStringLiteral(" "));
    auto escape = keyEvent(Qt::Key_Escape);

    QVERIFY(router.route(space, omanotes::EditorMode::Normal));
    QVERIFY(router.route(escape, omanotes::EditorMode::Normal));
    QVERIFY(!router.isPending());
    QCOMPARE(feedbackSpy.last().first().toString(), QStringLiteral("Application prefix cancelled"));
}

void PrefixRouterTest::controlBCandidateConsumesPageBackward() {
    omanotes::PrefixRouter router(omanotes::LeaderKey::ControlB);
    auto controlB = keyEvent(Qt::Key_B, QString{}, Qt::ControlModifier);

    QVERIFY(router.route(controlB, omanotes::EditorMode::Normal));
    QVERIFY(router.isPending());
}

void PrefixRouterTest::multiKeySequenceReportsEachStep() {
    const auto registry = bindings();
    omanotes::PrefixRouter router(omanotes::LeaderKey::Space);
    resolveAgainst(router, registry);
    QSignalSpy feedbackSpy(&router, &omanotes::PrefixRouter::feedbackChanged);
    QSignalSpy acceptedSpy(&router, &omanotes::PrefixRouter::sequenceAccepted);
    auto space = keyEvent(Qt::Key_Space, QStringLiteral(" "));
    auto b = keyEvent(Qt::Key_B, QStringLiteral("b"));
    auto d = keyEvent(Qt::Key_D, QStringLiteral("d"));

    QVERIFY(router.route(space, omanotes::EditorMode::Normal));
    QVERIFY(router.route(b, omanotes::EditorMode::Normal));
    QVERIFY(router.isPending());
    QCOMPARE(feedbackSpy.last().first().toString(), QStringLiteral("Space+b …"));
    QCOMPARE(acceptedSpy.count(), 0);
    QVERIFY(router.route(d, omanotes::EditorMode::Normal));
    QVERIFY(!router.isPending());
    QCOMPARE(acceptedSpy.count(), 1);
    QCOMPARE(acceptedSpy.first().at(0).toString(), QStringLiteral("buffer.close"));
    QCOMPARE(acceptedSpy.first().at(1).toString(), QStringLiteral("Space+b+d"));

    // The router holds nothing between sequences: a plain `d` is Vim's again.
    QVERIFY(!router.route(d, omanotes::EditorMode::Normal));
}

void PrefixRouterTest::unknownContinuationCancelsWithTheWholeSequence() {
    const auto registry = bindings();
    omanotes::PrefixRouter router(omanotes::LeaderKey::Space);
    resolveAgainst(router, registry);
    QSignalSpy feedbackSpy(&router, &omanotes::PrefixRouter::feedbackChanged);
    auto space = keyEvent(Qt::Key_Space, QStringLiteral(" "));
    auto b = keyEvent(Qt::Key_B, QStringLiteral("b"));
    auto x = keyEvent(Qt::Key_X, QStringLiteral("x"));

    QVERIFY(router.route(space, omanotes::EditorMode::Normal));
    QVERIFY(router.route(b, omanotes::EditorMode::Normal));
    QVERIFY(router.route(x, omanotes::EditorMode::Normal));
    QVERIFY(!router.isPending());
    QCOMPARE(feedbackSpy.last().first().toString(),
             QStringLiteral("Unknown application command: Space+b+x"));
}

void PrefixRouterTest::withoutAResolverEveryKeyIsUnknown() {
    omanotes::PrefixRouter router(omanotes::LeaderKey::Space);
    QSignalSpy feedbackSpy(&router, &omanotes::PrefixRouter::feedbackChanged);
    QSignalSpy acceptedSpy(&router, &omanotes::PrefixRouter::sequenceAccepted);
    auto space = keyEvent(Qt::Key_Space, QStringLiteral(" "));
    auto help = keyEvent(Qt::Key_Question, QStringLiteral("?"), Qt::ShiftModifier);

    QVERIFY(router.route(space, omanotes::EditorMode::Normal));
    QVERIFY(router.route(help, omanotes::EditorMode::Normal));
    QVERIFY(!router.isPending());
    QCOMPARE(acceptedSpy.count(), 0);
    QCOMPARE(feedbackSpy.last().first().toString(),
             QStringLiteral("Unknown application command: Space+?"));
}

void PrefixRouterTest::secondSpaceIsAKeyInItsOwnRight() {
    omanotes::CommandRegistry registry;
    omanotes::CommandDescriptor descriptor;
    descriptor.id = QStringLiteral("search.files");
    descriptor.label = descriptor.id;
    descriptor.category = QStringLiteral("test");
    descriptor.execute = [](omanotes::AppContext&) {};
    std::ignore = registry.add(descriptor);
    std::ignore = registry.bind(omanotes::LeaderSequence{QStringLiteral("Space")}, descriptor.id);
    omanotes::PrefixRouter router(omanotes::LeaderKey::Space);
    resolveAgainst(router, registry);
    QSignalSpy acceptedSpy(&router, &omanotes::PrefixRouter::sequenceAccepted);
    auto space = keyEvent(Qt::Key_Space, QStringLiteral(" "));

    QVERIFY(router.route(space, omanotes::EditorMode::Normal));
    QVERIFY(router.route(space, omanotes::EditorMode::Normal));
    QVERIFY(!router.isPending());
    QCOMPARE(acceptedSpy.count(), 1);
    QCOMPARE(acceptedSpy.first().at(0).toString(), QStringLiteral("search.files"));
    QCOMPARE(acceptedSpy.first().at(1).toString(), QStringLiteral("Space+Space"));
}

QTEST_MAIN(PrefixRouterTest)
#include "prefix_router_test.moc"
