#include <iiFileProviderNative.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <future>
using namespace iiFileProvider::native;
static_assert(Authorship::MaximumBytes == 2 * 1024 * 1024);
static_assert(Authorship::MaximumLinks == 64);
void check(bool value) {
    if (!value)
        throw std::runtime_error("Native author/link compatibility contract failed");
}
int main() {
    const std::string now = "2026-10-14T10:00:00.000Z";
    auto author = FileAuthor::fromIisaccAccount(
        {{"sub", "subject"}, {"email", "straße@example.com"}}, "https://iisacc.com", now);
    check(author.has_value() && author->metadata().account.email == "straße@example.com");
    for (auto url :
         {"ipfs://QmAbCdEfGh/Asset%7e?signature=a%2fb%2Bc", "society://ContainerABC/Files/AssetXYZ",
          "https://EXAMPLE.com/file%7e?signature=a%2fb%2bc#%63",
          "../assets/reference.svg?scale=2#layer", "#layer-1", "//nas.local/shared/file.iisc",
          "file:///Volumes/Storage/Workspace/그림.png", "smb://[2001:db8::1]/share/file",
          "society:document?#", "urn:uuid:12345678", "mailto:writer@example.com",
          "content://com.example.documents/document/"
          "primary%3ADocuments%2Fimage.png"}) {
        auto link = FileLink::create("자료", url);
        check(link.has_value() && link->url == url);
        Authorship ledger;
        check(ledger.setAuthor(*author, {*link}, now));
        auto loaded = Authorship::fromDump(ledger.dump());
        check(loaded.has_value() && loaded->links().front().url == url);
    }
    for (auto url :
         {"https://[broken/file", "https://example.com:invalid/file", "file:///files/my file",
          "society:doc%ZZ", "society:doc%2", "society:doc\nvalue"})
        check(!FileLink::create("자료", url));
    auto profile = nlohmann::json::parse(author->toJsonText());
    auto complete = profile;
    complete["details"]["timeZone"] = "Asia/Seoul";
    complete["details"]["links"] =
        nlohmann::json::array({{{"relation", "source"},
                                {"label", "Reference"},
                                {"url", "https://example.com/file?email=writer@example.com"}}});
    complete["attribution"]["licenseUrl"] = "https://example.com/license@v1";
    complete["device"] = {{"id", std::string(64, 'a')}, {"type", "pc"},
                          {"name", "Fixture"},          {"platform", "macOS"},
                          {"osVersion", "26.0"},        {"appId", "com.iisacc.fixture"},
                          {"appVersion", "1.0"}};
    auto restored = FileAuthor::fromJsonText(complete.dump());
    check(restored && nlohmann::json::parse(restored->toJsonText()) == complete);
    const auto validProfile = complete.dump();
    std::vector<std::future<bool>> readers;
    for (int worker=0;worker<4;++worker) readers.push_back(std::async(std::launch::async,[validProfile] {
        for (int i=0;i<64;++i) if (!FileAuthor::fromJsonText(validProfile)) return false;
        return true;
    }));
    for (auto& reader:readers) check(reader.get());
    for (const auto& [path,value] : std::vector<std::pair<std::string,std::string>>{
        {"/account/sub","invalid@subject"},{"/account/email","missing-at"},
        {"/account/userId","@UPPERCASE"},{"/account/avatarUrl","https://iisacc.com/media/avatars/short.webp"},
        {"/details/countryCode","KOR"},{"/details/locale","ko_kr"},
        {"/details/contactEmail","invalid"},{"/details/timeZone","UTC+15:00"},
        {"/capturedAt","2026-10-14T99:00:00Z"},{"/device/id","short"}}) {
        auto invalid=complete;
        invalid[nlohmann::json::json_pointer(path)]=value;
        check(!FileAuthor::fromJsonText(invalid.dump()));
    }
    complete["details"]["timeZone"] = "Unknown/Invalid";
    check(!FileAuthor::fromJsonText(complete.dump()));
    complete["details"]["timeZone"] = "UTC";
    complete["device"].erase("appVersion");
    check(!FileAuthor::fromJsonText(complete.dump()));
    profile["schemaVersion"] = true;
    check(!FileAuthor::fromJsonText(profile.dump()));
    Authorship history;
    history.setAuthor(*author, now);
    auto data = nlohmann::json::parse(history.dump());
    data["schemaVersion"] = true;
    check(!Authorship::fromDump(data.dump()));
    data = nlohmann::json::parse(history.dump());
    data["schemaVersion"] = 1;
    data.erase("firstEditor");
    data.erase("participants");
    data.erase("links");
    auto v1 = Authorship::fromDump(data.dump());
    check(v1.has_value() && v1->firstEditor()->metadata().account.email == "straße@example.com");
    data["unexpected"] = "value";
    check(!Authorship::fromDump(data.dump()));
    auto offsetAuthor =
        FileAuthor::fromIisaccAccount({{"sub", "offset"}, {"email", "fixture@example.com"}},
                                      "https://iisacc.com", "2026-10-14T19:00:00+09:00");
    check(offsetAuthor && offsetAuthor->metadata().capturedAt == now);
    return 0;
}
