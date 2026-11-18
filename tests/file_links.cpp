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
const auto at = QDateTime::fromString("2026-09-08T12:00:00Z", Qt::ISODate);
FileAuthor author(const QString& subject)
{
    return FileAuthor::fromIisaccAccount({{"sub", subject}, {"email", "writer@example.com"}},
        QUrl("https://iisacc.com"), at).value();
}
FileLink link(const QString& entry) { return FileLink::fromString(entry).value(); }
}

class FileLinksTest final {
public:
    void preserveCustomAndSignedUrlText()
    {
        for (const auto& raw : {"ipfs://QmAbCdEfGh/Asset%7e?signature=a%2fb%2Bc",
                 "society://ContainerABC/Files/AssetXYZ", "https://EXAMPLE.com/file%7e?signature=a%2fb%2bc#%63"}) {
            const auto input = QString("[원본|%1]").arg(raw);
            auto parsed = FileLink::fromString(input);
            CTEST_CHECK(parsed);
            CTEST_COMPARE(parsed->urlText(), QString(raw));
            CTEST_COMPARE(parsed->toString(), input);
            CTEST_COMPARE(parsed->toJson()["url"].toString(), QString(raw));
            auto pair = FileLink::create("원본", QString(raw));
            CTEST_CHECK(pair);
            CTEST_COMPARE(pair->toString(), input);
            Authorship history;
            history.setAuthor(author("first"), {*parsed}, at);
            auto restored = Authorship::fromDump(history.dump());
            CTEST_CHECK(restored);
            CTEST_COMPARE(restored->links().first().toString(), input);
        }
    }
    void urlKinds_data(provider_test::CTestRunner& ctest_runner)
    {
        const auto ctest_row = [&](const char* label, QString url) {
            ctest_runner.run(std::string("urlKinds/") + label, [&] { urlKinds(url); });
        };
        ctest_row("society", "society://container/Files/artwork.iisc?version=2#layer-1");
        ctest_row("local-file", "file:///Volumes/Storage/Workspace/Assets/my%20image.png");
        ctest_row("windows-file", "file:///C:/Users/Writer/Documents/artwork.iisc");
        ctest_row("https", "https://example.com/file?token=explicit-url-value&x=a+b%2Bc#preview");
        ctest_row("http", "http://localhost:8080/file");
        ctest_row("smb", "smb://writer@nas.local/shared/artwork.iisc");
        ctest_row("ftp", "ftp://writer:explicit-password@example.com/file");
        ctest_row("sftp", "sftp://example.com:2222/files/project");
        ctest_row("ipfs", "ipfs://bafyexample/assets/image.png");
        ctest_row("urn", "urn:uuid:12345678-1234-1234-1234-123456789012");
        ctest_row("mailto", "mailto:writer@example.com?subject=File%20review");
        ctest_row("tel", "tel:+821012345678");
        ctest_row("data", "data:text/plain;charset=utf-8,hello%20world%7Csample");
        ctest_row("android-content", "content://com.example.documents/document/primary%3ADocuments%2Fimage.png");
        ctest_row("qt-resource", "qrc:/templates/default.iisc");
        ctest_row("custom-opaque", "my.app+files-v2:workspace/project?id=42#item");
        ctest_row("relative", "../assets/reference.svg?scale=2#layer");
        ctest_row("fragment", "#layer-1");
        ctest_row("network-path", "//nas.local/shared/file.iisc");
        ctest_row("ipv6-brackets", "smb://[2001:db8::1]/share/file");
        ctest_row("empty-query-fragment", "society:document?#");
        ctest_row("encoded-delimiters", "society:files/a%7Cb%5D?q=a+b%2Bc");
        ctest_row("inert-script-url", "javascript:alert(1)");

    }
    void urlKinds(QString url)
    {
        const auto entry = QString("[자료|%1]").arg(url);
        QString error = "stale";
        const auto parsed = FileLink::fromString(entry, &error);
        CTEST_CHECK_MESSAGE(parsed, qPrintable(error));
        CTEST_CHECK(error.isEmpty());
        CTEST_COMPARE(parsed->name(), QString("자료"));
        CTEST_COMPARE(parsed->url().toString(QUrl::FullyEncoded), url);
        CTEST_COMPARE(parsed->toString(), entry);
        const auto restored = FileLink::fromJson(parsed->toJson());
        CTEST_CHECK(restored);
        CTEST_CHECK(*restored == *parsed);
        Authorship history;
        CTEST_CHECK(history.setLinks({*parsed}, at));
        auto loaded = Authorship::fromDump(history.dump());
        CTEST_CHECK(loaded);
        CTEST_COMPARE(loaded->links().size(), 1);
        CTEST_COMPARE(loaded->links().first().toString(), entry);

    }
    void unicodeAndLimits()
    {
        const auto local = QUrl::fromLocalFile("/Volumes/Storage/Workspace/Assets/그림 파일.png");
        auto value = FileLink::create("  원본 자료  ", local);
        CTEST_CHECK(value);
        CTEST_COMPARE(value->name(), QString("원본 자료"));
        CTEST_COMPARE(value->url().toLocalFile(), QString("/Volumes/Storage/Workspace/Assets/그림 파일.png"));
        CTEST_CHECK(value->toString().contains("%20"));
        auto restored = FileLink::fromString(value->toString());
        CTEST_CHECK(restored && *restored == *value);
        const auto unicodeUrl = FileLink::fromString("[자료|society://files/원본/이미지.png]");
        CTEST_CHECK(unicodeUrl);
        CTEST_COMPARE(unicodeUrl->url().path(), QString("/원본/이미지.png"));
        const auto emoji = QString::fromUcs4(U"🚀");
        CTEST_CHECK(FileLink::create(emoji.repeated(FileLink::MaximumNameLength), local));
        CTEST_CHECK(!FileLink::create(emoji.repeated(FileLink::MaximumNameLength + 1), local));
        const QString maximumUrl = "society:" + QString(FileLink::MaximumUrlBytes - 8, 'a');
        auto maximum = FileLink::fromString("[자료|" + maximumUrl + ']');
        CTEST_CHECK(maximum);
        CTEST_COMPARE(maximum->url().toEncoded().size(), FileLink::MaximumUrlBytes);
        CTEST_CHECK(!FileLink::fromString("[자료|" + maximumUrl + "a]"));
        CTEST_CHECK(!FileLink::create("자료", QUrl("society:" + QString(3000, QChar(0xAC00)))));
        CTEST_CHECK(!FileLink::create("name|with-separator", local));
        CTEST_CHECK(!FileLink::create("자료", QUrl()));
    }
    void invalidNotation_data(provider_test::CTestRunner& ctest_runner)
    {
        const auto ctest_row = [&](const char* label, QString entry) {
            ctest_runner.run(std::string("invalidNotation/") + label, [&] { invalidNotation(entry); });
        };
        ctest_row("no-brackets", "자료|society:doc");
        ctest_row("no-separator", "[society:doc]");
        ctest_row("empty-name", "[|society:doc]");
        ctest_row("blank-name", "[   |society:doc]");
        ctest_row("empty-url", "[자료|]");
        ctest_row("malformed-percent", "[자료|society:doc%ZZ]");
        ctest_row("truncated-percent", "[자료|society:doc%2]");
        ctest_row("bad-authority", "[자료|https://[broken/file]");
        ctest_row("bad-port", "[자료|https://example.com:invalid/file]");
        ctest_row("unencoded-space", "[자료|file:///files/my file]");
        ctest_row("url-newline", "[자료|society:doc\nvalue]");
        ctest_row("name-newline", "[자\n료|society:doc]");
        ctest_row("invalid-utf16", (QString("[자료|society:") + QChar(0xD800) + ']'));

    }
    void invalidNotation(QString entry)
    {
        QString error;
        CTEST_CHECK(!FileLink::fromString(entry, &error));
        CTEST_CHECK(!error.isEmpty());
        CTEST_CHECK(!error.contains("example.com"));

    }
    void authorAndLinksCommitOnce()
    {
        const auto first = author("first");
        const auto second = author("second");
        const QList<FileLink> references{link("[Society|society://container/Files/artwork.iisc]"),
            link("[원본|file:///files/original.png]")};
        Authorship history;
        CTEST_CHECK(history.setAuthor(first, references, at));
        CTEST_COMPARE(history.revision(), quint64(1));
        CTEST_COMPARE(history.links().size(), 2);
        CTEST_CHECK(!history.setAuthor(first, references, at.addSecs(1)));
        CTEST_COMPARE(history.revision(), quint64(1));
        CTEST_CHECK(history.setAuthor(second, at.addSecs(2)));
        CTEST_COMPARE(history.revision(), quint64(2));
        CTEST_COMPARE(history.links().first().toString(), references.first().toString());
        const auto roster = history.toJson();
        CTEST_CHECK(history.setAuthor(second, {link("[참고|ipfs://bafyexample/reference]")}, at.addSecs(3)));
        CTEST_COMPARE(history.revision(), quint64(3));
        CTEST_COMPARE(history.toJson()["firstEditor"], roster["firstEditor"]);
        CTEST_COMPARE(history.toJson()["participants"], roster["participants"]);
        CTEST_COMPARE(history.links().size(), 1);
        auto copy = history.links();
        copy.clear();
        CTEST_COMPARE(history.links().size(), 1);
        CTEST_CHECK(history.setAuthor(second, QList<FileLink>{}, at.addSecs(4)));
        CTEST_CHECK(history.links().isEmpty());
        CTEST_COMPARE(history.participants().size(), 1);
        CTEST_COMPARE(history.firstEditor()->metadata().account.sub, QString("first"));
    }
    void fileRoundTripAndLinkUpdates()
    {
        Authorship history;
        CTEST_CHECK(history.setLinksFromStrings({"[Society|society:document-1]", "[원본|../original.png]"}, at));
        CTEST_CHECK(!history.firstEditor());
        CTEST_COMPARE(history.revision(), quint64(1));
        QTemporaryDir directory(QDir::currentPath() + "/named-url-XXXXXX");
        CTEST_CHECK(directory.isValid());
        iiFileProvider::FileDevice file(directory.filePath("metadata.json"));
        CTEST_CHECK(file.open(QIODevice::WriteOnly));
        CTEST_COMPARE(file.write(history.dump()), history.dump().size());
        file.close();
        CTEST_CHECK(file.open(QIODevice::ReadOnly));
        auto restored = Authorship::fromDump(file.readAll());
        CTEST_CHECK(restored);
        CTEST_COMPARE(restored->dump(), history.dump());
        CTEST_CHECK(restored->setAuthor(author("first"), at.addSecs(1)));
        CTEST_COMPARE(restored->links().size(), 2);
        restored->clearActiveAuthor();
        restored->recordChange(at.addSecs(2));
        CTEST_COMPARE(restored->links().size(), 2);
        CTEST_CHECK(restored->setLinks({}, at.addSecs(3)));
        CTEST_CHECK(!restored->setLinks({}, at.addSecs(4)));
        CTEST_COMPARE(restored->firstEditor()->metadata().account.sub, QString("first"));
    }
    void invalidBatchAndCapacityAreAtomic()
    {
        Authorship history;
        const auto reference = link("[Society|society:doc]");
        history.setAuthor(author("first"), {reference}, at);
        const auto before = history.dump();
        CTEST_THROWS(std::invalid_argument,
            history.setLinksFromStrings({"[유효|file:///files/item]", "[잘못됨|https://example.com/%ZZ]"}, at));
        CTEST_THROWS(std::invalid_argument,
            history.setAuthor(author("second"), {reference, reference}, at));
        CTEST_THROWS(std::invalid_argument,
            history.setAuthor(author("second"), QList<FileLink>{}, QDateTime()));
        CTEST_COMPARE(history.dump(), before);
        CTEST_CHECK(history.participants().isEmpty());
        history.recordChange(at.addSecs(1));
        CTEST_COMPARE(history.toJson()["lastAuthor"], history.toJson()["firstEditor"]);
        QList<FileLink> references;
        for (qsizetype index = 0; index < Authorship::MaximumLinks; ++index)
            references.append(link(QString("[자료|society:doc-%1]").arg(index)));
        CTEST_CHECK(history.setLinks(references, at.addSecs(2))); // Labels may repeat; complete pairs may not.
        const auto full = history.dump();
        references.append(link("[추가|society:overflow]"));
        CTEST_THROWS(std::length_error, history.setAuthor(author("second"), references, at));
        CTEST_COMPARE(history.dump(), full);
        CTEST_CHECK(history.participants().isEmpty());
        auto exhaustedJson = history.toJson();
        exhaustedJson["revision"] = QString::number(std::numeric_limits<quint64>::max());
        auto exhausted = Authorship::fromJson(exhaustedJson);
        CTEST_CHECK(exhausted);
        const auto exhaustedDump = exhausted->dump();
        CTEST_THROWS(std::overflow_error, exhausted->setLinks({}, at));
        CTEST_COMPARE(exhausted->dump(), exhaustedDump);
    }
    void malformedMetadata_data(provider_test::CTestRunner& ctest_runner)
    {
        const auto ctest_row = [&](const char* label, QJsonObject json) {
            ctest_runner.run(std::string("malformedMetadata/") + label, [&] { malformedMetadata(json); });
        };
        Authorship history;
        history.setAuthor(author("first"), {link("[원본|society:document]")}, at);
        const auto valid = history.toJson();
        const auto changed = [&](const char* name, const QJsonValue& value) {
            auto json = valid;
            json["links"] = value;
            ctest_row(name, json);
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
        ctest_row("legacy-with-new-fields", json);
        json = Authorship().toJson();
        json["links"] = valid["links"];
        ctest_row("unrecorded-links", json);

    }
    void malformedMetadata(QJsonObject json)
    {
        QString error;
        CTEST_CHECK(!Authorship::fromJson(json, &error));
        CTEST_CHECK(!error.isEmpty());
        CTEST_CHECK(!Authorship::fromDump(QJsonDocument(json).toJson(QJsonDocument::Compact)));

    }
};

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    provider_test::CTestRunner runner;
    FileLinksTest tests;
    runner.run("preserveCustomAndSignedUrlText", [&] { tests.preserveCustomAndSignedUrlText(); });
    runner.run("urlKinds_data", [&] { tests.urlKinds_data(runner); });
    runner.run("unicodeAndLimits", [&] { tests.unicodeAndLimits(); });
    runner.run("invalidNotation_data", [&] { tests.invalidNotation_data(runner); });
    runner.run("authorAndLinksCommitOnce", [&] { tests.authorAndLinksCommitOnce(); });
    runner.run("fileRoundTripAndLinkUpdates", [&] { tests.fileRoundTripAndLinkUpdates(); });
    runner.run("invalidBatchAndCapacityAreAtomic", [&] { tests.invalidBatchAndCapacityAreAtomic(); });
    runner.run("malformedMetadata_data", [&] { tests.malformedMetadata_data(runner); });
    return runner.result();
}
