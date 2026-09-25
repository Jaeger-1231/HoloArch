#include "commands/command_result.h"
#include "commands/sysmon_command.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QTextStream>

int main(int argc, char *argv[]) {
  QCoreApplication application(argc, argv);
  application.setApplicationName(QStringLiteral("key-sysmon"));
  const CommandResult result =
      SysmonCommand().run(application.arguments().mid(1));
  if (result.outputHandled)
    return result.exitCode;
  QTextStream output(result.textIsError ? stderr : stdout);
  if (result.jsonRequested)
    output << QJsonDocument(result.json).toJson(QJsonDocument::Compact);
  else
    output << result.text;
  if (!result.text.isEmpty() || result.jsonRequested)
    output << Qt::endl;
  if (output.status() != QTextStream::Ok) {
    QTextStream(stderr) << "key-sysmon: unable to write command output"
                        << Qt::endl;
    return 3;
  }
  return result.exitCode;
}
