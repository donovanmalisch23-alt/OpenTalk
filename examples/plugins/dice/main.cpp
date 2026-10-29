// SPDX-License-Identifier: GPL-3.0-only
// An example OpenTalk plugin in C++: a dice roller. OpenTalk runs it as its own program
// and talks to it in JSON lines over standard input and output (docs/plugins/ipc.md).
// It adds "Roll a dice" to the Plugins menu and answers "!dice" in your channel.
// Apart from Qt's JSON classes it needs nothing: no OpenTalk library is linked.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QString>

#include <iostream>
#include <string>

namespace {

void send(const QJsonObject &message)
{
    std::cout << QJsonDocument(message).toJson(QJsonDocument::Compact).toStdString() << '\n' << std::flush;
}

void say(const QString &text)
{
    send({{QStringLiteral("type"), QStringLiteral("sendMessage")},
          {QStringLiteral("to"), QStringLiteral("channel")},
          {QStringLiteral("text"), text}});
}

QString roll(int sides)
{
    return QStringLiteral("%1 (1 to %2)").arg(QRandomGenerator::global()->bounded(1, sides + 1)).arg(sides);
}

} // namespace

int main()
{
    std::string line;
    QString myName;
    bool connected = false;
    while (std::getline(std::cin, line)) {
        const QJsonObject message = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const QString type = message.value(QLatin1String("type")).toString();
        if (type == QLatin1String("hello")) {
            send({{QStringLiteral("type"), QStringLiteral("ready")},
                  {QStringLiteral("commands"),
                   QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("roll")},
                                          {QStringLiteral("title"), QStringLiteral("Roll a dice")}}}}});
        } else if (type == QLatin1String("connected")) {
            connected = true;
            myName = message.value(QLatin1String("self")).toObject().value(QLatin1String("name")).toString();
        } else if (type == QLatin1String("disconnected")) {
            connected = false;
        } else if (type == QLatin1String("command") &&
                   message.value(QLatin1String("id")).toString() == QLatin1String("roll")) {
            if (!connected) {
                send({{QStringLiteral("type"), QStringLiteral("notify")},
                      {QStringLiteral("text"), QStringLiteral("Connect to a server first.")}});
            } else {
                say(QStringLiteral("%1 rolled %2.").arg(myName, roll(6)));
            }
        } else if (type == QLatin1String("message") &&
                   message.value(QLatin1String("kind")).toString() == QLatin1String("channel") &&
                   !message.value(QLatin1String("fromSelf")).toBool()) {
            const QStringList words =
                message.value(QLatin1String("text")).toString().trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (!words.isEmpty() && words.first().compare(QLatin1String("!dice"), Qt::CaseInsensitive) == 0) {
                const int sides = words.size() > 1 ? words.at(1).toInt() : 6;
                const QString who =
                    message.value(QLatin1String("from")).toObject().value(QLatin1String("name")).toString();
                say(sides >= 2 && sides <= 1000 ? QStringLiteral("%1 rolled %2.").arg(who, roll(sides))
                                                : QStringLiteral("A dice needs 2 to 1000 sides."));
            }
        } else if (type == QLatin1String("result") && !message.value(QLatin1String("ok")).toBool()) {
            send({{QStringLiteral("type"), QStringLiteral("log")},
                  {QStringLiteral("text"),
                   QStringLiteral("not sent: ") + message.value(QLatin1String("error")).toString()}});
        } else if (type == QLatin1String("shutdown")) {
            return 0;
        }
    }
    return 0;
}
