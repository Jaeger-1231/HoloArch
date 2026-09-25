#pragma once

#include "parsers.h"
#include "types.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>
#include <optional>

namespace Clavis::Sysmon {

class GpuManager;

// Machine output keeps statm semantics; the TUI can reuse the cheaper stat RSS.
enum class ProcessMemorySource { Statm, Stat };

struct RawCpuInfo {
  CpuCounters counters;
  OptionalNumber frequencyCurrentMHz;
  OptionalNumber frequencyAverageMHz;
  OptionalNumber frequencyMinMHz;
  OptionalNumber frequencyMaxMHz;
  OptionalNumber temperatureCelsius;
  OptionalNumber packageTemperatureCelsius;
  OptionalInteger packageEnergyMicroJoules;
  OptionalInteger packageEnergyRangeMicroJoules;
  OptionalNumber fanRpm;
};

struct RawDiskInfo {
  DiskInfo info;
  QString counterKey;
  std::optional<DiskCounter> counters;
};

struct RawNetworkInterfaceInfo {
  NetworkInterfaceInfo info;
  NetworkCounter counters;
};

struct RawProcessInfo {
  ProcessInfo info;
  quint64 cpuTicks = 0;
  quint64 startTicks = 0;
};

struct RawSnapshot {
  qint64 cpuTimestampNs = 0;
  qint64 diskTimestampNs = 0;
  qint64 networkTimestampNs = 0;
  qint64 processTimestampNs = 0;
  SystemInfo system;
  RawCpuInfo cpu;
  MemoryCounters memory;
  QVector<GpuInfo> gpus;
  QVector<RawDiskInfo> disks;
  QVector<RawNetworkInterfaceInfo> networkInterfaces;
  QString defaultNetworkInterface;
  BatteryInfo battery;
  QVector<RawProcessInfo> processes;
  QVector<Error> errors;
};

class LinuxCollector {
public:
  LinuxCollector();
  ~LinuxCollector();

  RawSnapshot
  collect(const ModuleSet &modules,
          ProcessMemorySource processMemory = ProcessMemorySource::Statm);

private:
  SystemInfo collectSystem(QVector<Error> *errors) const;
  RawCpuInfo collectCpu(QVector<Error> *errors);
  MemoryCounters collectMemory(QVector<Error> *errors) const;
  QVector<RawDiskInfo> collectDisks(QVector<Error> *errors);
  QVector<RawNetworkInterfaceInfo> collectNetwork(QString *defaultInterface,
                                                  QVector<Error> *errors);
  BatteryInfo collectBattery(QVector<Error> *errors);
  QVector<RawProcessInfo> collectProcesses(quint64 totalMemoryBytes,
                                           qint64 bootTimeMs,
                                           ProcessMemorySource processMemory,
                                           QVector<Error> *errors);

  struct CpuFrequencyPolicy {
    QString name;
    QString currentPath;
    QString fallbackCurrentPath;
    int cpuCount = 1;
    OptionalNumber minimumMHz;
    OptionalNumber maximumMHz;
  };
  struct NetworkTopology {
    int ifIndex = 0;
    bool wireless = false;
    QString basePath;
    QString canonicalPath;
  };
  struct BatteryTopology {
    QStringList entries;
    QString batteryPath;
    QString batteryName;
    QStringList acPaths;
    bool supported = true;
  };
  struct DiskTopology {
    QString blockName;
    QString counterKey;
    QString statPath;
  };
  struct ProcessMetadata {
    quint64 startTicks = 0;
    uint uid = 0;
    QString name;
    QString command;
    QString executablePath;
    QString user;
  };

  void loadStaticSystemInfo();
  SystemInfo m_staticSystem;
  mutable QStringList m_cpuTemperaturePaths;
  mutable QString m_packageTemperaturePath;
  mutable QString m_packageEnergyPath;
  mutable QString m_packageEnergyRangePath;
  mutable QString m_fanPath;
  QStringList m_cpuPolicyNames;
  QVector<CpuFrequencyPolicy> m_cpuFrequencyPolicies;
  bool m_cpuFrequencyTopologyInitialized = false;
  bool m_cpuUsingPolicyTopology = false;
  QHash<QString, NetworkTopology> m_networkTopology;
  quint64 m_networkTopologyValidationCounter = 0;
  BatteryTopology m_batteryTopology;
  QByteArray m_mountTopologyFingerprint;
  QHash<QString, DiskTopology> m_diskTopology;
  QHash<qint64, ProcessMetadata> m_processMetadata;
  QHash<uint, QString> m_userNames;
  std::unique_ptr<GpuManager> m_gpuManager;
};

} // namespace Clavis::Sysmon
