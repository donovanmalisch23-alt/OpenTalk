// SPDX-License-Identifier: GPL-3.0-only
// Private helper: sends requests and calls back with their answer.
#pragma once

#include "opentalk/client/Connection.h"

#include <QHash>
#include <QObject>

#include <functional>
#include <optional>
#include <utility>

namespace opentalk::ui::detail {

/// The answer to one request: an error, or success with an optional result ID (for
/// example a new role's ID) or data (a list, the audit log, settings or statistics).
struct RequestResult {
    std::optional<protocol::Error> error;
    std::optional<quint32> resultId;
    std::optional<protocol::Message> data;
    bool ok() const { return !error.has_value(); }
};

/// Sends requests on a connection and calls a handler once with each answer. Handlers
/// are dropped with the tracker, so a closed dialog is never called back.
class RequestTracker : public QObject {
public:
    using Handler = std::function<void(const RequestResult &)>;

    RequestTracker(client::Connection &connection, QObject *parent)
        : QObject(parent)
        , m_connection(connection)
    {
        connect(&connection, &client::Connection::responseReceived, this,
                [this](quint64 id, const protocol::Message &message) {
                    finish(id, {std::nullopt, std::nullopt, message});
                });
        connect(&connection, &client::Connection::requestFinished, this,
                [this](quint64 id, const std::optional<protocol::Error> &error, std::optional<quint32> resultId) {
                    finish(id, {error, resultId, std::nullopt});
                });
    }

    /// Returns the request ID, or 0 if not connected (the handler is then called at once
    /// with an error).
    quint64 send(protocol::Message request, Handler handler)
    {
        const quint64 id = m_connection.sendRequest(std::move(request));
        if (id == 0) {
            if (handler) {
                handler({protocol::Error{protocol::ErrorCode::Timeout, std::nullopt, tr("not connected")}, std::nullopt,
                         std::nullopt});
            }
            return 0;
        }
        m_handlers.insert(id, std::move(handler));
        return id;
    }

    bool isPending(quint64 id) const { return m_handlers.contains(id); }
    int pendingCount() const { return static_cast<int>(m_handlers.size()); }

private:
    void finish(quint64 id, const RequestResult &result)
    {
        const auto it = m_handlers.find(id);
        if (it == m_handlers.end()) {
            return;
        }
        const Handler handler = std::move(it.value());
        m_handlers.erase(it);
        if (handler) {
            handler(result);
        }
    }

    client::Connection &m_connection;
    QHash<quint64, Handler> m_handlers;
};

} // namespace opentalk::ui::detail
