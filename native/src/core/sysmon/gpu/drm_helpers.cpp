#include "drm_helpers.h"

#include "gpu_provider.h"

#include <QRegularExpression>

#include <algorithm>

namespace Clavis::Sysmon {

namespace {

QString clientKey(const DrmClientCounters &client) {
  return client.pciId + QLatin1Char('|') + client.clientId;
}

} // namespace

std::optional<DrmClientCounters> parseDrmFdinfo(const QByteArray &contents,
                                                const QString &fallbackPciId) {
  DrmClientCounters result;
  result.pciId = normalizePciId(fallbackPciId);
  const QRegularExpression counterPattern(QStringLiteral(
      R"(^drm-(total-cycles|engine-capacity|cycles|engine)-([^:]+):\s*(\d+))"));
  for (const QByteArray &rawLine : contents.split('\n')) {
    const QString line = QString::fromUtf8(rawLine).trimmed();
    const qsizetype colon = line.indexOf(QLatin1Char(':'));
    if (colon > 0) {
      const QString key = line.left(colon);
      const QString value = line.mid(colon + 1).trimmed();
      if (key == QStringLiteral("drm-client-id"))
        result.clientId = value;
      else if (key == QStringLiteral("drm-pdev"))
        result.pciId = normalizePciId(value);
    }

    const QRegularExpressionMatch match = counterPattern.match(line);
    if (!match.hasMatch())
      continue;
    bool ok = false;
    const quint64 value = match.captured(3).toULongLong(&ok);
    if (!ok)
      continue;
    DrmEngineCounter &engine = result.engines[match.captured(2)];
    const QString counterKind = match.captured(1);
    if (counterKind == QStringLiteral("total-cycles")) {
      engine.total = value;
      engine.hasTotal = true;
    } else if (counterKind == QStringLiteral("engine-capacity")) {
      if (value > 0)
        engine.capacity = value;
    } else if (counterKind == QStringLiteral("cycles")) {
      engine.busy = value;
      engine.busyIsCycles = true;
    } else {
      // drm-engine-* is the legacy i915 busy-time spelling.
      if (!engine.busyIsCycles)
        engine.busy = value;
    }
  }
  if (result.pciId.isEmpty() || result.clientId.isEmpty() ||
      result.engines.isEmpty())
    return std::nullopt;
  return result;
}

void insertDrmClient(DrmFdinfoSnapshot *snapshot,
                     const DrmClientCounters &client) {
  const QString key = clientKey(client);
  auto existing = snapshot->find(key);
  if (existing == snapshot->end()) {
    snapshot->insert(key, client);
    return;
  }
  // The kernel can expose the same drm-client-id through duplicated fds. Keep
  // one monotonically greatest view instead of counting the client twice.
  for (auto iterator = client.engines.cbegin();
       iterator != client.engines.cend(); ++iterator) {
    DrmEngineCounter &counter = existing->engines[iterator.key()];
    if (counter.busyIsCycles == iterator->busyIsCycles) {
      counter.busy = std::max(counter.busy, iterator->busy);
    } else if (iterator->busyIsCycles) {
      counter.busy = iterator->busy;
      counter.busyIsCycles = true;
    }
    counter.capacity = std::max(counter.capacity, iterator->capacity);
    if (iterator->hasTotal &&
        (!counter.hasTotal || iterator->total > counter.total)) {
      counter.total = iterator->total;
      counter.hasTotal = true;
    }
  }
}

QHash<QString, OptionalNumber>
calculateDrmFdinfoUtilization(const DrmFdinfoSnapshot &previous,
                              const DrmFdinfoSnapshot &current,
                              quint64 elapsedNanoseconds) {
  struct EngineDelta {
    long double busy = 0;
    quint64 greatestTotal = 0;
    quint64 capacity = 1;
    bool hasTotal = false;
    bool busyIsCycles = false;
  };
  QHash<QString, QHash<QString, EngineDelta>> devices;
  for (auto currentIt = current.cbegin(); currentIt != current.cend();
       ++currentIt) {
    const auto previousIt = previous.constFind(currentIt.key());
    if (previousIt == previous.cend())
      continue;
    for (auto engineIt = currentIt->engines.cbegin();
         engineIt != currentIt->engines.cend(); ++engineIt) {
      const auto before = previousIt->engines.constFind(engineIt.key());
      if (before == previousIt->engines.cend() ||
          engineIt->busyIsCycles != before->busyIsCycles ||
          engineIt->busy < before->busy)
        continue;
      if (engineIt->busyIsCycles && (!engineIt->hasTotal || !before->hasTotal ||
                                     engineIt->total < before->total))
        continue;
      EngineDelta &delta = devices[currentIt->pciId][engineIt.key()];
      delta.busy += static_cast<long double>(engineIt->busy - before->busy);
      delta.busyIsCycles = engineIt->busyIsCycles;
      delta.capacity = std::max(delta.capacity, engineIt->capacity);
      if (engineIt->hasTotal && before->hasTotal &&
          engineIt->total >= before->total) {
        delta.hasTotal = true;
        delta.greatestTotal =
            std::max(delta.greatestTotal, engineIt->total - before->total);
      }
    }
  }

  QHash<QString, OptionalNumber> result;
  for (auto deviceIt = devices.cbegin(); deviceIt != devices.cend();
       ++deviceIt) {
    OptionalNumber busiest;
    for (auto engineIt = deviceIt->cbegin(); engineIt != deviceIt->cend();
         ++engineIt) {
      const quint64 baseDenominator =
          engineIt->busyIsCycles ? engineIt->greatestTotal : elapsedNanoseconds;
      if (baseDenominator == 0 || engineIt->capacity == 0)
        continue;
      const long double denominator =
          static_cast<long double>(baseDenominator) * engineIt->capacity;
      const double utilization =
          std::clamp(static_cast<double>(engineIt->busy * 100.0L / denominator),
                     0.0, 100.0);
      if (!busiest || utilization > *busiest)
        busiest = utilization;
    }
    result.insert(deviceIt.key(), busiest);
  }
  return result;
}

DrmFdinfoSnapshot advanceDrmFdinfoBaseline(const DrmFdinfoSnapshot &previous,
                                           const DrmFdinfoSnapshot &current) {
  DrmFdinfoSnapshot result = current;
  for (auto clientIt = result.begin(); clientIt != result.end(); ++clientIt) {
    const auto previousClient = previous.constFind(clientIt.key());
    if (previousClient == previous.cend())
      continue;
    for (auto engineIt = clientIt->engines.begin();
         engineIt != clientIt->engines.end(); ++engineIt) {
      const auto previousEngine =
          previousClient->engines.constFind(engineIt.key());
      if (previousEngine == previousClient->engines.cend() ||
          previousEngine->busyIsCycles != engineIt->busyIsCycles) {
        continue;
      }
      engineIt->busy = std::max(engineIt->busy, previousEngine->busy);
      if (engineIt->hasTotal && previousEngine->hasTotal)
        engineIt->total = std::max(engineIt->total, previousEngine->total);
    }
  }
  return result;
}

} // namespace Clavis::Sysmon
