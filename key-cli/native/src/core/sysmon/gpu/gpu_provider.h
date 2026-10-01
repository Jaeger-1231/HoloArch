#pragma once

#include "../types.h"

#include <QVector>

namespace Clavis::Sysmon {

class GpuProvider {
public:
  virtual ~GpuProvider() = default;
  virtual QVector<GpuInfo> sample(QVector<Error> *errors) = 0;
};

void updateGpuCapabilities(GpuInfo *gpu);
QString normalizePciId(const QString &value);
QString stableGpuId(const QString &pciId, const QString &fallback);
QVector<GpuInfo>
mergeAndSortGpus(const QVector<QVector<GpuInfo>> &providerResults);

} // namespace Clavis::Sysmon
