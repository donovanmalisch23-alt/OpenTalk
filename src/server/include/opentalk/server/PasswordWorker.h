// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "opentalk/security/PasswordHash.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QThreadPool>

#include <functional>
#include <optional>

namespace opentalk::server {

/// Runs Argon2id hashing off the server's event-loop thread. Each operation takes tens
/// to hundreds of milliseconds and about 64 MiB of memory, so it must never block the
/// server, and the number running at once is capped to bound memory use.
///
/// Callbacks run on the thread that owns this object, and only if `context` still exists.
class PasswordWorker : public QObject {
    Q_OBJECT

public:
    explicit PasswordWorker(security::PasswordHashCost cost, int maxThreads = 2, QObject *parent = nullptr);
    /// Waits for running operations; their callbacks are then dropped.
    ~PasswordWorker() override;

    void verify(const QByteArray &storedHash, const QString &password, QObject *context,
                std::function<void(bool matches)> done);
    void hash(const QString &password, QObject *context, std::function<void(std::optional<QByteArray> hash)> done);

    security::PasswordHashCost cost() const { return m_cost; }

private:
    security::PasswordHashCost m_cost;
    QThreadPool m_pool;
};

} // namespace opentalk::server
