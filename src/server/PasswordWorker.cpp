// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/server/PasswordWorker.h"

#include <QMetaObject>
#include <QPointer>

namespace opentalk::server {

PasswordWorker::PasswordWorker(security::PasswordHashCost cost, int maxThreads, QObject *parent)
    : QObject(parent)
    , m_cost(cost)
{
    m_pool.setMaxThreadCount(maxThreads);
}

PasswordWorker::~PasswordWorker()
{
    m_pool.waitForDone();
}

void PasswordWorker::verify(const QByteArray &storedHash, const QString &password, QObject *context,
                            std::function<void(bool)> done)
{
    QPointer<QObject> guard(context);
    m_pool.start([this, storedHash, password, guard, done = std::move(done)] {
        const bool matches = security::verifyPassword(storedHash, password);
        // Deliver through this object (which outlives the pool's tasks), and check the
        // context on the owning thread, where checking a QPointer is safe.
        QMetaObject::invokeMethod(
            this,
            [guard, done, matches] {
                if (guard) {
                    done(matches);
                }
            },
            Qt::QueuedConnection);
    });
}

void PasswordWorker::hash(const QString &password, QObject *context,
                          std::function<void(std::optional<QByteArray>)> done)
{
    QPointer<QObject> guard(context);
    const security::PasswordHashCost cost = m_cost;
    m_pool.start([this, password, cost, guard, done = std::move(done)] {
        std::optional<QByteArray> result = security::hashPassword(password, cost);
        QMetaObject::invokeMethod(
            this,
            [guard, done, result] {
                if (guard) {
                    done(result);
                }
            },
            Qt::QueuedConnection);
    });
}

} // namespace opentalk::server
