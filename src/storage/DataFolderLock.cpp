// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/storage/DataFolderLock.h"

namespace opentalk::storage {

DataFolderLock::DataFolderLock(const QString &dataRoot)
    : m_lock(dataRoot + QStringLiteral("/opentalk.lock"))
{
    // Never stale because of its age (OpenTalk may run for weeks); only when the process
    // that holds it is gone.
    m_lock.setStaleLockTime(0);
}

DataFolderLock::Result DataFolderLock::acquire()
{
    qint64 pid = 0;
    QString host;
    QString application;
    const bool leftBehind = m_lock.getLockInfo(&pid, &host, &application);
    if (!m_lock.tryLock(0)) {
        return Result::InUse;
    }
    return leftBehind ? Result::AcquiredAfterUnexpectedExit : Result::Acquired;
}

} // namespace opentalk::storage
