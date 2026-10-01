#include "cache_helpers.h"
#include "collector.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStorageInfo>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Clavis::Sysmon {

namespace {

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

OptionalNumber readNumber(const QString &path, double scale = 1.0) {
  bool ok = false;
  const double value = readText(path).toDouble(&ok);
  return ok ? OptionalNumber(value * scale) : std::nullopt;
}

OptionalInteger readInteger(const QString &path) {
  bool ok = false;
  const qint64 value = readText(path).toLongLong(&ok);
  return ok ? OptionalInteger(value) : std::nullopt;
}

QString blockDeviceName(const QByteArray &device) {
  if (device.isEmpty() || !device.startsWith('/'))
    return {};
  QFileInfo info(QString::fromUtf8(device));
  QString canonical = info.canonicalFilePath();
  if (canonical.isEmpty())
    canonical = info.absoluteFilePath();
  return QFileInfo(canonical).fileName();
}

QString physicalBlockDeviceName(const QString &name) {
  QString current = name;
  QSet<QString> visited;
  while (!current.isEmpty() && !visited.contains(current)) {
    visited.insert(current);
    const QString blockPath =
        QStringLiteral("/sys/class/block/%1").arg(current);
    if (QFileInfo::exists(blockPath + QStringLiteral("/partition"))) {
      const QString canonical = QFileInfo(blockPath).canonicalFilePath();
      const QString parentName =
          QFileInfo(QFileInfo(canonical).path()).fileName();
      if (!parentName.isEmpty() && parentName != current) {
        current = parentName;
        continue;
      }
    }

    const QStringList slaves =
        QDir(blockPath + QStringLiteral("/slaves"))
            .entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (slaves.size() == 1 && slaves.first() != current) {
      current = slaves.first();
      continue;
    }
    break;
  }
  return current;
}

QString blockDeviceCursorKey(const QString &name) {
  if (name.isEmpty())
    return {};

  const QString blockPath = QStringLiteral("/sys/class/block/%1").arg(name);
  QString identityPath = blockPath;
  if (QFileInfo::exists(blockPath + QStringLiteral("/partition"))) {
    const QString canonical = QFileInfo(blockPath).canonicalFilePath();
    const QString parentName =
        QFileInfo(QFileInfo(canonical).path()).fileName();
    if (!parentName.isEmpty())
      identityPath = QStringLiteral("/sys/class/block/%1").arg(parentName);
  }

  QString generation = readText(identityPath + QStringLiteral("/diskseq"));
  if (generation.isEmpty())
    generation = readText(identityPath + QStringLiteral("/device/wwid"));
  if (generation.isEmpty())
    generation = readText(identityPath + QStringLiteral("/device/serial"));

  const QString fallback =
      QFileInfo(identityPath).canonicalFilePath() + QLatin1Char('|') +
      readText(identityPath + QStringLiteral("/dev")) + QLatin1Char('|') +
      QString::fromUtf8(readAll(identityPath + QStringLiteral("/uevent")))
          .trimmed();
  return composeDeviceCursorKey(name, generation, fallback);
}

bool isPseudoFilesystem(const QByteArray &filesystem) {
  static const QSet<QByteArray> pseudo = {
      "autofs",    "bpf",      "cgroup",   "cgroup2", "configfs",  "debugfs",
      "devpts",    "devtmpfs", "efivarfs", "fusectl", "hugetlbfs", "mqueue",
      "nsfs",      "overlay",  "proc",     "pstore",  "ramfs",     "securityfs",
      "selinuxfs", "squashfs", "sysfs",    "tmpfs",   "tracefs",
  };
  return pseudo.contains(filesystem);
}

bool isAcType(const QString &type) {
  const QString lower = type.toLower();
  return lower == QStringLiteral("mains") || lower == QStringLiteral("usb") ||
         lower == QStringLiteral("usb_c") ||
         lower == QStringLiteral("usb_pd") ||
         lower == QStringLiteral("wireless");
}

} // namespace

QVector<RawDiskInfo> LinuxCollector::collectDisks(QVector<Error> *errors) {
  QHash<QString, RawDiskInfo> disksByDevice;
  QHash<QString, QSet<QString>> seenPartitions;
  QHash<QString, quint64> mountedTotals;
  QSet<QString> seenMounts;
  const QByteArray mountFingerprint =
      QCryptographicHash::hash(readAll(QStringLiteral("/proc/self/mountinfo")),
                               QCryptographicHash::Sha256);
  bool cachedPathsPresent = !m_diskTopology.isEmpty();
  for (const DiskTopology &topology : std::as_const(m_diskTopology)) {
    if (!topology.statPath.isEmpty() && !QFileInfo::exists(topology.statPath)) {
      cachedPathsPresent = false;
      break;
    }
  }
  const bool refreshTopology = topologyCacheNeedsRefresh(
      m_mountTopologyFingerprint, mountFingerprint, cachedPathsPresent);
  QHash<QString, DiskTopology> nextTopology;
  const QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
  for (const QStorageInfo &storage : volumes) {
    if (!storage.isValid() || !storage.isReady() ||
        storage.rootPath().isEmpty() ||
        seenMounts.contains(storage.rootPath()) ||
        isPseudoFilesystem(storage.fileSystemType())) {
      continue;
    }
    const QString mountPoint = storage.rootPath();
    const int pathDepth = mountPoint.count(QLatin1Char('/')) -
                          (mountPoint.endsWith(QLatin1Char('/')) ? 1 : 0);
    const bool conventionalExternal =
        mountPoint.startsWith(QStringLiteral("/mnt/")) ||
        mountPoint.startsWith(QStringLiteral("/media/")) ||
        mountPoint.startsWith(QStringLiteral("/run/media/"));
    // Sandboxes and container runtimes commonly bind individual project
    // directories. They are not useful storage volumes, while ordinary
    // top-level mounts such as /home and /boot remain visible.
    if (pathDepth > 2 && !conventionalExternal)
      continue;
    seenMounts.insert(storage.rootPath());

    const QString partitionName = blockDeviceName(storage.device());
    const QString blockName = physicalBlockDeviceName(partitionName);
    if (blockName.isEmpty())
      continue;
    DiskTopology topology = m_diskTopology.value(blockName);
    if (refreshTopology || topology.blockName != blockName) {
      topology.blockName = blockName;
      topology.counterKey = blockDeviceCursorKey(topology.blockName);
      topology.statPath = topology.blockName.isEmpty()
                              ? QString()
                              : QStringLiteral("/sys/class/block/%1/stat")
                                    .arg(topology.blockName);
    }
    nextTopology.insert(blockName, topology);

    RawDiskInfo &disk = disksByDevice[blockName];
    disk.info.available = true;
    disk.info.device = QStringLiteral("/dev/%1").arg(blockName);
    disk.counterKey = topology.counterKey;
    if (!topology.statPath.isEmpty())
      disk.counters = parseDiskStatLine(readAll(topology.statPath));

    const QString partitionDevice = QString::fromUtf8(storage.device());
    if (!disk.info.partitions.contains(partitionDevice))
      disk.info.partitions.push_back(partitionDevice);
    if (!disk.info.mountPoints.contains(mountPoint))
      disk.info.mountPoints.push_back(mountPoint);
    const QString filesystem = QString::fromUtf8(storage.fileSystemType());
    if (!filesystem.isEmpty() && !disk.info.filesystems.contains(filesystem))
      disk.info.filesystems.push_back(filesystem);

    if (!seenPartitions[blockName].contains(partitionDevice)) {
      seenPartitions[blockName].insert(partitionDevice);
      const quint64 filesystemTotal =
          static_cast<quint64>(std::max<qint64>(0, storage.bytesTotal()));
      const quint64 filesystemFree = std::min(
          filesystemTotal,
          static_cast<quint64>(std::max<qint64>(0, storage.bytesAvailable())));
      mountedTotals[blockName] += filesystemTotal;
      disk.info.usedBytes += filesystemTotal - filesystemFree;
    }
  }
  m_diskTopology = std::move(nextTopology);
  m_mountTopologyFingerprint = mountFingerprint;

  QVector<RawDiskInfo> result;
  result.reserve(disksByDevice.size());
  for (auto iterator = disksByDevice.begin(); iterator != disksByDevice.end();
       ++iterator) {
    RawDiskInfo &disk = iterator.value();
    const QString blockName = iterator.key();
    const OptionalInteger sectors =
        readInteger(QStringLiteral("/sys/class/block/%1/size").arg(blockName));
    if (sectors && *sectors > 0 &&
        static_cast<quint64>(*sectors) <=
            std::numeric_limits<quint64>::max() / 512ULL) {
      disk.info.deviceTotalBytes = static_cast<quint64>(*sectors) * 512ULL;
    }
    disk.info.totalBytes = mountedTotals.value(blockName);
    if (disk.info.totalBytes == 0)
      disk.info.totalBytes = disk.info.deviceTotalBytes;
    disk.info.usedBytes = std::min(disk.info.usedBytes, disk.info.totalBytes);
    disk.info.freeBytes = disk.info.totalBytes - disk.info.usedBytes;
    if (disk.info.totalBytes > 0) {
      disk.info.usagePercent = static_cast<double>(disk.info.usedBytes) *
                               100.0 /
                               static_cast<double>(disk.info.totalBytes);
    }
    std::sort(disk.info.partitions.begin(), disk.info.partitions.end());
    std::sort(disk.info.mountPoints.begin(), disk.info.mountPoints.end());
    std::sort(disk.info.filesystems.begin(), disk.info.filesystems.end());
    result.push_back(std::move(disk));
  }
  if (result.isEmpty()) {
    errors->push_back({
        QStringLiteral("disk"),
        QStringLiteral("mounts_unavailable"),
        QStringLiteral("No meaningful mounted filesystems were found"),
    });
  }
  std::sort(result.begin(), result.end(),
            [](const RawDiskInfo &left, const RawDiskInfo &right) {
              return left.info.device < right.info.device;
            });
  return result;
}

QVector<RawNetworkInterfaceInfo>
LinuxCollector::collectNetwork(QString *defaultInterface,
                               QVector<Error> *errors) {
  bool ok = false;
  const QHash<QString, NetworkCounter> counters =
      parseProcNetDev(readAll(QStringLiteral("/proc/net/dev"), &ok));
  if (!ok) {
    errors->push_back({
        QStringLiteral("network"),
        QStringLiteral("netdev_unavailable"),
        QStringLiteral("Unable to read /proc/net/dev"),
    });
    return {};
  }

  *defaultInterface = parseDefaultRouteInterface(
      readAll(QStringLiteral("/proc/net/route")),
      readAll(QStringLiteral("/proc/net/ipv6_route")));

  QVector<RawNetworkInterfaceInfo> result;
  QHash<QString, NetworkTopology> nextTopology;
  const bool periodicValidation =
      (++m_networkTopologyValidationCounter % 30) == 0;
  const QStringList names = counters.keys();
  for (const QString &name : names) {
    RawNetworkInterfaceInfo interface;
    interface.info.available = true;
    interface.info.name = name;
    const QString basePath = QStringLiteral("/sys/class/net/%1").arg(name);
    const QString canonicalPath = QFileInfo(basePath).canonicalFilePath();
    NetworkTopology topology = m_networkTopology.value(name);
    const bool refresh =
        topology.basePath.isEmpty() || canonicalPath.isEmpty() ||
        topology.canonicalPath != canonicalPath || periodicValidation;
    if (refresh) {
      bool ifIndexOk = false;
      topology.ifIndex =
          readText(basePath + QStringLiteral("/ifindex")).toInt(&ifIndexOk);
      if (!ifIndexOk || topology.ifIndex <= 0)
        topology.ifIndex = 0;
      topology.wireless =
          QFileInfo::exists(basePath + QStringLiteral("/wireless"));
      topology.basePath = basePath;
      topology.canonicalPath = canonicalPath;
    }
    nextTopology.insert(name, topology);
    interface.info.ifIndex = topology.ifIndex;
    interface.info.loopback = name == QStringLiteral("lo");
    interface.info.wireless = topology.wireless;
    const QString state =
        readText(topology.basePath + QStringLiteral("/operstate"));
    interface.info.up =
        state == QStringLiteral("up") || state == QStringLiteral("unknown");
    interface.counters = counters.value(name);
    interface.info.downloadTotalBytes = interface.counters.receiveBytes;
    interface.info.uploadTotalBytes = interface.counters.transmitBytes;
    result.push_back(interface);
  }
  m_networkTopology = std::move(nextTopology);
  const auto activeDefault = std::find_if(
      result.cbegin(), result.cend(),
      [defaultInterface](const RawNetworkInterfaceInfo &interface) {
        return interface.info.name == *defaultInterface && interface.info.up;
      });
  if (activeDefault == result.cend()) {
    const auto fallback =
        std::find_if(result.cbegin(), result.cend(),
                     [](const RawNetworkInterfaceInfo &interface) {
                       return interface.info.up && !interface.info.loopback;
                     });
    *defaultInterface =
        fallback == result.cend() ? QString() : fallback->info.name;
  }
  std::sort(result.begin(), result.end(),
            [defaultInterface](const RawNetworkInterfaceInfo &left,
                               const RawNetworkInterfaceInfo &right) {
              if (left.info.name == *defaultInterface)
                return true;
              if (right.info.name == *defaultInterface)
                return false;
              if (left.info.loopback != right.info.loopback)
                return !left.info.loopback;
              return left.info.name < right.info.name;
            });
  return result;
}

BatteryInfo LinuxCollector::collectBattery(QVector<Error> *errors) {
  BatteryInfo result;
  result.supported = true;
  const QDir supplies(QStringLiteral("/sys/class/power_supply"));
  if (!supplies.exists()) {
    result.supported = false;
    errors->push_back({
        QStringLiteral("battery"),
        QStringLiteral("power_supply_unavailable"),
        QStringLiteral("/sys/class/power_supply is unavailable"),
    });
    return result;
  }

  const QStringList entries =
      supplies.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  const bool cachedBatteryPresent =
      m_batteryTopology.batteryPath.isEmpty() ||
      QFileInfo::exists(m_batteryTopology.batteryPath);
  bool cachedAcPresent = true;
  for (const QString &path : std::as_const(m_batteryTopology.acPaths))
    cachedAcPresent = cachedAcPresent && QFileInfo::exists(path);
  if (entries != m_batteryTopology.entries || !cachedBatteryPresent ||
      !cachedAcPresent) {
    BatteryTopology topology;
    topology.entries = entries;
    QString fallbackBatteryPath;
    QString fallbackBatteryName;
    for (const QString &entry : entries) {
      const QString path = supplies.absoluteFilePath(entry);
      const QString type = readText(path + QStringLiteral("/type"));
      if (type.compare(QStringLiteral("Battery"), Qt::CaseInsensitive) == 0) {
        if (fallbackBatteryPath.isEmpty()) {
          fallbackBatteryPath = path;
          fallbackBatteryName = entry;
        }
        const OptionalInteger present =
            readInteger(path + QStringLiteral("/present"));
        if (topology.batteryPath.isEmpty() && (!present || *present > 0)) {
          topology.batteryPath = path;
          topology.batteryName = entry;
        }
      } else if (isAcType(type)) {
        topology.acPaths.push_back(path);
      }
    }
    if (topology.batteryPath.isEmpty()) {
      topology.batteryPath = fallbackBatteryPath;
      topology.batteryName = fallbackBatteryName;
    }
    m_batteryTopology = std::move(topology);
  }

  std::optional<bool> acOnline;
  for (const QString &path : std::as_const(m_batteryTopology.acPaths)) {
    const OptionalInteger online =
        readInteger(path + QStringLiteral("/online"));
    if (online)
      acOnline = acOnline.value_or(false) || *online > 0;
  }
  result.acOnline = acOnline;
  const QString batteryPath = m_batteryTopology.batteryPath;
  const QString batteryName = m_batteryTopology.batteryName;
  if (batteryPath.isEmpty()) {
    result.available = true;
    result.present = false;
    return result;
  }
  result.name = batteryName;

  const OptionalInteger present =
      readInteger(batteryPath + QStringLiteral("/present"));
  result.present = !present || *present > 0;
  result.available = result.present;
  if (!result.present)
    return result;

  result.chargePercent = readNumber(batteryPath + QStringLiteral("/capacity"));
  result.status = readText(batteryPath + QStringLiteral("/status")).toLower();

  OptionalInteger energyNow =
      readInteger(batteryPath + QStringLiteral("/energy_now"));
  OptionalInteger energyFull =
      readInteger(batteryPath + QStringLiteral("/energy_full"));
  OptionalInteger energyDesign =
      readInteger(batteryPath + QStringLiteral("/energy_full_design"));
  OptionalInteger powerNow =
      readInteger(batteryPath + QStringLiteral("/power_now"));

  if (!energyNow || !energyFull) {
    const OptionalInteger chargeNow =
        readInteger(batteryPath + QStringLiteral("/charge_now"));
    const OptionalInteger chargeFull =
        readInteger(batteryPath + QStringLiteral("/charge_full"));
    const OptionalInteger chargeDesign =
        readInteger(batteryPath + QStringLiteral("/charge_full_design"));
    const OptionalInteger voltage =
        readInteger(batteryPath + QStringLiteral("/voltage_now"));
    if (voltage && chargeNow)
      energyNow = static_cast<qint64>(static_cast<long double>(*voltage) *
                                      static_cast<long double>(*chargeNow) /
                                      1'000'000.0L);
    if (voltage && chargeFull)
      energyFull = static_cast<qint64>(static_cast<long double>(*voltage) *
                                       static_cast<long double>(*chargeFull) /
                                       1'000'000.0L);
    if (voltage && chargeDesign)
      energyDesign = static_cast<qint64>(
          static_cast<long double>(*voltage) *
          static_cast<long double>(*chargeDesign) / 1'000'000.0L);
  }

  if (!powerNow) {
    const OptionalInteger current =
        readInteger(batteryPath + QStringLiteral("/current_now"));
    const OptionalInteger voltage =
        readInteger(batteryPath + QStringLiteral("/voltage_now"));
    if (current && voltage) {
      powerNow = static_cast<qint64>(static_cast<long double>(*current) *
                                     static_cast<long double>(*voltage) /
                                     1'000'000.0L);
    }
  }

  result.energyNowMicroWh = energyNow;
  result.energyFullMicroWh = energyFull;
  result.energyDesignMicroWh = energyDesign;
  if (powerNow && *powerNow >= 0)
    result.powerWatts = static_cast<double>(*powerNow) / 1'000'000.0;
  if (energyFull && energyDesign && *energyDesign > 0) {
    result.healthPercent = static_cast<double>(*energyFull) * 100.0 /
                           static_cast<double>(*energyDesign);
  }
  if (powerNow && *powerNow > 0) {
    qint64 relevantEnergy = 0;
    if (result.status == QStringLiteral("charging") && energyNow &&
        energyFull && *energyFull >= *energyNow) {
      relevantEnergy = *energyFull - *energyNow;
    } else if (energyNow) {
      relevantEnergy = *energyNow;
    }
    if (relevantEnergy > 0) {
      result.timeRemainingSeconds =
          static_cast<qint64>(static_cast<long double>(relevantEnergy) *
                              3600.0L / static_cast<long double>(*powerNow));
    }
  }
  return result;
}

} // namespace Clavis::Sysmon
