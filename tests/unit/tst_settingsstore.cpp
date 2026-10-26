// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/storage/ClientSettings.h"
#include "opentalk/storage/SettingsStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace opentalk::storage;

namespace {

SettingsSchema testSchema(int version = 2)
{
    SettingsSchema schema(version);
    schema.add({QStringLiteral("a/flag"), SettingType::Bool, false, {}});
    schema.add({QStringLiteral("a/count"), SettingType::Int, 5,
                [](const QVariant &v) { return v.toInt() >= 0 && v.toInt() <= 10; }});
    schema.add({QStringLiteral("a/name"), SettingType::String, QStringLiteral("default"), {}});
    schema.add({QStringLiteral("a/blob"), SettingType::ByteArray, QByteArray(), {}});
    return schema;
}

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(contents);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray settingsJson(int version, const QJsonObject &values)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("opentalk-settings")},
                                     {QStringLiteral("schemaVersion"), version},
                                     {QStringLiteral("values"), values}})
        .toJson();
}

} // namespace

class TestSettingsStore : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString path() const { return m_dir.filePath(QStringLiteral("settings.json")); }

private slots:
    void init()
    {
        QDir dir(m_dir.path());
        for (const QString &name : dir.entryList(QDir::Files)) {
            dir.remove(name);
        }
    }

    void schemaRejectsDuplicateKeysAndBadDefaults()
    {
        SettingsSchema schema(1);
        schema.add({QStringLiteral("k"), SettingType::Bool, true, {}});
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, schema.add({QStringLiteral("k"), SettingType::Bool, true, {}}));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                                 schema.add({QStringLiteral("x"), SettingType::Int, QStringLiteral("text"), {}}));
        QCOMPARE(schema.definitions().size(), 1);
    }

    void clientSchemaIsWellFormed()
    {
        const SettingsSchema schema = client::schema();
        QCOMPARE(schema.version(), client::kSchemaVersion);
        QVERIFY(schema.find(client::kLogLevel) != nullptr);
        QVERIFY(schema.isValid(client::kLogLevel, QStringLiteral("debug")));
        QVERIFY(!schema.isValid(client::kLogLevel, QStringLiteral("loud")));
    }

    void missingFileGivesDefaults()
    {
        SettingsStore store(testSchema(), path());
        const LoadReport report = store.load();
        QVERIFY(report.status == LoadReport::Status::CreatedDefaults);
        QCOMPARE(store.value(QStringLiteral("a/count")), QVariant(5));
        QVERIFY(!QFile::exists(path()));
    }

    void saveAndReloadRoundTrip()
    {
        {
            SettingsStore store(testSchema(), path());
            store.load();
            QVERIFY(store.setValue(QStringLiteral("a/flag"), true));
            QVERIFY(store.setValue(QStringLiteral("a/count"), 9));
            QVERIFY(store.setValue(QStringLiteral("a/name"), QStringLiteral("Ünïcødé")));
            QVERIFY(store.setValue(QStringLiteral("a/blob"), QByteArray("\x00\x01\xff", 3)));
            QVERIFY(store.save());
        }
        SettingsStore store(testSchema(), path());
        QVERIFY(store.load().status == LoadReport::Status::Loaded);
        QCOMPARE(store.value(QStringLiteral("a/flag")), QVariant(true));
        QCOMPARE(store.value(QStringLiteral("a/count")), QVariant(9));
        QCOMPARE(store.value(QStringLiteral("a/name")), QVariant(QStringLiteral("Ünïcødé")));
        QCOMPARE(store.value(QStringLiteral("a/blob")), QVariant(QByteArray("\x00\x01\xff", 3)));
    }

    void setValueValidatesAndSignals()
    {
        SettingsStore store(testSchema(), path());
        store.load();
        QSignalSpy spy(&store, &SettingsStore::valueChanged);
        QVERIFY(!store.setValue(QStringLiteral("a/count"), 11));                  // out of range
        QVERIFY(!store.setValue(QStringLiteral("a/count"), QStringLiteral("3"))); // wrong type
        QVERIFY(!store.setValue(QStringLiteral("unknown"), 1));
        QCOMPARE(spy.count(), 0);
        QVERIFY(store.setValue(QStringLiteral("a/count"), 5)); // same as default: no signal
        QCOMPARE(spy.count(), 0);
        QVERIFY(store.setValue(QStringLiteral("a/count"), 6));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("a/count"));
    }

    void invalidValuesFallBackButArePreserved()
    {
        writeFile(path(), settingsJson(2, {{QStringLiteral("a/count"), 99},
                                           {QStringLiteral("a/flag"), QStringLiteral("yes")},
                                           {QStringLiteral("a/name"), QStringLiteral("kept")},
                                           {QStringLiteral("future/key"), QStringLiteral("from a newer build")}}));
        SettingsStore store(testSchema(), path());
        const LoadReport report = store.load();
        QVERIFY(report.status == LoadReport::Status::Loaded);
        QCOMPARE(report.warnings.size(), 2);
        QCOMPARE(store.value(QStringLiteral("a/count")), QVariant(5));
        QCOMPARE(store.value(QStringLiteral("a/flag")), QVariant(false));
        QCOMPARE(store.value(QStringLiteral("a/name")), QVariant(QStringLiteral("kept")));

        QVERIFY(store.save());
        const QJsonObject values =
            QJsonDocument::fromJson(readFile(path())).object().value(QStringLiteral("values")).toObject();
        QCOMPARE(values.value(QStringLiteral("future/key")).toString(), QStringLiteral("from a newer build"));
        QCOMPARE(values.value(QStringLiteral("a/count")).toInt(), 99); // untouched until the user changes it
    }

    void corruptFileIsMovedAside()
    {
        writeFile(path(), "{ this is not json");
        SettingsStore store(testSchema(), path());
        const LoadReport report = store.load();
        QVERIFY(report.status == LoadReport::Status::RecoveredFromCorruption);
        QVERIFY(!QFile::exists(path()));
        QVERIFY(QFile::exists(report.backupPath));
        QCOMPARE(readFile(report.backupPath), QByteArray("{ this is not json"));
        QVERIFY(!store.isReadOnly());
        QVERIFY(store.save());
    }

    void wrongFormatCountsAsCorrupt()
    {
        writeFile(path(), R"({"format":"something-else","schemaVersion":2,"values":{}})");
        SettingsStore store(testSchema(), path());
        QVERIFY(store.load().status == LoadReport::Status::RecoveredFromCorruption);
    }

    void newerVersionIsNeverOverwritten()
    {
        const QByteArray original = settingsJson(3, {{QStringLiteral("a/count"), 7}});
        writeFile(path(), original);
        SettingsStore store(testSchema(2), path());
        QVERIFY(store.load().status == LoadReport::Status::NewerVersionReadOnly);
        QVERIFY(store.isReadOnly());
        QCOMPARE(store.value(QStringLiteral("a/count")), QVariant(5));
        QVERIFY(store.setValue(QStringLiteral("a/count"), 8));
        QVERIFY(!store.save());
        QCOMPARE(readFile(path()), original);
    }

    void olderVersionIsBackedUpAndMigrated()
    {
        const QByteArray original = settingsJson(1, {{QStringLiteral("old/count"), 4}});
        writeFile(path(), original);
        SettingsStore store(testSchema(2), path());
        store.addMigration(1, [](QJsonObject &values, QString &) {
            values.insert(QStringLiteral("a/count"), values.take(QStringLiteral("old/count")));
            return true;
        });
        const LoadReport report = store.load();
        QVERIFY(report.status == LoadReport::Status::Migrated);
        QCOMPARE(readFile(report.backupPath), original);
        QCOMPARE(store.value(QStringLiteral("a/count")), QVariant(4));
        QVERIFY(store.save());
        QCOMPARE(QJsonDocument::fromJson(readFile(path())).object().value(QStringLiteral("schemaVersion")).toInt(), 2);
    }

    void missingMigrationMakesStoreReadOnly()
    {
        const QByteArray original = settingsJson(1, {});
        writeFile(path(), original);
        SettingsStore store(testSchema(2), path());
        QVERIFY(store.load().status == LoadReport::Status::Failed);
        QVERIFY(store.isReadOnly());
        QVERIFY(!store.save());
        QCOMPARE(readFile(path()), original);
    }

    void failedMigrationLeavesFileUntouched()
    {
        const QByteArray original = settingsJson(1, {});
        writeFile(path(), original);
        SettingsStore store(testSchema(2), path());
        store.addMigration(1, [](QJsonObject &, QString &error) {
            error = QStringLiteral("cannot convert");
            return false;
        });
        const LoadReport report = store.load();
        QVERIFY(report.status == LoadReport::Status::Failed);
        QVERIFY(report.warnings.join(QLatin1Char(' ')).contains(QStringLiteral("cannot convert")));
        QCOMPARE(readFile(path()), original);
    }
};

QTEST_GUILESS_MAIN(TestSettingsStore)
#include "tst_settingsstore.moc"
