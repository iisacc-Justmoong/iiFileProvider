#include <iiFileProvider.h>

#include <QtCore/QDebug>
#include "iiFileProviderDevice.h"
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtTest/QTest>

using namespace iiFileProvider;

namespace {
const QUrl origin(QStringLiteral("https://iisacc.com"));
const QDateTime captured = QDateTime::fromString(QStringLiteral("2026-09-07T10:00:00Z"), Qt::ISODate);

QJsonObject account()
{
    return {{"sub", "abc_123-def"}, {"email", "author@example.com"},
            {"displayName", "작가"}, {"userId", "@author_1"},
            {"societyCloudMembership", "Pro"},
            {"avatarUrl", "/media/avatars/" + QString(64, 'a') + ".webp"}};
}

QJsonObject appSession()
{
    return {{"account", account()}, {"session", QJsonObject{
        {"id", QString(32, 'b')}, {"client", "app"}, {"current", true},
        {"createdAt", "2026-09-07T09:00:00Z"}, {"lastSeenAt", "2026-09-07T09:59:00Z"},
        {"expiresAt", "2026-10-07T09:00:00Z"},
        {"device", QJsonObject{{"id", QString(64, 'c')}, {"type", "pc"},
            {"name", "Studio Mac"}, {"platform", "macOS"}, {"osVersion", "27.0"},
            {"appId", "com.iisacc.society"}, {"appVersion", "0.1.0"}}}}},
        {"limits", QJsonObject{{"pc", 2}, {"tablet", 2}}}};
}

AuthenticationToken token(QString subject = QStringLiteral("abc_123-def"))
{
    AuthenticationTokenInfo info;
    info.kind = AuthenticationTokenKind::IisaccSession;
    info.subject = subject;
    info.serviceOrigin = origin;
    info.sessionId = QString(32, 'b');
    info.issuedAt = captured.addSecs(-3600);
    info.expiresAt = captured.addDays(30);
    return *AuthenticationToken::create(info, QByteArray(43, 's'));
}
}

class AuthorContractTest final : public QObject
{
    Q_OBJECT
private slots:
    void pairedAccountDetails()
    {
        iiFileProvider::FileDevice fixture(QFINDTESTDATA("fixtures/iisacc-account.json"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto snapshot = QJsonDocument::fromJson(fixture.readAll()).object();
        QString error;
        auto author = FileAuthor::fromIisaccAccount(snapshot, origin, captured, &error);
        QVERIFY2(author.has_value(), qPrintable(error));
        QCOMPARE(author->toJson().value("details"), snapshot.value("authorDetails"));
        auto response = appSession();
        response["account"] = snapshot;
        auto appAuthor = FileAuthor::fromIisaccAppSession(response, origin, captured, &error);
        QVERIFY2(appAuthor.has_value(), qPrintable(error));
        QCOMPARE(appAuthor->toJson().value("details"), snapshot.value("authorDetails"));
        QVERIFY(!appAuthor->metadata().attribution.createdAt.isValid());
    }

    void profileUpdateHasOnlyEditableAccountFields()
    {
        auto author = FileAuthor::fromIisaccAppSession(appSession(), origin, captured);
        QVERIFY(author);
        QVERIFY(author->setAuthenticationToken(token()));
        auto metadata = author->metadata();
        metadata.details.fullName = "Studio Author";
        metadata.attribution.documentId = "private-document-id";
        metadata.attribution.roles = {"editor"};
        QVERIFY(author->setMetadata(metadata));
        const auto update = author->toIisaccProfileUpdate();
        QCOMPARE(update.keys(), QStringList({"authorDetails", "displayName"}));
        QCOMPARE(update.value("authorDetails"), author->toJson().value("details"));
        const auto bytes = QJsonDocument(update).toJson();
        QVERIFY(!bytes.contains("private-document-id"));
        QVERIFY(!bytes.contains(QByteArray(43, 's')));
        QVERIFY(!bytes.contains(QByteArray(32, 'b')));
        auto updatedAccount = account();
        updatedAccount["displayName"] = update["displayName"];
        updatedAccount["authorDetails"] = update["authorDetails"];
        auto loaded = FileAuthor::fromIisaccAccount(updatedAccount, origin, captured);
        QVERIFY(loaded);
        QCOMPARE(loaded->toIisaccProfileUpdate(), update);
        QVERIFY(!loaded->authenticationToken());
        QVERIFY(!loaded->loginSession());
        QVERIFY(loaded->metadata().attribution.roles.isEmpty());
    }

    void invalidAccountAuthorDetails_data()
    {
        QTest::addColumn<QJsonValue>("details");
        QTest::newRow("null") << QJsonValue(QJsonValue::Null);
        QTest::newRow("array") << QJsonValue(QJsonArray{});
        QTest::newRow("scalar") << QJsonValue("invalid");
        QTest::newRow("unknown") << QJsonValue(QJsonObject{{"refreshToken", "sentinel"}});
        QTest::newRow("phone-type") << QJsonValue(QJsonObject{{"phoneNumber", 7}});
        QTest::newRow("name-control") << QJsonValue(QJsonObject{{"fullName", "a\nb"}});
        QTest::newRow("name-limit") << QJsonValue(QJsonObject{{"fullName", QString(161, 'x')}});
        QTest::newRow("bad-email") << QJsonValue(QJsonObject{{"contactEmail", "not-an-email"}});
        QTest::newRow("bad-locale") << QJsonValue(QJsonObject{{"locale", "ko_KR"}});
        QTest::newRow("bad-zone") << QJsonValue(QJsonObject{{"timeZone", "Mars/Nowhere"}});
        QTest::newRow("bad-country") << QJsonValue(QJsonObject{{"countryCode", "kr"}});
        QTest::newRow("link-credential") << QJsonValue(QJsonObject{{"links", QJsonArray{QJsonObject{{"relation", "site"}, {"url", "https://user:secret@example.org"}}}}});
        QTest::newRow("http-link") << QJsonValue(QJsonObject{{"links", QJsonArray{QJsonObject{{"relation", "site"}, {"url", "http://example.org"}}}}});
        QTest::newRow("identifier-null") << QJsonValue(QJsonObject{{"identifiers", QJsonArray{QJsonObject{{"scheme", "studio"}, {"value", QJsonValue::Null}}}}});
    }

    void invalidAccountAuthorDetails()
    {
        QFETCH(QJsonValue, details);
        auto snapshot = account();
        snapshot["authorDetails"] = details;
        QString error;
        QVERIFY(!FileAuthor::fromIisaccAccount(snapshot, origin, captured, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!error.contains("sentinel"));
    }

    void richAccountBudgetsAndNormalization()
    {
        QJsonArray links;
        for (int i = 0; i < 32; ++i)
            links.append(QJsonObject{{"relation", "portfolio"}, {"url", "https://example.org/" + QString(1500, 'a')}});
        auto snapshot = account();
        snapshot["authorDetails"] = QJsonObject{{"fullName", "  e\u0301  "},
            {"contactEmail", "  STUDIO@EXAMPLE.ORG  "}, {"links", links},
            {"biography", "First line\nSecond line"}, {"timeZone", "UTC+09:00"}};
        QVERIFY(QJsonDocument(snapshot).toJson(QJsonDocument::Compact).size() > 32768);
        auto author = FileAuthor::fromIisaccAccount(snapshot, origin, captured);
        QVERIFY(author);
        QCOMPARE(author->metadata().details.fullName, QString::fromUtf8("é"));
        QCOMPARE(author->metadata().details.contactEmail, QString("studio@example.org"));
        auto response = appSession(); response["account"] = snapshot;
        QVERIFY(FileAuthor::fromIisaccAppSession(response, origin, captured));
        QVERIFY(FileAuthor::fromJson(author->toJson()));
        links.append(links.first());
        snapshot["authorDetails"] = QJsonObject{{"links", links}};
        QVERIFY(!FileAuthor::fromIisaccAccount(snapshot, origin, captured));
        links.removeLast();
        for (auto value : links) value = QJsonObject{{"relation", "site"}, {"url", "https://example.org/" + QString(2029, 'a')}};
        snapshot["authorDetails"] = QJsonObject{{"links", links}};
        QVERIFY(!FileAuthor::fromIisaccAccount(snapshot, origin, captured));
        snapshot["authorDetails"] = QJsonObject{};
        snapshot["internalPadding"] = QString(131072, 'x');
        QVERIFY(!FileAuthor::fromIisaccAccount(snapshot, origin, captured));
    }

    void normalizedDetailsBudgetIncludesDefaultFields()
    {
        QJsonArray links;
        for (int i = 0; i < 32; ++i)
            links.append(QJsonObject{{"relation", "p"}, {"url", "https://example.org/" + QString(1990, 'x')}});
        const QJsonObject details{{"links", links}};
        QVERIFY(QJsonDocument(details).toJson(QJsonDocument::Compact).size() <= 65536);
        auto author = FileAuthor::fromIisaccAccount(account(), origin, captured);
        auto json = author->toJson();
        json["details"] = details;
        QVERIFY(!FileAuthor::fromJson(json));
        auto snapshot = account();
        snapshot["authorDetails"] = details;
        QVERIFY(!FileAuthor::fromIisaccAccount(snapshot, origin, captured));
    }

    void mapsCurrentAccount()
    {
        auto author = FileAuthor::fromIisaccAccount(account(), origin, captured);
        QVERIFY(author);
        QCOMPARE(author->metadata().account.sub, QString("abc_123-def"));
        QCOMPARE(author->metadata().account.userId, QString("@author_1"));
        QCOMPARE(author->metadata().account.societyCloudMembership, SocietyCloudMembership::Pro);
        QCOMPARE(author->metadata().account.avatarUrl.host(), QString("iisacc.com"));
        QCOMPARE(author->metadata().capturedAt, captured);
        QVERIFY(!author->authenticationToken());
        QVERIFY(!author->metadata().attribution.createdAt.isValid());
    }

    void legacyAccountAndExplicitNormalization()
    {
        QJsonObject legacy{{"sub", "legacy"}, {"email", "  AUTHOR@EXAMPLE.COM  "}};
        auto author = FileAuthor::fromIisaccAccount(legacy, origin, captured);
        QVERIFY(author);
        QCOMPARE(author->metadata().account.email, QString("author@example.com"));
        QCOMPARE(author->displayLabel(), QString("author@example.com"));
        QCOMPARE(author->metadata().account.societyCloudMembership, SocietyCloudMembership::Free);
        QVERIFY(author->metadata().account.userId.isEmpty());
        QVERIFY(author->metadata().account.avatarUrl.isEmpty());
        auto json = account();
        json["displayName"] = "  e\u0301  ";
        auto normalized = FileAuthor::fromIisaccAccount(json, origin, captured);
        QVERIFY(normalized);
        QCOMPARE(normalized->displayLabel(), QString::fromUtf8("é"));
    }

    void invalidAccounts_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QJsonValue>("value");
        QTest::newRow("subject-type") << QString("sub") << QJsonValue(17);
        QTest::newRow("subject-empty") << QString("sub") << QJsonValue("");
        QTest::newRow("email-type") << QString("email") << QJsonValue(false);
        QTest::newRow("email-format") << QString("email") << QJsonValue("a@b");
        QTest::newRow("name-null") << QString("displayName") << QJsonValue(QJsonValue::Null);
        QTest::newRow("name-control") << QString("displayName") << QJsonValue("a\nb");
        QTest::newRow("name-long") << QString("displayName") << QJsonValue(QString(81, 'x'));
        QTest::newRow("handle") << QString("userId") << QJsonValue("author");
        QTest::newRow("membership") << QString("societyCloudMembership") << QJsonValue("Admin");
        QTest::newRow("avatar-external") << QString("avatarUrl") << QJsonValue("https://evil.example/avatar.webp");
        QTest::newRow("avatar-traversal") << QString("avatarUrl") << QJsonValue("/media/avatars/../secret");
    }

    void invalidAccounts()
    {
        QFETCH(QString, key);
        QFETCH(QJsonValue, value);
        auto json = account();
        json[key] = value;
        QString error;
        QVERIFY(!FileAuthor::fromIisaccAccount(json, origin, captured, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!error.contains("author@example.com"));
    }

    void unicodeCodePointLimit()
    {
        auto json = account();
        const auto emoji = QString::fromUcs4(U"😀");
        json["displayName"] = emoji.repeated(80);
        QVERIFY(FileAuthor::fromIisaccAccount(json, origin, captured));
        json["displayName"] = emoji.repeated(81);
        QVERIFY(!FileAuthor::fromIisaccAccount(json, origin, captured));
    }

    void richMetadataRoundTrip()
    {
        auto author = FileAuthor::fromIisaccAccount(account(), origin, captured);
        QVERIFY(author);
        auto data = author->metadata();
        data.details.fullName = "Author Example";
        data.details.givenName = "Author";
        data.details.familyName = "Example";
        data.details.organization = "Example Studio";
        data.details.department = "Design";
        data.details.team = "Canvas";
        data.details.jobTitle = "Art Director";
        data.details.biography = "An independent artist.";
        data.details.contactEmail = "studio@example.com";
        data.details.phoneNumber = "+82 2 0000 0000";
        data.details.locale = "ko-KR";
        data.details.timeZone = "Asia/Seoul";
        data.details.countryCode = "KR";
        data.details.region = "Seoul";
        data.details.city = "Seoul";
        data.details.links = {{"portfolio", "Portfolio", QUrl("https://example.com/portfolio")}};
        data.details.identifiers = {{"ORCID", "0000-0000-0000-0000"}};
        data.attribution.roles = {"creator", "editor"};
        data.attribution.credit = "Design by Author Example";
        data.attribution.copyrightNotice = "Copyright 2026 Example Studio";
        data.attribution.licenseIdentifier = "CC-BY-4.0";
        data.attribution.licenseUrl = QUrl("https://creativecommons.org/licenses/by/4.0/");
        data.attribution.createdAt = captured.addDays(-1);
        data.attribution.modifiedAt = captured;
        data.attribution.documentId = "document-1";
        data.attribution.projectId = "project-1";
        data.attribution.workspaceId = "workspace-1";
        QVERIFY(author->setMetadata(data));
        const auto json = author->toJson();
        QString error;
        auto loaded = FileAuthor::fromJson(json, &error);
        QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->toJson(), json);
        QVERIFY(!loaded->authenticationToken());
    }

    void importedPrivateFieldsAreNotCopied()
    {
        auto json = account();
        for (const auto* key : {"password", "refreshToken", "token_hash", "emailMarketingConsent", "termsAcceptedAt"})
            json[key] = "private-sentinel";
        auto author = FileAuthor::fromIisaccAccount(json, origin, captured);
        QVERIFY(author);
        QVERIFY(!QJsonDocument(author->toJson()).toJson().contains("private-sentinel"));
    }

    void appSessionCapturesDeviceWithoutInventingCreationTime()
    {
        auto author = FileAuthor::fromIisaccAppSession(appSession(), origin, captured);
        QVERIFY(author);
        QVERIFY(author->loginSession());
        QVERIFY(author->metadata().device);
        QCOMPARE(author->metadata().device->appId, QString("com.iisacc.society"));
        QCOMPARE(author->loginSession()->id, QString(32, 'b'));
        QVERIFY(!author->metadata().attribution.createdAt.isValid());
        QVERIFY(!author->toJson().contains("session"));
    }

    void incompleteAndExpiredAppSessionsFail()
    {
        auto response = appSession();
        auto session = response["session"].toObject();
        auto device = session["device"].toObject();
        device["type"] = "phone";
        session["device"] = device;
        response["session"] = session;
        QVERIFY(!FileAuthor::fromIisaccAppSession(response, origin, captured));
        response = appSession();
        session = response["session"].toObject();
        session["expiresAt"] = "2026-09-07T10:00:00Z";
        response["session"] = session;
        QVERIFY(!FileAuthor::fromIisaccAppSession(response, origin, captured));
        QVERIFY(!FileAuthor::fromIisaccAppSession(QJsonObject{{"account", account()}}, origin, captured));
    }

    void tokenBindingRedactionAndRoundTrip()
    {
        auto author = FileAuthor::fromIisaccAppSession(appSession(), origin, captured);
        QVERIFY(author);
        QVERIFY(author->setAuthenticationToken(token()));
        QCOMPARE(author->authenticationToken()->secret(), QByteArray(43, 's'));
        const auto metadata = QJsonDocument(author->toJson()).toJson();
        QVERIFY(!metadata.contains(QByteArray(43, 's')));
        QVERIFY(!metadata.contains("authentication"));
        QString debug;
        { QDebug stream(&debug); stream << *author->authenticationToken(); }
        QVERIFY(!debug.contains(QString(43, 's')));
        auto loaded = FileAuthor::fromJson(author->toJson());
        QVERIFY(loaded);
        QVERIFY(!loaded->authenticationToken());
        QVERIFY(!author->setAuthenticationToken(token("different-user")));
        QVERIFY(author->authenticationToken());
        author->clearAuthenticationToken();
        QVERIFY(!author->authenticationToken());
    }

    void tokenTimeBoundsAndValidation()
    {
        auto credential = token();
        QVERIFY(credential.isWithinValidityWindow(captured));
        QVERIFY(!credential.isWithinValidityWindow(credential.info().expiresAt));
        QVERIFY(!credential.isWithinValidityWindow(credential.info().issuedAt.addSecs(-1)));
        auto info = credential.info();
        QVERIFY(!AuthenticationToken::create(info, QByteArray("bad\r\nCookie: injected")));
        QVERIFY(!AuthenticationToken::create(info, QByteArray(42, 's')));
        info.expiresAt = info.issuedAt;
        QVERIFY(!AuthenticationToken::create(info, QByteArray(43, 's')));
        info = credential.info();
        info.notBefore = captured.addSecs(10);
        auto future = AuthenticationToken::create(info, QByteArray(43, 's'));
        QVERIFY(future);
        QVERIFY(!future->isWithinValidityWindow(captured));
        QVERIFY(future->isWithinValidityWindow(captured.addSecs(10)));
    }

    void wrongOriginAndSessionCannotBind()
    {
        auto author = FileAuthor::fromIisaccAppSession(appSession(), origin, captured);
        QVERIFY(author);
        auto info = token().info();
        info.serviceOrigin = QUrl("https://another.example");
        auto credential = AuthenticationToken::create(info, QByteArray(43, 's'));
        QVERIFY(credential);
        QVERIFY(!author->setAuthenticationToken(*credential));
        info.serviceOrigin = origin;
        info.sessionId = QString(32, 'd');
        credential = AuthenticationToken::create(info, QByteArray(43, 's'));
        QVERIFY(credential);
        QVERIFY(!author->setAuthenticationToken(*credential));
    }

    void metadataUpdatesAreAtomicAndIdentityChangesClearCredentials()
    {
        auto author = FileAuthor::fromIisaccAppSession(appSession(), origin, captured);
        QVERIFY(author);
        QVERIFY(author->setAuthenticationToken(token()));
        const auto before = author->toJson();
        auto invalid = author->metadata();
        invalid.attribution.createdAt = captured;
        invalid.attribution.modifiedAt = captured.addSecs(-1);
        QVERIFY(!author->setMetadata(invalid));
        QCOMPARE(author->toJson(), before);
        QVERIFY(author->authenticationToken());
        auto changed = author->metadata();
        changed.account.sub = "different-user";
        QVERIFY(author->setMetadata(changed));
        QVERIFY(!author->authenticationToken());
        QVERIFY(!author->loginSession());
    }

    void schemaAndTimezoneAreExplicit()
    {
        auto author = FileAuthor::fromIisaccAccount(account(), origin, captured);
        QVERIFY(author);
        auto json = author->toJson();
        json["schemaVersion"] = 1.5;
        QVERIFY(!FileAuthor::fromJson(json));
        json = author->toJson();
        json["capturedAt"] = "2026-09-07T10:00:00";
        QVERIFY(!FileAuthor::fromJson(json));
        json = author->toJson();
        json["authenticationToken"] = "do-not-import";
        QVERIFY(!FileAuthor::fromJson(json));
        QVERIFY(!FileAuthor::fromIisaccAccount(account(), QUrl("http://iisacc.com"), captured));
        QVERIFY(!FileAuthor::fromIisaccAccount(account(), origin, QDateTime()));
    }

    void malformedMetadataIsRejected_data()
    {
        QTest::addColumn<QString>("section");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QJsonValue>("value");
        QTest::newRow("profile-type") << QString("details") << QString("fullName") << QJsonValue(17);
        QTest::newRow("profile-timezone") << QString("details") << QString("timeZone") << QJsonValue("Not/A_Zone");
        QTest::newRow("profile-country") << QString("details") << QString("countryCode") << QJsonValue("KOR");
        QTest::newRow("profile-extra-token") << QString("details") << QString("accessToken") << QJsonValue("private");
        QTest::newRow("profile-identifiers") << QString("details") << QString("identifiers") << QJsonValue(QJsonArray{17});
        QTest::newRow("roles-duplicates") << QString("attribution") << QString("roles") << QJsonValue(QJsonArray{"creator", "creator"});
        QTest::newRow("roles-type") << QString("attribution") << QString("roles") << QJsonValue("creator");
        QTest::newRow("license-file-url") << QString("attribution") << QString("licenseUrl") << QJsonValue("file:///private/license");
        QTest::newRow("date-number") << QString("attribution") << QString("createdAt") << QJsonValue(17);
        QTest::newRow("date-invalid-day") << QString("attribution") << QString("createdAt") << QJsonValue("2026-02-30T12:00:00Z");
    }

    void malformedMetadataIsRejected()
    {
        QFETCH(QString, section);
        QFETCH(QString, key);
        QFETCH(QJsonValue, value);
        auto author = FileAuthor::fromIisaccAccount(account(), origin, captured);
        QVERIFY(author);
        auto json = author->toJson();
        auto nested = json[section].toObject();
        nested[key] = value;
        json[section] = nested;
        QVERIFY(!FileAuthor::fromJson(json));
    }

    void timezoneOffsetsAreNormalized()
    {
        auto author = FileAuthor::fromIisaccAccount(account(), origin, captured);
        QVERIFY(author);
        auto json = author->toJson();
        json["capturedAt"] = "2026-09-07T19:00:00+09:00";
        auto loaded = FileAuthor::fromJson(json);
        QVERIFY(loaded);
        QCOMPARE(loaded->metadata().capturedAt, captured);
        QCOMPARE(loaded->toJson()["capturedAt"].toString(), QString("2026-09-07T10:00:00.000Z"));
    }

    void accountAndAppSizeAndStateLimits()
    {
        auto json = account();
        json["ignored"] = QString(131073, 'x');
        QVERIFY(!FileAuthor::fromIisaccAccount(json, origin, captured));
        auto response = appSession();
        response["error"] = QJsonObject{{"code", "unauthenticated"}};
        QVERIFY(!FileAuthor::fromIisaccAppSession(response, origin, captured));
        response = appSession();
        response["challenge"] = QJsonObject{{"type", "email_code"}};
        QVERIFY(!FileAuthor::fromIisaccAppSession(response, origin, captured));
        response = appSession();
        auto session = response["session"].toObject();
        session["current"] = "true";
        response["session"] = session;
        QVERIFY(!FileAuthor::fromIisaccAppSession(response, origin, captured));
    }

    void tokenKindsRemainOpaqueAndSafe()
    {
        for (auto kind : {AuthenticationTokenKind::CognitoId, AuthenticationTokenKind::CognitoAccess,
                          AuthenticationTokenKind::CognitoRefresh}) {
            auto info = token().info();
            info.kind = kind;
            info.issuer = "https://cognito-idp.example.invalid/pool";
            info.audience = "example-client";
            info.scopes = {"files:read", "files:write"};
            auto credential = AuthenticationToken::create(info, "opaque-test-credential");
            QVERIFY(credential);
            QCOMPARE(credential->info().subject, info.subject);
            const auto redacted = QJsonDocument(credential->toRedactedJson()).toJson();
            QVERIFY(!redacted.contains("opaque-test-credential"));
            QVERIFY(redacted.contains("[REDACTED]"));
            QVERIFY(!AuthenticationToken::create(info, QByteArray(16385, 'x')));
        }
        auto info = token().info();
        info.kind = static_cast<AuthenticationTokenKind>(100);
        QVERIFY(!AuthenticationToken::create(info, QByteArray(43, 's')));
        info = token().info();
        info.scopes = {"read write"};
        QVERIFY(!AuthenticationToken::create(info, QByteArray(43, 's')));
    }

    void profileEditsKeepBindingButDeviceChangesClearIt()
    {
        auto author = FileAuthor::fromIisaccAppSession(appSession(), origin, captured);
        QVERIFY(author);
        QVERIFY(author->setAuthenticationToken(token()));
        auto edited = author->metadata();
        edited.details.biography = "Updated bio\nWith another line.";
        QVERIFY(author->setMetadata(edited));
        QVERIFY(author->authenticationToken());
        QVERIFY(author->loginSession());
        edited.device->appId = "com.iisacc.other";
        QVERIFY(author->setMetadata(edited));
        QVERIFY(!author->authenticationToken());
        QVERIFY(!author->loginSession());
    }
};

QTEST_GUILESS_MAIN(AuthorContractTest)
#include "author_contract.moc"
