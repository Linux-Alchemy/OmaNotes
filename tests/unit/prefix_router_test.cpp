#include "app/prefix_router.hpp"

#include <QKeyEvent>
#include <QSignalSpy>
#include <QtTest>

namespace {

QKeyEvent keyEvent(Qt::Key key, const QString& text = {},
                   Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    return {QEvent::KeyPress, key, modifiers, text};
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
};

void PrefixRouterTest::spaceCandidateRoutesKnownSequence() {
    omanotes::PrefixRouter router(omanotes::LeaderKey::Space);
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
    QCOMPARE(acceptedSpy.first().first().toString(), QStringLiteral("Space+?"));
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
    omanotes::PrefixRouter router(omanotes::LeaderKey::Space);
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
    omanotes::PrefixRouter router(omanotes::LeaderKey::Space);
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

QTEST_MAIN(PrefixRouterTest)
#include "prefix_router_test.moc"
