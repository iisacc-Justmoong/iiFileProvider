#include "ObjectMetadataCodec_p.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <stdexcept>

namespace iiFileProvider::detail {
namespace {
using Json = nlohmann::json;
[[noreturn]] void fail(const char *message) { throw std::runtime_error(message); }
void text(const std::string &s, std::size_t limit, bool required = false) {
    if ((required && s.empty()) || s.size() > limit || s.find('\0') != std::string::npos)
        fail("invalid envelope/session text or byte limit");
    // dump() rejects malformed UTF-8. Values are preserved without normalization.
    (void)Json(s).dump();
}
void strings(const std::vector<std::string> &v, std::size_t count, std::size_t bytes) {
    if (v.size() > count) fail("too many envelope metadata values");
    for (const auto &s : v) text(s, bytes, true);
}
bool credential(const std::string &key) {
    std::string name;
    for (const unsigned char c : key) if (c != '_' && c != '-') name += static_cast<char>(std::tolower(c));
    return name == "token" || name == "secret" || name == "password" || name == "authorization"
        || name == "accesstoken" || name == "refreshtoken" || name == "authenticationtoken";
}
Json metadataJson(const FileMetadata &m) {
    text(m.title, 4096); text(m.description, 64 * 1024);
    strings(m.keywords, 256, 256); strings(m.collections, 128, 128);
    if (m.rating > 5 || static_cast<unsigned>(m.color) > static_cast<unsigned>(ColorLabel::Gray) || m.fields.size() > 64)
        fail("invalid envelope classification");
    for (const auto &[key, value] : m.fields) { text(key, 256, true); text(value, 64 * 1024); }
    Json result{{"title", m.title}, {"description", m.description}, {"keywords", m.keywords},
        {"rating", m.rating}, {"color", static_cast<unsigned>(m.color)}, {"collections", m.collections}, {"fields", m.fields}};
    if (result.dump().size() > FileCatalog::maximumMetadataBytes) fail("envelope metadata exceeds 256 KiB");
    return result;
}
FileMetadata parseMetadata(const Json &j) {
    if (!j.is_object() || j.size() != 7) fail("invalid envelope classification schema");
    FileMetadata m;
    m.title = j.at("title").get<std::string>(); m.description = j.at("description").get<std::string>();
    m.keywords = j.at("keywords").get<std::vector<std::string>>();
    if (!j.at("rating").is_number_unsigned() || !j.at("color").is_number_unsigned()
        || j.at("rating").get<std::uint64_t>() > 5 || j.at("color").get<std::uint64_t>() > 7)
        fail("invalid envelope classification values");
    m.rating = j.at("rating").get<unsigned>(); m.color = static_cast<ColorLabel>(j.at("color").get<unsigned>());
    m.fields = j.at("fields").get<std::map<std::string, std::string>>();
    m.collections = j.at("collections").get<std::vector<std::string>>();
    (void)metadataJson(m); return m;
}
Json parse(const std::string &s) {
    if (s.size() > FileCatalog::maximumMetadataBytes + 65536) fail("envelope/session document exceeds limit");
    // Writer output is canonical. Parsing never accepts trailing data or invalid UTF-8.
    return Json::parse(s);
}
std::string pointerToken(const std::string &s) {
    std::string result;
    for (char c : s) result += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return result;
}
void differences(const Json *before, const Json *after, const std::string &path, std::vector<MetadataChange> &out) {
    if (before && after && *before == *after) return;
    if (before && after && before->is_object() && after->is_object()) {
        std::set<std::string> keys;
        for (const auto &[key, value] : before->items()) keys.insert(key);
        for (const auto &[key, value] : after->items()) keys.insert(key);
        for (const auto &key : keys) differences(before->contains(key) ? &before->at(key) : nullptr,
            after->contains(key) ? &after->at(key) : nullptr, path + "/" + pointerToken(key), out);
    } else out.push_back({path, before ? std::optional(before->dump()) : std::nullopt,
        after ? std::optional(after->dump()) : std::nullopt});
}
}
std::string encodeRawMetadata(const RawMetadata &raw) {
    text(raw.originalName, 4096, true); text(raw.mediaType, 256, true);
    if (raw.originalName == "." || raw.originalName == ".." || raw.originalName.find_first_of("/\\") != std::string::npos)
        fail("RAW original name must be a basename");
    return Json{{"schemaVersion", 1}, {"originalName", raw.originalName}, {"mediaType", raw.mediaType},
        {"metadata", metadataJson(raw.annotations)}}.dump();
}
RawMetadata decodeRawMetadata(const std::string &s) {
    auto j = parse(s);
    if (!j.is_object() || j.size() != 4 || j.at("schemaVersion") != 1) fail("unknown RAW envelope schema");
    RawMetadata raw{j.at("originalName").get<std::string>(), j.at("mediaType").get<std::string>(), parseMetadata(j.at("metadata"))};
    if (encodeRawMetadata(raw) != s) fail("noncanonical RAW metadata");
    return raw;
}
std::string encodeSessionContext(const ObjectSessionContext &s) {
    text(s.application, 4096); text(s.applicationVersion, 256); text(s.project, 4096); text(s.document, 4096);
    if (s.attributes.size() > 64) fail("too many work-session attributes");
    for (const auto &[key, value] : s.attributes) {
        text(key, 256, true); text(value, 16384);
        if (credential(key)) fail("runtime credentials are forbidden in work sessions");
    }
    if (s == ObjectSessionContext{}) return {};
    auto result = Json{{"schemaVersion", 1}, {"application", s.application}, {"applicationVersion", s.applicationVersion},
        {"project", s.project}, {"document", s.document}, {"attributes", s.attributes}}.dump();
    if (result.size() > FileCatalog::maximumMetadataBytes) fail("work-session context exceeds 256 KiB");
    return result;
}
ObjectSessionContext decodeSessionContext(const std::string &s) {
    if (s.empty()) return {};
    auto j = parse(s);
    if (!j.is_object() || j.size() != 6 || j.at("schemaVersion") != 1) fail("unknown work-session context schema");
    ObjectSessionContext result{j.at("application").get<std::string>(), j.at("applicationVersion").get<std::string>(),
        j.at("project").get<std::string>(), j.at("document").get<std::string>(), j.at("attributes").get<std::map<std::string,std::string>>()};
    if (encodeSessionContext(result) != s) fail("noncanonical work-session context");
    return result;
}
std::vector<MetadataChange> metadataChanges(const std::string &before, const std::string &after) {
    auto a = before.empty() ? Json::object() : metadataJson(decodeRawMetadata(before).annotations);
    auto b = after.empty() ? Json::object() : metadataJson(decodeRawMetadata(after).annotations);
    std::vector<MetadataChange> result; differences(&a, &b, "", result); return result;
}
}
