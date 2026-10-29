// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QLockFile>
#include <QString>

namespace opentalk::storage {

/// Makes sure only one copy of OpenTalk uses a data folder at a time (two would
/// overwrite each other's settings), and tells whether the previous copy ended without
/// closing properly, for example because it crashed or the computer lost power.
///
/// The lock is the file "opentalk.lock" in the data folder, held until this object is
/// destroyed. A lock left by a process that no longer exists is taken over.
class DataFolderLock {
public:
    enum class Result {
        Acquired,
        AcquiredAfterUnexpectedExit, ///< The previous copy did not close properly.
        InUse,                       ///< Another running copy uses this folder.
    };

    explicit DataFolderLock(const QString &dataRoot);
    Result acquire();

private:
    QLockFile m_lock;
};

} // namespace opentalk::storage
