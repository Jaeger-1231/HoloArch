#pragma once

#include "../types.h"

#include <QByteArray>
#include <QHash>
#include <QString>

namespace Clavis::Sysmon {

struct DrmEngineCounter {
  quint64 busy = 0;
  quint64 total = 0;
  quint64 capacity = 1;
  bool hasTotal = false;
  bool busyIsCycles = false;
};

struct DrmClientCounters {
  QString pciId;
  QString clientId;
  QHash<QString, DrmEngineCounter> engines;
};

using DrmFdinfoSnapshot = QHash<QString, DrmClientCounters>;

std::optional<DrmClientCounters>
parseDrmFdinfo(const QByteArray &contents, const QString &fallbackPciId = {});
void insertDrmClient(DrmFdinfoSnapshot *snapshot,
                     const DrmClientCounters &client);
QHash<QString, OptionalNumber>
calculateDrmFdinfoUtilization(const DrmFdinfoSnapshot &previous,
                              const DrmFdinfoSnapshot &current,
                              quint64 elapsedNanoseconds);
DrmFdinfoSnapshot advanceDrmFdinfoBaseline(const DrmFdinfoSnapshot &previous,
                                           const DrmFdinfoSnapshot &current);

} // namespace Clavis::Sysmon
