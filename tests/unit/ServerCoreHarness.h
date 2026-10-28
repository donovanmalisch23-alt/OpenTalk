// SPDX-License-Identifier: GPL-3.0-only
// Drives ServerCore directly: no sockets, a real SQLite database in a temporary folder,
// real (but minimum-cost) Argon2id hashing, and fake clocks.
#pragma once

#include "opentalk/security/PasswordHash.h"
#include "opentalk/server/PasswordWorker.h"
#include "opentalk/server/ServerCore.h"
#include "opentalk/server/ServerDatabase.h"

#include <QTemporaryDir>
#include <QTest>

#include <type_traits>

namespace opentalk::test {

using namespace opentalk::protocol;
using namespace opentalk::server;

inline constexpr qint64 kWallClock = 1'790'000'000'000;

/// The list-style answers that replace RequestDone for some requests.
inline bool answersRequest(const Message &m, quint64 requestId)
{
    return std::visit(
        [requestId](const auto &x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, ListResult> || std::is_same_v<T, AuditPage> ||
                          std::is_same_v<T, ServerSettingsResult> || std::is_same_v<T, ServerStatsResult>) {
                return x.requestId == requestId;
            } else {
                return false;
            }
        },
        m);
}

struct Harness {
    QTemporaryDir dir;
    ServerDatabase db;
    PasswordWorker worker{security::PasswordHashCost::minimumForTests()};
    qint64 now = 0;
    qint64 wall = kWallClock;
    std::unique_ptr<ServerCore> core;
    QHash<quint32, QList<Message>> sent;
    QHash<quint32, std::pair<ErrorCode, QString>> closed;

    explicit Harness(ServerCore::Config config = guestsAllowed())
    {
        QVERIFY(db.open(dir.filePath(QStringLiteral("core.db"))));
        addAccount(QStringLiteral("admin"), Role::Admin);
        addAccount(QStringLiteral("alice"), Role::User);
        addAccount(QStringLiteral("bob"), Role::User);
        start(config);
    }

    static ServerCore::Config guestsAllowed()
    {
        ServerCore::Config c;
        c.allowGuests = true;
        return c;
    }

    void addAccount(const QString &name, Role role)
    {
        const auto hash =
            security::hashPassword(name + QStringLiteral("-pw"), security::PasswordHashCost::minimumForTests());
        QVERIFY(hash && db.createAccount(name, *hash, role));
    }

    void start(ServerCore::Config config)
    {
        core = std::make_unique<ServerCore>(config, &db, worker, [this] { return now; }, [this] { return wall; });
        QObject::connect(core.get(), &ServerCore::sendTo, [this](quint32 id, const Message &m) { sent[id].append(m); });
        QObject::connect(core.get(), &ServerCore::closeSession, [this](quint32 id, ErrorCode code, const QString &d) {
            closed.insert(id, {code, d});
            core->sessionEnded(id); // what the network layer does after closing
        });
        QVERIFY(core->loadChannels());
    }

    void send(quint32 id, const Message &m) { core->handleMessage(id, m); }

    /// Starts a session and logs in. Returns true once AuthResult arrived.
    bool login(quint32 id, const QString &name, bool guest = false, const QString &password = {},
               const QHostAddress &address = QHostAddress(QStringLiteral("192.0.2.1")))
    {
        core->sessionStarted(id, address);
        const QString pw = password.isNull() ? name + QStringLiteral("-pw") : password;
        send(id, Authenticate{guest ? Authenticate::Mode::Guest : Authenticate::Mode::Account, name, pw});
        return QTest::qWaitFor([&] { return has<AuthResult>(id) || closed.contains(id); }, 5000) && has<AuthResult>(id);
    }

    template <typename T> QList<T> all(quint32 id) const
    {
        QList<T> result;
        for (const Message &m : sent.value(id)) {
            if (const auto *t = std::get_if<T>(&m)) {
                result.append(*t);
            }
        }
        return result;
    }
    template <typename T> bool has(quint32 id) const { return !all<T>(id).isEmpty(); }
    template <typename T> std::optional<T> last(quint32 id) const
    {
        const QList<T> list = all<T>(id);
        return list.isEmpty() ? std::nullopt : std::optional<T>(list.last());
    }

    /// Waits for the answer to `requestId`: nullopt on success, else the error code.
    std::optional<ErrorCode> answer(quint32 id, quint64 requestId)
    {
        std::optional<ErrorCode> result;
        bool done = false;
        [[maybe_unused]] const bool finished = QTest::qWaitFor(
            [&] {
                for (const Message &m : sent.value(id)) {
                    if (const auto *d = std::get_if<RequestDone>(&m); d && d->requestId == requestId) {
                        done = true;
                    } else if (answersRequest(m, requestId)) {
                        done = true;
                    } else if (const auto *e = std::get_if<Error>(&m); e && e->requestId == requestId) {
                        done = true;
                        result = e->code;
                    }
                }
                return done;
            },
            5000);
        if (!done) {
            return ErrorCode::Timeout;
        }
        return result;
    }

    quint32 createdChannel(quint32 id, quint64 requestId) const
    {
        for (const Message &m : sent.value(id)) {
            if (const auto *d = std::get_if<RequestDone>(&m); d && d->requestId == requestId) {
                return d->resultId.value_or(0);
            }
        }
        return 0;
    }
};

#define EXPECT_OK(expr) QCOMPARE((expr), std::optional<ErrorCode>())
#define EXPECT_ERROR(expr, code) QVERIFY((expr) == std::optional<ErrorCode>(code))

} // namespace opentalk::test
