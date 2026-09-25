#include "sysmon/cache_helpers.h"
#include "sysmon/gpu/drm_helpers.h"
#include "sysmon/gpu/gpu_provider.h"
#include "sysmon/parsers.h"
#include "sysmon/serialization.h"
#include "sysmon/types.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Clavis::Sysmon;

class SysmonCoreTest : public QObject {
  Q_OBJECT

private slots:
  void cpuFirstSampleIsUnavailable();
  void cpuDeltaUsesLinuxAccounting();
  void cpuCoresMatchByKernelIdAcrossHotplug();
  void bootTimeParsingIsStableAndValidated();
  void memoryUsesAvailableAndExposesCacheSwap();
  void memoryPreservesLegitimateZeroAvailable();
  void networkCountersHandleResetAndZeroInterval();
  void defaultRouteUsesMetricAndSupportsIpv6();
  void diskCountersHandleDeltaAndReset();
  void deviceCursorChangesAcrossReplacement();
  void missingDeviceCannotReusePreviousCursor();
  void processStatHandlesSpacesAndParentheses();
  void processStatResidentPages_data();
  void processStatResidentPages();
  void processStatRejectsMalformedCounters();
  void processCpuHandlesPidResetAndZeroInterval();
  void unavailableMetricsSerializeAsNull();
  void systemSerializationOmitsLoadSnapshotFields();
  void snapshotSerializationUsesStableUnitsAndModules();
  void batteryPercentageUsesPercentSuffix();
  void jsonLineIsOneCompleteObject();
  void gpuPciIdentityIsNormalizedAndSorted();
  void gpuPartialMetricsDriveCapabilitiesAndSerialization();
  void drmFdinfoFirstDeltaAndResetAreUnavailable();
  void drmFdinfoDeduplicatesClientsAndAggregatesPerEngine();
  void drmFdinfoUsesBusiestEngineInsteadOfSumming();
  void drmFdinfoHonorsEngineCapacity();
  void gpuProviderFallbackAndHybridIdentityAreStable();
  void cadenceUsesAbsoluteDeadlinesAndSkipsMissedTicks();
  void cpuPolicyTopologyIsNormalized();
  void topologyCacheInvalidatesForChangesAndMissingPaths();
  void networkIdentityTracksIfIndexReplacement();
  void processIdentityRejectsPidReuse();
};

namespace {

CpuCounters beforeCpu() {
  return parseProcStat("cpu  100 10 40 800 20 5 5 0 4 0\n"
                       "cpu0 50 5 20 400 10 2 3 0 2 0\n"
                       "cpu1 50 5 20 400 10 3 2 0 2 0\n");
}

CpuCounters afterCpu() {
  return parseProcStat("cpu  140 20 60 880 30 10 10 0 8 0\n"
                       "cpu0 70 10 30 440 15 4 6 0 4 0\n"
                       "cpu1 70 10 30 440 15 6 4 0 4 0\n");
}

} // namespace

void SysmonCoreTest::cpuFirstSampleIsUnavailable() {
  const CpuInfo info = calculateCpuInfo({}, beforeCpu());
  QVERIFY(info.available);
  QVERIFY(!info.sampleReady);
  QVERIFY(!info.usagePercent.has_value());
  QCOMPARE(info.coreUsagePercent.size(), 0);
}

void SysmonCoreTest::cpuDeltaUsesLinuxAccounting() {
  const CpuCounters before = beforeCpu();
  const CpuCounters after = afterCpu();
  QVERIFY(before.valid);
  QVERIFY(after.valid);
  // Guest time is present in the fixture but must not be double-counted.
  QCOMPARE(before.total.total(), quint64(980));

  const CpuInfo info = calculateCpuInfo(before, after);
  QVERIFY(info.sampleReady);
  QVERIFY(info.usagePercent.has_value());
  QVERIFY(info.userPercent.has_value());
  QVERIFY(info.systemPercent.has_value());
  QVERIFY(info.iowaitPercent.has_value());
  QCOMPARE(info.coreUsagePercent.size(), 2);
  QCOMPARE(info.coreIds, QVector<int>({0, 1}));
  QVERIFY(*info.usagePercent >= 0.0 && *info.usagePercent <= 100.0);
  QVERIFY(*info.userPercent > 0.0);
  const double mainShares = *info.userPercent + *info.systemPercent +
                            *info.idlePercent + *info.iowaitPercent;
  QVERIFY(qAbs(mainShares - 100.0) < 0.0001);
  QVERIFY(*info.idlePercent > *info.iowaitPercent);
}

void SysmonCoreTest::cpuCoresMatchByKernelIdAcrossHotplug() {
  const CpuCounters before = parseProcStat("cpu  300 0 0 1200 0 0 0 0\n"
                                           "cpu0 100 0 0 400 0 0 0 0\n"
                                           "cpu1 100 0 0 400 0 0 0 0\n"
                                           "cpu2 100 0 0 400 0 0 0 0\n");
  const CpuCounters after = parseProcStat("cpu  340 0 0 1280 0 0 0 0\n"
                                          "cpu0 110 0 0 440 0 0 0 0\n"
                                          "cpu2 130 0 0 440 0 0 0 0\n");
  const CpuInfo info = calculateCpuInfo(before, after);

  QCOMPARE(info.coreIds, QVector<int>({0, 2}));
  QCOMPARE(info.coreUsagePercent.size(), 2);
  QVERIFY(info.coreUsagePercent.at(0).has_value());
  QVERIFY(info.coreUsagePercent.at(1).has_value());
  QVERIFY(*info.coreUsagePercent.at(1) > *info.coreUsagePercent.at(0));
}

void SysmonCoreTest::bootTimeParsingIsStableAndValidated() {
  const OptionalInteger bootTime =
      parseProcBootTimeMs("cpu  1 2 3 4 5 6 7 8 9 10\n"
                          "intr 42\n"
                          "btime 1760000000\n");
  QVERIFY(bootTime.has_value());
  QCOMPARE(*bootTime, qint64(1760000000000));
  QVERIFY(!parseProcBootTimeMs("btime invalid\n").has_value());
  QVERIFY(!parseProcBootTimeMs("cpu 1 2 3\n").has_value());
}

void SysmonCoreTest::memoryUsesAvailableAndExposesCacheSwap() {
  const MemoryCounters counters = parseMeminfo("MemTotal:       1000 kB\n"
                                               "MemFree:         100 kB\n"
                                               "MemAvailable:    400 kB\n"
                                               "Buffers:          50 kB\n"
                                               "Cached:          200 kB\n"
                                               "SReclaimable:     25 kB\n"
                                               "SwapTotal:       500 kB\n"
                                               "SwapFree:        300 kB\n");
  const MemoryInfo memory = calculateMemoryInfo(counters);
  QVERIFY(memory.available);
  QCOMPARE(memory.totalBytes, quint64(1000 * 1024));
  QCOMPARE(memory.availableBytes, quint64(400 * 1024));
  QCOMPARE(memory.usedBytes, quint64(600 * 1024));
  QCOMPARE(memory.freeBytes, quint64(100 * 1024));
  QCOMPARE(memory.cachedBytes, quint64(225 * 1024));
  QCOMPARE(memory.buffersBytes, quint64(50 * 1024));
  QCOMPARE(memory.swapUsedBytes, quint64(200 * 1024));
  QCOMPARE(*memory.usagePercent, 60.0);
}

void SysmonCoreTest::memoryPreservesLegitimateZeroAvailable() {
  const MemoryCounters counters = parseMeminfo("MemTotal:       1000 kB\n"
                                               "MemFree:         100 kB\n"
                                               "MemAvailable:      0 kB\n"
                                               "Buffers:          50 kB\n"
                                               "Cached:          200 kB\n");
  const MemoryInfo memory = calculateMemoryInfo(counters);
  QVERIFY(memory.available);
  QCOMPARE(memory.availableBytes, quint64(0));
  QCOMPARE(memory.usedBytes, memory.totalBytes);
  QCOMPARE(*memory.usagePercent, 100.0);
}

void SysmonCoreTest::networkCountersHandleResetAndZeroInterval() {
  const auto counters = parseProcNetDev(
      "Inter-| Receive | Transmit\n"
      " face |bytes packets errs drop fifo frame compressed multicast|"
      "bytes packets errs drop fifo colls carrier compressed\n"
      "eth0: 1000 1 0 0 0 0 0 0 2000 2 0 0 0 0 0 0\n");
  QCOMPARE(counters.value(QStringLiteral("eth0")).receiveBytes, quint64(1000));
  QCOMPARE(*counterRate(1000, 3000, 2.0), 1000.0);
  QVERIFY(!counterRate(3000, 1000, 1.0).has_value());
  QVERIFY(!counterRate(1000, 3000, 0.0).has_value());
}

void SysmonCoreTest::defaultRouteUsesMetricAndSupportsIpv6() {
  const QByteArray ipv4 =
      "Iface Destination Gateway Flags RefCnt Use Metric Mask\n"
      "eth0 00000000 01020304 0003 0 0 600 00000000\n"
      "wlan0 00000000 01020304 0003 0 0 100 00000000\n";
  QCOMPARE(parseDefaultRouteInterface(ipv4, {}), QStringLiteral("wlan0"));

  const QByteArray ipv6 = "00000000000000000000000000000000 00 "
                          "00000000000000000000000000000000 00 "
                          "fe800000000000000000000000000001 00000064 00000000 "
                          "00000000 00000001 enp0s1\n";
  QCOMPARE(parseDefaultRouteInterface({}, ipv6), QStringLiteral("enp0s1"));

  const QByteArray rejectedIpv4 =
      "eth0 00000000 00000000 0201 0 0 1 00000000\n";
  QVERIFY(parseDefaultRouteInterface(rejectedIpv4, {}).isEmpty());
}

void SysmonCoreTest::diskCountersHandleDeltaAndReset() {
  const auto before = parseDiskStatLine("10 0 100 0 20 0 200 0 0 0 0");
  const auto after = parseDiskStatLine("12 0 140 0 25 0 260 0 0 0 0");
  QVERIFY(before.has_value());
  QVERIFY(after.has_value());
  QCOMPARE(*counterRate(before->sectorsRead, after->sectorsRead, 2.0), 20.0);
  QCOMPARE(*counterRate(before->writesCompleted, after->writesCompleted, 1.0),
           5.0);
  QVERIFY(
      !counterRate(after->sectorsRead, before->sectorsRead, 1.0).has_value());
}

void SysmonCoreTest::deviceCursorChangesAcrossReplacement() {
  const QString first = composeDeviceCursorKey(
      QStringLiteral("sdb"), QStringLiteral("41"), QStringLiteral("8:16"));
  const QString replacement = composeDeviceCursorKey(
      QStringLiteral("sdb"), QStringLiteral("42"), QStringLiteral("8:16"));
  QVERIFY(!first.isEmpty());
  QVERIFY(first != replacement);
  QCOMPARE(composeDeviceCursorKey(QStringLiteral("sdb"), {},
                                  QStringLiteral("8:16|DEVNAME=sdb")),
           QStringLiteral("sdb#fallback:8:16|DEVNAME=sdb"));
}

void SysmonCoreTest::missingDeviceCannotReusePreviousCursor() {
  QHash<QString, NetworkCounter> previous{
      {QStringLiteral("eth0"), NetworkCounter{100, 200}},
  };
  QHash<QString, NetworkCounter> current{
      {QStringLiteral("wlan0"), NetworkCounter{10, 20}},
  };
  QVERIFY(previous.constFind(QStringLiteral("wlan0")) == previous.cend());
  QVERIFY(current.constFind(QStringLiteral("eth0")) == current.cend());
}

void SysmonCoreTest::processStatHandlesSpacesAndParentheses() {
  const ProcessStat stat =
      parseProcessStat("123 (name with ) paren) S 7 2 3 4 5 6 7 8 9 10 "
                       "120 30 0 0 20 0 5 0 9000 0 0 0\n");
  QVERIFY(stat.valid);
  QCOMPARE(stat.pid, qint64(123));
  QCOMPARE(stat.name, QStringLiteral("name with ) paren"));
  QCOMPARE(stat.state, QChar(QLatin1Char('S')));
  QCOMPARE(stat.ppid, qint64(7));
  QCOMPARE(stat.userTicks, quint64(120));
  QCOMPARE(stat.systemTicks, quint64(30));
  QCOMPARE(stat.threadCount, 5);
  QCOMPARE(stat.startTicks, quint64(9000));
  QVERIFY(stat.residentPages.has_value());
  QCOMPARE(*stat.residentPages, qint64(0));
}

void SysmonCoreTest::processStatResidentPages_data() {
  QTest::addColumn<QByteArray>("tail");
  QTest::addColumn<qint64>("pages");
  QTest::newRow("resident-pages")
      << QByteArray("4096 1234 18446744073709551615\n") << qint64(1234);
  QTest::newRow("zero-rss") << QByteArray("0 0\n") << qint64(0);
  QTest::newRow("whitespace") << QByteArray("\t4096  \t56\r\n") << qint64(56);
  QTest::newRow("missing-rss") << QByteArray("4096\n") << qint64(-1);
  QTest::newRow("negative-rss") << QByteArray("4096 -1\n") << qint64(-1);
  QTest::newRow("overflow-rss")
      << QByteArray("4096 9223372036854775808\n") << qint64(-1);
  QTest::newRow("malformed-rss") << QByteArray("4096 12x\n") << qint64(-1);
}

void SysmonCoreTest::processStatResidentPages() {
  QFETCH(QByteArray, tail);
  QFETCH(qint64, pages);
  const ProcessStat stat =
      parseProcessStat("123 (worker) S 7 2 3 4 5 6 7 8 9 10 "
                       "120 30 0 0 20 0 5 0 9000 " +
                       tail);
  QVERIFY(stat.valid);
  QCOMPARE(stat.residentPages.has_value(), pages >= 0);
  if (pages >= 0)
    QCOMPARE(*stat.residentPages, pages);
  QCOMPARE(stat.userTicks, quint64(120));
  QCOMPARE(stat.startTicks, quint64(9000));
}

void SysmonCoreTest::processStatRejectsMalformedCounters() {
  const QByteArray valid =
      "123 (worker ) name) S 7 2 3 4 5 6 7 8 9 10 120 30 0 0 20 0 5 0 9000";
  // CPU/identity remain usable when optional memory fields are absent.
  QVERIFY(parseProcessStat(valid).valid);
  QVERIFY(!parseProcessStat(valid).residentPages.has_value());
  QVERIFY(!parseProcessStat("123 worker S 7").valid);
  QVERIFY(!parseProcessStat("123 (worker) S 7").valid);
  QByteArray invalid = valid;
  invalid.replace(" 120 ", " invalid ");
  QVERIFY(!parseProcessStat(invalid).valid);
  invalid = valid;
  invalid.replace("9000", "18446744073709551616");
  QVERIFY(!parseProcessStat(invalid).valid);
}

void SysmonCoreTest::processCpuHandlesPidResetAndZeroInterval() {
  QCOMPARE(*processCpuPercent(100, 150, 100, 0.5), 100.0);
  QVERIFY(!processCpuPercent(150, 100, 100, 1.0).has_value());
  QVERIFY(!processCpuPercent(100, 150, 100, 0.0).has_value());
  QVERIFY(!processCpuPercent(100, 150, 0, 1.0).has_value());
}

void SysmonCoreTest::cadenceUsesAbsoluteDeadlinesAndSkipsMissedTicks() {
  QCOMPARE(nextCadenceDeadlineMs(0, 150, 2000), qint64(2000));
  QCOMPARE(nextCadenceDeadlineMs(2000, 2150, 2000), qint64(4000));
  QCOMPARE(nextCadenceDeadlineMs(4000, 8500, 2000), qint64(10000));
  QVERIFY(nextCadenceDeadlineMs(4000, 8500, 2000) > 8500);
}

void SysmonCoreTest::cpuPolicyTopologyIsNormalized() {
  QCOMPARE(
      normalizedCpuPolicyNames(
          {QStringLiteral("policy10"), QStringLiteral("policy2"),
           QStringLiteral("cpu0"), QStringLiteral("policy2"),
           QStringLiteral("policyx")}),
      QStringList({QStringLiteral("policy2"), QStringLiteral("policy10")}));
}

void SysmonCoreTest::topologyCacheInvalidatesForChangesAndMissingPaths() {
  QVERIFY(!topologyCacheNeedsRefresh("same", "same", true));
  QVERIFY(topologyCacheNeedsRefresh("old", "new", true));
  QVERIFY(topologyCacheNeedsRefresh("same", "same", false));
  QVERIFY(topologyCacheNeedsRefresh({}, "present", true));
}

void SysmonCoreTest::networkIdentityTracksIfIndexReplacement() {
  QCOMPARE(networkInterfaceIdentity(QStringLiteral("eth0"), 2),
           QStringLiteral("eth0#2"));
  QVERIFY(networkInterfaceIdentity(QStringLiteral("eth0"), 2) !=
          networkInterfaceIdentity(QStringLiteral("eth0"), 7));
}

void SysmonCoreTest::processIdentityRejectsPidReuse() {
  QVERIFY(processIdentityMatches(9000, 9000));
  QVERIFY(!processIdentityMatches(9000, 9001));
  QVERIFY(!processIdentityMatches(0, 0));
}

void SysmonCoreTest::unavailableMetricsSerializeAsNull() {
  Snapshot snapshot;
  snapshot.timestampMs = 1;
  snapshot.sequence = 1;
  snapshot.requestedModules = {QStringLiteral("cpu")};
  snapshot.cpu.available = false;
  const QJsonObject cpu =
      snapshotToJson(snapshot).value(QStringLiteral("cpu")).toObject();
  QVERIFY(cpu.value(QStringLiteral("usagePercent")).isNull());
  QVERIFY(cpu.value(QStringLiteral("temperatureCelsius")).isNull());
  QCOMPARE(cpu.value(QStringLiteral("available")).toBool(), false);
}

void SysmonCoreTest::systemSerializationOmitsLoadSnapshotFields() {
  Snapshot snapshot;
  snapshot.timestampMs = 1;
  snapshot.sequence = 1;
  snapshot.requestedModules = {QStringLiteral("system")};
  snapshot.system.available = true;
  snapshot.system.cpuModelName = QStringLiteral("Fixture CPU");

  const QJsonObject system =
      snapshotToJson(snapshot).value(QStringLiteral("system")).toObject();
  QCOMPARE(system.value(QStringLiteral("cpuModelName")).toString(),
           QStringLiteral("Fixture CPU"));
  for (const QString &field :
       {QStringLiteral("load1"), QStringLiteral("load5"),
        QStringLiteral("load15"), QStringLiteral("runningTasks"),
        QStringLiteral("totalTasks")}) {
    QVERIFY2(!system.contains(field), qPrintable(field));
  }
}

void SysmonCoreTest::snapshotSerializationUsesStableUnitsAndModules() {
  Snapshot snapshot;
  snapshot.timestampMs = 1760000000000;
  snapshot.sequence = 42;
  snapshot.intervalMs = 1000;
  snapshot.requestedModules = {
      QStringLiteral("disk"),
      QStringLiteral("memory"),
      QStringLiteral("network"),
  };
  snapshot.memory.available = true;
  snapshot.memory.totalBytes = 1024;
  snapshot.memory.usedBytes = 512;
  snapshot.memory.availableBytes = 512;
  snapshot.memory.freeBytes = 128;
  snapshot.memory.usagePercent = 50.0;
  DiskInfo disk;
  disk.available = true;
  disk.device = QStringLiteral("/dev/nvme0n1");
  disk.partitions = {
      QStringLiteral("/dev/nvme0n1p2"),
      QStringLiteral("/dev/nvme0n1p3"),
  };
  disk.mountPoints = {
      QStringLiteral("/"),
      QStringLiteral("/efi"),
      QStringLiteral("/home"),
  };
  disk.filesystems = {
      QStringLiteral("btrfs"),
      QStringLiteral("vfat"),
  };
  disk.deviceTotalBytes = 4096;
  disk.totalBytes = 2048;
  disk.usedBytes = 1024;
  disk.freeBytes = 1024;
  disk.usagePercent = 50.0;
  disk.readBytesPerSecond = 4096.0;
  disk.writeBytesPerSecond = 2048.0;
  snapshot.disks.push_back(disk);
  snapshot.network.available = true;
  snapshot.network.defaultInterface = QStringLiteral("eth0");
  snapshot.network.downloadBytesPerSecond = 123.5;
  snapshot.network.wifiAvailable = true;
  snapshot.network.wifiConnected = true;
  snapshot.errors.push_back({
      QStringLiteral("network"),
      QStringLiteral("partial"),
      QStringLiteral("fixture"),
  });

  const QJsonObject json = snapshotToJson(snapshot);
  QCOMPARE(json.value(QStringLiteral("schemaVersion")).toInt(), 1);
  QCOMPARE(json.value(QStringLiteral("timestampMs")).toInteger(),
           qint64(1760000000000));
  QCOMPARE(json.value(QStringLiteral("sequence")).toInteger(), qint64(42));
  QVERIFY(json.contains(QStringLiteral("memory")));
  QVERIFY(json.contains(QStringLiteral("network")));
  QVERIFY(json.contains(QStringLiteral("disks")));
  QVERIFY(!json.contains(QStringLiteral("cpu")));
  QCOMPARE(json.value(QStringLiteral("memory"))
               .toObject()
               .value(QStringLiteral("totalBytes"))
               .toInteger(),
           qint64(1024));
  QCOMPARE(json.value(QStringLiteral("memory"))
               .toObject()
               .value(QStringLiteral("freeBytes"))
               .toInteger(),
           qint64(128));
  QCOMPARE(json.value(QStringLiteral("network"))
               .toObject()
               .value(QStringLiteral("downloadBytesPerSecond"))
               .toDouble(),
           123.5);
  const QJsonObject diskJson =
      json.value(QStringLiteral("disks")).toArray().first().toObject();
  QCOMPARE(diskJson.value(QStringLiteral("device")).toString(),
           QStringLiteral("/dev/nvme0n1"));
  QCOMPARE(diskJson.value(QStringLiteral("partitions")).toArray().size(), 2);
  QCOMPARE(diskJson.value(QStringLiteral("mountPoints")).toArray().size(), 3);
  QCOMPARE(diskJson.value(QStringLiteral("deviceTotalBytes")).toInteger(),
           qint64(4096));
  QCOMPARE(diskJson.value(QStringLiteral("readBytesPerSecond")).toDouble(),
           4096.0);
  QCOMPARE(json.value(QStringLiteral("network"))
               .toObject()
               .value(QStringLiteral("wifiAvailable"))
               .toBool(),
           true);
  QCOMPARE(json.value(QStringLiteral("network"))
               .toObject()
               .value(QStringLiteral("wifiConnected"))
               .toBool(),
           true);
  QVERIFY(json.value(QStringLiteral("network"))
              .toObject()
              .value(QStringLiteral("wifiSignalPercent"))
              .isNull());
  QCOMPARE(json.value(QStringLiteral("errors")).toArray().size(), 1);
}

void SysmonCoreTest::batteryPercentageUsesPercentSuffix() {
  Snapshot snapshot;
  snapshot.timestampMs = 1;
  snapshot.sequence = 1;
  snapshot.requestedModules = {QStringLiteral("battery")};
  snapshot.battery.available = true;
  snapshot.battery.present = true;
  snapshot.battery.chargePercent = 75.0;

  const QJsonObject battery =
      snapshotToJson(snapshot).value(QStringLiteral("battery")).toObject();
  QCOMPARE(battery.value(QStringLiteral("chargePercent")).toDouble(), 75.0);
  QVERIFY(!battery.contains(QStringLiteral("percent")));
}

void SysmonCoreTest::jsonLineIsOneCompleteObject() {
  Snapshot snapshot;
  snapshot.timestampMs = 1;
  snapshot.sequence = 1;
  snapshot.requestedModules = {QStringLiteral("battery")};
  const QByteArray line = snapshotToJsonLine(snapshot);
  QVERIFY(line.endsWith('\n'));
  QCOMPARE(line.count('\n'), 1);
  QJsonParseError error;
  const QJsonDocument document =
      QJsonDocument::fromJson(line.trimmed(), &error);
  QCOMPARE(error.error, QJsonParseError::NoError);
  QVERIFY(document.isObject());
}

void SysmonCoreTest::gpuPciIdentityIsNormalizedAndSorted() {
  QCOMPARE(normalizePciId(QStringLiteral("PCI:01:00.0")),
           QStringLiteral("0000:01:00.0"));
  QCOMPARE(normalizePciId(QStringLiteral("0000:AB:02.1")),
           QStringLiteral("0000:ab:02.1"));
  QVERIFY(normalizePciId(QStringLiteral("card0")).isEmpty());

  GpuInfo later;
  later.pciId = QStringLiteral("0000:02:00.0");
  later.id = stableGpuId(later.pciId, {});
  GpuInfo earlier;
  earlier.pciId = QStringLiteral("01:00.0");
  earlier.id = stableGpuId(earlier.pciId, {});
  const QVector<GpuInfo> result = mergeAndSortGpus({{later, earlier}});
  QCOMPARE(result.size(), 2);
  QCOMPARE(result.at(0).id, QStringLiteral("pci:0000:01:00.0"));
  QCOMPARE(result.at(1).id, QStringLiteral("pci:0000:02:00.0"));
}

void SysmonCoreTest::gpuPartialMetricsDriveCapabilitiesAndSerialization() {
  GpuInfo gpu;
  gpu.available = true;
  gpu.id = QStringLiteral("pci:0000:01:00.0");
  gpu.pciId = QStringLiteral("0000:01:00.0");
  gpu.provider = QStringLiteral("fixture");
  gpu.temperatureCelsius = 47.0;
  updateGpuCapabilities(&gpu);
  QVERIFY(gpu.supported);
  QVERIFY(!gpu.capabilities.utilization);
  QVERIFY(gpu.capabilities.temperature);
  QVERIFY(!gpu.capabilities.memory);

  Snapshot snapshot;
  snapshot.requestedModules = {QStringLiteral("gpu")};
  snapshot.gpus = {gpu};
  const QJsonObject json = snapshotToJson(snapshot)
                               .value(QStringLiteral("gpus"))
                               .toArray()
                               .first()
                               .toObject();
  QCOMPARE(json.value(QStringLiteral("provider")).toString(),
           QStringLiteral("fixture"));
  QVERIFY(json.value(QStringLiteral("utilizationPercent")).isNull());
  QCOMPARE(json.value(QStringLiteral("capabilities"))
               .toObject()
               .value(QStringLiteral("temperature"))
               .toBool(),
           true);
  QCOMPARE(
      snapshotToJson(snapshot).value(QStringLiteral("schemaVersion")).toInt(),
      1);
}

void SysmonCoreTest::drmFdinfoFirstDeltaAndResetAreUnavailable() {
  const auto first = parseDrmFdinfo("drm-client-id:\t7\n"
                                    "drm-pdev:\t0000:00:02.0\n"
                                    "drm-cycles-render:\t100 ns\n"
                                    "drm-total-cycles-render:\t1000 ns\n");
  QVERIFY(first.has_value());
  DrmFdinfoSnapshot initial;
  insertDrmClient(&initial, *first);
  QVERIFY(!calculateDrmFdinfoUtilization({}, initial, 1'000)
               .value(QStringLiteral("0000:00:02.0"))
               .has_value());

  const auto reset = parseDrmFdinfo("drm-client-id: 7\n"
                                    "drm-pdev: 0000:00:02.0\n"
                                    "drm-cycles-render: 10 ns\n"
                                    "drm-total-cycles-render: 20 ns\n");
  DrmFdinfoSnapshot afterReset;
  insertDrmClient(&afterReset, *reset);
  QVERIFY(!calculateDrmFdinfoUtilization(initial, afterReset, 1'000)
               .value(QStringLiteral("0000:00:02.0"))
               .has_value());

  const DrmFdinfoSnapshot held = advanceDrmFdinfoBaseline(initial, afterReset);
  const auto belowHighWatermark =
      parseDrmFdinfo("drm-client-id: 7\n"
                     "drm-pdev: 0000:00:02.0\n"
                     "drm-cycles-render: 90 ns\n"
                     "drm-total-cycles-render: 900 ns\n");
  DrmFdinfoSnapshot catchingUp;
  insertDrmClient(&catchingUp, *belowHighWatermark);
  QVERIFY(!calculateDrmFdinfoUtilization(held, catchingUp, 1'000)
               .value(QStringLiteral("0000:00:02.0"))
               .has_value());

  const DrmFdinfoSnapshot heldAgain =
      advanceDrmFdinfoBaseline(held, catchingUp);
  const auto heldIterator = heldAgain.constBegin();
  const DrmClientCounters &heldClient = heldIterator.value();
  QCOMPARE(heldClient.engines.value(QStringLiteral("render")).busy,
           quint64(100));
  QCOMPARE(heldClient.engines.value(QStringLiteral("render")).total,
           quint64(1000));
}

void SysmonCoreTest::drmFdinfoDeduplicatesClientsAndAggregatesPerEngine() {
  const auto client = [](int id, quint64 busy, quint64 total) {
    return parseDrmFdinfo(QStringLiteral("drm-client-id: %1\n"
                                         "drm-pdev: 0000:00:02.0\n"
                                         "drm-cycles-render: %2 ns\n"
                                         "drm-total-cycles-render: %3 ns\n")
                              .arg(id)
                              .arg(busy)
                              .arg(total)
                              .toUtf8());
  };
  DrmFdinfoSnapshot before;
  insertDrmClient(&before, *client(1, 100, 1000));
  insertDrmClient(&before, *client(2, 200, 1000));
  DrmFdinfoSnapshot after;
  insertDrmClient(&after, *client(1, 120, 1100));
  insertDrmClient(&after, *client(1, 120, 1100)); // duplicated fd
  insertDrmClient(&after, *client(2, 230, 1100));
  const OptionalNumber utilization =
      calculateDrmFdinfoUtilization(before, after, 100)
          .value(QStringLiteral("0000:00:02.0"));
  QVERIFY(utilization.has_value());
  QCOMPARE(*utilization, 50.0);
}

void SysmonCoreTest::drmFdinfoUsesBusiestEngineInsteadOfSumming() {
  const auto sample = [](quint64 render, quint64 copy, quint64 total) {
    return parseDrmFdinfo(QStringLiteral("drm-client-id: 9\n"
                                         "drm-pdev: 0000:00:02.0\n"
                                         "drm-cycles-render: %1 ns\n"
                                         "drm-total-cycles-render: %3 ns\n"
                                         "drm-cycles-copy: %2 ns\n"
                                         "drm-total-cycles-copy: %3 ns\n")
                              .arg(render)
                              .arg(copy)
                              .arg(total)
                              .toUtf8());
  };
  DrmFdinfoSnapshot before;
  insertDrmClient(&before, *sample(100, 200, 1000));
  DrmFdinfoSnapshot after;
  insertDrmClient(&after, *sample(170, 260, 1100));
  const OptionalNumber utilization =
      calculateDrmFdinfoUtilization(before, after, 100)
          .value(QStringLiteral("0000:00:02.0"));
  QVERIFY(utilization.has_value());
  QCOMPARE(*utilization, 70.0);
}

void SysmonCoreTest::drmFdinfoHonorsEngineCapacity() {
  const auto sample = [](quint64 busy, quint64 total) {
    return parseDrmFdinfo(QStringLiteral("drm-client-id: 5\n"
                                         "drm-pdev: 0000:00:02.0\n"
                                         "drm-cycles-ccs: %1\n"
                                         "drm-total-cycles-ccs: %2\n"
                                         "drm-engine-capacity-ccs: 4\n")
                              .arg(busy)
                              .arg(total)
                              .toUtf8());
  };
  DrmFdinfoSnapshot before;
  insertDrmClient(&before, *sample(100, 1000));
  DrmFdinfoSnapshot after;
  insertDrmClient(&after, *sample(300, 1100));
  const OptionalNumber utilization =
      calculateDrmFdinfoUtilization(before, after, 100)
          .value(QStringLiteral("0000:00:02.0"));
  QVERIFY(utilization.has_value());
  QCOMPARE(*utilization, 50.0);
}

void SysmonCoreTest::gpuProviderFallbackAndHybridIdentityAreStable() {
  GpuInfo intel;
  intel.pciId = QStringLiteral("0000:00:02.0");
  intel.id = stableGpuId(intel.pciId, {});
  intel.provider = QStringLiteral("drm-fdinfo");
  intel.utilizationPercent = 20.0;
  GpuInfo nvidia;
  nvidia.pciId = QStringLiteral("0000:01:00.0");
  nvidia.id = stableGpuId(nvidia.pciId, {});
  nvidia.provider = QStringLiteral("nvml");

  const QVector<GpuInfo> fallbackOnly = mergeAndSortGpus({{}, {intel}});
  QCOMPARE(fallbackOnly.size(), 1);
  QCOMPARE(fallbackOnly.first().id, QStringLiteral("pci:0000:00:02.0"));
  const QVector<GpuInfo> hybrid = mergeAndSortGpus({{nvidia}, {intel}});
  QCOMPARE(hybrid.size(), 2);
  QVERIFY(hybrid.at(0).id != hybrid.at(1).id);
  QCOMPARE(stableGpuId(QStringLiteral("00:02.0"), {}), intel.id);
  QCOMPARE(stableGpuId({}, QStringLiteral("nvml:GPU-fixture")),
           QStringLiteral("nvml:GPU-fixture"));

  GpuInfo nvidiaSysfs = nvidia;
  nvidiaSysfs.provider = QStringLiteral("drm-sysfs");
  nvidiaSysfs.temperatureCelsius = 42.0;
  const QVector<GpuInfo> primaryProvider =
      mergeAndSortGpus({{nvidia}, {nvidiaSysfs}});
  QCOMPARE(primaryProvider.size(), 1);
  QCOMPARE(primaryProvider.first().provider, QStringLiteral("nvml"));
  QVERIFY(!primaryProvider.first().temperatureCelsius.has_value());
}

QTEST_MAIN(SysmonCoreTest)
#include "sysmon_core_test.moc"
