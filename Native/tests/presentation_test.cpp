#include "iiFileProviderNative.h"
#include "iiFileProviderPresentation.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <cstring>
#endif
#ifndef _WIN32
#include <sys/xattr.h>
#include <sys/stat.h>
#endif
using namespace iiFileProvider::native;
namespace fs = std::filesystem;
static void require(bool v, const char *message) { if (!v) throw std::runtime_error(message); }
#ifdef _WIN32
static std::vector<unsigned char> dacl(const fs::path &path) {
    DWORD size=0;
    GetFileSecurityW(path.c_str(),DACL_SECURITY_INFORMATION,nullptr,0,&size);
    require(size!=0,"DACL size is available");
    std::vector<unsigned char> descriptor(size);
    require(GetFileSecurityW(path.c_str(),DACL_SECURITY_INFORMATION,descriptor.data(),size,&size),"DACL is readable");
    PACL acl=nullptr;BOOL present=FALSE,defaulted=FALSE;
    require(GetSecurityDescriptorDacl(descriptor.data(),&present,&acl,&defaulted) && present,"DACL is present");
    if (!acl) return {};
    const auto *bytes=reinterpret_cast<const unsigned char *>(acl);
    return {bytes,bytes+acl->AclSize};
}
#endif
int main(int argc, char **argv) {
    try {
        require(argc == 2, "fixture directory required");
        struct Fixture {
            fs::path value;
            explicit Fixture(const fs::path &parent) : value(parent / ("presentation-fixture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) { fs::create_directories(value); }
            ~Fixture() { std::error_code ec; fs::remove_all(value, ec); }
            const fs::path &path() const { return value; }
        } temporary(fs::absolute(argv[1]));
        const auto file = temporary.path() / u8"문서.txt";
        File::write(file, "A real preview <script> & original\nSecond line");
        require(Presentation::waitForIdle(std::chrono::seconds(15)), "creation queue drained");
        auto record = Presentation::load(file);
        require(record.has_value(), "a new provider file automatically has presentation");
        require(record->details.size == File::read(file).size(), "real source size");
        require(record->preview.kind == "text" && record->preview.mimeType == "image/svg+xml", "text content preview");
        require(record->preview.bytes.find("&lt;script&gt; &amp; original") != std::string::npos, "preview escapes external text");
        require(File::read(file) == "A real preview <script> & original\nSecond line", "original data fork unchanged");
        const auto portable = nlohmann::json::parse(File::read(Presentation::companionPath(file)));
        require(portable.at("preview").contains("data"), "preview bytes embedded in portable document");
        require(portable.at("details").at("name") == "문서.txt", "Unicode detail name");
#ifdef _WIN32
        require(dacl(Presentation::companionPath(file))==dacl(file),"derived content preserves the source Windows DACL");
#else
        require((fs::status(Presentation::companionPath(file)).permissions() & (fs::perms::group_read | fs::perms::others_read)) == fs::perms::none, "derived content does not broaden source access");
#endif
        require(Presentation::prepare(file).cacheHit, "an unchanged file reuses its embedded preview");
#ifdef _WIN32
        // Preview reads and NTFS stream writes must coexist with deletion rights.
        const auto deleteAccess = CreateFileW(file.c_str(), DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(deleteAccess != INVALID_HANDLE_VALUE, "source deletion rights can be reserved");
        fs::remove(Presentation::companionPath(file));
        try {
            require(Presentation::prepare(file).embedded, "preview publication shares deletion access");
        } catch (...) { CloseHandle(deleteAccess); throw; }
        CloseHandle(deleteAccess);
#endif
#ifndef _WIN32
        const auto clockFile = temporary.path() / "pinned-time.txt";
        { std::ofstream external(clockFile); external << "original"; }
        const auto pinnedToken = [&](const fs::path &path) {
            struct stat state{}; require(::lstat(path.c_str(), &state) == 0, "pinned stat");
#ifdef __APPLE__
            const auto time = state.st_ctimespec;
#else
            const auto time = state.st_ctim;
#endif
            return Presentation::normalizeChangeTime(std::uint64_t(state.st_dev), std::uint64_t(state.st_ino), time.tv_sec, time.tv_nsec);
        };
        const auto originalTime = pinnedToken(clockFile);
        (void)Presentation::prepare(clockFile);
        require(pinnedToken(clockFile) == originalTime, "a pinned descriptor/stat token survives our own attribute publication");
        const auto savedTime = fs::last_write_time(clockFile);
        { std::ofstream external(clockFile); external << "modified"; }
        fs::last_write_time(clockFile, savedTime);
        require(pinnedToken(clockFile) != originalTime, "normalization does not bless an external restored-mtime edit");
#endif
#ifdef __APPLE__
        const auto decomposed = temporary.path() / fs::path(u8"문서.txt");
        auto spelling = nlohmann::json::parse(record->document);
        spelling["details"]["name"] = std::string("문서.txt");
        File::write(Presentation::companionPath(file), spelling.dump());
        require(Presentation::load(file).has_value() && Presentation::load(decomposed).has_value(), "native decomposed and application composed Unicode names share a verified preview");
        require(Presentation::cacheChangeToken(file) == Presentation::cacheChangeToken(decomposed), "Unicode spelling does not change a thumbnail token after native metadata publication");
#endif
        std::cout << "Native attribute embedding: " << (record->embedded ? "yes" : "portable companion fallback") << '\n';
        const auto bitmapPath = temporary.path() / "pixels.bmp";
        std::string bitmap(66, '\0'); bitmap[0] = 'B'; bitmap[1] = 'M';
        const auto put32 = [](std::string &s, std::size_t at, std::uint32_t n) { for (int i = 0; i < 4; ++i) s[at + std::size_t(i)] = char(n >> (i * 8)); };
        put32(bitmap, 2, 66); put32(bitmap, 10, 54); put32(bitmap, 14, 40); put32(bitmap, 18, 3); put32(bitmap, 22, 1);
        bitmap[26] = 1; bitmap[28] = 24; put32(bitmap, 34, 12); bitmap[56] = char(255); bitmap[58] = char(255); bitmap[60] = char(255);
        File::write(bitmapPath, bitmap);
        require(Presentation::waitForIdle(std::chrono::seconds(15)), "image queue complete");
        const auto image = Presentation::load(bitmapPath);
        require(image && image->preview.kind == "image" && image->preview.mimeType == "image/png" && image->preview.width == 3 && image->preview.height == 1, "real raster content is converted to a bounded PNG");
        require(File::read(bitmapPath) == bitmap, "image original retained byte-for-byte");
        const auto custom = temporary.path() / "native.demo";
        File::write(custom, std::string("\0custom", 7));
        (void)Presentation::prepare(custom);
        Presentation::setRenderer(".demo", [preview = image->preview](const auto &, auto &properties) {
            properties["renderer"] = "domain-fixture"; return std::optional<FilePreview>(preview);
        });
        require(Presentation::prepare(custom).preview.kind == "image", "registered native domain renderer upgrades an information card");
        Presentation::setRenderer(".demo", {});
        std::string pcm(44 + 128, '\0');
        pcm.replace(0, 4, "RIFF"); put32(pcm, 4, unsigned(pcm.size() - 8)); pcm.replace(8, 8, "WAVEfmt ");
        put32(pcm, 16, 16); pcm[20] = 3; pcm[22] = 2; put32(pcm, 24, 48000); put32(pcm, 28, 48000 * 8);
        pcm[32] = 8; pcm[34] = 32; pcm.replace(36, 4, "data"); put32(pcm, 40, 128);
        for (std::size_t at = 44; at < pcm.size(); at += 4) put32(pcm, at, 0x3f000000);
        const auto audio = temporary.path() / "float32.wav"; File::write(audio, pcm);
        const auto waveform = Presentation::prepare(audio);
        require(waveform.preview.kind == "audio" && waveform.details.properties.at("sampleRate") == "48000" && waveform.details.properties.at("channels") == "2", "float32 PCM has actual audio details and waveform");
        // A foreign writer does not pass through the provider. A stale preview
        // must be refused even if the OS restores its original timestamp.
        const auto mtime = fs::last_write_time(file);
        { std::ofstream external(file, std::ios::binary | std::ios::trunc); external << "B real preview <script> & original\nSecond line"; }
        fs::last_write_time(file, mtime);
        require(!Presentation::load(file), "same-size same-time content edits invalidate preview");
        const auto updated = Presentation::prepare(file);
        require(!updated.cacheHit && updated.preview.bytes.find("B real preview") != std::string::npos, "changed content regenerated");
        const auto before = File::read(file);
        bool rejected = false;
        try { (void)Presentation::attach(file, FilePreview{"image/png", "image", "invalid", 100000, 1}); }
        catch (...) { rejected = true; }
        require(rejected && File::read(file) == before, "invalid supplied preview preserves original");
        const auto unknown = temporary.path() / "unknown.bin";
        File::write(unknown, std::string("\0\1\2", 3));
        require(Presentation::waitForIdle(std::chrono::seconds(15)), "unknown file intake drained before bridge cache replacement");
        const auto fallback = Presentation::prepare(unknown);
        require(fallback.preview.kind == "information", "unsupported content has honest information card");
        auto bridgeFallback = nlohmann::json::parse(fallback.document);
        bridgeFallback["generator"] = "iiFileProvider.Swift/0.7";
        File::write(Presentation::companionPath(unknown), bridgeFallback.dump());
        require(!Presentation::prepare(unknown).cacheHit, "a source-only bridge information card receives a native decoder attempt");
        require(Presentation::prepare(unknown).cacheHit, "unsupported native information cards do not repeatedly decode");
        const auto middle = temporary.path() / "large.txt";
        File::write(middle, std::string(128 * 1024, 'a'));
        require(Presentation::waitForIdle(std::chrono::seconds(15)), "large file initial preview");
        const auto middleTime = fs::last_write_time(middle);
        { std::fstream external(middle, std::ios::in | std::ios::out | std::ios::binary); external.seekp(64 * 1024); external.put('b'); }
        fs::last_write_time(middle, middleTime);
        require(!Presentation::load(middle), "native change token detects a middle edit outside fingerprint samples");
        // A moved/copy asset is revalidated rather than trusting a foreign path.
        const auto copied = temporary.path() / "copy.txt";
        fs::copy_file(file, copied);
        fs::copy_file(Presentation::companionPath(file), Presentation::companionPath(copied));
        require(!Presentation::load(copied), "copy identity is revalidated");
        require(Presentation::prepare(copied).details.name == "copy.txt", "copy gets current details");
        // Native recursive intake ignores control files, waits for a quiet source,
        // and observes files added by an external program after registration.
        const auto drive = temporary.path() / "Test.societycontainer";
        File::createDirectories(drive / "Files");
        Presentation::watch(drive, {"Files"}, std::chrono::milliseconds(50));
        const auto incoming = drive / "Files" / "external.txt";
        { std::ofstream external(incoming); external << "Imported from outside iisacc"; }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!Presentation::load(incoming) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        require(Presentation::load(incoming).has_value(), "external Society intake creates details and embedded preview");
        Presentation::unwatch(drive);
        require(!fs::exists(Presentation::companionPath(Presentation::companionPath(file))), "no recursive companion generation");
        require(Presentation::waitForIdle(std::chrono::seconds(15)), "shutdown jobs complete");
        std::cout << "Automatic creation, external intake, portable embedding, freshness, Unicode and source preservation passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
