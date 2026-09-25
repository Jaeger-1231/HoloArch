#include "gpu_manager.h"

#include "drm_helpers.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace Clavis::Sysmon {

namespace {

QByteArray readAll(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
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

OptionalNumber readDpmFrequencyMHz(const QString &path) {
  const QRegularExpression pattern(QStringLiteral(R"((\d+(?:\.\d+)?)\s*MHz\b)"),
                                   QRegularExpression::CaseInsensitiveOption);
  OptionalNumber fallback;
  for (const QString &line : readText(path).split(QLatin1Char('\n'))) {
    const QRegularExpressionMatch match = pattern.match(line);
    if (!match.hasMatch())
      continue;
    bool ok = false;
    const double value = match.captured(1).toDouble(&ok);
    if (!ok)
      continue;
    if (line.contains(QLatin1Char('*')))
      return value;
    if (!fallback)
      fallback = value;
  }
  return fallback;
}

QString vendorName(const QString &raw) {
  const QString value = raw.trimmed().toLower();
  if (value == QStringLiteral("0x10de"))
    return QStringLiteral("NVIDIA");
  if (value == QStringLiteral("0x1002"))
    return QStringLiteral("AMD");
  if (value == QStringLiteral("0x8086"))
    return QStringLiteral("Intel");
  return raw.trimmed();
}

QString driverName(const QString &devicePath) {
  const QString link =
      QFileInfo(devicePath + QStringLiteral("/driver")).symLinkTarget();
  if (!link.isEmpty())
    return QFileInfo(link).fileName();
  for (const QByteArray &raw :
       readAll(devicePath + QStringLiteral("/uevent")).split('\n')) {
    if (raw.startsWith("DRIVER="))
      return QString::fromUtf8(raw.mid(7)).trimmed();
  }
  return {};
}

QString pciIdForDevice(const QString &devicePath) {
  for (const QByteArray &raw :
       readAll(devicePath + QStringLiteral("/uevent")).split('\n')) {
    if (raw.startsWith("PCI_SLOT_NAME="))
      return normalizePciId(QString::fromUtf8(raw.mid(14)));
  }
  const QString canonical = QFileInfo(devicePath).canonicalFilePath();
  const QRegularExpression pattern(QStringLiteral(
      R"((?:^|/)([0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7])(?:/|$))"));
  const QRegularExpressionMatch match = pattern.match(canonical);
  return match.hasMatch() ? normalizePciId(match.captured(1)) : QString();
}

class DrmSysfsProvider final : public GpuProvider {
public:
  QVector<GpuInfo> sample(QVector<Error> *) override {
    if (!m_discoveryTimer.isValid() || m_discoveryTimer.elapsed() >= 5000)
      refreshDevices();
    const bool hasIntel = std::any_of(
        m_devices.cbegin(), m_devices.cend(), [](const DrmDevice &device) {
          return device.metadata.vendor == QStringLiteral("Intel");
        });
    DrmFdinfoSnapshot fdinfo = hasIntel ? readFdinfo() : DrmFdinfoSnapshot{};
    QHash<QString, OptionalNumber> intelUtilization;
    if (m_timer.isValid()) {
      intelUtilization = calculateDrmFdinfoUtilization(
          m_previousFdinfo, fdinfo,
          static_cast<quint64>(m_timer.nsecsElapsed()));
    }
    m_previousFdinfo = advanceDrmFdinfoBaseline(m_previousFdinfo, fdinfo);
    m_timer.restart();

    QVector<GpuInfo> result;
    result.reserve(m_devices.size());
    for (const DrmDevice &device : std::as_const(m_devices)) {
      const QString &devicePath = device.path;
      GpuInfo gpu = device.metadata;
      gpu.utilizationPercent.reset();
      gpu.temperatureCelsius.reset();
      gpu.vramTotalBytes.reset();
      gpu.vramUsedBytes.reset();
      gpu.powerWatts.reset();
      gpu.frequencyMHz.reset();
      gpu.utilizationSource.clear();
      gpu.available = QFileInfo::exists(devicePath);
      if (!gpu.available) {
        updateGpuCapabilities(&gpu);
        result.push_back(gpu);
        continue;
      }

      gpu.utilizationPercent =
          readNumber(devicePath + QStringLiteral("/gpu_busy_percent"));
      if (gpu.utilizationPercent) {
        gpu.utilizationSource = QStringLiteral("sysfs-device");
      } else if (gpu.vendor == QStringLiteral("Intel")) {
        gpu.utilizationPercent = intelUtilization.value(gpu.pciId);
        if (gpu.utilizationPercent) {
          gpu.provider = QStringLiteral("drm-fdinfo");
          gpu.utilizationSource = QStringLiteral("drm-fdinfo-engine-max");
        }
      }
      gpu.frequencyMHz =
          readNumber(devicePath + QStringLiteral("/gt_cur_freq_mhz"));
      if (!gpu.frequencyMHz)
        gpu.frequencyMHz =
            readDpmFrequencyMHz(devicePath + QStringLiteral("/pp_dpm_sclk"));
      gpu.vramTotalBytes =
          readInteger(devicePath + QStringLiteral("/mem_info_vram_total"));
      gpu.vramUsedBytes =
          readInteger(devicePath + QStringLiteral("/mem_info_vram_used"));

      const QDir hwmon(devicePath + QStringLiteral("/hwmon"));
      for (const QString &name :
           hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = hwmon.absoluteFilePath(name);
        if (!gpu.temperatureCelsius)
          gpu.temperatureCelsius =
              readNumber(path + QStringLiteral("/temp1_input"), 0.001);
        if (!gpu.powerWatts)
          gpu.powerWatts =
              readNumber(path + QStringLiteral("/power1_average"), 0.000001);
        if (!gpu.powerWatts)
          gpu.powerWatts =
              readNumber(path + QStringLiteral("/power1_input"), 0.000001);
      }
      updateGpuCapabilities(&gpu);
      result.push_back(gpu);
    }
    return result;
  }

private:
  struct DrmDevice {
    QString path;
    GpuInfo metadata;
  };

  void refreshDevices() {
    QHash<QString, DrmDevice> discovered;
    const QDir drm(QStringLiteral("/sys/class/drm"));
    const QRegularExpression cardPattern(QStringLiteral("^card\\d+$"));
    for (const QString &entry :
         drm.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      if (!cardPattern.match(entry).hasMatch())
        continue;
      const QString devicePath =
          drm.absoluteFilePath(entry) + QStringLiteral("/device");
      if (!QFileInfo::exists(devicePath))
        continue;
      const QString pciId = pciIdForDevice(devicePath);
      const QString id =
          stableGpuId(pciId, QFileInfo(devicePath).canonicalFilePath());
      if (id.isEmpty() || discovered.contains(id))
        continue;
      DrmDevice device;
      device.path = devicePath;
      device.metadata.available = true;
      device.metadata.id = id;
      device.metadata.pciId = pciId;
      device.metadata.vendor =
          vendorName(readText(devicePath + QStringLiteral("/vendor")));
      if (device.metadata.vendor.isEmpty())
        device.metadata.vendor = QStringLiteral("Unknown");
      device.metadata.driver = driverName(devicePath);
      device.metadata.name =
          device.metadata.driver.isEmpty()
              ? QStringLiteral("%1 GPU").arg(device.metadata.vendor)
              : QStringLiteral("%1 (%2)").arg(device.metadata.vendor,
                                              device.metadata.driver);
      device.metadata.provider = device.metadata.vendor == QStringLiteral("AMD")
                                     ? QStringLiteral("amd-sysfs")
                                     : QStringLiteral("drm-sysfs");
      discovered.insert(id, device);
    }
    m_devices = discovered.values();
    m_discoveryTimer.restart();
  }

  DrmFdinfoSnapshot readFdinfo() const {
    DrmFdinfoSnapshot result;
    const QDir proc(QStringLiteral("/proc"));
    const QRegularExpression pidPattern(QStringLiteral("^\\d+$"));
    for (const QString &pid :
         proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      if (!pidPattern.match(pid).hasMatch())
        continue;
      const QDir directory(
          proc.absoluteFilePath(pid + QStringLiteral("/fdinfo")));
      for (const QString &fd :
           directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        const auto client =
            parseDrmFdinfo(readAll(directory.absoluteFilePath(fd)));
        if (client)
          insertDrmClient(&result, *client);
      }
    }
    return result;
  }

  QElapsedTimer m_timer;
  QElapsedTimer m_discoveryTimer;
  DrmFdinfoSnapshot m_previousFdinfo;
  QVector<DrmDevice> m_devices;
};

void mergeMetric(OptionalNumber *destination, const OptionalNumber &source) {
  if (!*destination && source)
    *destination = source;
}

void mergeMetric(OptionalInteger *destination, const OptionalInteger &source) {
  if (!*destination && source)
    *destination = source;
}

} // namespace

QString normalizePciId(const QString &value) {
  QString result = value.trimmed().toLower();
  if (result.startsWith(QStringLiteral("pci:")))
    result.remove(0, 4);
  const QRegularExpression shortPattern(
      QStringLiteral(R"(^([0-9a-f]{2}):([0-9a-f]{2})\.([0-7])$)"));
  const QRegularExpressionMatch shortMatch = shortPattern.match(result);
  if (shortMatch.hasMatch())
    result.prepend(QStringLiteral("0000:"));
  const QRegularExpression fullPattern(
      QStringLiteral(R"(^[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\.[0-7]$)"));
  return fullPattern.match(result).hasMatch() ? result : QString();
}

QString stableGpuId(const QString &pciId, const QString &fallback) {
  const QString normalized = normalizePciId(pciId);
  if (!normalized.isEmpty())
    return QStringLiteral("pci:") + normalized;
  if (fallback.isEmpty())
    return {};
  if (fallback.startsWith(QStringLiteral("nvml:")) ||
      fallback.startsWith(QStringLiteral("drm:"))) {
    return fallback;
  }
  return QStringLiteral("drm:") + fallback;
}

void updateGpuCapabilities(GpuInfo *gpu) {
  const auto sanitize = [](OptionalNumber *value, bool nonNegative) {
    if (*value && (!std::isfinite(**value) || (nonNegative && **value < 0.0)))
      value->reset();
  };
  sanitize(&gpu->utilizationPercent, true);
  sanitize(&gpu->temperatureCelsius, false);
  sanitize(&gpu->powerWatts, true);
  sanitize(&gpu->frequencyMHz, true);
  if (gpu->utilizationPercent)
    gpu->utilizationPercent = std::clamp(*gpu->utilizationPercent, 0.0, 100.0);
  if (!gpu->utilizationPercent)
    gpu->utilizationSource.clear();
  if (!gpu->vramTotalBytes || !gpu->vramUsedBytes || *gpu->vramTotalBytes < 0 ||
      *gpu->vramUsedBytes < 0) {
    gpu->vramTotalBytes.reset();
    gpu->vramUsedBytes.reset();
  }
  gpu->capabilities.utilization = gpu->utilizationPercent.has_value();
  gpu->capabilities.temperature = gpu->temperatureCelsius.has_value();
  gpu->capabilities.memory =
      gpu->vramTotalBytes.has_value() && gpu->vramUsedBytes.has_value();
  gpu->capabilities.power = gpu->powerWatts.has_value();
  gpu->capabilities.frequency = gpu->frequencyMHz.has_value();
  gpu->supported = gpu->capabilities.utilization ||
                   gpu->capabilities.temperature || gpu->capabilities.memory ||
                   gpu->capabilities.power || gpu->capabilities.frequency;
}

QVector<GpuInfo>
mergeAndSortGpus(const QVector<QVector<GpuInfo>> &providerResults) {
  QHash<QString, GpuInfo> merged;
  for (const QVector<GpuInfo> &devices : providerResults) {
    for (GpuInfo gpu : devices) {
      gpu.pciId = normalizePciId(gpu.pciId);
      if (gpu.id.isEmpty())
        gpu.id = stableGpuId(gpu.pciId, gpu.name);
      if (gpu.id.isEmpty())
        continue;
      auto existing = merged.find(gpu.id);
      if (existing == merged.end()) {
        merged.insert(gpu.id, gpu);
        continue;
      }
      existing->available = existing->available || gpu.available;
      if (existing->name.isEmpty())
        existing->name = gpu.name;
      if (existing->vendor.isEmpty())
        existing->vendor = gpu.vendor;
      if (existing->driver.isEmpty())
        existing->driver = gpu.driver;
      // Provider order expresses priority. Do not silently fill a primary
      // provider's unavailable fields from a different provider while
      // continuing to label the resulting sample as the primary one.
      if (existing->provider != gpu.provider)
        continue;
      mergeMetric(&existing->utilizationPercent, gpu.utilizationPercent);
      mergeMetric(&existing->temperatureCelsius, gpu.temperatureCelsius);
      mergeMetric(&existing->vramTotalBytes, gpu.vramTotalBytes);
      mergeMetric(&existing->vramUsedBytes, gpu.vramUsedBytes);
      mergeMetric(&existing->powerWatts, gpu.powerWatts);
      mergeMetric(&existing->frequencyMHz, gpu.frequencyMHz);
      if (existing->utilizationSource.isEmpty())
        existing->utilizationSource = gpu.utilizationSource;
    }
  }
  QVector<GpuInfo> result = merged.values();
  for (GpuInfo &gpu : result)
    updateGpuCapabilities(&gpu);
  std::sort(result.begin(), result.end(),
            [](const GpuInfo &left, const GpuInfo &right) {
              return left.id < right.id;
            });
  return result;
}

std::unique_ptr<GpuProvider> createDrmSysfsProvider() {
  return std::make_unique<DrmSysfsProvider>();
}

GpuManager::GpuManager() {
  m_providers.push_back(createNvidiaNvmlProvider());
  m_providers.push_back(createDrmSysfsProvider());
}

GpuManager::~GpuManager() = default;

QVector<GpuInfo> GpuManager::sample(QVector<Error> *errors) {
  QVector<QVector<GpuInfo>> samples;
  samples.reserve(static_cast<qsizetype>(m_providers.size()));
  for (const auto &provider : m_providers)
    samples.push_back(provider->sample(errors));
  return mergeAndSortGpus(samples);
}

} // namespace Clavis::Sysmon
