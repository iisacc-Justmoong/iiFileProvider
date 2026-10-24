#include "iiFileProviderPresentation.h"
#include "iiFileProviderNative.h"
#include "PresentationImage_p.h"
#include <nlohmann/json.hpp>
#include <utf8proc.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/xattr.h>
#endif
#endif

namespace iiFileProvider::native {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
constexpr auto suffix = ".iisacc-preview";
constexpr auto attribute =
#ifdef __linux__
    "user.iisacc.file.presentation";
#else
    "com.iisacc.file.presentation";
#endif
std::mutex sourceAccessMutex;
std::map<std::string, std::weak_ptr<std::recursive_mutex>> sourceAccessLocks;
std::mutex rendererMutex;
std::map<std::string, Presentation::Renderer> renderers;
std::atomic_uint64_t rendererRevision{};
std::atomic_bool presentationStopped = false;
std::mutex cacheTokenMutex;
struct CacheToken final {
    std::string observed, normalized;
    std::optional<std::int64_t> observedSequence, normalizedSequence;
};
std::map<std::string, CacheToken> cacheTokens;
std::string text(const fs::path &p) { const auto u = p.u8string(); return {u.begin(), u.end()}; }
std::string lower(std::string s) { for (auto &c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; return s; }
bool sameName(const std::string &left, const std::string &right) {
    if (left == right) return true;
#ifdef __APPLE__
    // Native directory enumeration may decompose Unicode while an app retains
    // the composed spelling. Source identity/version still pins the actual file.
    const auto normalized = [](const std::string &value) {
        utf8proc_uint8_t *bytes{};
        const auto count = utf8proc_map(reinterpret_cast<const utf8proc_uint8_t *>(value.data()),
            static_cast<utf8proc_ssize_t>(value.size()), &bytes, static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE));
        if (count < 0) throw FileError("Invalid UTF-8 filename");
        std::string result(reinterpret_cast<const char *>(bytes), static_cast<std::size_t>(count));
        std::free(bytes); return result;
    };
    return normalized(left) == normalized(right);
#else
    return false;
#endif
}
void fail(const char *message) { throw FileError(message); }
bool control(const fs::path &path) {
    const auto name = lower(text(path.filename()));
    return name.empty() || name.front() == '.' || name.ends_with(suffix) || name.ends_with(".iisacc.json")
        || name.ends_with(".iisacc-lock") || name.ends_with(".lock") || name.ends_with(".tmp")
        || name.ends_with(".log") || name.ends_with(".sqlite3") || name.ends_with(".sqlite")
        || name.ends_with(".db") || name.ends_with("-wal") || name.ends_with("-shm") || name.ends_with("-journal");
}
bool providerControl(const fs::path &path) {
    const auto name = lower(text(path.filename()));
    return name == ".iisacc-file-provider" || name == ".iisacc-temporary.lock"
        || name.starts_with(".iifile-stage-") || name.ends_with(suffix) || name.ends_with(".iisacc.json") || name.ends_with(".iisacc-lock");
}
bool managedVisible(const fs::path &path) {
    for (const auto &part : path) {
        const auto name = text(part);
        if ((name.size() > 1 && name.front() == '.') || providerControl(part)) return false;
    }
    return true;
}
bool visible(const fs::path &path) {
    for (const auto &part : path) {
        const auto name = text(part);
        if (name.size() > 1 && name.front() == '.') return false;
    }
    return !control(path);
}
bool direct(const fs::path &path) {
    fs::path at = path.root_path();
    for (const auto &part : path.relative_path()) {
        at /= part;
        const auto status = fs::symlink_status(at);
        if (fs::is_symlink(status) || !fs::exists(status)) return false;
#ifdef _WIN32
        const auto a = GetFileAttributesW(at.c_str());
        if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#endif
    }
    return true;
}
struct Source {
    std::string identity, stamp;
    std::uint64_t size{};
    std::int64_t modified{};
    std::string changed;
    std::optional<std::int64_t> sequence;
};
Source inspect(const fs::path &path, bool directory = false) {
    const auto s = fs::symlink_status(path);
    if (fs::is_symlink(s) || (directory ? !fs::is_directory(s) : !fs::is_regular_file(s))) fail("Presentation requires a regular, unredirected source");
#ifdef _WIN32
    const auto h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr);
    if (h == INVALID_HANDLE_VALUE) fail("Cannot inspect presentation source");
    BY_HANDLE_FILE_INFORMATION info{};
    const bool valid = GetFileInformationByHandle(h, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
    FILE_BASIC_INFO basic{}; const bool basicValid = GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic));
    // Closing successive writes can leave equal timestamps. A journal sequence
    // distinguishes those edits without rereading model/tensor contents.
    std::optional<std::int64_t> sequence;
    READ_FILE_USN_DATA request{2, 3};
    std::array<unsigned char, 1024> journal{}; DWORD returned = 0;
    if (DeviceIoControl(h, FSCTL_READ_FILE_USN_DATA, &request, sizeof(request),
                        journal.data(), DWORD(journal.size()), &returned, nullptr)
        && returned >= sizeof(USN_RECORD_COMMON_HEADER)) {
        USN_RECORD_COMMON_HEADER header{};
        std::memcpy(&header, journal.data(), sizeof(header));
        if (header.MajorVersion == 2 && returned >= offsetof(USN_RECORD_V2, FileName)) {
            USN_RECORD_V2 record{};
            std::memcpy(&record, journal.data(), std::min<std::size_t>(sizeof(record), returned));
            if (record.Usn > 0) sequence = record.Usn;
        } else if (header.MajorVersion == 3 && returned >= offsetof(USN_RECORD_V3, FileName)) {
            USN_RECORD_V3 record{};
            std::memcpy(&record, journal.data(), std::min<std::size_t>(sizeof(record), returned));
            if (record.Usn > 0) sequence = record.Usn;
        }
    }
    CloseHandle(h); if (!valid) fail("Cannot identify presentation source");
    const auto size = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    const auto time = (std::uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
    const auto id = std::to_string(info.dwVolumeSerialNumber) + ":" + std::to_string(info.nFileIndexHigh) + ":" + std::to_string(info.nFileIndexLow);
    return {id, id + ":" + std::to_string(size) + ":" + std::to_string(time), size,
        static_cast<std::int64_t>((time - 116444736000000000ull) * 100), id + ":" + std::to_string(basicValid ? basic.ChangeTime.QuadPart : static_cast<LONGLONG>(time)), sequence};
#else
    struct stat info{};
    if (::lstat(path.c_str(), &info) || (directory ? !S_ISDIR(info.st_mode) : !S_ISREG(info.st_mode)) || info.st_size < 0) fail("Cannot inspect presentation source");
#ifdef __APPLE__
    const auto time = info.st_mtimespec;
    const auto changed = info.st_ctimespec;
#else
    const auto time = info.st_mtim;
    const auto changed = info.st_ctim;
#endif
    const auto id = std::to_string(info.st_dev) + ":" + std::to_string(info.st_ino);
    return {id, id + ":" + std::to_string(info.st_size) + ":" + std::to_string(time.tv_sec) + ":" + std::to_string(time.tv_nsec),
        std::uint64_t(info.st_size), time.tv_sec * 1000000000ll + time.tv_nsec,
        id + ":" + std::to_string(changed.tv_sec) + ":" + std::to_string(changed.tv_nsec)};
#endif
}
fs::path sourcePath(const fs::path &input) {
    if (input.empty() || input.native().find(fs::path::value_type{}) != fs::path::string_type::npos) fail("A native source path without NUL is required");
    const auto p = fs::absolute(input).lexically_normal();
    if (!direct(p)) fail("Presentation source or parent is redirected");
    return p;
}
std::string prefix(const fs::path &path, std::uint64_t length) {
    ReadFile in(path);
    std::string bytes(static_cast<std::size_t>(std::min<std::uint64_t>(length, 256 * 1024)), '\0');
    if (in.read(bytes.data(), static_cast<std::int64_t>(bytes.size())) != static_cast<std::int64_t>(bytes.size())) fail("Presentation source changed during reading");
    return bytes;
}
std::string version(const fs::path &path, const Source &state) {
    // Native identity + size + high resolution mtime, plus two bounded samples.
    // Attribute writes change ctime, so ctime cannot key an embedded document.
    ReadFile in(path);
    const auto amount = static_cast<std::size_t>(std::min<std::uint64_t>(state.size, 32 * 1024));
    std::string bytes(amount, '\0');
    if (in.read(bytes.data(), static_cast<std::int64_t>(amount)) != static_cast<std::int64_t>(amount)) fail("Unstable presentation fingerprint");
    if (state.size > amount) {
        const auto tail = std::min<std::uint64_t>(32 * 1024, state.size - amount);
        if (!in.seek(state.size - tail)) fail("Unstable presentation fingerprint");
        const auto at = bytes.size(); bytes.resize(at + static_cast<std::size_t>(tail));
        if (in.read(bytes.data() + at, static_cast<std::int64_t>(tail)) != static_cast<std::int64_t>(tail)) fail("Unstable presentation fingerprint");
    }
    if (inspect(path).stamp != state.stamp) fail("Source changed during presentation fingerprinting");
    auto result = "1:" + state.stamp + ":" + sha256(bytes);
    if (lower(text(path.extension())) == ".iisc") {
        auto wal = path; wal += "-wal";
        if (fs::exists(fs::symlink_status(wal))) { const auto state = inspect(wal); result += ":wal:" + version(wal, state) + ":" + state.changed; }
    }
    return result;
}
std::string changeToken(const fs::path &path, const Source &state) {
    auto result = state.changed;
    if (lower(text(path.extension())) == ".iisc") {
        auto wal = path; wal += "-wal";
        if (fs::exists(fs::symlink_status(wal))) result += ":wal:" + inspect(wal).changed;
    }
    return result;
}
constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string base64(std::string_view bytes) {
    std::string out; out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t at = 0; at < bytes.size(); at += 3) {
        const auto n = std::min<std::size_t>(3, bytes.size() - at);
        const auto v = (std::uint32_t(static_cast<unsigned char>(bytes[at])) << 16)
            | (n > 1 ? std::uint32_t(static_cast<unsigned char>(bytes[at + 1])) << 8 : 0)
            | (n > 2 ? static_cast<unsigned char>(bytes[at + 2]) : 0);
        out += alphabet[(v >> 18) & 63]; out += alphabet[(v >> 12) & 63];
        out += n > 1 ? alphabet[(v >> 6) & 63] : '='; out += n > 2 ? alphabet[v & 63] : '=';
    }
    return out;
}
std::string unbase64(std::string_view input) {
    if (input.size() % 4 || input.size() > Presentation::maximumDocumentBytes) fail("Invalid preview encoding");
    std::string out; out.reserve(input.size() / 4 * 3);
    for (std::size_t at = 0; at < input.size(); at += 4) {
        std::uint32_t value = 0; int padding = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const auto c = input[at + i];
            if (c == '=') { if (i < 2 || at + 4 != input.size()) fail("Invalid preview padding"); ++padding; value <<= 6; }
            else { const auto n = alphabet.find(c); if (padding || n == alphabet.npos) fail("Invalid preview base64"); value = (value << 6) | static_cast<std::uint32_t>(n); }
        }
        out += char(value >> 16); if (padding < 2) out += char(value >> 8); if (!padding) out += char(value);
    }
    if (base64(out) != input) fail("Noncanonical preview encoding");
    return out;
}
std::string xml(std::string_view input) {
    std::string output;
    for (std::size_t at = 0; at < input.size();) {
        utf8proc_int32_t cp{};
        const auto n = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t *>(input.data() + at), static_cast<utf8proc_ssize_t>(input.size() - at), &cp);
        if (n <= 0) { output += "\xef\xbf\xbd"; ++at; continue; }
        if (cp == '&') output += "&amp;"; else if (cp == '<') output += "&lt;"; else if (cp == '>') output += "&gt;";
        else if (cp == '"') output += "&quot;"; else if (cp == '\'') output += "&apos;";
        else if (cp >= 32 || cp == '\t') output.append(input, at, static_cast<std::size_t>(n));
        at += static_cast<std::size_t>(n);
    }
    return output;
}
std::string svgStart() { return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"512\" height=\"512\" viewBox=\"0 0 512 512\"><rect width=\"512\" height=\"512\" fill=\"#f7f7f8\"/>"; }
FilePreview card(const FileDetails &details, const std::string &content, bool isText) {
    auto svg = svgStart();
    svg += "<text x=\"24\" y=\"38\" font-family=\"sans-serif\" font-size=\"18\" fill=\"#202124\">" + xml(details.name.substr(0, 120)) + "</text>";
    svg += "<text x=\"24\" y=\"64\" font-family=\"sans-serif\" font-size=\"12\" fill=\"#555\">" + xml(details.mimeType + " · " + std::to_string(details.size) + " bytes") + "</text>";
    std::size_t at = 0;
    for (unsigned row = 0; row < 23 && at < content.size(); ++row) {
        auto end = content.find('\n', at);
        if (end == std::string::npos || end > at + 66) {
            end = std::min(content.size(), at + 66);
            while (end < content.size() && (static_cast<unsigned char>(content[end]) & 0xc0) == 0x80) ++end;
        }
        svg += "<text x=\"24\" y=\"" + std::to_string(96 + row * 17) + "\" font-family=\"monospace\" font-size=\"12\" fill=\"#303134\">" + xml(std::string_view(content).substr(at, end - at)) + "</text>";
        at = end + (end < content.size() && content[end] == '\n' ? 1 : 0);
    }
    svg += "</svg>";
    return {"image/svg+xml", isText ? "text" : "information", std::move(svg), 512, 512};
}
bool textual(std::string_view bytes) {
    for (std::size_t at = 0; at < bytes.size();) {
        utf8proc_int32_t cp{};
        const auto n = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t *>(bytes.data() + at), static_cast<utf8proc_ssize_t>(bytes.size() - at), &cp);
        if (n <= 0) return bytes.size() - at < 4 && at > 0; // Bounded prefix can split its last codepoint.
        if ((cp < 32 && cp != '\n' && cp != '\r' && cp != '\t') || cp == 127) return false;
        at += static_cast<std::size_t>(n);
    }
    return true;
}
std::uint32_t le32(const char *p) { return std::uint32_t(static_cast<unsigned char>(p[0])) | (std::uint32_t(static_cast<unsigned char>(p[1])) << 8) | (std::uint32_t(static_cast<unsigned char>(p[2])) << 16) | (std::uint32_t(static_cast<unsigned char>(p[3])) << 24); }
unsigned le16(const char *p) { return static_cast<unsigned char>(p[0]) | (unsigned(static_cast<unsigned char>(p[1])) << 8); }
std::optional<FilePreview> wave(std::string_view bytes, FileDetails &details) {
    if (bytes.size() < 12 || bytes.substr(0, 4) != "RIFF" || bytes.substr(8, 4) != "WAVE") return {};
    unsigned format = 0, channels = 0, bits = 0, sampleRate = 0, align = 0;
    for (std::size_t at = 12; at + 8 <= bytes.size();) {
        const auto length = le32(bytes.data() + at + 4); const auto data = at + 8;
        if (bytes.substr(at, 4) == "fmt " && length >= 16 && data + 16 <= bytes.size()) {
            format = le16(bytes.data() + data); channels = le16(bytes.data() + data + 2); sampleRate = le32(bytes.data() + data + 4);
            align = le16(bytes.data() + data + 12); bits = le16(bytes.data() + data + 14);
        }
        if (bytes.substr(at, 4) == "data" && channels && sampleRate && align && length <= details.size - std::min<std::uint64_t>(details.size, data)) {
            details.mimeType = "audio/wav";
            details.properties["sampleRate"] = std::to_string(sampleRate); details.properties["channels"] = std::to_string(channels);
            details.properties["durationSeconds"] = std::to_string(double(length) / sampleRate / align);
            details.properties["bitsPerSample"] = std::to_string(bits);
            const unsigned sampleBytes = bits / 8;
            if (!((format == 1 && bits == 16) || (format == 3 && bits == 32)) || channels > 64 || align != channels * sampleBytes) return {};
            const auto frames = std::min<std::size_t>(length, bytes.size() - data) / align;
            if (!frames) return {};
            auto svg = svgStart();
            svg += "<text x=\"24\" y=\"40\" font-family=\"sans-serif\" font-size=\"16\">" + xml(details.name.substr(0, 120)) + "</text>";
            svg += "<text x=\"24\" y=\"68\" font-family=\"sans-serif\" font-size=\"12\">" + xml(details.properties["durationSeconds"] + " s · " + std::to_string(sampleRate) + " Hz · " + std::to_string(channels) + " channels") + "</text>";
            // A bounded leading waveform; never imply that an unscanned tail was analysed.
            details.properties["waveformScope"] = frames * align == length ? "complete" : "leading-samples";
            for (std::size_t bin = 0; bin < 232; ++bin) {
                double peak = 0;
                const auto start = bin * frames / 232, end = std::max(start + 1, (bin + 1) * frames / 232);
                for (auto frame = start; frame < std::min(end, frames); ++frame) for (unsigned c = 0; c < channels; ++c) {
                    const auto *p = bytes.data() + data + frame * align + c * sampleBytes;
                    const double sample = format == 1 ? double(std::int16_t(le16(p))) / 32768.0 : double(std::bit_cast<float>(le32(p)));
                    if (std::isfinite(sample)) peak = std::max(peak, std::min(1.0, std::abs(sample)));
                }
                const auto h = std::max(1, int(peak * 144));
                svg += "<rect x=\"" + std::to_string(24 + bin * 2) + "\" y=\"" + std::to_string(280 - h) + "\" width=\"1\" height=\"" + std::to_string(h * 2) + "\" fill=\"#4475cb\"/>";
            }
            return FilePreview{"image/svg+xml", "audio", svg + "</svg>", 512, 512};
        }
        if (std::uint64_t(data) + length + (length & 1u) > bytes.size()) break;
        at = data + length + (length & 1u);
    }
    return {};
}
std::string mime(const fs::path &path, std::string_view bytes) {
    if (bytes.starts_with(std::string_view("\x89PNG\r\n\x1a\n", 8))) return "image/png";
    if (bytes.starts_with(std::string_view("\xff\xd8\xff", 3))) return "image/jpeg";
    if (bytes.starts_with("GIF87a") || bytes.starts_with("GIF89a")) return "image/gif";
    if (bytes.starts_with("%PDF-")) return "application/pdf";
    if (bytes.size() >= 12 && bytes.substr(0, 4) == "RIFF" && bytes.substr(8, 4) == "WEBP") return "image/webp";
    if (bytes.starts_with("BM")) return "image/bmp";
    if (bytes.starts_with(std::string_view("II*\0", 4)) || bytes.starts_with(std::string_view("MM\0*", 4))) return "image/tiff";
    if (bytes.size() >= 12 && bytes.substr(4, 4) == "ftyp") {
        const auto brand = bytes.substr(8, 4);
        if (brand == "heic" || brand == "heix" || brand == "mif1") return "image/heic";
        if (brand == "avif") return "image/avif";
        return "video/mp4";
    }
    const auto ext = lower(text(path.extension()));
    if (ext == ".iisc") return "application/vnd.iisacc.canvas";
    if (ext == ".mid" || ext == ".midi") return "audio/midi";
    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".docx") return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    if (ext == ".xlsx") return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
    if (ext == ".pptx") return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
    if (textual(bytes)) return ext == ".json" ? "application/json" : "text/plain";
    return "application/octet-stream";
}
void validatePreview(const FilePreview &p, bool supplied = false) {
    if (!p.width || !p.height || p.width > 512 || p.height > 512 || p.bytes.empty() || p.bytes.size() > Presentation::maximumDocumentBytes / 2) fail("Preview exceeds the supported bounds");
    if (p.mimeType == "image/png") {
        if (p.bytes.size() < 24 || p.bytes.substr(0, 8) != std::string("\x89PNG\r\n\x1a\n", 8) || p.bytes.substr(12, 4) != "IHDR") fail("Invalid PNG preview");
        const auto be = [](const char *b) { return (unsigned(static_cast<unsigned char>(b[0])) << 24) | (unsigned(static_cast<unsigned char>(b[1])) << 16) | (unsigned(static_cast<unsigned char>(b[2])) << 8) | static_cast<unsigned char>(b[3]); };
        if (be(p.bytes.data() + 16) != p.width || be(p.bytes.data() + 20) != p.height) fail("Preview dimensions do not match its payload");
    } else if (p.mimeType == "image/svg+xml" && !supplied) {
        // Only inert provider cards are accepted from portable documents.
        if (!p.bytes.starts_with(svgStart()) || !p.bytes.ends_with("</svg>")) fail("Invalid provider SVG card");
        for (std::size_t at = 0; (at = p.bytes.find('<', at)) != std::string::npos;) {
            const auto end = p.bytes.find('>', at); if (end == std::string::npos) fail("Invalid SVG tag");
            const auto tag = lower(p.bytes.substr(at + 1, end - at - 1));
            const auto name = tag.substr(0, tag.find_first_of(" \t\r\n"));
            if (name != "svg" && name != "/svg" && name != "rect" && name != "text" && name != "/text") fail("Unsupported SVG element");
            for (auto token : {" on", "\ton", "\non", "\ron", "href", "url(", "style", "<!", "<?"})
                if (tag.find(token) != std::string::npos) fail("Active or externally referenced preview content");
            at = end + 1;
        }
    } else fail("Only bounded PNGs may be supplied by a document encoder");
    if (p.kind != "image" && p.kind != "text" && p.kind != "audio" && p.kind != "information") fail("Unknown preview kind");
}
std::string serialize(const PresentationRecord &r) {
    return Json{{"format", "iiFileProvider.Presentation"}, {"schema", 1}, {"generator", "iiFileProvider.Native/0.7"}, {"sourceVersion", r.sourceVersion},
        {"details", {{"name", r.details.name}, {"extension", r.details.extension}, {"mimeType", r.details.mimeType}, {"size", r.details.size}, {"modifiedAtNs", r.details.modifiedAtNs}, {"properties", r.details.properties}}},
        {"preview", {{"mimeType", r.preview.mimeType}, {"kind", r.preview.kind}, {"width", r.preview.width}, {"height", r.preview.height}, {"encoding", "base64"}, {"data", base64(r.preview.bytes)}, {"sha256", sha256(r.preview.bytes)}}}}.dump();
}
PresentationRecord parse(const std::string &input) {
    if (input.size() > Presentation::maximumDocumentBytes) fail("Presentation document exceeds limit");
    auto j = Json::parse(input);
    if (j.at("format") != "iiFileProvider.Presentation" || j.at("schema") != 1) fail("Unknown presentation document");
    PresentationRecord r; r.document = input; r.sourceVersion = j.at("sourceVersion").get<std::string>();
    const auto &d = j.at("details"), &p = j.at("preview");
    r.details = {d.at("name").get<std::string>(), d.at("extension").get<std::string>(), d.at("mimeType").get<std::string>(), d.at("size").get<std::uint64_t>(), d.at("modifiedAtNs").get<std::int64_t>(), d.at("properties").get<std::map<std::string, std::string>>()};
    if (p.at("encoding") != "base64") fail("Unknown preview encoding");
    r.preview = {p.at("mimeType").get<std::string>(), p.at("kind").get<std::string>(), unbase64(p.at("data").get<std::string>()), p.at("width").get<unsigned>(), p.at("height").get<unsigned>()};
    validatePreview(r.preview);
    if (sha256(r.preview.bytes) != p.at("sha256").get<std::string>()) fail("Preview checksum mismatch");
    return r;
}
std::optional<std::string> embedded(const fs::path &path) {
#ifdef _WIN32
    auto stream = path; stream += L":iisacc.presentation";
    try { if (fs::exists(stream)) return File::read(stream, Presentation::maximumDocumentBytes); } catch (...) {}
#elif defined(__APPLE__) || defined(__linux__)
#ifdef __APPLE__
    const auto length = ::getxattr(path.c_str(), attribute, nullptr, 0, 0, XATTR_NOFOLLOW);
#else
    const auto length = ::lgetxattr(path.c_str(), attribute, nullptr, 0);
#endif
    if (length <= 0 || std::size_t(length) > Presentation::maximumDocumentBytes) return {};
    std::string data(static_cast<std::size_t>(length), '\0');
#ifdef __APPLE__
    const auto read = ::getxattr(path.c_str(), attribute, data.data(), data.size(), 0, XATTR_NOFOLLOW);
#else
    const auto read = ::lgetxattr(path.c_str(), attribute, data.data(), data.size());
#endif
    if (read == length) return data;
#endif
    return {};
}
bool embed(const fs::path &path, const std::string &data) {
#ifdef _WIN32
    auto stream = path; stream += L":iisacc.presentation";
    const auto metadata = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (metadata == INVALID_HANDLE_VALUE) return false;
    FILETIME modified{};
    if (!GetFileTime(metadata, nullptr, nullptr, &modified)) { CloseHandle(metadata); return false; }
    // Named streams cannot be staged by renaming another NTFS stream. Write the
    // bounded derived document; checksum + source version reject torn writes.
    const auto output = CreateFileW(stream.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD count = 0;
    const bool written = output != INVALID_HANDLE_VALUE &&
        WriteFile(output, data.data(), static_cast<DWORD>(data.size()), &count, nullptr) &&
        count == data.size() && FlushFileBuffers(output);
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    // NTFS streams share file timestamps. Keep the data fork's original mtime;
    // the native change time still journals this metadata publication.
    const bool restored = SetFileTime(metadata, nullptr, nullptr, &modified); CloseHandle(metadata);
    return written && restored;
#elif defined(__APPLE__)
    return ::setxattr(path.c_str(), attribute, data.data(), data.size(), 0, XATTR_NOFOLLOW) == 0;
#elif defined(__linux__)
    return ::lsetxattr(path.c_str(), attribute, data.data(), data.size(), 0) == 0;
#else
    (void)path; (void)data; return false;
#endif
}
std::optional<PresentationRecord> loadAt(const fs::path &path, const Source &state) {
    const auto current = version(path, state);
    const auto valid = [&](const std::string &bytes) -> std::optional<PresentationRecord> {
        auto r = parse(bytes);
        if (r.sourceVersion != current || !sameName(r.details.name, text(path.filename())) || r.details.size != state.size || r.details.modifiedAtNs != state.modified) return {};
        const auto j = Json::parse(bytes);
        if (j.value("sourceChangeToken", std::string{}) != changeToken(path, state)) return {};
        if (const auto data = embedded(path)) {
            try { const auto stored = parse(*data); r.embedded = stored.sourceVersion == current && stored.preview.bytes == r.preview.bytes; } catch (...) {}
        }
        r.cacheHit = true; return r;
    };
    const auto companion = Presentation::companionPath(path);
    if (fs::is_regular_file(fs::symlink_status(companion)) && direct(companion)) {
        try { return valid(File::read(companion, Presentation::maximumDocumentBytes)); } catch (...) {}
    }
    return {};
}
PresentationRecord persist(const fs::path &path, const Source &before, const std::string &stamp, FilePreview preview, FileDetails details) {
    validatePreview(preview);
    PresentationRecord r; r.details = std::move(details); r.preview = std::move(preview); r.sourceVersion = stamp; r.document = serialize(r);
    if (r.document.size() > Presentation::maximumDocumentBytes) fail("Presentation document exceeds byte limit");
    if (version(path, inspect(path)) != stamp) fail("Source changed before preview publication");
    const auto companion = Presentation::companionPath(path);
    if (fs::exists(fs::symlink_status(companion))) {
        // Never replace an unrelated user file or follow a companion symlink.
        if (!direct(companion) || !fs::is_regular_file(fs::symlink_status(companion))) fail("Presentation companion is redirected");
        const auto previous = Json::parse(File::read(companion, Presentation::maximumDocumentBytes));
        if (!previous.is_object() || previous.value("format", std::string{}) != "iiFileProvider.Presentation" || previous.value("schema", 0) != 1)
            fail("Cannot replace an unrelated presentation companion");
        fs::permissions(companion, fs::perms::owner_read | fs::perms::owner_write);
    }
    const auto renderedSource = inspect(path);
    if (renderedSource.changed != before.changed || renderedSource.sequence != before.sequence)
        fail("Native source changed during preview rendering");
    // Attribute publication changes ctime. Journal that resulting token in the
    // portable document, outside the source, without a self-invalidating loop.
    // A missing companion or any later native change requires regeneration.
    auto portable = Json::parse(r.document);
    std::string resultingToken;
    {
        std::lock_guard lock(cacheTokenMutex);
        const auto previous = cacheTokens.find(before.identity);
        auto originalToken = before.changed;
        auto originalSequence = before.sequence;
        if (previous != cacheTokens.end() && previous->second.observed == originalToken
            && previous->second.observedSequence == before.sequence) {
            originalToken = previous->second.normalized;
            originalSequence = previous->second.normalizedSequence;
        }
        r.embedded = embed(path, r.document);
        const auto after = inspect(path);
        resultingToken = changeToken(path, after);
        if (cacheTokens.size() >= 65536 && !cacheTokens.contains(before.identity)) cacheTokens.erase(cacheTokens.begin());
        cacheTokens[before.identity] = {after.changed, std::move(originalToken), after.sequence, originalSequence};
    }
    portable["sourceChangeToken"] = resultingToken; r.document = portable.dump();
    if (r.document.size() > Presentation::maximumDocumentBytes) fail("Portable presentation exceeds byte limit");
    StagedFile staged(companion);
    fs::permissions(staged.path(), fs::perms::owner_read | fs::perms::owner_write);
    {
        std::ofstream output(staged.path(), std::ios::binary | std::ios::trunc);
        output.write(r.document.data(), static_cast<std::streamsize>(r.document.size())); output.flush();
        if (!output) fail("Cannot write portable presentation");
    }
    if (!staged.publish().succeeded) fail("Cannot publish portable presentation");
    if (inspect(path).stamp != before.stamp || version(path, inspect(path)) != stamp) fail("Source changed during preview publication");
    return r;
}
FileDetails detailsFor(const fs::path &p, const Source &s, std::string_view bytes) {
    FileDetails details{text(p.filename()), lower(text(p.extension())), mime(p, bytes), s.size, s.modified, {}};
#ifdef __APPLE__
    struct stat info{};
    if (::lstat(p.c_str(), &info) == 0) details.properties["createdAtNs"] = std::to_string(info.st_birthtimespec.tv_sec * 1000000000ll + info.st_birthtimespec.tv_nsec);
#endif
    return details;
}

class Worker {
    struct Seen { std::string stamp; Clock::time_point since; bool ready = false; };
    struct Watch {
        fs::path root;
        std::string identity;
        std::vector<fs::path> directories;
        std::size_t index = 0;
        fs::recursive_directory_iterator cursor;
        std::map<fs::path, Seen> seen;
        std::chrono::milliseconds interval;
        Clock::time_point next{};
        std::function<bool()> available;
        std::uint64_t rendererVersion = 0;
    };
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<fs::path> queue;
    std::set<fs::path> queued;
    std::map<fs::path, std::shared_ptr<Watch>> watches;
    std::thread thread;
    bool stopping = false, active = false;
    fs::path activePath;
    static bool within(const fs::path &path, const fs::path &root) {
        const auto relative = path.lexically_relative(root);
        return path == root || (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..");
    }
    void scan(Watch &w) {
        if ((w.available && !w.available()) || !direct(w.root) || inspect(w.root, true).identity != w.identity) return;
        if (w.rendererVersion != rendererRevision.load()) {
            for (auto &[path, seen] : w.seen) seen.ready = false;
            w.rendererVersion = rendererRevision.load();
        }
        std::error_code error;
        if (w.cursor == fs::recursive_directory_iterator{}) {
            if (w.index >= w.directories.size()) w.index = 0;
            const auto directory = w.root / w.directories[w.index++];
            if (!fs::is_directory(fs::symlink_status(directory)) || !direct(directory)) return;
            w.cursor = fs::recursive_directory_iterator(directory, fs::directory_options::skip_permission_denied, error);
        }
        // Slices keep a large drive from starving completed application saves.
        for (unsigned count = 0; count < 128 && w.cursor != fs::recursive_directory_iterator{}; ++count) {
            const auto p = w.cursor->path(); const auto status = w.cursor->symlink_status(error);
            if (!error) {
                if (fs::is_symlink(status) || !managedVisible(p.lexically_relative(w.root))) w.cursor.disable_recursion_pending();
                else if (fs::is_regular_file(status)) {
                    const auto s = inspect(p);
                    auto observed = s.stamp + ":" + s.changed;
                    if (lower(text(p.extension())) == ".iisc") {
                        auto wal = p; wal += "-wal";
                        if (fs::exists(fs::symlink_status(wal))) { const auto w = inspect(wal); observed += w.stamp + w.changed; }
                    }
                    const auto now = Clock::now(); auto found = w.seen.find(p);
                    if (found == w.seen.end() || found->second.stamp != observed) {
                        if (w.seen.size() >= 65536) w.seen.erase(w.seen.begin());
                        w.seen[p] = {observed, now, false};
                    } else if (!found->second.ready && now - found->second.since >= std::min(w.interval, std::chrono::milliseconds(500))) {
                        try { (void)Presentation::prepare(p); found->second.ready = true; } catch (...) {}
                        w.cursor.increment(error);
                        if (error) w.cursor = {};
                        return; // At most one decoder working set per intake slice.
                    }
                }
            }
            error.clear(); w.cursor.increment(error);
            if (error) { w.cursor = {}; break; }
        }
    }
    void run() {
        std::unique_lock lock(mutex);
        for (;;) {
            if (!queue.empty()) {
                auto path = std::move(queue.front()); queue.pop_front(); queued.erase(path); active = true; activePath = path;
                lock.unlock(); try { (void)Presentation::prepare(path); } catch (...) {} lock.lock();
                active = false; activePath.clear(); changed.notify_all(); continue;
            }
            if (stopping) return;
            std::shared_ptr<Watch> due;
            auto next = Clock::now() + std::chrono::seconds(2);
            for (auto &[path, watch] : watches) {
                if (watch->next <= Clock::now()) { due = watch; break; }
                next = std::min(next, watch->next);
            }
            if (due) {
                due->next = Clock::now() + due->interval; active = true; activePath = due->root;
                lock.unlock(); try { scan(*due); } catch (...) { due->cursor = {}; } lock.lock();
                if (due->cursor != fs::recursive_directory_iterator{} || due->index < due->directories.size())
                    due->next = Clock::now() + std::chrono::milliseconds(5);
                active = false; activePath.clear(); changed.notify_all(); continue;
            }
            changed.wait_until(lock, next);
        }
    }
    void start() { if (!thread.joinable()) thread = std::thread([this] { run(); }); }
public:
    ~Worker() { { std::lock_guard lock(mutex); stopping = true; } changed.notify_all(); if (thread.joinable()) thread.join(); presentationStopped = true; }
    void add(fs::path p) {
        std::unique_lock lock(mutex);
        if (stopping || queued.contains(p)) return;
        // Backpressure bounds memory without silently dropping completed saves.
        changed.wait(lock, [&] { return stopping || queue.size() < 4096; });
        if (stopping || queued.contains(p)) return;
        queued.insert(p); queue.push_back(std::move(p)); start(); changed.notify_all();
    }
    bool idle(std::chrono::milliseconds timeout) { std::unique_lock lock(mutex); return changed.wait_for(lock, timeout, [&] { return queue.empty() && !active; }); }
    void cancel(const fs::path &root) {
        // A metadata publication can retire its stage while executing intake.
        // The worker already owns that operation and must not wait for itself.
        std::unique_lock lock(mutex);
        if (thread.get_id()==std::this_thread::get_id()) return;
        for (auto it = queue.begin(); it != queue.end();) {
            if (within(*it, root)) { queued.erase(*it); it = queue.erase(it); } else ++it;
        }
        for (auto it = watches.begin(); it != watches.end();) {
            if (within(it->first, root)) it = watches.erase(it); else ++it;
        }
        changed.notify_all();
        changed.wait(lock, [&] { return !active || (!within(activePath, root) && !within(root, activePath)); });
    }
    void watch(const fs::path &root, const std::vector<fs::path> &directories, std::chrono::milliseconds interval, std::function<bool()> available) {
        auto w = std::make_shared<Watch>(); w->root = root; w->identity = inspect(root, true).identity; w->directories = directories; w->interval = std::max(interval, std::chrono::milliseconds(50));
        w->available = std::move(available);
        std::lock_guard lock(mutex);
        if (const auto found = watches.find(root); !w->available && found != watches.end() && found->second->identity == w->identity && found->second->directories == directories) return;
        if (watches.size() >= 128 && !watches.contains(root)) fail("Presentation intake root limit exceeded");
        watches[root] = std::move(w); start(); changed.notify_all();
    }
    void unwatch(const fs::path &root) { std::lock_guard lock(mutex); watches.erase(root); changed.notify_all(); }
};
Worker &worker() { static Worker w; return w; }
} // namespace

struct SourceAccess::Impl {
    std::string identity;
    std::shared_ptr<std::recursive_mutex> mutex;
    static std::string identify(const fs::path &input) {
        if (input.empty()) throw FileError("A source path is required", FileCode::InvalidPath);
        try { return inspect(fs::canonical(input)).identity; }
        catch (const std::exception &error) { throw FileError(std::string("Cannot identify source access: ") + error.what()); }
    }
    static std::shared_ptr<std::recursive_mutex> acquire(const std::string &identity) {
        std::lock_guard guard(sourceAccessMutex);
        auto &entry = sourceAccessLocks[identity];
        auto mutex = entry.lock();
        if (!mutex) { mutex = std::make_shared<std::recursive_mutex>(); entry = mutex; }
        return mutex;
    }
    explicit Impl(const fs::path &path) : identity(identify(path)), mutex(acquire(identity)) {}
    ~Impl() {
        mutex.reset();
        std::lock_guard guard(sourceAccessMutex);
        const auto entry = sourceAccessLocks.find(identity);
        if (entry != sourceAccessLocks.end() && entry->second.expired()) sourceAccessLocks.erase(entry);
    }
};
SourceAccess::SourceAccess(const fs::path &path) : impl_(std::make_unique<Impl>(path)) {}
SourceAccess::~SourceAccess() = default;
void SourceAccess::lock() { impl_->mutex->lock(); }
void SourceAccess::unlock() noexcept { impl_->mutex->unlock(); }

fs::path Presentation::companionPath(const fs::path &p) { auto name = fs::path("."); name += p.filename(); name += suffix; return p.parent_path() / name; }
bool Presentation::isControlFile(const fs::path &p) noexcept { try { return providerControl(p); } catch (...) { return true; } }
std::string Presentation::cacheChangeToken(const fs::path &input) {
    const auto path = fs::absolute(input).lexically_normal();
    std::lock_guard lock(cacheTokenMutex); const auto state = inspect(path);
    const auto found = cacheTokens.find(state.identity);
    const auto normalized = found != cacheTokens.end() && found->second.observed == state.changed
        && found->second.observedSequence == state.sequence;
    auto result = normalized ? found->second.normalized : state.changed;
    const auto sequence = normalized ? found->second.normalizedSequence : state.sequence;
    if (sequence) result += ":usn:" + std::to_string(*sequence);
    if (lower(text(path.extension())) == ".iisc") {
        auto wal = path; wal += "-wal";
        if (fs::exists(fs::symlink_status(wal))) {
            const auto current = inspect(wal);
            result += ":wal:" + current.changed;
            if (current.sequence) result += ":usn:" + std::to_string(*current.sequence);
        }
    }
    return result;
}
std::string Presentation::normalizeChangeTime(std::uint64_t device, std::uint64_t fileIndex, std::int64_t seconds, std::int64_t nanoseconds) {
    if (nanoseconds < 0 || nanoseconds >= 1000000000) fail("Invalid native change-time fraction");
#ifdef _WIN32
    if (seconds < -11644473600ll || seconds > (std::numeric_limits<std::int64_t>::max() - nanoseconds / 100) / 10000000ll - 11644473600ll)
        fail("Native change-time is outside the FILETIME range");
    const auto identity = std::to_string(device) + ":" + std::to_string(fileIndex >> 32) + ":" + std::to_string(fileIndex & 0xffffffffu);
    const auto token = identity + ":" + std::to_string((seconds + 11644473600ll) * 10000000ll + nanoseconds / 100);
#else
    const auto identity = std::to_string(device) + ":" + std::to_string(fileIndex);
    const auto token = identity + ":" + std::to_string(seconds) + ":" + std::to_string(nanoseconds);
#endif
    std::lock_guard lock(cacheTokenMutex);
    const auto found = cacheTokens.find(identity);
    return found != cacheTokens.end() && found->second.observed == token ? found->second.normalized : token;
}
std::optional<PresentationRecord> Presentation::load(const fs::path &input) {
    try { const auto p = sourcePath(input); return loadAt(p, inspect(p)); } catch (...) { return {}; }
}
PresentationRecord Presentation::prepare(const fs::path &input) {
    const auto p = sourcePath(input);
    if (providerControl(p)) fail("Provider control files do not receive previews");
    const auto identity = inspect(p).identity;
    SourceAccess access(p);
    std::unique_lock sourceLock(access);
    Renderer renderer;
    { std::lock_guard registryLock(rendererMutex); const auto it = renderers.find(lower(text(p.extension()))); if (it != renderers.end()) renderer = it->second; }
    auto before = inspect(p);
    if (before.identity != identity) fail("Source was replaced before presentation");
    if (auto r = loadAt(p, before)) {
        // Source-only platform bridges may have fewer decoders. Give a cached
        // information card one native attempt, while preserving usable previews.
        if (r->preview.kind != "information" || (!renderer && Json::parse(r->document).value("generator", std::string{}) == "iiFileProvider.Native/0.7")) return *r;
    }
    const auto stamp = version(p, before);
    const auto initialChangeToken = cacheChangeToken(p);
    const auto bytes = prefix(p, before.size);
    auto details = detailsFor(p, before, bytes);
    std::optional<FilePreview> preview;
    if (renderer && before.size <= maximumSourceBytes) {
        // Domain readers coordinate their source copy. Their detached rendering
        // must not retain ownership of the file while an editor needs to write.
        sourceLock.unlock();
        try { preview = renderer(p, details.properties); } catch (...) {}
        sourceLock.lock();
        const auto current = inspect(p);
        if (current.identity != before.identity || current.stamp != before.stamp
            || version(p, current) != stamp || cacheChangeToken(p) != initialChangeToken)
            fail("Source changed during preview rendering");
        if (preview) { try { validatePreview(*preview); } catch (...) { preview.reset(); } }
        if (auto cached = loadAt(p, current);
            cached && (cached->preview.kind != "information" || !preview)) return *cached;
        // Another renderer may have published attributes while this one worked.
        // Only our own normalized metadata changes are accepted above.
        before = current;
    }
    if (!preview) preview = wave(bytes, details);
#ifdef __APPLE__
    if (!preview && before.size <= maximumSourceBytes && details.mimeType.starts_with("image/"))
        preview = detail::applePreview(p, details);
#endif
    if (!preview && (details.mimeType.starts_with("image/") || details.extension == ".tga" || details.extension == ".psd") && before.size <= maximumSourceBytes) {
        const auto all = File::read(p, maximumSourceBytes);
        preview = detail::imagePreview(all, details);
    }
#ifdef __APPLE__
    if (!preview && before.size <= maximumSourceBytes && !details.mimeType.starts_with("image/") && details.mimeType != "text/plain" && details.mimeType != "application/json"
        && details.mimeType != "application/octet-stream" && details.mimeType != "application/vnd.iisacc.canvas")
        preview = detail::applePreview(p, details);
#endif
    if (!preview) {
        const bool isText = details.mimeType == "text/plain" || details.mimeType == "application/json";
        if (isText) {
            details.properties["textEncoding"] = "UTF-8";
            details.properties["textPreviewTruncated"] = before.size > 4096 ? "true" : "false";
        }
        preview = card(details, isText ? bytes.substr(0, 4096) : "Content preview unavailable. Open the file to view its contents.", isText);
    }
    return persist(p, before, stamp, std::move(*preview), std::move(details));
}
void Presentation::setRenderer(std::string extension, Renderer renderer) {
    extension = lower(std::move(extension));
    if (extension.size() < 2 || extension.size() > 32 || extension.front() != '.' || extension.find_first_not_of(".abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
        fail("A renderer needs a simple filename extension");
    std::lock_guard lock(rendererMutex);
    if (renderer) { if (renderers.size() >= 64 && !renderers.contains(extension)) fail("Renderer count exceeds limit"); renderers[extension] = std::move(renderer); }
    else renderers.erase(extension);
    ++rendererRevision;
}
PresentationRecord Presentation::attach(const fs::path &input, const FilePreview &preview, const std::map<std::string, std::string> &properties) {
    validatePreview(preview, true);
    if (properties.size() > 64) fail("Too many presentation properties");
    for (const auto &[key, value] : properties) if (key.empty() || key.size() > 256 || value.size() > 8192) fail("Presentation property exceeds limits");
    const auto p = sourcePath(input); if (providerControl(p)) fail("Cannot attach a preview to provider controls");
    SourceAccess access(p);
    std::lock_guard sourceLock(access);
    const auto before = inspect(p); auto details = detailsFor(p, before, prefix(p, before.size)); details.properties = properties;
    return persist(p, before, version(p, before), preview, std::move(details));
}
void Presentation::enqueue(const fs::path &input) noexcept {
    try {
        if (input.empty()) return;
        const auto p = fs::absolute(input).lexically_normal();
        if (visible(p)) { if (presentationStopped) (void)prepare(p); else worker().add(p); }
    } catch (...) {}
}
bool Presentation::waitForIdle(std::chrono::milliseconds timeout) { return presentationStopped || worker().idle(timeout); }
void Presentation::cancelPending(const fs::path &path) { if (!path.empty() && !presentationStopped) worker().cancel(fs::absolute(path).lexically_normal()); }
void Presentation::removeCompanion(const fs::path &path) {
    const auto companion = companionPath(path);
    if (!fs::exists(fs::symlink_status(companion))) return;
    if (!direct(fs::absolute(companion)) || !fs::is_regular_file(fs::symlink_status(companion))) return;
    try {
        const auto j = Json::parse(File::read(companion, maximumDocumentBytes));
        if (j.is_object() && j.value("format", std::string{}) == "iiFileProvider.Presentation" && j.value("schema", 0) == 1) fs::remove(companion);
    } catch (...) {}
}
void Presentation::watch(const fs::path &input, const std::vector<fs::path> &directories, std::chrono::milliseconds interval, std::function<bool()> available) {
    const auto p = sourcePath(input); (void)inspect(p, true);
    if (directories.empty() || directories.size() > 64) fail("Intake requires one to 64 relative directories");
    for (const auto &d : directories) {
        if (d.empty() || d.is_absolute() || d.lexically_normal() != d || d == ".") fail("Intake directory must be a relative path");
        for (const auto &part : d) if (part == ".." || text(part).starts_with('.')) fail("Intake cannot traverse private or parent directories");
    }
    worker().watch(p, directories, interval, std::move(available));
}
void Presentation::unwatch(const fs::path &root) { worker().unwatch(fs::absolute(root).lexically_normal()); }
} // namespace iiFileProvider::native
