// SPDX-License-Identifier: GPL-3.0-only
// Feeds arbitrary bytes from one hostile client through a real ServerSession into
// ServerCore, with a second, well-behaved guest online to receive whatever the hostile
// client can cause to be broadcast. Checks that nothing crashes, that only messages
// meant for clients are ever sent, and that a closed session is never written to.

#include "opentalk/protocol/Session.h"
#include "opentalk/server/PasswordWorker.h"
#include "opentalk/server/ServerCore.h"

#include <QCoreApplication>
#include <QHash>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

using namespace opentalk;
using namespace opentalk::protocol;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static int argc = 1;
    static char name[] = "fuzz";
    static char *argv[] = {name, nullptr};
    static QCoreApplication app(argc, argv);
    if (size < 1) {
        return 0;
    }
    const size_t chunk = static_cast<size_t>(data[0] % 64u) + 1u;
    data += 1;
    size -= 1;

    static server::PasswordWorker passwords(security::PasswordHashCost::minimumForTests(), 1);
    qint64 now = 0;
    server::ServerCore::Config config;
    config.allowGuests = true;
    server::ServerCore core(config, nullptr, passwords, [&now] { return now; }, [] { return qint64(1); });

    ServerSession::Config sessionConfig;
    sessionConfig.serverName = QStringLiteral("fuzz");
    ServerSession hostile(sessionConfig, 0);
    bool hostileClosed = false;
    QObject::connect(&hostile, &Session::closed, [&](const CloseInfo &) {
        hostileClosed = true;
        core.sessionEnded(1);
    });
    QObject::connect(&hostile, &Session::applicationMessage, [&](const Message &m) { core.handleMessage(1, m); });
    QObject::connect(&hostile, &ServerSession::established, [&] { core.sessionStarted(1, QHostAddress::LocalHost); });

    bool badOutput = false;
    QObject::connect(&core, &server::ServerCore::sendTo, [&](quint32 id, const Message &m) {
        if (!isServerToClient(messageType(m)) || (id == 1 && hostileClosed) || (id != 1 && id != 2)) {
            badOutput = true;
        }
        if (id == 1) {
            hostile.sendMessage(m);
        }
    });
    QObject::connect(&core, &server::ServerCore::closeSession, [&](quint32 id, ErrorCode code, const QString &detail) {
        if (id == 1) {
            hostile.closeWithError(code, detail);
        }
    });

    // A bystander who is already online.
    core.sessionStarted(2, QHostAddress::LocalHost);
    core.handleMessage(2, Authenticate{Authenticate::Mode::Guest, QStringLiteral("Bystander"), QString()});

    for (size_t offset = 0; offset < size && !hostileClosed; offset += chunk) {
        const size_t n = (size - offset < chunk) ? size - offset : chunk;
        now += data[offset];
        hostile.receive(QByteArrayView(reinterpret_cast<const char *>(data + offset), static_cast<qsizetype>(n)), now);
        hostile.tick(now);
    }
    core.sessionEnded(2);
    if (badOutput) {
        std::abort();
    }
    return 0;
}
