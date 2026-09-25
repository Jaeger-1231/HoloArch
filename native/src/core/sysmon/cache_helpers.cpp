#include "cache_helpers.h"

#include <QRegularExpression>

#include <algorithm>
#include <limits>

namespace Clavis::Sysmon {

qint64 nextCadenceDeadlineMs(qint64 previousDeadlineMs, qint64 nowMs,
                             qint64 intervalMs) {
  if (intervalMs <= 0)
    return nowMs;
  if (previousDeadlineMs < 0)
    previousDeadlineMs = 0;
  if (previousDeadlineMs > std::numeric_limits<qint64>::max() - intervalMs)
    return std::numeric_limits<qint64>::max();

  qint64 next = previousDeadlineMs + intervalMs;
  if (next > nowMs)
    return next;
  const qint64 missed = (nowMs - next) / intervalMs + 1;
  if (missed > (std::numeric_limits<qint64>::max() - next) / intervalMs)
    return std::numeric_limits<qint64>::max();
  return next + missed * intervalMs;
}

QStringList normalizedCpuPolicyNames(const QStringList &entries) {
  static const QRegularExpression pattern(QStringLiteral("^policy([0-9]+)$"));
  QStringList result;
  for (const QString &entry : entries) {
    if (pattern.match(entry).hasMatch() && !result.contains(entry))
      result.push_back(entry);
  }
  std::sort(result.begin(), result.end(),
            [](const QString &left, const QString &right) {
              return left.mid(6).toInt() < right.mid(6).toInt();
            });
  return result;
}

QString networkInterfaceIdentity(const QString &name, int ifIndex) {
  return name + QLatin1Char('#') + QString::number(std::max(0, ifIndex));
}

bool processIdentityMatches(quint64 cachedStartTicks,
                            quint64 currentStartTicks) {
  return cachedStartTicks > 0 && cachedStartTicks == currentStartTicks;
}

bool topologyCacheNeedsRefresh(const QByteArray &cachedFingerprint,
                               const QByteArray &currentFingerprint,
                               bool cachedPathsPresent) {
  return !cachedPathsPresent || cachedFingerprint.isEmpty() ||
         cachedFingerprint != currentFingerprint;
}

} // namespace Clavis::Sysmon
