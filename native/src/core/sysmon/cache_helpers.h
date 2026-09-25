#pragma once

#include <QString>
#include <QStringList>

namespace Clavis::Sysmon {

qint64 nextCadenceDeadlineMs(qint64 previousDeadlineMs, qint64 nowMs,
                             qint64 intervalMs);
QStringList normalizedCpuPolicyNames(const QStringList &entries);
QString networkInterfaceIdentity(const QString &name, int ifIndex);
bool processIdentityMatches(quint64 cachedStartTicks,
                            quint64 currentStartTicks);
bool topologyCacheNeedsRefresh(const QByteArray &cachedFingerprint,
                               const QByteArray &currentFingerprint,
                               bool cachedPathsPresent);

} // namespace Clavis::Sysmon
