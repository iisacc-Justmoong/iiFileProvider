#include <iiFileProvider.h>
#include <QtCore/QDir>
#include "iiFileProviderDevice.h"
#include <QtCore/QJsonDocument>
#include <QtCore/QTemporaryDir>
#include <QtCore/QCoreApplication>
#include "CTestSupport.h"
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

class AuthorshipTest final {
public:
    void emptyFileLinkMetadata()
    {
        Authorship history;
        CTEST_CHECK(history.toJson()["links"].isArray());
        CTEST_CHECK(history.toJson()["links"].toArray().isEmpty());
    }
    void permanentEditorRoster()
    {
        const auto at = QDateTime::fromString("2026-09-08T12:00:00Z", Qt::ISODate);
        Authorship history;
        CTEST_COMPARE(history.toJson()["schemaVersion"].toInt(), Authorship::SchemaVersion);
        CTEST_CHECK(history.toJson()["firstEditor"].isNull());
        CTEST_CHECK(history.toJson()["participants"].toArray().isEmpty());
        CTEST_CHECK(!history.firstEditor());
        CTEST_CHECK(history.participants().isEmpty());
        CTEST_CHECK(Authorship::fromDump(history.dump()));

        for (const auto& sub : {"original", "participant-1", "participant-2"}) {
            const auto author = FileAuthor::fromIisaccAccount(
                {{"sub", sub}, {"email", "writer@example.com"}}, QUrl("https://iisacc.com"), at);
            CTEST_CHECK(author);
            CTEST_CHECK(history.setAuthor(*author, at));
        }
        const auto recorded = history.toJson();
        const auto authors = recorded["authors"].toArray();
        CTEST_COMPARE(recorded["firstEditor"], authors[0].toObject()["key"]);
        CTEST_COMPARE(recorded["participants"].toArray(),
            QJsonArray({authors[1].toObject()["key"], authors[2].toObject()["key"]}));
        CTEST_COMPARE(history.firstEditor()->metadata().account.sub, QString("original"));
        const auto participants = history.participants();
        CTEST_COMPARE(participants.size(), 2);
        CTEST_COMPARE(participants[0].metadata().account.sub, QString("participant-1"));
        CTEST_COMPARE(participants[1].metadata().account.sub, QString("participant-2"));

        history.clearActiveAuthor();
        history.recordChange(at.addSecs(1));
        auto restored = Authorship::fromDump(history.dump());
        CTEST_CHECK(restored);
        restored->recordChange(at.addSecs(2));
        CTEST_COMPARE(restored->toJson()["firstEditor"], recorded["firstEditor"]);
        CTEST_COMPARE(restored->toJson()["participants"], recorded["participants"]);
        CTEST_COMPARE(restored->toJson()["authors"], recorded["authors"]);
    }
    void firstKnownEditorAfterAnonymousChanges()
    {
        Authorship history;
        history.recordChange(recordedAt);
        CTEST_CHECK(!history.firstEditor());
        CTEST_CHECK(history.participants().isEmpty());
        const auto author = sampleAuthor("first-known");
        CTEST_CHECK(history.setAuthor(author, recordedAt.addSecs(1)));
        CTEST_COMPARE(history.firstEditor()->toJson(), author.toJson());
        CTEST_CHECK(history.participants().isEmpty());
        history.recordChange(recordedAt.addSecs(2));
        CTEST_CHECK(!history.setAuthor(author, recordedAt.addSecs(3)));
        CTEST_COMPARE(history.firstEditor()->toJson(), author.toJson());
        CTEST_CHECK(history.participants().isEmpty());
    }
    void profileRefreshNeverReplacesIdentities()
    {
        Authorship history;
        auto original = sampleAuthor("original");
        auto participant = sampleAuthor("participant");
        CTEST_CHECK(history.setAuthor(original, recordedAt));
        CTEST_CHECK(history.setAuthor(participant, recordedAt.addSecs(1)));
        const auto before = history.toJson();
        CTEST_CHECK(!history.setAuthor(original, recordedAt.addSecs(2)));
        CTEST_CHECK(!history.setAuthor(participant, recordedAt.addSecs(3)));
        CTEST_COMPARE(history.toJson(), before);

        auto metadata = original.metadata();
        metadata.account.displayName = "바뀐 최초 편집자 이름";
        metadata.account.email = "updated@example.com";
        CTEST_CHECK(original.setMetadata(metadata));
        CTEST_CHECK(history.setAuthor(original, recordedAt.addSecs(4)));
        metadata = participant.metadata();
        metadata.account.displayName = "바뀐 참여자 이름";
        CTEST_CHECK(participant.setMetadata(metadata));
        CTEST_CHECK(history.setAuthor(participant, recordedAt.addSecs(5)));
        const auto refreshed = history.toJson();
        CTEST_COMPARE(refreshed["firstEditor"], before["firstEditor"]);
        CTEST_COMPARE(refreshed["participants"], before["participants"]);
        CTEST_COMPARE(history.firstEditor()->toJson(), original.toJson());
        CTEST_COMPARE(history.participants().size(), 1);
        CTEST_COMPARE(history.participants().first().toJson(), participant.toJson());
        const auto records = refreshed["authors"].toArray();
        for (qsizetype index = 0; index < records.size(); ++index)
            CTEST_COMPARE(records[index].toObject()["firstChangedAt"],
                before["authors"].toArray()[index].toObject()["firstChangedAt"]);

        // A changed account identity is a new participant, even when the name/email match.
        metadata.account.sub = "another-account";
        CTEST_CHECK(participant.setMetadata(metadata));
        CTEST_CHECK(history.setAuthor(participant, recordedAt.addSecs(6)));
        CTEST_COMPARE(history.firstEditor()->metadata().account.sub, QString("original"));
        CTEST_COMPARE(history.participants().size(), 2);
        CTEST_COMPARE(history.participants()[0].metadata().account.sub, QString("participant"));
        CTEST_COMPARE(history.participants()[1].metadata().account.sub, QString("another-account"));
    }
    void identitiesAreScopedToService()
    {
        Authorship history;
        CTEST_CHECK(history.setAuthor(sampleAuthor("same-subject"), recordedAt));
        CTEST_CHECK(history.setAuthor(sampleAuthor("same-subject", QUrl("https://other.example")), recordedAt));
        CTEST_CHECK(!history.setAuthor(sampleAuthor("same-subject", QUrl("https://iisacc.com:443/")), recordedAt));
        CTEST_COMPARE(history.participants().size(), 1);
        CTEST_COMPARE(history.firstEditor()->metadata().serviceOrigin, QUrl("https://iisacc.com"));
        CTEST_COMPARE(history.participants().first().metadata().serviceOrigin, QUrl("https://other.example"));
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
        CTEST_CHECK(original->setMetadata(metadata));
        original.reset();
        auto participants = history.participants();
        CTEST_CHECK(participants[0].setMetadata(metadata));
        participants.clear();
        auto json = history.toJson();
        json["authors"] = QJsonArray();
        json["participants"] = QJsonArray();
        json["firstEditor"] = QJsonValue::Null;
        CTEST_COMPARE(history.dump(), before);
        CTEST_COMPARE(history.firstEditor()->metadata().account.sub, QString("original"));
        CTEST_COMPARE(history.participants().size(), 1);
    }
    void rosterSurvivesFileReloadAndFurtherEdits()
    {
        QTemporaryDir directory(QDir::currentPath() + "/editor-roster-XXXXXX");
        CTEST_CHECK(directory.isValid());
        Authorship history;
        history.setAuthor(sampleAuthor("original"), recordedAt);
        history.setAuthor(sampleAuthor("participant-1"), recordedAt.addSecs(1));
        iiFileProvider::FileDevice file(directory.filePath("metadata.json"));
        CTEST_CHECK(file.open(QIODevice::WriteOnly));
        CTEST_COMPARE(file.write(history.dump()), history.dump().size());
        file.close();
        CTEST_CHECK(file.open(QIODevice::ReadOnly));
        auto restored = Authorship::fromDump(file.readAll());
        file.close();
        CTEST_CHECK(restored);
        CTEST_CHECK(!restored->hasActiveAuthor());
        CTEST_CHECK(!restored->setAuthor(sampleAuthor("participant-1"), recordedAt.addSecs(2)));
        restored->recordChange(recordedAt.addSecs(3));
        // Backward clock changes cannot reorder the roster.
        CTEST_CHECK(restored->setAuthor(sampleAuthor("participant-2"), recordedAt.addSecs(-100)));
        CTEST_CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        CTEST_COMPARE(file.write(restored->dump()), restored->dump().size());
        file.close();
        CTEST_CHECK(file.open(QIODevice::ReadOnly));
        auto reopened = Authorship::fromDump(file.readAll());
        CTEST_CHECK(reopened);
        CTEST_COMPARE(reopened->firstEditor()->metadata().account.sub, QString("original"));
        CTEST_COMPARE(reopened->participants().size(), 2);
        CTEST_COMPARE(reopened->participants()[0].metadata().account.sub, QString("participant-1"));
        CTEST_COMPARE(reopened->participants()[1].metadata().account.sub, QString("participant-2"));
        CTEST_COMPARE(reopened->toJson()["authors"].toArray()[0], history.toJson()["authors"].toArray()[0]);
    }
    void legacyVersionOneMigration_data(provider_test::CTestRunner& ctest_runner)
    {
        const auto ctest_row = [&](const char* label, int authorCount, bool anonymousChange) {
            ctest_runner.run(std::string("legacyVersionOneMigration/") + label, [&] { legacyVersionOneMigration(authorCount, anonymousChange); });
        };
        ctest_row("empty", 0, false);
        ctest_row("anonymous", 0, true);
        ctest_row("single-editor", 1, false);
        ctest_row("ordered-participants-with-equal-times", 3, false);

    }
    void legacyVersionOneMigration(int authorCount, bool anonymousChange)
    {
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
        CTEST_CHECK_MESSAGE(restored, qPrintable(error));
        CTEST_CHECK(error.isEmpty());
        CTEST_COMPARE(restored->dump(), history.dump());
        CTEST_COMPARE(restored->revision(), history.revision());
        CTEST_CHECK(!restored->hasActiveAuthor());
        auto fromBytes = Authorship::fromDump(QJsonDocument(legacy).toJson(QJsonDocument::Compact));
        CTEST_CHECK(fromBytes);
        CTEST_COMPARE(fromBytes->dump(), restored->dump());
        restored->setAuthor(sampleAuthor("new-editor"), recordedAt.addSecs(1));
        CTEST_COMPARE(restored->firstEditor()->metadata().account.sub,
            authorCount == 0 ? QString("new-editor") : QString("legacy-0"));
        CTEST_COMPARE(restored->participants().size(), authorCount);

    }
    void fullLegacyMetadataMigratesWithoutDroppingAuthors_data(provider_test::CTestRunner& ctest_runner)
    {
        const auto ctest_row = [&](const char* label, int version) {
            ctest_runner.run(std::string("fullLegacyMetadataMigratesWithoutDroppingAuthors/") + label, [&] { fullLegacyMetadataMigratesWithoutDroppingAuthors(version); });
        };
        ctest_row("version-one", 1);
        ctest_row("version-two", 2);

    }
    void fullLegacyMetadataMigratesWithoutDroppingAuthors(int version)
    {
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
        CTEST_COMPARE(bytes.size(), legacyMaximumBytes);
        QString error;
        const auto restored = Authorship::fromDump(bytes, &error);
        CTEST_CHECK_MESSAGE(restored, qPrintable(error));
        CTEST_COMPARE(restored->toJson()["authors"], legacy["authors"]);
        CTEST_COMPARE(restored->revision(), history.revision());
        CTEST_COMPARE(restored->firstEditor()->metadata().account.sub, QString("legacy-0"));
        CTEST_COMPARE(restored->participants().size(), 255);
        CTEST_CHECK(restored->links().isEmpty());
        CTEST_CHECK(restored->dump().size() <= Authorship::MaximumBytes);
        const auto roundTrip = Authorship::fromDump(restored->dump());
        CTEST_CHECK(roundTrip);
        CTEST_COMPARE(roundTrip->dump(), restored->dump());

    }
    void malformedRoster_data(provider_test::CTestRunner& ctest_runner)
    {
        const auto ctest_row = [&](const char* label, QJsonObject json) {
            ctest_runner.run(std::string("malformedRoster/") + label, [&] { malformedRoster(json); });
        };
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
            ctest_row(name, json);
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
        ctest_row("empty-with-first-editor", empty);
        empty = Authorship().toJson();
        empty["participants"] = participants;
        ctest_row("empty-with-participants", empty);

    }
    void malformedRoster(QJsonObject json)
    {
        QString error;
        CTEST_CHECK(!Authorship::fromJson(json, &error));
        CTEST_CHECK(!error.isEmpty());
        CTEST_CHECK(!Authorship::fromDump(QJsonDocument(json).toJson(QJsonDocument::Compact), &error));
        CTEST_CHECK(!error.isEmpty());

    }
    void failedRegistrationsPreserveRoster()
    {
        Authorship history;
        const auto original = sampleAuthor("original");
        const auto participant = sampleAuthor("participant");
        CTEST_THROWS(std::invalid_argument, history.setAuthor(original, QDateTime()));
        CTEST_CHECK(history.isEmpty());
        CTEST_CHECK(!history.firstEditor());
        history.setAuthor(original, recordedAt);
        const auto before = history.dump();
        CTEST_THROWS(std::invalid_argument, history.setAuthor(participant, QDateTime()));
        CTEST_COMPARE(history.dump(), before);
        CTEST_CHECK(history.participants().isEmpty());
        history.recordChange(recordedAt.addSecs(1));
        CTEST_COMPARE(history.toJson()["lastAuthor"], history.toJson()["firstEditor"]);

        auto json = history.toJson();
        json["revision"] = QString::number(std::numeric_limits<quint64>::max());
        auto exhausted = Authorship::fromJson(json);
        CTEST_CHECK(exhausted);
        CTEST_CHECK(!exhausted->setAuthor(original, recordedAt));
        const auto exhaustedDump = exhausted->dump();
        CTEST_THROWS(std::overflow_error, exhausted->setAuthor(participant, recordedAt));
        CTEST_COMPARE(exhausted->dump(), exhaustedDump);
        CTEST_CHECK(exhausted->participants().isEmpty());
    }
    void capacityFailureNeverEvictsEditors_data(provider_test::CTestRunner& ctest_runner)
    {
        const auto ctest_row = [&](const char* label, int biographyLength) {
            ctest_runner.run(std::string("capacityFailureNeverEvictsEditors/") + label, [&] { capacityFailureNeverEvictsEditors(biographyLength); });
        };
        ctest_row("author-count-limit", 0);
        ctest_row("metadata-byte-limit", 4096);

    }
    void capacityFailureNeverEvictsEditors(int biographyLength)
    {
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
            CTEST_CHECK(author.setMetadata(metadata));
            const auto before = history.dump();
            try {
                CTEST_CHECK(history.setAuthor(author, recordedAt.addSecs(index)));
                ++accepted;
            } catch (const std::length_error&) {
                CTEST_COMPARE(history.dump(), before);
                rejected = true;
                break;
            }
        }
        CTEST_CHECK(rejected);
        if (biographyLength == 0) CTEST_COMPARE(accepted, 256);
        else CTEST_CHECK(accepted > 1 && accepted < 256);
        CTEST_COMPARE(history.firstEditor()->metadata().account.sub, QString("author-0"));
        CTEST_COMPARE(history.participants().size(), accepted - 1);
        const auto before = history.toJson();
        history.recordChange(recordedAt.addSecs(1000));
        CTEST_COMPARE(history.toJson()["firstEditor"], before["firstEditor"]);
        CTEST_COMPARE(history.toJson()["participants"], before["participants"]);
        CTEST_COMPARE(history.toJson()["lastAuthor"], before["authors"].toArray().last().toObject()["key"]);
        auto restored = Authorship::fromDump(history.dump());
        CTEST_CHECK(restored);
        CTEST_COMPARE(restored->participants().size(), accepted - 1);

    }
    void immediateDumpAndRoundTrip()
    {
        const auto at = QDateTime::fromString("2026-09-07T12:00:00Z", Qt::ISODate);
        auto author = FileAuthor::fromIisaccAccount({{"sub", "writer-1"}, {"email", "writer@example.com"}},
            QUrl("https://iisacc.com"), at);
        CTEST_CHECK(author);
        Authorship history;
        CTEST_CHECK(history.setAuthor(*author, at));
        const auto first = history.dump();
        CTEST_COMPARE(history.revision(), quint64(1));
        CTEST_CHECK(!history.setAuthor(*author, at));
        CTEST_COMPARE(history.dump(), first);
        history.recordChange(at.addSecs(1));
        CTEST_COMPARE(history.revision(), quint64(2));
        CTEST_CHECK(history.dump() != first);
        auto loaded = Authorship::fromJson(history.toJson());
        CTEST_CHECK(loaded);
        CTEST_COMPARE(loaded->dump(), history.dump());
        CTEST_CHECK(!loaded->hasActiveAuthor());
        loaded->recordChange(at.addSecs(2));
        CTEST_CHECK(loaded->toJson()["lastAuthor"].isNull());
    }
    void credentialsNeverEnterDump()
    {
        auto at = QDateTime::currentDateTimeUtc();
        auto author = FileAuthor::fromIisaccAccount({{"sub", "writer"}, {"email", "writer@example.com"}},
            QUrl("https://iisacc.com"), at);
        CTEST_CHECK(author);
        AuthenticationTokenInfo info;
        info.subject = "writer"; info.serviceOrigin = QUrl("https://iisacc.com");
        info.sessionId = QString(32, 'a'); info.issuedAt = at; info.expiresAt = at.addDays(1);
        auto token = AuthenticationToken::create(info, QByteArray(43, 's'));
        CTEST_CHECK(token); CTEST_CHECK(author->setAuthenticationToken(*token));
        Authorship history;
        CTEST_CHECK(history.setAuthor(*author, at));
        CTEST_CHECK(!history.dump().contains(QByteArray(43, 's')));
        CTEST_CHECK(!history.dump().contains("token"));
        CTEST_CHECK(!history.firstEditor()->authenticationToken());
        auto participant = sampleAuthor("participant");
        info.subject = "participant";
        token = AuthenticationToken::create(info, QByteArray(43, 's'));
        CTEST_CHECK(token);
        CTEST_CHECK(participant.setAuthenticationToken(*token));
        history.setAuthor(participant, at);
        CTEST_CHECK(!history.participants().first().authenticationToken());
        CTEST_CHECK(!history.dump().contains(QByteArray(43, 's')));
        CTEST_CHECK(!history.dump().contains("token"));
    }
    void invalidReadAndClockRollback()
    {
        Authorship history;
        const auto at = QDateTime::currentDateTimeUtc();
        history.recordChange(at);
        history.recordChange(at.addSecs(-100));
        CTEST_COMPARE(history.toJson()["modifiedAt"].toString(), at.toUTC().toString(Qt::ISODateWithMs));
        auto json = history.toJson(); json["revision"] = 1.5;
        CTEST_CHECK(!Authorship::fromJson(json));
        json = history.toJson(); json["token"] = "forbidden";
        CTEST_CHECK(!Authorship::fromJson(json));
        const auto before = history.dump();
        CTEST_THROWS(std::invalid_argument, history.recordChange(QDateTime()));
        CTEST_COMPARE(history.dump(), before);
    }
};
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    provider_test::CTestRunner runner;
    AuthorshipTest tests;
    runner.run("emptyFileLinkMetadata", [&] { tests.emptyFileLinkMetadata(); });
    runner.run("permanentEditorRoster", [&] { tests.permanentEditorRoster(); });
    runner.run("firstKnownEditorAfterAnonymousChanges", [&] { tests.firstKnownEditorAfterAnonymousChanges(); });
    runner.run("profileRefreshNeverReplacesIdentities", [&] { tests.profileRefreshNeverReplacesIdentities(); });
    runner.run("identitiesAreScopedToService", [&] { tests.identitiesAreScopedToService(); });
    runner.run("returnedSnapshotsCannotRemoveOrEditRoster", [&] { tests.returnedSnapshotsCannotRemoveOrEditRoster(); });
    runner.run("rosterSurvivesFileReloadAndFurtherEdits", [&] { tests.rosterSurvivesFileReloadAndFurtherEdits(); });
    runner.run("legacyVersionOneMigration_data", [&] { tests.legacyVersionOneMigration_data(runner); });
    runner.run("fullLegacyMetadataMigratesWithoutDroppingAuthors_data", [&] { tests.fullLegacyMetadataMigratesWithoutDroppingAuthors_data(runner); });
    runner.run("malformedRoster_data", [&] { tests.malformedRoster_data(runner); });
    runner.run("failedRegistrationsPreserveRoster", [&] { tests.failedRegistrationsPreserveRoster(); });
    runner.run("capacityFailureNeverEvictsEditors_data", [&] { tests.capacityFailureNeverEvictsEditors_data(runner); });
    runner.run("immediateDumpAndRoundTrip", [&] { tests.immediateDumpAndRoundTrip(); });
    runner.run("credentialsNeverEnterDump", [&] { tests.credentialsNeverEnterDump(); });
    runner.run("invalidReadAndClockRollback", [&] { tests.invalidReadAndClockRollback(); });
    return runner.result();
}
