#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTest>

class SysmonIntegrationTest : public QObject {
  Q_OBJECT

private slots:
  void helpDescribesStandaloneValueInterface();
  void snapshotProducesVersionedJsonWithoutProcesses();
  void moduleCommandSelectsOnlyRequestedModule();
  void processesHonorsLimit();
  void streamProducesIndependentJsonLines();
  void invalidOptionsUseStableUsageExit();
  void outputFailureReturnsDependencyExit();

private:
  struct Result {
    int exitCode = -1;
    QByteArray stdoutText;
    QByteArray stderrText;
  };

  Result run(const QStringList &arguments, int timeoutMs = 10000) const;
};

SysmonIntegrationTest::Result
SysmonIntegrationTest::run(const QStringList &arguments, int timeoutMs) const {
  QProcess process;
  process.start(QStringLiteral(KEY_SYSMON_EXECUTABLE), arguments);
  if (!process.waitForStarted(timeoutMs))
    return {-1, {}, process.errorString().toUtf8()};
  if (!process.waitForFinished(timeoutMs)) {
    process.kill();
    process.waitForFinished();
  }
  return {
      process.exitCode(),
      process.readAllStandardOutput(),
      process.readAllStandardError(),
  };
}

void SysmonIntegrationTest::helpDescribesStandaloneValueInterface() {
  const Result result = run({QStringLiteral("--help")});
  QCOMPARE(result.exitCode, 0);
  QVERIFY(result.stderrText.isEmpty());
  for (const QByteArray command :
       {"key sysmon", "key sysmon stream", "key sysmon modules"}) {
    QVERIFY2(result.stdoutText.contains(command), command.constData());
  }
}

void SysmonIntegrationTest::snapshotProducesVersionedJsonWithoutProcesses() {
  const Result result = run({
      QStringLiteral("snapshot"),
      QStringLiteral("--format"),
      QStringLiteral("json"),
  });
  QCOMPARE(result.exitCode, 0);
  QVERIFY(result.stderrText.isEmpty());
  QJsonParseError error;
  const QJsonObject json =
      QJsonDocument::fromJson(result.stdoutText, &error).object();
  QCOMPARE(error.error, QJsonParseError::NoError);
  QCOMPARE(json.value(QStringLiteral("schemaVersion")).toInt(), 1);
  QVERIFY(json.contains(QStringLiteral("timestampMs")));
  QVERIFY(json.contains(QStringLiteral("sequence")));
  QVERIFY(json.contains(QStringLiteral("cpu")));
  QVERIFY(json.contains(QStringLiteral("memory")));
  QVERIFY(json.contains(QStringLiteral("errors")));
  QVERIFY(!json.contains(QStringLiteral("processes")));
}

void SysmonIntegrationTest::moduleCommandSelectsOnlyRequestedModule() {
  const Result result = run({
      QStringLiteral("cpu"),
      QStringLiteral("--format"),
      QStringLiteral("json"),
  });
  QCOMPARE(result.exitCode, 0);
  const QJsonObject json = QJsonDocument::fromJson(result.stdoutText).object();
  QVERIFY(json.contains(QStringLiteral("cpu")));
  QVERIFY(!json.contains(QStringLiteral("memory")));
  QVERIFY(!json.contains(QStringLiteral("system")));
}

void SysmonIntegrationTest::processesHonorsLimit() {
  const Result result = run({
      QStringLiteral("processes"),
      QStringLiteral("--limit"),
      QStringLiteral("1"),
      QStringLiteral("--format"),
      QStringLiteral("json"),
  });
  QCOMPARE(result.exitCode, 0);
  const QJsonObject json = QJsonDocument::fromJson(result.stdoutText).object();
  QVERIFY(json.contains(QStringLiteral("processes")));
  QVERIFY(json.value(QStringLiteral("processes")).toArray().size() <= 1);
}

void SysmonIntegrationTest::streamProducesIndependentJsonLines() {
  QProcess process;
  process.start(QStringLiteral(KEY_SYSMON_EXECUTABLE),
                {
                    QStringLiteral("stream"),
                    QStringLiteral("--format"),
                    QStringLiteral("jsonl"),
                    QStringLiteral("--interval"),
                    QStringLiteral("100"),
                    QStringLiteral("--modules"),
                    QStringLiteral("cpu,memory"),
                });
  QVERIFY(process.waitForStarted(5000));

  QByteArray output;
  QElapsedTimer timer;
  timer.start();
  while (output.count('\n') < 2 && timer.elapsed() < 5000) {
    process.waitForReadyRead(500);
    output += process.readAllStandardOutput();
  }
  process.terminate();
  QVERIFY(process.waitForFinished(3000));
  output += process.readAllStandardOutput();
  QVERIFY(process.readAllStandardError().isEmpty());

  const QList<QByteArray> lines = output.split('\n');
  int validLines = 0;
  qint64 previousSequence = 0;
  for (const QByteArray &line : lines) {
    if (line.trimmed().isEmpty())
      continue;
    QJsonParseError error;
    const QJsonObject json = QJsonDocument::fromJson(line, &error).object();
    QCOMPARE(error.error, QJsonParseError::NoError);
    QCOMPARE(json.value(QStringLiteral("schemaVersion")).toInt(), 1);
    QVERIFY(json.contains(QStringLiteral("cpu")));
    QVERIFY(json.contains(QStringLiteral("memory")));
    const qint64 sequence = json.value(QStringLiteral("sequence")).toInteger();
    QVERIFY(sequence > previousSequence);
    previousSequence = sequence;
    ++validLines;
  }
  QVERIFY(validLines >= 2);
}

void SysmonIntegrationTest::invalidOptionsUseStableUsageExit() {
  const Result textResult = run({

      QStringLiteral("modules"),
      QStringLiteral("--format"),
      QStringLiteral("yaml"),
  });
  QCOMPARE(textResult.exitCode, 2);
  QVERIFY(textResult.stdoutText.isEmpty());
  QVERIFY(textResult.stderrText.contains("format must be json or text"));

  const Result jsonResult = run({
      QStringLiteral("snapshot"),
      QStringLiteral("--format"),
      QStringLiteral("json"),
      QStringLiteral("--modules"),
      QStringLiteral("unknown"),
  });
  QCOMPARE(jsonResult.exitCode, 2);
  QVERIFY(jsonResult.stderrText.isEmpty());
  const QJsonObject json =
      QJsonDocument::fromJson(jsonResult.stdoutText).object();
  QCOMPARE(json.value(QStringLiteral("schemaVersion")).toInt(), 1);
  QCOMPARE(json.value(QStringLiteral("ok")).toBool(), false);
  QCOMPARE(json.value(QStringLiteral("error"))
               .toObject()
               .value(QStringLiteral("code"))
               .toString(),
           QStringLiteral("usage_error"));
}

void SysmonIntegrationTest::outputFailureReturnsDependencyExit() {
  if (!QFileInfo::exists(QStringLiteral("/dev/full")))
    QSKIP("/dev/full is unavailable on this platform");

  const QList<QStringList> cases{
      {

          QStringLiteral("snapshot"),
          QStringLiteral("--modules"),
          QStringLiteral("memory"),
          QStringLiteral("--format"),
          QStringLiteral("json"),
      },
      {

          QStringLiteral("stream"),
          QStringLiteral("--modules"),
          QStringLiteral("memory"),
          QStringLiteral("--format"),
          QStringLiteral("jsonl"),
          QStringLiteral("--interval"),
          QStringLiteral("100"),
      },
  };

  for (const QStringList &arguments : cases) {
    QProcess process;
    process.setStandardOutputFile(QStringLiteral("/dev/full"),
                                  QIODevice::Truncate);
    process.start(QStringLiteral(KEY_SYSMON_EXECUTABLE), arguments);
    QVERIFY(process.waitForStarted(5000));
    QVERIFY(process.waitForFinished(5000));
    QCOMPARE(process.exitCode(), 3);
    QVERIFY2(!process.readAllStandardError().isEmpty(),
             qPrintable(arguments.join(QLatin1Char(' '))));
  }
}

QTEST_MAIN(SysmonIntegrationTest)
#include "sysmon_integration_test.moc"
