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
const auto at = QDateTime::fromString("2026-09-08T12:00:00Z", Qt::ISODate);
FileAuthor author(const QString& subject)
{
    return FileAuthor::fromIisaccAccount({{"sub", subject}, {"email", "writer@example.com"}},
        QUrl("https://iisacc.com"), at).value();
}
FileLink link(const QString& entry) { return FileLink::fromString(entry).value(); }
}

class FileLinksTest : public QObject {
    Q_OBJECT
private slots:
    void preserveCustomAndSignedUrlText()
    {
        for (const auto& raw : {"ipfs://QmAbCdEfGh/Asset%7e?signature=a%2fb%2Bc",
                 "society://ContainerABC/Files/AssetXYZ", "https://EXAMPLE.com/file%7e?signature=a%2fb%2bc#%63"}) {
            const auto input = QString("[원본|%1]").arg(raw);
            auto parsed = FileLink::fromString(input);
            QVERIFY(parsed);
            QCOMPARE(parsed->urlText(), QString(raw));
            QCOMPARE(parsed->toString(), input);
            QCOMPARE(parsed->toJson()["url"].toString(), QString(raw));
            auto pair = FileLink::create("원본", QString(raw));
            QVERIFY(pair);
            QCOMPARE(pair->toString(), input);
            Authorship history;
            history.setAuthor(author("first"), {*parsed}, at);
            auto restored = Authorship::fromDump(history.dump());
            QVERIFY(restored);
            QCOMPARE(restored->links().first().toString(), input);
        }
    }
    void urlKinds_data()
    {
        QTest::addColumn<QString>("url");
        QTest::newRow("society") << "society://container/Files/artwork.iisc?version=2#layer-1";
        QTest::newRow("local-file") << "file:///Volumes/Storage/Workspace/Assets/my%20image.png";
        QTest::newRow("windows-file") << "file:///C:/Users/Writer/Documents/artwork.iisc";
        QTest::newRow("https") << "https://example.com/file?token=explicit-url-value&x=a+b%2Bc#preview";
        QTest::newRow("http") << "http://localhost:8080/file";
        QTest::newRow("smb") << "smb://writer@nas.local/shared/artwork.iisc";
        QTest::newRow("ftp") << "ftp://writer:explicit-password@example.com/file";
        QTest::newRow("sftp") << "sftp://example.com:2222/files/project";
        QTest::newRow("ipfs") << "ipfs://bafyexample/assets/image.png";
        QTest::newRow("urn") << "urn:uuid:12345678-1234-1234-1234-123456789012";
        QTest::newRow("mailto") << "mailto:writer@example.com?subject=File%20review";
        QTest::newRow("tel") << "tel:+821012345678";
        QTest::newRow("data") << "data:text/plain;charset=utf-8,hello%20world%7Csample";
        QTest::newRow("android-content") << "content://com.example.documents/document/primary%3ADocuments%2Fimage.png";
        QTest::newRow("qt-resource") << "qrc:/templates/default.iisc";
        QTest::newRow("custom-opaque") << "my.app+files-v2:workspace/project?id=42#item";
        QTest::newRow("relative") << "../assets/reference.svg?scale=2#layer";
        QTest::newRow("fragment") << "#layer-1";
        QTest::newRow("network-path") << "//nas.local/shared/file.iisc";
        QTest::newRow("ipv6-brackets") << "smb://[2001:db8::1]/share/file";
        QTest::newRow("empty-query-fragment") << "society:document?#";
        QTest::newRow("encoded-delimiters") << "society:files/a%7Cb%5D?q=a+b%2Bc";
        QTest::newRow("inert-script-url") << "javascript:alert(1)";
    }
    void urlKinds()
    {
        QFETCH(QString, url);
        const auto entry = QString("[자료|%1]").arg(url);
        QString error = "stale";
        const auto parsed = FileLink::fromString(entry, &error);
        QVERIFY2(parsed, qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(parsed->name(), QString("자료"));
        QCOMPARE(parsed->url().toString(QUrl::FullyEncoded), url);
        QCOMPARE(parsed->toString(), entry);
        const auto restored = FileLink::fromJson(parsed->toJson());
        QVERIFY(restored);
        QVERIFY(*restored == *parsed);
        Authorship history;
        QVERIFY(history.setLinks({*parsed}, at));
        auto loaded = Authorship::fromDump(history.dump());
        QVERIFY(loaded);
        QCOMPARE(loaded->links().size(), 1);
        QCOMPARE(loaded->links().first().toString(), entry);
    }
    void unicodeAndLimits()
    {
        const auto local = QUrl::fromLocalFile("/Volumes/Storage/Workspace/Assets/그림 파일.png");
        auto value = FileLink::create("  원본 자료  ", local);
        QVERIFY(value);
        QCOMPARE(value->name(), QString("원본 자료"));
        QCOMPARE(value->url().toLocalFile(), QString("/Volumes/Storage/Workspace/Assets/그림 파일.png"));
        QVERIFY(value->toString().contains("%20"));
        auto restored = FileLink::fromString(value->toString());
        QVERIFY(restored && *restored == *value);
        const auto unicodeUrl = FileLink::fromString("[자료|society://files/원본/이미지.png]");
        QVERIFY(unicodeUrl);
        QCOMPARE(unicodeUrl->url().path(), QString("/원본/이미지.png"));
        const auto emoji = QString::fromUcs4(U"🚀");
        QVERIFY(FileLink::create(emoji.repeated(FileLink::MaximumNameLength), local));
        QVERIFY(!FileLink::create(emoji.repeated(FileLink::MaximumNameLength + 1), local));
        const QString maximumUrl = "society:" + QString(FileLink::MaximumUrlBytes - 8, 'a');
        auto maximum = FileLink::fromString("[자료|" + maximumUrl + ']');
        QVERIFY(maximum);
        QCOMPARE(maximum->url().toEncoded().size(), FileLink::MaximumUrlBytes);
        QVERIFY(!FileLink::fromString("[자료|" + maximumUrl + "a]"));
        QVERIFY(!FileLink::create("자료", QUrl("society:" + QString(3000, QChar(0xAC00)))));
        QVERIFY(!FileLink::create("name|with-separator", local));
        QVERIFY(!FileLink::create("자료", QUrl()));
    }
    void invalidNotation_data()
    {
        QTest::addColumn<QString>("entry");
        QTest::newRow("no-brackets") << "자료|society:doc";
        QTest::newRow("no-separator") << "[society:doc]";
        QTest::newRow("empty-name") << "[|society:doc]";
        QTest::newRow("blank-name") << "[   |society:doc]";
        QTest::newRow("empty-url") << "[자료|]";
        QTest::newRow("malformed-percent") << "[자료|society:doc%ZZ]";
        QTest::newRow("truncated-percent") << "[자료|society:doc%2]";
        QTest::newRow("bad-authority") << "[자료|https://[broken/file]";
        QTest::newRow("bad-port") << "[자료|https://example.com:invalid/file]";
        QTest::newRow("unencoded-space") << "[자료|file:///files/my file]";
        QTest::newRow("url-newline") << "[자료|society:doc\nvalue]";
        QTest::newRow("name-newline") << "[자\n료|society:doc]";
        QTest::newRow("invalid-utf16") << (QString("[자료|society:") + QChar(0xD800) + ']');
    }
    void invalidNotation()
    {
        QFETCH(QString, entry);
        QString error;
        QVERIFY(!FileLink::fromString(entry, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!error.contains("example.com"));
    }
    void authorAndLinksCommitOnce()
    {
        const auto first = author("first");
        const auto second = author("second");
        const QList<FileLink> references{link("[Society|society://container/Files/artwork.iisc]"),
            link("[원본|file:///files/original.png]")};
        Authorship history;
        QVERIFY(history.setAuthor(first, references, at));
        QCOMPARE(history.revision(), quint64(1));
        QCOMPARE(history.links().size(), 2);
        QVERIFY(!history.setAuthor(first, references, at.addSecs(1)));
        QCOMPARE(history.revision(), quint64(1));
        QVERIFY(history.setAuthor(second, at.addSecs(2)));
        QCOMPARE(history.revision(), quint64(2));
        QCOMPARE(history.links().first().toString(), references.first().toString());
        const auto roster = history.toJson();
        QVERIFY(history.setAuthor(second, {link("[참고|ipfs://bafyexample/reference]")}, at.addSecs(3)));
        QCOMPARE(history.revision(), quint64(3));
        QCOMPARE(history.toJson()["firstEditor"], roster["firstEditor"]);
        QCOMPARE(history.toJson()["participants"], roster["participants"]);
        QCOMPARE(history.links().size(), 1);
        auto copy = history.links();
        copy.clear();
        QCOMPARE(history.links().size(), 1);
        QVERIFY(history.setAuthor(second, QList<FileLink>{}, at.addSecs(4)));
        QVERIFY(history.links().isEmpty());
        QCOMPARE(history.participants().size(), 1);
        QCOMPARE(history.firstEditor()->metadata().account.sub, QString("first"));
    }
    void fileRoundTripAndLinkUpdates()
    {
        Authorship history;
        QVERIFY(history.setLinksFromStrings({"[Society|society:document-1]", "[원본|../original.png]"}, at));
        QVERIFY(!history.firstEditor());
        QCOMPARE(history.revision(), quint64(1));
        QTemporaryDir directory(QDir::currentPath() + "/named-url-XXXXXX");
        QVERIFY(directory.isValid());
        iiFileProvider::FileDevice file(directory.filePath("metadata.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(history.dump()), history.dump().size());
        file.close();
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto restored = Authorship::fromDump(file.readAll());
        QVERIFY(restored);
        QCOMPARE(restored->dump(), history.dump());
        QVERIFY(restored->setAuthor(author("first"), at.addSecs(1)));
        QCOMPARE(restored->links().size(), 2);
        restored->clearActiveAuthor();
        restored->recordChange(at.addSecs(2));
        QCOMPARE(restored->links().size(), 2);
        QVERIFY(restored->setLinks({}, at.addSecs(3)));
        QVERIFY(!restored->setLinks({}, at.addSecs(4)));
        QCOMPARE(restored->firstEditor()->metadata().account.sub, QString("first"));
    }
    void invalidBatchAndCapacityAreAtomic()
    {
        Authorship history;
        const auto reference = link("[Society|society:doc]");
        history.setAuthor(author("first"), {reference}, at);
        const auto before = history.dump();
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
            history.setLinksFromStrings({"[유효|file:///files/item]", "[잘못됨|https://example.com/%ZZ]"}, at));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
            history.setAuthor(author("second"), {reference, reference}, at));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
            history.setAuthor(author("second"), QList<FileLink>{}, QDateTime()));
        QCOMPARE(history.dump(), before);
        QVERIFY(history.participants().isEmpty());
        history.recordChange(at.addSecs(1));
        QCOMPARE(history.toJson()["lastAuthor"], history.toJson()["firstEditor"]);
        QList<FileLink> references;
        for (qsizetype index = 0; index < Authorship::MaximumLinks; ++index)
            references.append(link(QString("[자료|society:doc-%1]").arg(index)));
        QVERIFY(history.setLinks(references, at.addSecs(2))); // Labels may repeat; complete pairs may not.
        const auto full = history.dump();
        references.append(link("[추가|society:overflow]"));
        QVERIFY_THROWS_EXCEPTION(std::length_error, history.setAuthor(author("second"), references, at));
        QCOMPARE(history.dump(), full);
        QVERIFY(history.participants().isEmpty());
        auto exhaustedJson = history.toJson();
        exhaustedJson["revision"] = QString::number(std::numeric_limits<quint64>::max());
        auto exhausted = Authorship::fromJson(exhaustedJson);
        QVERIFY(exhausted);
        const auto exhaustedDump = exhausted->dump();
        QVERIFY_THROWS_EXCEPTION(std::overflow_error, exhausted->setLinks({}, at));
        QCOMPARE(exhausted->dump(), exhaustedDump);
    }
    void malformedMetadata_data()
    {
        QTest::addColumn<QJsonObject>("json");
        Authorship history;
        history.setAuthor(author("first"), {link("[원본|society:document]")}, at);
        const auto valid = history.toJson();
        const auto changed = [&](const char* name, const QJsonValue& value) {
            auto json = valid;
            json["links"] = value;
            QTest::newRow(name) << json;
        };
        changed("missing-list", QJsonValue::Undefined);
        changed("null-list", QJsonValue::Null);
        changed("not-a-list", "[원본|society:doc]");
        changed("not-an-object", QJsonArray({"[원본|society:doc]"}));
        changed("missing-name", QJsonArray({QJsonObject{{"url", "society:doc"}}}));
        changed("wrong-url-type", QJsonArray({QJsonObject{{"name", "원본"}, {"url", 123}}}));
        changed("empty-url", QJsonArray({QJsonObject{{"name", "원본"}, {"url", ""}}}));
        changed("bad-url", QJsonArray({QJsonObject{{"name", "원본"}, {"url", "society:doc%ZZ"}}}));
        changed("unknown-field", QJsonArray({QJsonObject{{"name", "원본"}, {"url", "society:doc"}, {"extra", true}}}));
        changed("duplicate", QJsonArray({link("[원본|society:doc]").toJson(), link("[원본|society:doc]").toJson()}));
        changed("normalized-name-duplicate", QJsonArray({QJsonObject{{"name", "원본 "}, {"url", "society:doc"}},
            QJsonObject{{"name", "원본"}, {"url", "society:doc"}}}));
        QJsonArray tooMany;
        for (qsizetype index = 0; index <= Authorship::MaximumLinks; ++index)
            tooMany.append(link(QString("[자료|society:doc-%1]").arg(index)).toJson());
        changed("too-many-links", tooMany);
        auto json = valid;
        json["schemaVersion"] = 2;
        QTest::newRow("legacy-with-new-fields") << json;
        json = Authorship().toJson();
        json["links"] = valid["links"];
        QTest::newRow("unrecorded-links") << json;
    }
    void malformedMetadata()
    {
        QFETCH(QJsonObject, json);
        QString error;
        QVERIFY(!Authorship::fromJson(json, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!Authorship::fromDump(QJsonDocument(json).toJson(QJsonDocument::Compact)));
    }
};

QTEST_GUILESS_MAIN(FileLinksTest)
#include "file_links.moc"
