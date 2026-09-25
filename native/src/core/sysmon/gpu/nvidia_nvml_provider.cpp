#include "gpu_manager.h"

#include <QElapsedTimer>

#include <algorithm>
#include <array>
#include <dlfcn.h>

namespace Clavis::Sysmon {

namespace {

using NvmlReturn = int;
using NvmlDevice = void *;
constexpr NvmlReturn NvmlSuccess = 0;
constexpr int NvmlTemperatureGpu = 0;
constexpr int NvmlClockGraphics = 0;

struct NvmlUtilization {
  unsigned int gpu;
  unsigned int memory;
};

struct NvmlMemory {
  unsigned long long total;
  unsigned long long free;
  unsigned long long used;
};

struct NvmlPciInfo {
  char busIdLegacy[16];
  unsigned int domain;
  unsigned int bus;
  unsigned int device;
  unsigned int pciDeviceId;
  unsigned int pciSubSystemId;
  char busId[32];
};

template <typename Function> Function symbol(void *library, const char *name) {
  return reinterpret_cast<Function>(dlsym(library, name));
}

template <typename Function>
Function symbolEither(void *library, const char *preferred,
                      const char *fallback) {
  Function result = symbol<Function>(library, preferred);
  return result ? result : symbol<Function>(library, fallback);
}

class NvidiaNvmlProvider final : public GpuProvider {
public:
  ~NvidiaNvmlProvider() override {
    if (m_initialized && m_shutdown)
      m_shutdown();
    if (m_library)
      dlclose(m_library);
  }

  QVector<GpuInfo> sample(QVector<Error> *errors) override {
    if (!m_initialized && (!m_initializationTimer.isValid() ||
                           m_initializationTimer.elapsed() >= 5000))
      initialize(errors);
    if (!m_initialized)
      return {};
    if (!m_discoveryTimer.isValid() || m_discoveryTimer.elapsed() >= 5000)
      enumerateDevices();

    QVector<GpuInfo> result;
    result.reserve(m_devices.size());
    for (const Device &device : m_devices) {
      GpuInfo gpu = device.metadata;
      gpu.available = true;
      NvmlUtilization utilization{};
      if (m_getUtilization &&
          m_getUtilization(device.handle, &utilization) == NvmlSuccess)
        gpu.utilizationPercent =
            std::clamp<double>(utilization.gpu, 0.0, 100.0);
      unsigned int value = 0;
      if (m_getTemperature &&
          m_getTemperature(device.handle, NvmlTemperatureGpu, &value) ==
              NvmlSuccess)
        gpu.temperatureCelsius = value;
      NvmlMemory memory{};
      if (m_getMemory && m_getMemory(device.handle, &memory) == NvmlSuccess) {
        if (memory.total <=
            static_cast<unsigned long long>(std::numeric_limits<qint64>::max()))
          gpu.vramTotalBytes = static_cast<qint64>(memory.total);
        if (memory.used <=
            static_cast<unsigned long long>(std::numeric_limits<qint64>::max()))
          gpu.vramUsedBytes = static_cast<qint64>(memory.used);
      }
      if (m_getPower && m_getPower(device.handle, &value) == NvmlSuccess)
        gpu.powerWatts = static_cast<double>(value) / 1000.0;
      if (m_getClock &&
          m_getClock(device.handle, NvmlClockGraphics, &value) == NvmlSuccess)
        gpu.frequencyMHz = value;
      updateGpuCapabilities(&gpu);
      result.push_back(gpu);
    }
    return result;
  }

private:
  using Init = NvmlReturn (*)();
  using Shutdown = NvmlReturn (*)();
  using GetCount = NvmlReturn (*)(unsigned int *);
  using GetHandle = NvmlReturn (*)(unsigned int, NvmlDevice *);
  using GetName = NvmlReturn (*)(NvmlDevice, char *, unsigned int);
  using GetPciInfo = NvmlReturn (*)(NvmlDevice, NvmlPciInfo *);
  using GetUuid = NvmlReturn (*)(NvmlDevice, char *, unsigned int);
  using GetDriver = NvmlReturn (*)(char *, unsigned int);
  using GetUtilization = NvmlReturn (*)(NvmlDevice, NvmlUtilization *);
  using GetTemperature = NvmlReturn (*)(NvmlDevice, int, unsigned int *);
  using GetMemory = NvmlReturn (*)(NvmlDevice, NvmlMemory *);
  using GetPower = NvmlReturn (*)(NvmlDevice, unsigned int *);
  using GetClock = NvmlReturn (*)(NvmlDevice, int, unsigned int *);

  struct Device {
    NvmlDevice handle = nullptr;
    GpuInfo metadata;
  };

  void initialize(QVector<Error> *) {
    m_initializationTimer.restart();
    if (!m_library) {
      for (const char *name : {"libnvidia-ml.so.1", "libnvidia-ml.so"}) {
        m_library = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
        if (m_library)
          break;
      }
    }
    // A missing optional runtime is normal on non-NVIDIA systems.
    if (!m_library)
      return;

    m_init = symbolEither<Init>(m_library, "nvmlInit_v2", "nvmlInit");
    m_shutdown = symbol<Shutdown>(m_library, "nvmlShutdown");
    m_getCount = symbolEither<GetCount>(m_library, "nvmlDeviceGetCount_v2",
                                        "nvmlDeviceGetCount");
    m_getHandle =
        symbolEither<GetHandle>(m_library, "nvmlDeviceGetHandleByIndex_v2",
                                "nvmlDeviceGetHandleByIndex");
    m_getName = symbolEither<GetName>(m_library, "nvmlDeviceGetName_v2",
                                      "nvmlDeviceGetName");
    m_getPciInfo = symbolEither<GetPciInfo>(
        m_library, "nvmlDeviceGetPciInfo_v3", "nvmlDeviceGetPciInfo_v2");
    if (!m_getPciInfo)
      m_getPciInfo = symbol<GetPciInfo>(m_library, "nvmlDeviceGetPciInfo");
    m_getUuid = symbol<GetUuid>(m_library, "nvmlDeviceGetUUID");
    m_getDriver = symbol<GetDriver>(m_library, "nvmlSystemGetDriverVersion");
    m_getUtilization =
        symbol<GetUtilization>(m_library, "nvmlDeviceGetUtilizationRates");
    m_getTemperature =
        symbol<GetTemperature>(m_library, "nvmlDeviceGetTemperature");
    m_getMemory = symbol<GetMemory>(m_library, "nvmlDeviceGetMemoryInfo");
    m_getPower = symbol<GetPower>(m_library, "nvmlDeviceGetPowerUsage");
    m_getClock = symbol<GetClock>(m_library, "nvmlDeviceGetClockInfo");
    if (!m_init || !m_shutdown || !m_getCount || !m_getHandle ||
        (!m_getPciInfo && !m_getUuid)) {
      return;
    }
    if (m_init() != NvmlSuccess) {
      return;
    }
    m_initialized = true;
    std::array<char, 96> driverBuffer{};
    m_driver = m_getDriver && m_getDriver(driverBuffer.data(),
                                          driverBuffer.size()) == NvmlSuccess
                   ? QString::fromUtf8(driverBuffer.data())
                   : QString();
    enumerateDevices();
  }

  void enumerateDevices() {
    unsigned int count = 0;
    if (m_getCount(&count) != NvmlSuccess)
      return;
    QVector<Device> devices;
    for (unsigned int index = 0; index < count; ++index) {
      Device device;
      if (m_getHandle(index, &device.handle) != NvmlSuccess)
        continue;
      NvmlPciInfo pci{};
      if (m_getPciInfo && m_getPciInfo(device.handle, &pci) == NvmlSuccess) {
        device.metadata.pciId = normalizePciId(QString::fromLatin1(pci.busId));
        if (device.metadata.pciId.isEmpty()) {
          device.metadata.pciId =
              normalizePciId(QStringLiteral("%1:%2:%3.0")
                                 .arg(pci.domain, 4, 16, QLatin1Char('0'))
                                 .arg(pci.bus, 2, 16, QLatin1Char('0'))
                                 .arg(pci.device, 2, 16, QLatin1Char('0')));
        }
      }
      QString fallback;
      if (m_getUuid) {
        std::array<char, 96> uuid{};
        if (m_getUuid(device.handle, uuid.data(), uuid.size()) == NvmlSuccess)
          fallback = QStringLiteral("nvml:") + QString::fromLatin1(uuid.data());
      }
      device.metadata.id = stableGpuId(device.metadata.pciId, fallback);
      if (device.metadata.id.isEmpty())
        continue;
      std::array<char, 128> name{};
      if (m_getName &&
          m_getName(device.handle, name.data(), name.size()) == NvmlSuccess)
        device.metadata.name = QString::fromUtf8(name.data());
      device.metadata.vendor = QStringLiteral("NVIDIA");
      device.metadata.driver = m_driver;
      device.metadata.provider = QStringLiteral("nvml");
      device.metadata.utilizationSource = QStringLiteral("nvml-device");
      devices.push_back(device);
    }
    m_devices = std::move(devices);
    m_discoveryTimer.restart();
  }

  void *m_library = nullptr;
  bool m_initialized = false;
  QString m_driver;
  QElapsedTimer m_initializationTimer;
  QElapsedTimer m_discoveryTimer;
  Init m_init = nullptr;
  Shutdown m_shutdown = nullptr;
  GetCount m_getCount = nullptr;
  GetHandle m_getHandle = nullptr;
  GetName m_getName = nullptr;
  GetPciInfo m_getPciInfo = nullptr;
  GetUuid m_getUuid = nullptr;
  GetDriver m_getDriver = nullptr;
  GetUtilization m_getUtilization = nullptr;
  GetTemperature m_getTemperature = nullptr;
  GetMemory m_getMemory = nullptr;
  GetPower m_getPower = nullptr;
  GetClock m_getClock = nullptr;
  QVector<Device> m_devices;
};

} // namespace

std::unique_ptr<GpuProvider> createNvidiaNvmlProvider() {
  return std::make_unique<NvidiaNvmlProvider>();
}

} // namespace Clavis::Sysmon
