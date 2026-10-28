// SPDX-License-Identifier: GPL-3.0-only
// A plugin used by the tests. Its first argument chooses how it behaves:
//   echo       answers "!echo <text>" in the channel, notifies about joins, results
//              and its "ping" command
//   forbidden  tries to send a message (which it was not granted) and reports the result
//   silent     never says "ready"
//   crash      says "ready" and exits with an error
//   flood      sends 1,000 notifications at once
//   longline   sends a 70 KiB line
//   garbage    sends lines that are not JSON
// It needs only Qt Core and reads its standard input line by line, like any plugin.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

namespace {

void send(const QJsonObject &message)
{
    std::cout << QJsonDocument(message).toJson(QJsonDocument::Compact).toStdString() << '\n' << std::flush;
}

void notify(const QString &text)
{
    send({{QStringLiteral("type"), QStringLiteral("notify")}, {QStringLiteral("text"), text}});
}

} // namespace

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "echo";
    std::string line;
    if (!std::getline(std::cin, line)) { // "hello"
        return 1;
    }
    if (mode == "silent") {
        while (std::getline(std::cin, line)) {
        }
        return 0;
    }
    QJsonObject ready{{QStringLiteral("type"), QStringLiteral("ready")}};
    if (mode == "echo") {
        ready.insert(QStringLiteral("commands"),
                     QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("ping")},
                                            {QStringLiteral("title"), QStringLiteral("Say ping")}}});
    }
    send(ready);
    if (mode == "crash") {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return 3;
    }
    if (mode == "flood") {
        for (int i = 0; i < 1000; ++i) {
            notify(QStringLiteral("flood %1").arg(i));
        }
    } else if (mode == "longline") {
        std::cout << std::string(70 * 1024, 'x') << '\n' << std::flush;
    } else if (mode == "garbage") {
        for (int i = 0; i < 30; ++i) {
            std::cout << "this is not JSON\n" << std::flush;
        }
    } else if (mode == "forbidden") {
        send({{QStringLiteral("type"), QStringLiteral("sendMessage")},
              {QStringLiteral("requestId"), 7},
              {QStringLiteral("to"), QStringLiteral("channel")},
              {QStringLiteral("text"), QStringLiteral("I should not be able to say this")}});
    }

    int nextRequest = 1;
    while (std::getline(std::cin, line)) {
        const QJsonObject message = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const QString type = message.value(QLatin1String("type")).toString();
        if (type == QLatin1String("shutdown")) {
            return 0;
        }
        if (type == QLatin1String("result")) {
            const bool ok = message.value(QLatin1String("ok")).toBool();
            notify(QStringLiteral("result %1 %2 %3")
                       .arg(message.value(QLatin1String("requestId")).toInt())
                       .arg(ok ? QStringLiteral("ok") : QStringLiteral("error"),
                            message.value(QLatin1String("error")).toString()));
        } else if (type == QLatin1String("command")) {
            notify(QStringLiteral("pong"));
        } else if (type == QLatin1String("userJoined")) {
            notify(QStringLiteral("joined %1")
                       .arg(message.value(QLatin1String("user")).toObject().value(QLatin1String("name")).toString()));
        } else if (type == QLatin1String("connected")) {
            send({{QStringLiteral("type"), QStringLiteral("log")},
                  {QStringLiteral("text"),
                   QStringLiteral("connected to ") + message.value(QLatin1String("server")).toString()}});
        } else if (type == QLatin1String("message") && !message.value(QLatin1String("fromSelf")).toBool()) {
            const QString text = message.value(QLatin1String("text")).toString();
            if (text.startsWith(QLatin1String("!echo "))) {
                send({{QStringLiteral("type"), QStringLiteral("sendMessage")},
                      {QStringLiteral("requestId"), nextRequest++},
                      {QStringLiteral("to"), QStringLiteral("channel")},
                      {QStringLiteral("text"), QStringLiteral("echo: ") + text.mid(6)}});
            }
        }
    }
    return 0;
}
