#include "cache_helpers.h"
#include "collector.h"

#include <QByteArrayView>
#include <QFile>
#include <QFileInfo>

#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <memory>

namespace Clavis::Sysmon {

namespace {

QByteArray readAll(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

ProcessStat readProcessStat(int procFd, qint64 pid) {
  char path[64];
  std::snprintf(path, sizeof(path), "%lld/stat", static_cast<long long>(pid));
  const int fd = ::openat(procFd, path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return {};

  // Linux stat records fit within one page. Avoid QFile's path/metadata work
  // and heap buffers for this file opened once per PID on every sample.
  std::array<char, 4096> buffer;
  ssize_t count;
  do {
    count = ::read(fd, buffer.data(), buffer.size());
  } while (count < 0 && errno == EINTR);
  ::close(fd);
  if (count <= 0 || static_cast<size_t>(count) == buffer.size())
    return {}; // Do not parse a truncated record.
  return parseProcessStat(QByteArrayView(buffer.data(), count));
}

QString userNameForUid(uid_t uid) {
  long suggested = ::sysconf(_SC_GETPW_R_SIZE_MAX);
  if (suggested < 1024)
    suggested = 16384;
  QByteArray buffer(static_cast<qsizetype>(suggested), Qt::Uninitialized);
  struct passwd entry{};
  struct passwd *result = nullptr;
  if (::getpwuid_r(uid, &entry, buffer.data(),
                   static_cast<size_t>(buffer.size()), &result) == 0 &&
      result && result->pw_name) {
    return QString::fromLocal8Bit(result->pw_name);
  }
  return QString::number(uid);
}

QString processStateName(QChar state) {
  switch (state.toLatin1()) {
  case 'R':
    return QStringLiteral("running");
  case 'S':
    return QStringLiteral("sleeping");
  case 'D':
    return QStringLiteral("disk-sleep");
  case 'Z':
    return QStringLiteral("zombie");
  case 'T':
  case 't':
    return QStringLiteral("stopped");
  case 'X':
  case 'x':
    return QStringLiteral("dead");
  case 'I':
    return QStringLiteral("idle");
  default:
    return state.isNull() ? QStringLiteral("unknown") : QString(state);
  }
}

QString fullCommand(const QByteArray &raw, const QString &fallback) {
  if (raw.isEmpty())
    return fallback;
  QList<QByteArray> arguments = raw.split('\0');
  while (!arguments.isEmpty() && arguments.last().isEmpty())
    arguments.removeLast();
  QStringList decoded;
  decoded.reserve(arguments.size());
  for (const QByteArray &argument : arguments)
    decoded.push_back(QString::fromLocal8Bit(argument));
  const QString command = decoded.join(QLatin1Char(' ')).trimmed();
  return command.isEmpty() ? fallback : command;
}

} // namespace

QVector<RawProcessInfo>
LinuxCollector::collectProcesses(quint64 totalMemoryBytes, qint64 bootTimeMs,
                                 ProcessMemorySource processMemory,
                                 QVector<Error> *errors) {
  QVector<RawProcessInfo> result;
  const auto closeDirectory = [](DIR *directory) { ::closedir(directory); };
  const std::unique_ptr<DIR, decltype(closeDirectory)> proc(::opendir("/proc"),
                                                            closeDirectory);
  const long pageSize = ::sysconf(_SC_PAGESIZE);
  const long clockTicks = ::sysconf(_SC_CLK_TCK);
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  int permissionFailures = 0;
  QHash<qint64, ProcessMetadata> nextMetadata;

  result.reserve(m_processMetadata.size());
  nextMetadata.reserve(m_processMetadata.size());
  // /proc PID entries need neither name sorting nor per-directory access/stat
  // checks. Opening stat below also handles exits and permission failures.
  while (const dirent *entry = proc ? ::readdir(proc.get()) : nullptr) {
    if (entry->d_name[0] < '0' || entry->d_name[0] > '9' ||
        (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN))
      continue;
    bool pidOk = false;
    const qint64 pid = QByteArrayView(entry->d_name).toLongLong(&pidOk);
    if (!pidOk || pid <= 0)
      continue;

    const ProcessStat stat = readProcessStat(::dirfd(proc.get()), pid);
    if (!stat.valid || stat.pid != pid)
      continue; // The process may have exited between directory and read.

    RawProcessInfo process;
    process.info.pid = pid;
    process.info.ppid = stat.ppid;
    process.info.name = stat.name;
    process.info.state = processStateName(stat.state);
    process.info.threadCount = stat.threadCount;
    process.cpuTicks = stat.userTicks + stat.systemTicks;
    process.startTicks = stat.startTicks;
    process.info.processStartTicks = stat.startTicks;
    if (clockTicks > 0 && bootTimeMs > 0) {
      process.info.startTimeMs =
          bootTimeMs +
          static_cast<qint64>(static_cast<long double>(stat.startTicks) *
                              1000.0L / static_cast<long double>(clockTicks));
      process.info.runtimeSeconds =
          std::max<qint64>(0, (nowMs - process.info.startTimeMs) / 1000);
    }

    // The TUI uses stat's approximate RSS, as top-style monitors do. Keep
    // statm for machine output and fall back to it for unusable stat RSS.
    if (pageSize > 0) {
      std::optional<quint64> residentBytes;
      const quint64 bytesPerPage = static_cast<quint64>(pageSize);
      if (processMemory == ProcessMemorySource::Stat && stat.residentPages &&
          static_cast<quint64>(*stat.residentPages) <=
              std::numeric_limits<quint64>::max() / bytesPerPage) {
        const quint64 bytes =
            static_cast<quint64>(*stat.residentPages) * bytesPerPage;
        if (totalMemoryBytes == 0 || bytes < totalMemoryBytes)
          residentBytes = bytes;
      }
      if (!residentBytes) {
        const QString path = QStringLiteral("/proc/") + QString::number(pid) +
                             QStringLiteral("/statm");
        const QList<QByteArray> statm = readAll(path).simplified().split(' ');
        bool rssOk = false;
        const quint64 pages = statm.value(1).toULongLong(&rssOk);
        if (rssOk &&
            pages <= std::numeric_limits<quint64>::max() / bytesPerPage)
          residentBytes = pages * bytesPerPage;
      }
      if (residentBytes) {
        process.info.memoryBytes = *residentBytes;
        if (totalMemoryBytes > 0) {
          process.info.memoryPercent =
              static_cast<double>(process.info.memoryBytes) * 100.0 /
              static_cast<double>(totalMemoryBytes);
        }
      }
    }

    ProcessMetadata metadata;
    const auto cached = m_processMetadata.constFind(pid);
    if (cached != m_processMetadata.cend() &&
        processIdentityMatches(cached->startTicks, stat.startTicks) &&
        cached->name == stat.name) {
      metadata = *cached;
    } else {
      const QString base = QStringLiteral("/proc/") + QString::number(pid);
      metadata.startTicks = stat.startTicks;
      metadata.name = stat.name;
      metadata.command = fullCommand(readAll(base + QStringLiteral("/cmdline")),
                                     process.info.name);
      metadata.executablePath =
          QFileInfo(base + QStringLiteral("/exe")).symLinkTarget();
      const uint uid = QFileInfo(base).ownerId();
      metadata.uid = uid;
      if (uid == std::numeric_limits<uint>::max()) {
        ++permissionFailures;
      } else {
        auto user = m_userNames.constFind(uid);
        if (user == m_userNames.cend())
          user =
              m_userNames.insert(uid, userNameForUid(static_cast<uid_t>(uid)));
        metadata.user = *user;
      }
    }
    process.info.command = metadata.command;
    process.info.executablePath = metadata.executablePath;
    process.info.user = metadata.user;
    nextMetadata.insert(pid, std::move(metadata));
    result.push_back(std::move(process));
  }
  m_processMetadata = std::move(nextMetadata);

  if (result.isEmpty()) {
    errors->push_back({
        QStringLiteral("processes"),
        QStringLiteral("processes_unavailable"),
        QStringLiteral("No readable processes were found in /proc"),
    });
  } else if (permissionFailures > 0) {
    errors->push_back({
        QStringLiteral("processes"),
        QStringLiteral("partial_permissions"),
        QStringLiteral("Some process owners were not readable"),
    });
  }
  return result;
}

} // namespace Clavis::Sysmon
