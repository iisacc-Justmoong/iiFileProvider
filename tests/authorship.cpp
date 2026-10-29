#include <iiFileProvider.h>
#include <QtCore/QDir>
#include "iiFileProviderDevice.h"
#include <QtCore/QJsonDocument>
#include <QtCore/QTemporaryDir>
#include <QtTest/QTest>
#include <limits>
#include <stdexcept>

using namespace iiFileProvider;

namespace {
const auto recordedAt = QDateTime::fromString("2026-09-08T12:00:00Z", Qt::ISODate);
FileAuthor sampleAuthor(const QString& subject, const QUrl& origin = QUrl("https://iisacc.com"))
{
    return FileAuthor::fromIisaccAccount(
        {{"sub", subject}, {"email", "writer@example.com"}, {"displayName", subject}},
        origin, recordedAt).value();
}
}

class AuthorshipTest : public QObject {
    Q_OBJECT
private slots:
    void emptyFileLinkMetadata()
    {
        Authorship history;
        QVERIFY(history.toJson()["links"].isArray());
        QVERIFY(history.toJson()["links"].toArray().isEmpty());
    }
    void permanentEditorRoster()
    {
        const auto at = QDateTime::fromString("2026-09-08T12:00:00Z", Qt::ISODate);
        Authorship history;
        QCOMPARE(history.toJson()["schemaVersion"].toInt(), Authorship::SchemaVersion);
        QVERIFY(history.toJson()["firstEditor"].isNull());
        QVERIFY(history.toJson()["participants"].toArray().isEmpty());
        QVERIFY(!history.firstEditor());
        QVERIFY(history.participants().isEmpty());
        QVERIFY(Authorship::fromDump(history.dump()));

        for (const auto& sub : {"original", "participant-1", "participant-2"}) {
            const auto author = FileAuthor::fromIisaccAccount(
                {{"sub", sub}, {"email", "writer@example.com"}}, QUrl("https://iisacc.com"), at);
            QVERIFY(author);
            QVERIFY(history.setAuthor(*author, at));
        }
        const auto recorded = history.toJson();
        const auto authors = recorded["authors"].toArray();
        QCOMPARE(recorded["firstEditor"], authors[0].toObject()["key"]);
        QCOMPARE(recorded["participants"].toArray(),
            QJsonArray({authors[1].toObject()["key"], authors[2].toObject()["key"]}));
        QCOMPARE(history.firstEditor()->metadata().account.sub, QString("original"));
        const auto participants = history.participants();
        QCOMPARE(participants.size(), 2);
        QCOMPARE(participants[0].metadata().account.sub, QString("participant-1"));
        QCOMPARE(participants[1].metadata().account.sub, QString("participant-2"));

        history.clearActiveAuthor();
        history.recordChange(at.addSecs(1));
        auto restored = Authorship::fromDump(history.dump());
        QVERIFY(restored);
        restored->recordChange(at.addSecs(2));
        QCOMPARE(restored->toJson()["firstEditor"], recorded["firstEditor"]);
        QCOMPARE(restored->toJson()["participants"], recorded["participants"]);
        QCOMPARE(restored->toJson()["authors"], recorded["authors"]);
    }
    void firstKnownEditorAfterAnonymousChanges()
    {
        Authorship history;
        history.recordChange(recordedAt);
        QVERIFY(!history.firstEditor());
        QVERIFY(history.participants().isEmpty());
        const auto author = sampleAuthor("first-known");
        QVERIFY(history.setAuthor(author, recordedAt.addSecs(1)));
        QCOMPARE(history.firstEditor()->toJson(), author.toJson());
        QVERIFY(history.participants().isEmpty());
        history.recordChange(recordedAt.addSecs(2));
        QVERIFY(!history.setAuthor(author, recordedAt.addSecs(3)));
        QCOMPARE(history.firstEditor()->toJson(), author.toJson());
        QVERIFY(history.participants().isEmpty());
    }
    void profileRefreshNeverReplacesIdentities()
    {
        Authorship history;
        auto original = sampleAuthor("original");
        auto participant = sampleAuthor("participant");
        QVERIFY(history.setAuthor(original, recordedAt));
        QVERIFY(history.setAuthor(participant, recordedAt.addSecs(1)));
        const auto before = history.toJson();
        QVERIFY(!history.setAuthor(original, recordedAt.addSecs(2)));
        QVERIFY(!history.setAuthor(participant, recordedAt.addSecs(3)));
        QCOMPARE(history.toJson(), before);

        auto metadata = original.metadata();
        metadata.account.displayName = "바뀐 최초 편집자 이름";
        metadata.account.email = "updated@example.com";
        QVERIFY(original.setMetadata(metadata));
        QVERIFY(history.setAuthor(original, recordedAt.addSecs(4)));
        metadata = participant.metadata();
        metadata.account.displayName = "바뀐 참여자 이름";
        QVERIFY(participant.setMetadata(metadata));
        QVERIFY(history.setAuthor(participant, recordedAt.addSecs(5)));
        const auto refreshed = history.toJson();
        QCOMPARE(refreshed["firstEditor"], before["firstEditor"]);
        QCOMPARE(refreshed["participants"], before["participants"]);
        QCOMPARE(history.firstEditor()->toJson(), original.toJson());
        QCOMPARE(history.participants().size(), 1);
        QCOMPARE(history.participants().first().toJson(), participant.toJson());
        const auto records = refreshed["authors"].toArray();
        for (qsizetype index = 0; index < records.size(); ++index)
            QCOMPARE(records[index].toObject()["firstChangedAt"],
                before["authors"].toArray()[index].toObject()["firstChangedAt"]);

        // A changed account identity is a new participant, even when the name/email match.
        metadata.account.sub = "another-account";
        QVERIFY(participant.setMetadata(metadata));
        QVERIFY(history.setAuthor(participant, recordedAt.addSecs(6)));
        QCOMPARE(history.firstEditor()->metadata().account.sub, QString("original"));
        QCOMPARE(history.participants().size(), 2);
        QCOMPARE(history.participants()[0].metadata().account.sub, QString("participant"));
        QCOMPARE(history.participants()[1].metadata().account.sub, QString("another-account"));
    }
    void identitiesAreScopedToService()
    {
        Authorship history;
        QVERIFY(history.setAuthor(sampleAuthor("same-subject"), recordedAt));
        QVERIFY(history.setAuthor(sampleAuthor("same-subject", QUrl("https://other.example")), recordedAt));
        QVERIFY(!history.setAuthor(sampleAuthor("same-subject", QUrl("https://iisacc.com:443/")), recordedAt));
        QCOMPARE(history.participants().size(), 1);
        QCOMPARE(history.firstEditor()->metadata().serviceOrigin, QUrl("https://iisacc.com"));
        QCOMPARE(history.participants().first().metadata().serviceOrigin, QUrl("https://other.example"));
    }
    void returnedSnapshotsCannotRemoveOrEditRoster()
    {
        Authorship history;
        history.setAuthor(sampleAuthor("original"), recordedAt);
        history.setAuthor(sampleAuthor("participant"), recordedAt);
        const auto before = history.dump();
        auto original = history.firstEditor();
        auto metadata = original->metadata();
        metadata.account.sub = "replacement";
        QVERIFY(original->setMetadata(metadata));
        original.reset();
        auto participants = history.participants();
        QVERIFY(participants[0].setMetadata(metadata));
        participants.clear();
        auto json = history.toJson();
        json["authors"] = QJsonArray();
        json["participants"] = QJsonArray();
        json["firstEditor"] = QJsonValue::Null;
        QCOMPARE(history.dump(), before);
        QCOMPARE(history.firstEditor()->metadata().account.sub, QString("original"));
        QCOMPARE(history.participants().size(), 1);
    }
    void rosterSurvivesFileReloadAndFurtherEdits()
    {
        QTemporaryDir directory(QDir::currentPath() + "/editor-roster-XXXXXX");
        QVERIFY(directory.isValid());
        Authorship history;
        history.setAuthor(sampleAuthor("original"), recordedAt);
        history.setAuthor(sampleAuthor("participant-1"), recordedAt.addSecs(1));
        iiFileProvider::FileDevice file(directory.filePath("metadata.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(history.dump()), history.dump().size());
        file.close();
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto restored = Authorship::fromDump(file.readAll());
        file.close();
        QVERIFY(restored);
        QVERIFY(!restored->hasActiveAuthor());
        QVERIFY(!restored->setAuthor(sampleAuthor("participant-1"), recordedAt.addSecs(2)));
        restored->recordChange(recordedAt.addSecs(3));
        // Backward clock changes cannot reorder the roster.
        QVERIFY(restored->setAuthor(sampleAuthor("participant-2"), recordedAt.addSecs(-100)));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(restored->dump()), restored->dump().size());
        file.close();
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto reopened = Authorship::fromDump(file.readAll());
        QVERIFY(reopened);
        QCOMPARE(reopened->firstEditor()->metadata().account.sub, QString("original"));
        QCOMPARE(reopened->participants().size(), 2);
        QCOMPARE(reopened->participants()[0].metadata().account.sub, QString("participant-1"));
        QCOMPARE(reopened->participants()[1].metadata().account.sub, QString("participant-2"));
        QCOMPARE(reopened->toJson()["authors"].toArray()[0], history.toJson()["authors"].toArray()[0]);
    }
    void legacyVersionOneMigration_data()
    {
        QTest::addColumn<int>("authorCount");
        QTest::addColumn<bool>("anonymousChange");
        QTest::newRow("empty") << 0 << false;
        QTest::newRow("anonymous") << 0 << true;
        QTest::newRow("single-editor") << 1 << false;
        QTest::newRow("ordered-participants-with-equal-times") << 3 << false;
    }
    void legacyVersionOneMigration()
    {
        QFETCH(int, authorCount);
        QFETCH(bool, anonymousChange);
        Authorship history;
        if (anonymousChange) history.recordChange(recordedAt);
        for (int index = 0; index < authorCount; ++index)
            history.setAuthor(sampleAuthor(QString("legacy-%1").arg(index)), recordedAt);
        auto legacy = history.toJson();
        legacy["schemaVersion"] = 1;
        legacy.remove("firstEditor");
        legacy.remove("participants");
        legacy.remove("links");
        QString error = "stale error";
        auto restored = Authorship::fromJson(legacy, &error);
        QVERIFY2(restored, qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(restored->dump(), history.dump());
        QCOMPARE(restored->revision(), history.revision());
        QVERIFY(!restored->hasActiveAuthor());
        auto fromBytes = Authorship::fromDump(QJsonDocument(legacy).toJson(QJsonDocument::Compact));
        QVERIFY(fromBytes);
        QCOMPARE(fromBytes->dump(), restored->dump());
        restored->setAuthor(sampleAuthor("new-editor"), recordedAt.addSecs(1));
        QCOMPARE(restored->firstEditor()->metadata().account.sub,
            authorCount == 0 ? QString("new-editor") : QString("legacy-0"));
        QCOMPARE(restored->participants().size(), authorCount);
    }
    void fullLegacyMetadataMigratesWithoutDroppingAuthors_data()
    {
        QTest::addColumn<int>("version");
        QTest::newRow("version-one") << 1;
        QTest::newRow("version-two") << 2;
    }
    void fullLegacyMetadataMigratesWithoutDroppingAuthors()
    {
        QFETCH(int, version);
        const qsizetype legacyMaximumBytes = 1024 * 1024 + (version == 2 ? 32 * 1024 : 0);
        Authorship history;
        for (int index = 0; index < 256; ++index)
            history.setAuthor(sampleAuthor(QString("legacy-%1").arg(index)), recordedAt);
        auto legacy = history.toJson();
        legacy["schemaVersion"] = version;
        if (version == 1) {
            legacy.remove("firstEditor");
            legacy.remove("participants");
        }
        legacy.remove("links");
        auto authors = legacy["authors"].toArray();
        auto remaining = legacyMaximumBytes - QJsonDocument(legacy).toJson(QJsonDocument::Compact).size();
        for (qsizetype index = 0; index < authors.size() && remaining > 0; ++index) {
            auto record = authors[index].toObject();
            auto author = record["author"].toObject();
            auto details = author["details"].toObject();
            const auto length = qMin(qsizetype(4096), remaining);
            details["biography"] = QString(length, 'x');
            author["details"] = details;
            record["author"] = author;
            authors[index] = record;
            remaining -= length;
        }
        legacy["authors"] = authors;
        const auto bytes = QJsonDocument(legacy).toJson(QJsonDocument::Compact);
        QCOMPARE(bytes.size(), legacyMaximumBytes);
        QString error;
        const auto restored = Authorship::fromDump(bytes, &error);
        QVERIFY2(restored, qPrintable(error));
        QCOMPARE(restored->toJson()["authors"], legacy["authors"]);
        QCOMPARE(restored->revision(), history.revision());
        QCOMPARE(restored->firstEditor()->metadata().account.sub, QString("legacy-0"));
        QCOMPARE(restored->participants().size(), 255);
        QVERIFY(restored->links().isEmpty());
        QVERIFY(restored->dump().size() <= Authorship::MaximumBytes);
        const auto roundTrip = Authorship::fromDump(restored->dump());
        QVERIFY(roundTrip);
        QCOMPARE(roundTrip->dump(), restored->dump());
    }
    void malformedRoster_data()
    {
        QTest::addColumn<QJsonObject>("json");
        Authorship history;
        history.setAuthor(sampleAuthor("original"), recordedAt);
        history.setAuthor(sampleAuthor("second"), recordedAt);
        history.setAuthor(sampleAuthor("third"), recordedAt);
        const auto valid = history.toJson();
        const auto first = valid["firstEditor"];
        const auto participants = valid["participants"].toArray();
        const auto changedField = [&](const char* name, const char* field, const QJsonValue& value) {
            auto json = valid;
            json[field] = value;
            QTest::newRow(name) << json;
        };
        changedField("missing-first-editor", "firstEditor", QJsonValue::Undefined);
        changedField("removed-first-editor", "firstEditor", QJsonValue::Null);
        changedField("replaced-first-editor", "firstEditor", participants[0]);
        changedField("unknown-first-editor", "firstEditor", QString(64, 'a'));
        changedField("wrong-first-editor-type", "firstEditor", QJsonObject());
        changedField("missing-participants", "participants", QJsonValue::Undefined);
        changedField("null-participants", "participants", QJsonValue::Null);
        changedField("wrong-participants-type", "participants", "second");
        changedField("removed-participant", "participants", QJsonArray({participants[0]}));
        changedField("duplicate-participant", "participants", QJsonArray({participants[0], participants[0]}));
        changedField("first-editor-also-participant", "participants", QJsonArray({first, participants[1]}));
        changedField("unknown-participant", "participants", QJsonArray({participants[0], QString(64, 'a')}));
        changedField("reordered-participants", "participants", QJsonArray({participants[1], participants[0]}));
        changedField("unsupported-version", "schemaVersion", 4);
        changedField("fractional-version", "schemaVersion", 3.5);
        changedField("string-version", "schemaVersion", "3");
        changedField("version-one-with-version-two-fields", "schemaVersion", 1);
        auto empty = Authorship().toJson();
        empty["firstEditor"] = first;
        QTest::newRow("empty-with-first-editor") << empty;
        empty = Authorship().toJson();
        empty["participants"] = participants;
        QTest::newRow("empty-with-participants") << empty;
    }
    void malformedRoster()
    {
        QFETCH(QJsonObject, json);
        QString error;
        QVERIFY(!Authorship::fromJson(json, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!Authorship::fromDump(QJsonDocument(json).toJson(QJsonDocument::Compact), &error));
        QVERIFY(!error.isEmpty());
    }
    void failedRegistrationsPreserveRoster()
    {
        Authorship history;
        const auto original = sampleAuthor("original");
        const auto participant = sampleAuthor("participant");
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, history.setAuthor(original, QDateTime()));
        QVERIFY(history.isEmpty());
        QVERIFY(!history.firstEditor());
        history.setAuthor(original, recordedAt);
        const auto before = history.dump();
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, history.setAuthor(participant, QDateTime()));
        QCOMPARE(history.dump(), before);
        QVERIFY(history.participants().isEmpty());
        history.recordChange(recordedAt.addSecs(1));
        QCOMPARE(history.toJson()["lastAuthor"], history.toJson()["firstEditor"]);

        auto json = history.toJson();
        json["revision"] = QString::number(std::numeric_limits<quint64>::max());
        auto exhausted = Authorship::fromJson(json);
        QVERIFY(exhausted);
        QVERIFY(!exhausted->setAuthor(original, recordedAt));
        const auto exhaustedDump = exhausted->dump();
        QVERIFY_THROWS_EXCEPTION(std::overflow_error, exhausted->setAuthor(participant, recordedAt));
        QCOMPARE(exhausted->dump(), exhaustedDump);
        QVERIFY(exhausted->participants().isEmpty());
    }
    void capacityFailureNeverEvictsEditors_data()
    {
        QTest::addColumn<int>("biographyLength");
        QTest::newRow("author-count-limit") << 0;
        QTest::newRow("metadata-byte-limit") << 4096;
    }
    void capacityFailureNeverEvictsEditors()
    {
        QFETCH(int, biographyLength);
        Authorship history;
        int accepted = 0;
        bool rejected = false;
        for (int index = 0; index <= 256; ++index) {
            auto author = sampleAuthor(QString("author-%1").arg(index));
            auto metadata = author.metadata();
            metadata.details.biography = QString(biographyLength, 'x');
            if (biographyLength != 0) {
                for (int linkIndex = 0; linkIndex < 4; ++linkIndex)
                    metadata.details.links.append({"profile", QString::number(linkIndex),
                        QUrl("https://example.com/" + QString(1900, 'x'))});
            }
            QVERIFY(author.setMetadata(metadata));
            const auto before = history.dump();
            try {
                QVERIFY(history.setAuthor(author, recordedAt.addSecs(index)));
                ++accepted;
            } catch (const std::length_error&) {
                QCOMPARE(history.dump(), before);
                rejected = true;
                break;
            }
        }
        QVERIFY(rejected);
        if (biographyLength == 0) QCOMPARE(accepted, 256);
        else QVERIFY(accepted > 1 && accepted < 256);
        QCOMPARE(history.firstEditor()->metadata().account.sub, QString("author-0"));
        QCOMPARE(history.participants().size(), accepted - 1);
        const auto before = history.toJson();
        history.recordChange(recordedAt.addSecs(1000));
        QCOMPARE(history.toJson()["firstEditor"], before["firstEditor"]);
        QCOMPARE(history.toJson()["participants"], before["participants"]);
        QCOMPARE(history.toJson()["lastAuthor"], before["authors"].toArray().last().toObject()["key"]);
        auto restored = Authorship::fromDump(history.dump());
        QVERIFY(restored);
        QCOMPARE(restored->participants().size(), accepted - 1);
    }
    void immediateDumpAndRoundTrip()
    {
        const auto at = QDateTime::fromString("2026-09-07T12:00:00Z", Qt::ISODate);
        auto author = FileAuthor::fromIisaccAccount({{"sub", "writer-1"}, {"email", "writer@example.com"}},
            QUrl("https://iisacc.com"), at);
        QVERIFY(author);
        Authorship history;
        QVERIFY(history.setAuthor(*author, at));
        const auto first = history.dump();
        QCOMPARE(history.revision(), quint64(1));
        QVERIFY(!history.setAuthor(*author, at));
        QCOMPARE(history.dump(), first);
        history.recordChange(at.addSecs(1));
        QCOMPARE(history.revision(), quint64(2));
        QVERIFY(history.dump() != first);
        auto loaded = Authorship::fromJson(history.toJson());
        QVERIFY(loaded);
        QCOMPARE(loaded->dump(), history.dump());
        QVERIFY(!loaded->hasActiveAuthor());
        loaded->recordChange(at.addSecs(2));
        QVERIFY(loaded->toJson()["lastAuthor"].isNull());
    }
    void credentialsNeverEnterDump()
    {
        auto at = QDateTime::currentDateTimeUtc();
        auto author = FileAuthor::fromIisaccAccount({{"sub", "writer"}, {"email", "writer@example.com"}},
            QUrl("https://iisacc.com"), at);
        QVERIFY(author);
        AuthenticationTokenInfo info;
        info.subject = "writer"; info.serviceOrigin = QUrl("https://iisacc.com");
        info.sessionId = QString(32, 'a'); info.issuedAt = at; info.expiresAt = at.addDays(1);
        auto token = AuthenticationToken::create(info, QByteArray(43, 's'));
        QVERIFY(token); QVERIFY(author->setAuthenticationToken(*token));
        Authorship history;
        QVERIFY(history.setAuthor(*author, at));
        QVERIFY(!history.dump().contains(QByteArray(43, 's')));
        QVERIFY(!history.dump().contains("token"));
        QVERIFY(!history.firstEditor()->authenticationToken());
        auto participant = sampleAuthor("participant");
        info.subject = "participant";
        token = AuthenticationToken::create(info, QByteArray(43, 's'));
        QVERIFY(token);
        QVERIFY(participant.setAuthenticationToken(*token));
        history.setAuthor(participant, at);
        QVERIFY(!history.participants().first().authenticationToken());
        QVERIFY(!history.dump().contains(QByteArray(43, 's')));
        QVERIFY(!history.dump().contains("token"));
    }
    void invalidReadAndClockRollback()
    {
        Authorship history;
        const auto at = QDateTime::currentDateTimeUtc();
        history.recordChange(at);
        history.recordChange(at.addSecs(-100));
        QCOMPARE(history.toJson()["modifiedAt"].toString(), at.toUTC().toString(Qt::ISODateWithMs));
        auto json = history.toJson(); json["revision"] = 1.5;
        QVERIFY(!Authorship::fromJson(json));
        json = history.toJson(); json["token"] = "forbidden";
        QVERIFY(!Authorship::fromJson(json));
        const auto before = history.dump();
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, history.recordChange(QDateTime()));
        QCOMPARE(history.dump(), before);
    }
};
QTEST_GUILESS_MAIN(AuthorshipTest)
#include "authorship.moc"
