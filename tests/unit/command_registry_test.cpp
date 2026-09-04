#include "core/command_registry.hpp"

#include <QtTest>

namespace {

omanotes::CommandDescriptor command(const QString& id, const QString& label,
                                    const QString& category, int* counter) {
    omanotes::CommandDescriptor descriptor;
    descriptor.id = id;
    descriptor.label = label;
    descriptor.category = category;
    descriptor.execute = [counter](omanotes::AppContext&) { ++*counter; };
    return descriptor;
}

} // namespace

class CommandRegistryTest final : public QObject {
    Q_OBJECT

  private slots:
    void refusesDuplicateAndInvalidCommands();
    void neverRunsUnknownOrDisabledCommands();
    void disabledHintReachesTheUser();
    void bindsSequencesAndRefusesUnreachableOnes();
    void looksUpPrefixesAndExactSequences();
    void auditReportsUnroutedCommandsAndRepeatedLabels();
};

void CommandRegistryTest::refusesDuplicateAndInvalidCommands() {
    omanotes::CommandRegistry registry;
    int runs = 0;

    QVERIFY(registry
                .add(command(QStringLiteral("file.save"), QStringLiteral("Save"),
                             QStringLiteral("file"), &runs))
                .has_value());
    const auto duplicate = registry.add(command(
        QStringLiteral("file.save"), QStringLiteral("Save again"), QStringLiteral("file"), &runs));
    QVERIFY(!duplicate.has_value());
    QCOMPARE(duplicate.error().code, omanotes::CommandErrorCode::DuplicateCommand);

    omanotes::CommandDescriptor noAction;
    noAction.id = QStringLiteral("file.nothing");
    const auto invalid = registry.add(noAction);
    QVERIFY(!invalid.has_value());
    QCOMPARE(invalid.error().code, omanotes::CommandErrorCode::InvalidDescriptor);
    QCOMPARE(registry.commands().size(), std::size_t{1});
}

void CommandRegistryTest::neverRunsUnknownOrDisabledCommands() {
    omanotes::CommandRegistry registry;
    int runs = 0;
    auto guarded = command(QStringLiteral("buffer.close"), QStringLiteral("Close buffer"),
                           QStringLiteral("buffer"), &runs);
    guarded.enabled = [](const omanotes::AppContext& context) { return context.bufferCount > 1; };
    QVERIFY(registry.add(guarded).has_value());

    omanotes::AppContext context;
    const auto unknown = registry.execute(QStringLiteral("buffer.explode"), context);
    QVERIFY(!unknown.has_value());
    QCOMPARE(unknown.error().code, omanotes::CommandErrorCode::UnknownCommand);

    context.bufferCount = 1;
    const auto disabled = registry.execute(QStringLiteral("buffer.close"), context);
    QVERIFY(!disabled.has_value());
    QCOMPARE(disabled.error().code, omanotes::CommandErrorCode::Disabled);
    QCOMPARE(disabled.error().message, QStringLiteral("Close buffer is not available here"));
    QCOMPARE(runs, 0);
    QVERIFY(registry.available(context).empty());

    context.bufferCount = 2;
    QVERIFY(registry.execute(QStringLiteral("buffer.close"), context).has_value());
    QCOMPARE(runs, 1);
    QCOMPARE(registry.available(context).size(), std::size_t{1});
}

void CommandRegistryTest::disabledHintReachesTheUser() {
    omanotes::CommandRegistry registry;
    int runs = 0;
    auto future = command(QStringLiteral("search.files"), QStringLiteral("Find files"),
                          QStringLiteral("search"), &runs);
    future.enabled = [](const omanotes::AppContext&) { return false; };
    future.disabledHint = QStringLiteral("Find files is not available yet");
    QVERIFY(registry.add(future).has_value());

    omanotes::AppContext context;
    const auto result = registry.execute(QStringLiteral("search.files"), context);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().message, QStringLiteral("Find files is not available yet"));
    QCOMPARE(runs, 0);
}

void CommandRegistryTest::bindsSequencesAndRefusesUnreachableOnes() {
    omanotes::CommandRegistry registry;
    int runs = 0;
    QVERIFY(registry
                .add(command(QStringLiteral("buffer.close"), QStringLiteral("Close buffer"),
                             QStringLiteral("buffer"), &runs))
                .has_value());
    QVERIFY(registry
                .add(command(QStringLiteral("buffer.new"), QStringLiteral("New buffer"),
                             QStringLiteral("buffer"), &runs))
                .has_value());

    const auto unknown = registry.bind(omanotes::LeaderSequence{QStringLiteral("x")},
                                       QStringLiteral("buffer.missing"));
    QVERIFY(!unknown.has_value());
    QCOMPARE(unknown.error().code, omanotes::CommandErrorCode::UnknownCommand);

    QVERIFY(
        registry
            .bind(omanotes::LeaderSequence{QStringLiteral("b d")}, QStringLiteral("buffer.close"))
            .has_value());
    const auto duplicate = registry.bind(omanotes::LeaderSequence{QStringLiteral(" b  d ")},
                                         QStringLiteral("buffer.new"));
    QVERIFY(!duplicate.has_value());
    QCOMPARE(duplicate.error().code, omanotes::CommandErrorCode::DuplicateSequence);

    // `b` alone could never be typed: the router would wait for the `d`.
    const auto shadowing =
        registry.bind(omanotes::LeaderSequence{QStringLiteral("b")}, QStringLiteral("buffer.new"));
    QVERIFY(!shadowing.has_value());
    QCOMPARE(shadowing.error().code, omanotes::CommandErrorCode::ShadowedSequence);
    // And `b d x` would be swallowed by `b d` before its last key arrives.
    const auto shadowed = registry.bind(omanotes::LeaderSequence{QStringLiteral("b d x")},
                                        QStringLiteral("buffer.new"));
    QVERIFY(!shadowed.has_value());
    QCOMPARE(shadowed.error().code, omanotes::CommandErrorCode::ShadowedSequence);

    const auto empty = registry.bind(omanotes::LeaderSequence{QStringLiteral("   ")},
                                     QStringLiteral("buffer.new"));
    QVERIFY(!empty.has_value());

    // A second, distinct route to the same command is fine.
    QVERIFY(
        registry.bind(omanotes::LeaderSequence{QStringLiteral("b n")}, QStringLiteral("buffer.new"))
            .has_value());
    QVERIFY(
        registry.bind(omanotes::LeaderSequence{QStringLiteral("n")}, QStringLiteral("buffer.new"))
            .has_value());
    const auto sequences = registry.sequencesFor(QStringLiteral("buffer.new"));
    QCOMPARE(sequences.size(), std::size_t{2});
    QCOMPARE(registry.bindings().size(), std::size_t{3});
}

void CommandRegistryTest::looksUpPrefixesAndExactSequences() {
    omanotes::CommandRegistry registry;
    int runs = 0;
    QVERIFY(registry
                .add(command(QStringLiteral("buffer.close"), QStringLiteral("Close buffer"),
                             QStringLiteral("buffer"), &runs))
                .has_value());
    QVERIFY(registry
                .add(command(QStringLiteral("help.show"), QStringLiteral("Help"),
                             QStringLiteral("help"), &runs))
                .has_value());
    QVERIFY(
        registry
            .bind(omanotes::LeaderSequence{QStringLiteral("b d")}, QStringLiteral("buffer.close"))
            .has_value());
    QVERIFY(
        registry.bind(omanotes::LeaderSequence{QStringLiteral("?")}, QStringLiteral("help.show"))
            .has_value());

    QCOMPARE(registry.lookup(QStringLiteral("b")).match, omanotes::SequenceMatch::Prefix);
    QVERIFY(registry.lookup(QStringLiteral("b")).commandId.isEmpty());
    const auto exact = registry.lookup(QStringLiteral("b d"));
    QCOMPARE(exact.match, omanotes::SequenceMatch::Exact);
    QCOMPARE(exact.commandId, QStringLiteral("buffer.close"));
    QCOMPARE(registry.lookup(QStringLiteral("?")).match, omanotes::SequenceMatch::Exact);
    QCOMPARE(registry.lookup(QStringLiteral("b x")).match, omanotes::SequenceMatch::None);
    QCOMPARE(registry.lookup(QStringLiteral("z")).match, omanotes::SequenceMatch::None);
    QCOMPARE(registry.lookup(QStringLiteral("b d x")).match, omanotes::SequenceMatch::None);
}

void CommandRegistryTest::auditReportsUnroutedCommandsAndRepeatedLabels() {
    omanotes::CommandRegistry registry;
    int runs = 0;
    QVERIFY(registry
                .add(command(QStringLiteral("file.save"), QStringLiteral("Save"),
                             QStringLiteral("file"), &runs))
                .has_value());
    QVERIFY(registry
                .add(command(QStringLiteral("file.save.as"), QStringLiteral("Save"),
                             QStringLiteral("file"), &runs))
                .has_value());
    QVERIFY(registry
                .add(command(QStringLiteral("buffer.close"), QStringLiteral("Close buffer"),
                             QStringLiteral("buffer"), &runs))
                .has_value());
    QVERIFY(
        registry
            .bind(omanotes::LeaderSequence{QStringLiteral("b d")}, QStringLiteral("buffer.close"))
            .has_value());

    // file.save is reached by a shortcut the registry does not hold; file.save.as
    // by nothing at all, and its label repeats within its category.
    const auto findings = registry.audit({QStringLiteral("file.save"), QStringLiteral("ghost")});
    QCOMPARE(findings.size(), std::size_t{3});
    QCOMPARE(findings[0].commandId, QStringLiteral("ghost"));
    QCOMPARE(findings[1].commandId, QStringLiteral("file.save.as"));
    QVERIFY(findings[1].detail.contains(QStringLiteral("no keyboard or mouse route")));
    QCOMPARE(findings[2].commandId, QStringLiteral("file.save.as"));
    QVERIFY(findings[2].detail.contains(QStringLiteral("repeats")));

    QVERIFY(
        registry.bind(omanotes::LeaderSequence{QStringLiteral("s")}, QStringLiteral("file.save.as"))
            .has_value());
    const auto remaining = registry.audit({QStringLiteral("file.save")});
    QCOMPARE(remaining.size(), std::size_t{1});
    QVERIFY(remaining[0].detail.contains(QStringLiteral("repeats")));
}

QTEST_APPLESS_MAIN(CommandRegistryTest)
#include "command_registry_test.moc"
