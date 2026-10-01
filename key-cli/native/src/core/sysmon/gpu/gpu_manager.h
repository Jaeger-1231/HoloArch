#pragma once

#include "gpu_provider.h"

#include <memory>
#include <vector>

namespace Clavis::Sysmon {

class GpuManager {
public:
  GpuManager();
  ~GpuManager();

  QVector<GpuInfo> sample(QVector<Error> *errors);

private:
  std::vector<std::unique_ptr<GpuProvider>> m_providers;
};

std::unique_ptr<GpuProvider> createNvidiaNvmlProvider();
std::unique_ptr<GpuProvider> createDrmSysfsProvider();

} // namespace Clavis::Sysmon
