#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>

namespace {

struct ProcessResult {
    bool started = false;
    bool finished = false;
    int exitCode = -1;
    QProcess::ExitStatus exitStatus = QProcess::CrashExit;
    QByteArray standardError;
};

void writeFile(const std::filesystem::path& path, const QByteArray& contents = "# note\n") {
    QFile file(QString::fromStdString(path.string()));
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

ProcessResult runOmanotes(const QStringList& arguments, const QString& workingDirectory) {
    QProcess process;
    process.setWorkingDirectory(workingDirectory);
    process.start(QStringLiteral(OMANOTES_BINARY_PATH), arguments);

    ProcessResult result;
    result.started = process.waitForStarted(10'000);
    if (!result.started) {
        result.standardError = process.errorString().toUtf8();
        return result;
    }

    result.finished = process.waitForFinished(15'000);
    if (!result.finished) {
        process.kill();
        process.waitForFinished();
    }
    result.exitCode = process.exitCode();
    result.exitStatus = process.exitStatus();
    result.standardError = process.readAllStandardError();
    return result;
}

void verifySuccess(const ProcessResult& result) {
    QVERIFY2(result.started, result.standardError.constData());
    QVERIFY2(result.finished, result.standardError.constData());
    QCOMPARE(result.exitStatus, QProcess::NormalExit);
    QCOMPARE(result.exitCode, 0);
}

} // namespace

class LaunchProcessTest final : public QObject {
    Q_OBJECT

  private slots:
    void startsForEveryApprovedLaunchForm();
    void rejectsMissingFileWithoutCrashing();
};

void LaunchProcessTest::startsForEveryApprovedLaunchForm() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto root = std::filesystem::path(temporary.path().toStdString());
    const auto notes = root / "Notes";
    const auto projects = root / "projects";
    std::filesystem::create_directories(notes);
    std::filesystem::create_directories(projects);
    writeFile(root / "idea.md");
    writeFile(projects / "nested.md");
    writeFile(notes / "absolute.md");

    const auto smoke = QStringLiteral("--smoke-test");
    verifySuccess(runOmanotes({smoke}, temporary.path()));
    verifySuccess(runOmanotes({smoke, QStringLiteral("Notes")}, temporary.path()));
    verifySuccess(
        runOmanotes({smoke, QString::fromStdString(std::filesystem::canonical(notes).string())},
                    temporary.path()));
    verifySuccess(runOmanotes({smoke, QStringLiteral("idea.md")}, temporary.path()));
    verifySuccess(runOmanotes({smoke, QStringLiteral("projects/nested.md")}, temporary.path()));
    verifySuccess(runOmanotes({smoke, QString::fromStdString((notes / "absolute.md").string())},
                              temporary.path()));
    verifySuccess(runOmanotes({smoke, QStringLiteral("--fresh"), QStringLiteral("idea.md")},
                              temporary.path()));
}

void LaunchProcessTest::rejectsMissingFileWithoutCrashing() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const auto result = runOmanotes({QStringLiteral("--smoke-test"), QStringLiteral("missing.md")},
                                    temporary.path());

    QVERIFY2(result.started, result.standardError.constData());
    QVERIFY(result.finished);
    QCOMPARE(result.exitStatus, QProcess::NormalExit);
    QCOMPARE(result.exitCode, 2);
    QVERIFY(result.standardError.contains("does not exist"));
}

QTEST_MAIN(LaunchProcessTest)
#include "launch_process_test.moc"
