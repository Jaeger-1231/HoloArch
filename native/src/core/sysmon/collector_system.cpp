#include "collector.h"

#include "cache_helpers.h"
#include "gpu/gpu_manager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <limits>

namespace Clavis::Sysmon {

namespace {

qint64 monotonicNsecs() {
  struct timespec value{};
  return ::clock_gettime(CLOCK_MONOTONIC, &value) == 0
             ? static_cast<qint64>(value.tv_sec) * 1'000'000'000 +
                   static_cast<qint64>(value.tv_nsec)
             : 0;
}

QByteArray readAll(const QString &path, bool *ok = nullptr) {
  QFile file(path);
  const bool opened = file.open(QIODevice::ReadOnly);
  if (ok)
    *ok = opened;
  return opened ? file.readAll() : QByteArray();
}

QString readText(const QString &path) {
  return QString::fromUtf8(readAll(path)).trimmed();
}

QString osReleaseValue(const QString &key) {
  const QList<QByteArray> lines =
      readAll(QStringLiteral("/etc/os-release")).split('\n');
  for (const QByteArray &raw : lines) {
    const QString line = QString::fromUtf8(raw).trimmed();
    const int equal = line.indexOf(QLatin1Char('='));
    if (equal <= 0 || line.left(equal) != key)
      continue;
    QString value = line.mid(equal + 1).trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) &&
        value.endsWith(QLatin1Char('"'))) {
      value = value.mid(1, value.size() - 2);
    }
    return value;
  }
  return {};
}

OptionalNumber readNumber(const QString &path, double scale = 1.0) {
  bool ok = false;
  const double value = readText(path).toDouble(&ok);
  if (!ok)
    return std::nullopt;
  return value * scale;
}

OptionalInteger readInteger(const QString &path, bool *readOk = nullptr) {
  bool fileOk = false;
  const QString text = QString::fromUtf8(readAll(path, &fileOk)).trimmed();
  bool ok = false;
  const qint64 value = text.toLongLong(&ok);
  if (readOk)
    *readOk = fileOk && ok;
  return ok ? OptionalInteger(value) : std::nullopt;
}

QString normalizedDmi(const QString &path) {
  const QString value = readText(path);
  if (value.compare(QStringLiteral("Default string"), Qt::CaseInsensitive) ==
          0 ||
      value.compare(QStringLiteral("To be filled by O.E.M."),
                    Qt::CaseInsensitive) == 0) {
    return {};
  }
  return value;
}

} // namespace

LinuxCollector::LinuxCollector() {
  loadStaticSystemInfo();
  m_gpuManager = std::make_unique<GpuManager>();
}

LinuxCollector::~LinuxCollector() = default;

void LinuxCollector::loadStaticSystemInfo() {
  m_staticSystem.hostName =
      readText(QStringLiteral("/proc/sys/kernel/hostname"));
  if (m_staticSystem.hostName.isEmpty())
    m_staticSystem.hostName = readText(QStringLiteral("/etc/hostname"));

  m_staticSystem.osName = osReleaseValue(QStringLiteral("PRETTY_NAME"));
  if (m_staticSystem.osName.isEmpty())
    m_staticSystem.osName = osReleaseValue(QStringLiteral("NAME"));
  if (m_staticSystem.osName.isEmpty())
    m_staticSystem.osName = QStringLiteral("Linux");
  m_staticSystem.distroId = osReleaseValue(QStringLiteral("ID")).toLower();
  m_staticSystem.distroIdLike =
      osReleaseValue(QStringLiteral("ID_LIKE"))
          .toLower()
          .simplified()
          .split(QLatin1Char(' '), Qt::SkipEmptyParts);

  struct utsname uts{};
  if (::uname(&uts) == 0) {
    m_staticSystem.kernel = QString::fromLocal8Bit(uts.release);
    m_staticSystem.architecture = QString::fromLocal8Bit(uts.machine);
  }

  m_staticSystem.logicalCpuCount =
      std::max(1, static_cast<int>(::sysconf(_SC_NPROCESSORS_CONF)));

  const QByteArray procStat = readAll(QStringLiteral("/proc/stat"));
  const OptionalInteger bootTimeMs = parseProcBootTimeMs(procStat);
  if (bootTimeMs) {
    m_staticSystem.bootTimeMs = *bootTimeMs;
  } else {
    bool uptimeOk = false;
    const double uptime = readAll(QStringLiteral("/proc/uptime"))
                              .simplified()
                              .split(' ')
                              .value(0)
                              .toDouble(&uptimeOk);
    if (uptimeOk && uptime >= 0.0) {
      m_staticSystem.bootTimeMs = QDateTime::currentMSecsSinceEpoch() -
                                  static_cast<qint64>(uptime * 1000.0);
    }
  }

  const QList<QByteArray> cpuInfo =
      readAll(QStringLiteral("/proc/cpuinfo")).split('\n');
  QSet<QString> physicalCores;
  QString physicalId;
  QString coreId;
  int fallbackCores = 0;
  for (const QByteArray &raw : cpuInfo) {
    const QString line = QString::fromUtf8(raw);
    const int colon = line.indexOf(QLatin1Char(':'));
    if (colon < 0)
      continue;
    const QString key = line.left(colon).trimmed();
    const QString value = line.mid(colon + 1).trimmed();
    if (key == QStringLiteral("physical id"))
      physicalId = value;
    else if (key == QStringLiteral("core id"))
      coreId = value;
    else if (key == QStringLiteral("cpu cores"))
      fallbackCores = std::max(fallbackCores, value.toInt());
    else if (m_staticSystem.cpuModelName.isEmpty()) {
      bool processorIsIndex = false;
      value.toInt(&processorIsIndex);
      if (key.compare(QStringLiteral("model name"), Qt::CaseInsensitive) == 0 ||
          key.compare(QStringLiteral("hardware"), Qt::CaseInsensitive) == 0 ||
          (key.compare(QStringLiteral("processor"), Qt::CaseInsensitive) == 0 &&
           !processorIsIndex)) {
        m_staticSystem.cpuModelName = value.simplified();
      }
    }

    if (!physicalId.isEmpty() && !coreId.isEmpty()) {
      physicalCores.insert(physicalId + QLatin1Char(':') + coreId);
      physicalId.clear();
      coreId.clear();
    }
  }
  m_staticSystem.physicalCoreCount =
      !physicalCores.isEmpty()
          ? physicalCores.size()
          : (fallbackCores > 0 ? fallbackCores
                               : m_staticSystem.logicalCpuCount);

  m_staticSystem.vendor =
      normalizedDmi(QStringLiteral("/sys/class/dmi/id/sys_vendor"));
  m_staticSystem.productName =
      normalizedDmi(QStringLiteral("/sys/class/dmi/id/product_name"));
  m_staticSystem.boardName =
      normalizedDmi(QStringLiteral("/sys/class/dmi/id/board_name"));
  m_staticSystem.biosVersion =
      normalizedDmi(QStringLiteral("/sys/class/dmi/id/bios_version"));
  const QProcessEnvironment environment =
      QProcessEnvironment::systemEnvironment();
  m_staticSystem.systemUser = environment.value(QStringLiteral("USER"));
  const QString desktop = environment.value(
      QStringLiteral("XDG_CURRENT_DESKTOP"),
      environment.value(QStringLiteral("XDG_SESSION_DESKTOP")));
  m_staticSystem.wmName = desktop.split(QLatin1Char(':')).value(0).toLower();
  const QString shell = environment.value(QStringLiteral("SHELL"));
  m_staticSystem.shellName = QFileInfo(shell).fileName();
  m_staticSystem.chassis =
      QStringLiteral("%1 %2")
          .arg(m_staticSystem.vendor, m_staticSystem.productName)
          .trimmed();
  m_staticSystem.available = !m_staticSystem.hostName.isEmpty();

  const QDir hwmon(QStringLiteral("/sys/class/hwmon"));
  for (const QString &entry :
       hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const QString path = hwmon.absoluteFilePath(entry);
    const QString name = readText(path + QStringLiteral("/name")).toLower();
    if (m_fanPath.isEmpty()) {
      for (int index = 1; index <= 8; ++index) {
        const QString candidate =
            path + QStringLiteral("/fan%1_input").arg(index);
        if (QFileInfo::exists(candidate)) {
          m_fanPath = candidate;
          break;
        }
      }
    }
    if (name == QStringLiteral("coretemp") ||
        name == QStringLiteral("k10temp") ||
        name == QStringLiteral("zenpower") ||
        name == QStringLiteral("x86_pkg_temp")) {
      for (int index = 1; index <= 32; ++index) {
        const QString input = path + QStringLiteral("/temp%1_input").arg(index);
        if (!QFileInfo::exists(input))
          continue;
        m_cpuTemperaturePaths.push_back(input);
        const QString label =
            readText(path + QStringLiteral("/temp%1_label").arg(index))
                .toLower();
        if (m_packageTemperaturePath.isEmpty() &&
            (label.contains(QStringLiteral("package")) ||
             label.contains(QStringLiteral("tdie")) ||
             label.contains(QStringLiteral("tctl")))) {
          m_packageTemperaturePath = input;
        }
      }
    }
  }
  if (m_packageTemperaturePath.isEmpty() && !m_cpuTemperaturePaths.isEmpty()) {
    m_packageTemperaturePath = m_cpuTemperaturePaths.first();
  }

  const QDir powercap(QStringLiteral("/sys/class/powercap"));
  for (const QString &entry :
       powercap.entryList(QStringList{QStringLiteral("intel-rapl:*")},
                          QDir::Dirs | QDir::NoDotAndDotDot)) {
    const QString path = powercap.absoluteFilePath(entry);
    if (readText(path + QStringLiteral("/name"))
            .contains(QStringLiteral("package"), Qt::CaseInsensitive) ||
        m_packageEnergyPath.isEmpty()) {
      m_packageEnergyPath = path + QStringLiteral("/energy_uj");
      m_packageEnergyRangePath = path + QStringLiteral("/max_energy_range_uj");
      if (readText(path + QStringLiteral("/name"))
              .contains(QStringLiteral("package"), Qt::CaseInsensitive)) {
        break;
      }
    }
  }
}

RawSnapshot LinuxCollector::collect(const ModuleSet &modules,
                                    ProcessMemorySource processMemory) {
  RawSnapshot result;
  if (modules.contains(QStringLiteral("system")))
    result.system = collectSystem(&result.errors);
  if (modules.contains(QStringLiteral("cpu"))) {
    result.cpu = collectCpu(&result.errors);
    result.cpuTimestampNs = monotonicNsecs();
  }
  if (modules.contains(QStringLiteral("memory")) ||
      modules.contains(QStringLiteral("processes"))) {
    result.memory = collectMemory(&result.errors);
  }
  if (modules.contains(QStringLiteral("gpu")))
    result.gpus = m_gpuManager->sample(&result.errors);
  if (modules.contains(QStringLiteral("disk"))) {
    result.disks = collectDisks(&result.errors);
    result.diskTimestampNs = monotonicNsecs();
  }
  if (modules.contains(QStringLiteral("network"))) {
    result.networkInterfaces =
        collectNetwork(&result.defaultNetworkInterface, &result.errors);
    result.networkTimestampNs = monotonicNsecs();
  }
  if (modules.contains(QStringLiteral("battery")))
    result.battery = collectBattery(&result.errors);
  if (modules.contains(QStringLiteral("processes"))) {
    if (!result.system.available)
      result.system = collectSystem(&result.errors);
    result.processes = collectProcesses(result.memory.memTotalKiB * 1024ULL,
                                        result.system.bootTimeMs, processMemory,
                                        &result.errors);
    result.processTimestampNs = monotonicNsecs();
  }
  return result;
}

SystemInfo LinuxCollector::collectSystem(QVector<Error> *errors) const {
  SystemInfo result = m_staticSystem;
  bool uptimeOk = false;
  const QByteArray uptimeContents =
      readAll(QStringLiteral("/proc/uptime"), &uptimeOk);
  bool numberOk = false;
  const double uptime =
      uptimeContents.simplified().split(' ').value(0).toDouble(&numberOk);
  if (uptimeOk && numberOk && uptime >= 0.0) {
    result.uptimeSeconds = static_cast<qint64>(uptime);
    if (result.bootTimeMs <= 0) {
      result.bootTimeMs = QDateTime::currentMSecsSinceEpoch() -
                          static_cast<qint64>(uptime * 1000.0);
    }
  } else {
    errors->push_back({
        QStringLiteral("system"),
        QStringLiteral("uptime_unavailable"),
        QStringLiteral("Unable to read /proc/uptime"),
    });
  }

  result.available = result.available || uptimeOk;
  return result;
}

RawCpuInfo LinuxCollector::collectCpu(QVector<Error> *errors) {
  RawCpuInfo result;
  bool statOk = false;
  result.counters =
      parseProcStat(readAll(QStringLiteral("/proc/stat"), &statOk));
  if (!statOk || !result.counters.valid) {
    errors->push_back({
        QStringLiteral("cpu"),
        QStringLiteral("proc_stat_unavailable"),
        QStringLiteral("Unable to parse /proc/stat"),
    });
  }

  const auto discoverPolicies = [this]() {
    m_cpuFrequencyTopologyInitialized = true;
    m_cpuFrequencyPolicies.clear();
    const QDir policyRoot(QStringLiteral("/sys/devices/system/cpu/cpufreq"));
    m_cpuPolicyNames = normalizedCpuPolicyNames(
        policyRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot));
    m_cpuUsingPolicyTopology = !m_cpuPolicyNames.isEmpty();
    for (const QString &name : std::as_const(m_cpuPolicyNames)) {
      const QString base = policyRoot.absoluteFilePath(name) + QLatin1Char('/');
      const int cpuCount = std::max(
          1, static_cast<int>(readText(base + QStringLiteral("affected_cpus"))
                                  .split(' ', Qt::SkipEmptyParts)
                                  .size()));
      m_cpuFrequencyPolicies.push_back({
          name,
          base + QStringLiteral("scaling_cur_freq"),
          base + QStringLiteral("cpuinfo_cur_freq"),
          cpuCount,
          readNumber(base + QStringLiteral("cpuinfo_min_freq"), 0.001),
          readNumber(base + QStringLiteral("cpuinfo_max_freq"), 0.001),
      });
    }

    if (!m_cpuFrequencyPolicies.isEmpty())
      return;
    const QDir cpuRoot(QStringLiteral("/sys/devices/system/cpu"));
    m_cpuPolicyNames =
        cpuRoot.entryList(QStringList{QStringLiteral("cpu[0-9]*")},
                          QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : std::as_const(m_cpuPolicyNames)) {
      const QString base =
          cpuRoot.absoluteFilePath(name) + QStringLiteral("/cpufreq/");
      if (!QFileInfo::exists(base))
        continue;
      m_cpuFrequencyPolicies.push_back({
          name,
          base + QStringLiteral("scaling_cur_freq"),
          base + QStringLiteral("cpuinfo_cur_freq"),
          1,
          readNumber(base + QStringLiteral("cpuinfo_min_freq"), 0.001),
          readNumber(base + QStringLiteral("cpuinfo_max_freq"), 0.001),
      });
    }
  };

  const QDir policyRoot(QStringLiteral("/sys/devices/system/cpu/cpufreq"));
  const QStringList currentPolicyNames = normalizedCpuPolicyNames(
      policyRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot));
  if (!m_cpuFrequencyTopologyInitialized ||
      (m_cpuUsingPolicyTopology && currentPolicyNames != m_cpuPolicyNames) ||
      (!m_cpuUsingPolicyTopology && !currentPolicyNames.isEmpty())) {
    discoverPolicies();
  }

  QVector<double> frequencies;
  bool missingPolicyPath = false;
  for (const CpuFrequencyPolicy &policy :
       std::as_const(m_cpuFrequencyPolicies)) {
    OptionalNumber frequency = readNumber(policy.currentPath, 0.001);
    if (!frequency)
      frequency = readNumber(policy.fallbackCurrentPath, 0.001);
    if (frequency) {
      for (int index = 0; index < policy.cpuCount; ++index)
        frequencies.push_back(*frequency);
    } else if (!QFileInfo::exists(policy.currentPath) &&
               !QFileInfo::exists(policy.fallbackCurrentPath))
      missingPolicyPath = true;
    if (policy.minimumMHz && (!result.frequencyMinMHz ||
                              *policy.minimumMHz < *result.frequencyMinMHz)) {
      result.frequencyMinMHz = policy.minimumMHz;
    }
    if (policy.maximumMHz && (!result.frequencyMaxMHz ||
                              *policy.maximumMHz > *result.frequencyMaxMHz)) {
      result.frequencyMaxMHz = policy.maximumMHz;
    }
  }
  if (missingPolicyPath)
    discoverPolicies();
  if (!frequencies.isEmpty()) {
    double sum = 0.0;
    for (double value : frequencies)
      sum += value;
    result.frequencyAverageMHz = sum / frequencies.size();
    result.frequencyCurrentMHz = result.frequencyAverageMHz;
  }

  QVector<double> temperatures;
  for (const QString &path : m_cpuTemperaturePaths) {
    const OptionalNumber value = readNumber(path, 0.001);
    if (value && *value > -100.0 && *value < 250.0)
      temperatures.push_back(*value);
  }
  if (!temperatures.isEmpty()) {
    result.temperatureCelsius =
        *std::max_element(temperatures.cbegin(), temperatures.cend());
  }
  if (!m_packageTemperaturePath.isEmpty()) {
    const OptionalNumber package = readNumber(m_packageTemperaturePath, 0.001);
    if (package && *package > -100.0 && *package < 250.0)
      result.packageTemperatureCelsius = package;
  }
  if (!m_packageEnergyPath.isEmpty()) {
    bool energyOk = false;
    result.packageEnergyMicroJoules =
        readInteger(m_packageEnergyPath, &energyOk);
    result.packageEnergyRangeMicroJoules =
        readInteger(m_packageEnergyRangePath);
    if (!energyOk || !result.packageEnergyRangeMicroJoules) {
      QProcess helper;
      helper.start(QStringLiteral(KEY_CPU_POWER_HELPER), {});
      if (helper.waitForFinished(250) &&
          helper.exitStatus() == QProcess::NormalExit &&
          helper.exitCode() == 0) {
        const QList<QByteArray> fields =
            helper.readAllStandardOutput().trimmed().split(' ');
        bool energyParsed = false;
        bool rangeParsed = false;
        const qint64 energy = fields.value(0).toLongLong(&energyParsed);
        const qint64 range = fields.value(1).toLongLong(&rangeParsed);
        if (fields.size() == 2 && energyParsed && rangeParsed && energy >= 0 &&
            range > 0) {
          result.packageEnergyMicroJoules = energy;
          result.packageEnergyRangeMicroJoules = range;
          energyOk = true;
        }
      } else if (helper.state() != QProcess::NotRunning) {
        helper.kill();
        helper.waitForFinished();
      }
    }
    if (!energyOk && QFileInfo::exists(m_packageEnergyPath)) {
      const bool readable = QFileInfo(m_packageEnergyPath).isReadable();
      errors->push_back({
          QStringLiteral("cpu"),
          readable ? QStringLiteral("rapl_energy_read_failed")
                   : QStringLiteral("rapl_energy_permission_denied"),
          QStringLiteral(
              "Intel RAPL is present but unavailable; the energy reader "
              "could not access energy_uj. Other metrics remain available."),
      });
    }
  }
  if (!m_fanPath.isEmpty())
    result.fanRpm = readNumber(m_fanPath);
  return result;
}

MemoryCounters LinuxCollector::collectMemory(QVector<Error> *errors) const {
  bool ok = false;
  const MemoryCounters result =
      parseMeminfo(readAll(QStringLiteral("/proc/meminfo"), &ok));
  if (!ok || !result.valid) {
    errors->push_back({
        QStringLiteral("memory"),
        QStringLiteral("meminfo_unavailable"),
        QStringLiteral("Unable to parse /proc/meminfo"),
    });
  }
  return result;
}

} // namespace Clavis::Sysmon
