// SPDX-License-Identifier: GPL-3.0-only
// Plugin manifests: what is accepted, what is refused and why, and where the program
// may live.

#include "opentalk/plugins/PluginManifest.h"
#include "opentalk/plugins/PluginRegistry.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace opentalk::plugins;

namespace {

#ifdef Q_OS_WIN
const QString kProgram = QStringLiteral("bin/dice.exe");
#else
const QString kProgram = QStringLiteral("bin/dice");
#endif

QJsonObject validManifest()
{
    return QJsonObject{
        {QStringLiteral("id"), QStringLiteral("org.example.dice")},
        {QStringLiteral("name"), QStringLiteral("Dice roller")},
        {QStringLiteral("version"), QStringLiteral("1.2.0")},
        {QStringLiteral("description"), QStringLiteral("Rolls dice.")},
        {QStringLiteral("author"), QStringLiteral("Example")},
        {QStringLiteral("license"), QStringLiteral("MIT")},
        {QStringLiteral("executable"), kProgram},
        {QStringLiteral("capabilities"), QJsonArray{QStringLiteral("messages.read"), QStringLiteral("messages.send")}},
    };
}

QByteArray json(const QJsonObject &object)
{
    return QJsonDocument(object).toJson();
}

bool writeFile(const QString &path, const QByteArray &contents, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(contents);
    file.close();
    if (executable) {
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    }
    return true;
}

} // namespace

class TestPluginManifest : public QObject {
    Q_OBJECT

private slots:
    void validManifestIsRead()
    {
        QString error;
        const auto m = PluginManifest::parse(json(validManifest()), &error);
        QVERIFY2(m.has_value(), qPrintable(error));
        QCOMPARE(m->id, QStringLiteral("org.example.dice"));
        QCOMPARE(m->executable, kProgram);
        QCOMPARE(m->capabilities,
                 (QList<PluginCapability>{PluginCapability::MessagesRead, PluginCapability::MessagesSend}));
        QCOMPARE(m->hash.size(), 32);
        // Any change to the file changes the hash, so the user is asked again.
        QJsonObject changed = validManifest();
        changed.insert(QStringLiteral("version"), QStringLiteral("1.2.1"));
        QVERIFY(PluginManifest::parse(json(changed))->hash != m->hash);
    }

    void invalidManifestsAreRefusedWithAReason_data()
    {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<QString>("expected");
        QTest::newRow("upper-case id") << "id" << QJsonValue(QStringLiteral("Org.Example")) << "\"id\"";
        QTest::newRow("id without a dot") << "id" << QJsonValue(QStringLiteral("dice")) << "\"id\"";
        QTest::newRow("missing name") << "name" << QJsonValue(QJsonValue::Undefined) << "\"name\"";
        QTest::newRow("hidden characters in name")
            << "name" << QJsonValue(QStringLiteral("Dice") + QChar(0x202E) + QStringLiteral("roller")) << "\"name\"";
        QTest::newRow("bad version") << "version" << QJsonValue(QStringLiteral("one point two")) << "\"version\"";
        QTest::newRow("newer API") << "apiVersion" << QJsonValue(99) << "newer version of OpenTalk";
        QTest::newRow("absolute program") << "executable" << QJsonValue(QStringLiteral("/bin/sh")) << "inside";
        QTest::newRow("program outside the folder")
            << "executable" << QJsonValue(QStringLiteral("../../evil.exe")) << "inside";
        QTest::newRow("drive letter") << "executable" << QJsonValue(QStringLiteral("C:evil.exe")) << "inside";
        QTest::newRow("no program") << "executable" << QJsonValue(QJsonValue::Undefined) << "missing";
        QTest::newRow("other interpreter") << "interpreter" << QJsonValue(QStringLiteral("bash")) << "\"interpreter\"";
        QTest::newRow("unknown capability")
            << "capabilities" << QJsonValue(QJsonArray{QStringLiteral("files.delete")}) << "unknown capability";
        QTest::newRow("capability twice")
            << "capabilities"
            << QJsonValue(QJsonArray{QStringLiteral("presence.read"), QStringLiteral("presence.read")}) << "twice";
        QTest::newRow("homepage not a web address")
            << "homepage" << QJsonValue(QStringLiteral("file:///etc/passwd")) << "web address";
        QTest::newRow("arguments not text") << "arguments" << QJsonValue(QJsonArray{1, 2}) << "argument";
    }

    void invalidManifestsAreRefusedWithAReason()
    {
        QFETCH(QString, field);
        QFETCH(QJsonValue, value);
        QFETCH(QString, expected);
        QJsonObject object = validManifest();
        if (value.isUndefined()) {
            object.remove(field);
        } else {
            object.insert(field, value);
        }
        QString error;
        QVERIFY(!PluginManifest::parse(json(object), &error));
        QVERIFY2(error.contains(expected), qPrintable(error));
    }

    void notJsonOrTooLargeIsRefused()
    {
        QString error;
        QVERIFY(!PluginManifest::parse("{not json", &error));
        QVERIFY(error.contains(QStringLiteral("not valid JSON")));
        QVERIFY(!PluginManifest::parse("[1, 2]", &error));
        QVERIFY(!PluginManifest::parse(QByteArray(70 * 1024, ' '), &error));
        QVERIFY(error.contains(QStringLiteral("64 KiB")));
    }

    void programPerOperatingSystem()
    {
        QJsonObject object = validManifest();
        object.insert(QStringLiteral("executable"), QJsonObject{{QStringLiteral("windows"), QStringLiteral("dice.exe")},
                                                                {QStringLiteral("linux"), QStringLiteral("dice")},
                                                                {QStringLiteral("macos"), QStringLiteral("dice-mac")}});
        const auto m = PluginManifest::parse(json(object));
        QVERIFY(m.has_value());
#if defined(Q_OS_WIN)
        QCOMPARE(m->executable, QStringLiteral("dice.exe"));
#elif defined(Q_OS_MACOS)
        QCOMPARE(m->executable, QStringLiteral("dice-mac"));
#else
        QCOMPARE(m->executable, QStringLiteral("dice"));
#endif
        object.insert(QStringLiteral("executable"), QJsonObject{{QStringLiteral("beos"), QStringLiteral("x")}});
        QString error;
        QVERIFY(!PluginManifest::parse(json(object), &error));
        QVERIFY(error.contains(QStringLiteral("does not support")));
    }

    void launchCommandStaysInsideTheFolder()
    {
        QTemporaryDir dir;
        const QString pluginDir = dir.filePath(QStringLiteral("org.example.dice"));
        QJsonObject object = validManifest();
        QVERIFY(writeFile(QDir(pluginDir).filePath(QStringLiteral("plugin.json")), json(object)));
        auto m = PluginManifest::load(pluginDir);
        QVERIFY(m.has_value());
        QString error;
        QVERIFY(!launchCommand(*m, pluginDir, &error)); // the program is missing
        QVERIFY(error.contains(QStringLiteral("missing")));
        QVERIFY(writeFile(QDir(pluginDir).filePath(kProgram), "#!/bin/sh\n", true));
        const auto command = launchCommand(*m, pluginDir, &error);
        QVERIFY2(command.has_value(), qPrintable(error));
        QVERIFY(command->program.endsWith(kProgram));
#ifndef Q_OS_WIN
        // A link pointing outside the folder is refused, even though its name is inside.
        QVERIFY(writeFile(dir.filePath(QStringLiteral("outside")), "#!/bin/sh\n", true));
        QFile::remove(QDir(pluginDir).filePath(kProgram));
        QVERIFY(QFile::link(dir.filePath(QStringLiteral("outside")), QDir(pluginDir).filePath(kProgram)));
        QVERIFY(!launchCommand(*m, pluginDir, &error));
        QVERIFY(error.contains(QStringLiteral("outside")));
#endif
    }

    void capabilitiesAreDescribed()
    {
        for (PluginCapability c : allPluginCapabilities()) {
            QVERIFY(!capabilityName(c).isEmpty());
            QVERIFY(!describe(c).isEmpty());
            QCOMPARE(pluginCapabilityFromName(capabilityName(c)), std::optional<PluginCapability>(c));
        }
        QVERIFY(isSensitive(PluginCapability::PrivateRead));
        QVERIFY(isSensitive(PluginCapability::MessagesSend));
        QVERIFY(!isSensitive(PluginCapability::CommandsAdd));
    }

    void registryRemembersConsentUntilThePluginChanges()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("download/dice"));
        QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("plugin.json")), json(validManifest())));
        QVERIFY(writeFile(QDir(source).filePath(kProgram), "#!/bin/sh\n", true));
        const QString pluginsDir = dir.filePath(QStringLiteral("data/plugins"));
        const QString state = dir.filePath(QStringLiteral("data/config/plugins.json"));

        PluginRegistry registry(pluginsDir, state);
        registry.load();
        QString error;
        const auto id = registry.install(source, &error);
        QVERIFY2(id.has_value(), qPrintable(error));
        QCOMPARE(*id, QStringLiteral("org.example.dice"));
        QVERIFY(QFileInfo(QDir(pluginsDir).filePath(QStringLiteral("org.example.dice/") + kProgram)).isExecutable());
        QVERIFY(!registry.plugin(*id)->canRun()); // installed is not enabled
        QVERIFY(registry.enable(*id, {PluginCapability::MessagesRead, PluginCapability::CommandsAdd}));
        // Only what the manifest asks for can be granted.
        QCOMPARE(registry.plugin(*id)->granted, QList<PluginCapability>{PluginCapability::MessagesRead});
        QVERIFY(registry.plugin(*id)->canRun());

        PluginRegistry reloaded(pluginsDir, state);
        reloaded.load();
        QVERIFY(reloaded.plugin(*id)->canRun());
        QCOMPARE(reloaded.plugin(*id)->granted, QList<PluginCapability>{PluginCapability::MessagesRead});

        // An update that asks for more needs the user's review before it runs again.
        QJsonObject updated = validManifest();
        updated.insert(QStringLiteral("capabilities"),
                       QJsonArray{QStringLiteral("messages.read"), QStringLiteral("messages.private")});
        QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("plugin.json")), json(updated)));
        QVERIFY(reloaded.install(source, &error));
        QVERIFY(reloaded.plugin(*id)->needsReview);
        QVERIFY(!reloaded.plugin(*id)->canRun());
        QVERIFY(reloaded.enable(*id, {PluginCapability::MessagesRead}));
        QVERIFY(reloaded.plugin(*id)->canRun());
        QVERIFY(!reloaded.plugin(*id)->granted.contains(PluginCapability::PrivateRead));

        QVERIFY(reloaded.disable(*id));
        QVERIFY(!reloaded.plugin(*id)->canRun());
        QVERIFY(reloaded.remove(*id, true, &error));
        QVERIFY(!QFileInfo::exists(QDir(pluginsDir).filePath(*id)));
        QVERIFY(!reloaded.plugin(*id));
    }

    void brokenAndMisplacedPluginsAreListedWithAProblem()
    {
        QTemporaryDir dir;
        const QString pluginsDir = dir.filePath(QStringLiteral("plugins"));
        QVERIFY(writeFile(QDir(pluginsDir).filePath(QStringLiteral("broken/plugin.json")), "{"));
        QVERIFY(writeFile(QDir(pluginsDir).filePath(QStringLiteral("wrong-name/plugin.json")), json(validManifest())));
        PluginRegistry registry(pluginsDir, dir.filePath(QStringLiteral("plugins.json")));
        registry.load();
        const QList<InstalledPlugin> plugins = registry.plugins();
        QCOMPARE(plugins.size(), 2);
        for (const InstalledPlugin &p : plugins) {
            QVERIFY(!p.problem.isEmpty());
            QVERIFY(!p.canRun());
        }
        QVERIFY(!registry.enable(QStringLiteral("org.example.dice"), {}));
        // Installing from inside the plugins folder itself is refused.
        QString error;
        QVERIFY(!registry.install(QDir(pluginsDir).filePath(QStringLiteral("wrong-name")), &error));
        QVERIFY(error.contains(QStringLiteral("outside")));
    }
};

QTEST_GUILESS_MAIN(TestPluginManifest)
#include "tst_plugin_manifest.moc"
