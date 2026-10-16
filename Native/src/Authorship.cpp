#include "Sha256_p.h"
#include "iiFileProviderNative.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <utf8proc.h>

namespace iiFileProvider::native {
namespace {
using Json = nlohmann::json;
void require(bool condition) {
    if (!condition)
        throw std::invalid_argument("Invalid persisted author metadata");
}
void keys(const Json &value, std::initializer_list<std::string_view> allowed) {
    require(value.is_object());
    for (const auto &[key, item] : value.items()) {
        (void)item;
        require(std::find(allowed.begin(), allowed.end(), key) != allowed.end());
    }
}
std::vector<char32_t> scalars(std::string_view value) {
    std::vector<char32_t> result;
    for (std::size_t index = 0; index < value.size();) {
        utf8proc_int32_t code{};
        const auto length =
            utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t *>(value.data() + index),
                             static_cast<utf8proc_ssize_t>(value.size() - index), &code);
        require(length > 0);
        result.push_back(static_cast<char32_t>(code));
        index += static_cast<std::size_t>(length);
    }
    return result;
}
std::string normalize(std::string_view input, bool compatibility = false, bool fold = false) {
    utf8proc_uint8_t *normalized{};
    auto flags = static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE |
                                                (compatibility ? UTF8PROC_COMPAT : 0) | 0);
    auto size = utf8proc_map(reinterpret_cast<const utf8proc_uint8_t *>(input.data()),
                             static_cast<utf8proc_ssize_t>(input.size()), &normalized, flags);
    require(size >= 0);
    std::string output(reinterpret_cast<char *>(normalized), static_cast<std::size_t>(size));
    std::free(normalized);
    if (fold) {
        std::string lower;
        for (auto point : scalars(output)) {
            if (point == 0x130) {
                lower += "i\xcc\x87";
                continue;
            }
            utf8proc_uint8_t encoded[4]{};
            auto size = utf8proc_encode_char(utf8proc_tolower(static_cast<utf8proc_int32_t>(point)),
                                             encoded);
            lower.append(reinterpret_cast<const char *>(encoded), static_cast<std::size_t>(size));
        }
        output = std::move(lower);
    }
    return output;
}
bool whitespace(char32_t c) {
    return (c >= 9 && c <= 13) || c == 0x85 ||
           utf8proc_category(static_cast<utf8proc_int32_t>(c)) == UTF8PROC_CATEGORY_ZS ||
           c == 0x2028 || c == 0x2029;
}
std::string clean(std::string value, std::size_t maximum, bool required = false,
                  bool multiline = false) {
    value = normalize(value);
    auto points = scalars(value);
    while (!points.empty() && whitespace(points.front()))
        points.erase(points.begin());
    while (!points.empty() && whitespace(points.back()))
        points.pop_back();
    require(points.size() <= maximum && (!required || !points.empty()));
    std::string result;
    for (auto point : points) {
        const auto category = utf8proc_category(static_cast<utf8proc_int32_t>(point));
        require((multiline && point == 10) ||
                (category != UTF8PROC_CATEGORY_CC && category != UTF8PROC_CATEGORY_ZL &&
                 category != UTF8PROC_CATEGORY_ZP));
        utf8proc_uint8_t encoded[4]{};
        auto size = utf8proc_encode_char(static_cast<utf8proc_int32_t>(point), encoded);
        result.append(reinterpret_cast<const char *>(encoded), static_cast<std::size_t>(size));
    }
    return result;
}
std::string text(const Json &value, const char *name, std::size_t maximum, bool required = false,
                 bool multiline = false) {
    if (!value.contains(name)) {
        require(!required);
        return {};
    }
    require(value[name].is_string());
    return clean(value[name].get<std::string>(), maximum, required, multiline);
}
int digits(std::string_view value) {
    int result{};
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size());
    return result;
}
Timestamp timestamp(std::string_view raw) {
    require(
        std::regex_match(raw.begin(), raw.end(),
                         std::regex("[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\\.[0-"
                                    "9]{1,3})?(Z|[+-][0-9]{2}:[0-9]{2})")));
    using namespace std::chrono;
    year_month_day day{year{digits(raw.substr(0, 4))},
                       month{static_cast<unsigned>(digits(raw.substr(5, 2)))},
                       std::chrono::day{static_cast<unsigned>(digits(raw.substr(8, 2)))}};
    int h = digits(raw.substr(11, 2)), m = digits(raw.substr(14, 2)), s = digits(raw.substr(17, 2));
    require(day.ok() && h < 24 && m < 60 && s < 60);
    std::size_t offset = 19;
    int ms{};
    if (raw[offset] == '.') {
        auto end = raw.find_first_of("Z+-", ++offset);
        auto fraction = std::string(raw.substr(offset, end - offset));
        while (fraction.size() < 3)
            fraction += '0';
        ms = digits(fraction);
        offset = end;
    }
    int timezone{};
    if (raw[offset] != 'Z') {
        int hours = digits(raw.substr(offset + 1, 2)), minutes = digits(raw.substr(offset + 4, 2));
        require(hours <= 23 && minutes < 60);
        timezone = (hours * 60 + minutes) * (raw[offset] == '+' ? 1 : -1);
    }
    auto instant = sys_days(day) + hours(h) + minutes(m - timezone) + seconds(s) + milliseconds(ms);
    auto date = floor<days>(instant);
    year_month_day utc{date};
    hh_mm_ss time{instant - date};
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02uT%02lld:%02lld:%02lld.%03lldZ",
                  int(utc.year()), unsigned(utc.month()), unsigned(utc.day()),
                  static_cast<long long>(time.hours().count()),
                  static_cast<long long>(time.minutes().count()),
                  static_cast<long long>(time.seconds().count()),
                  static_cast<long long>(time.subseconds().count()));
    return buffer;
}
Json dateValue(const std::string &value) { return value.empty() ? Json(nullptr) : Json(value); }
std::string optionalDate(const Json &value, const char *name, bool required = false) {
    if (!value.contains(name) || value[name].is_null()) {
        require(!required);
        return {};
    }
    require(value[name].is_string());
    return timestamp(value[name].get<std::string>());
}
bool validUri(std::string_view value, bool absolute = true) {
    if (value.empty() || value.size() > 8192)
        return false;
    try {
        std::size_t encodedBytes = 0;
        for (auto c : scalars(value)) {
            if (c <= 32 ||
                utf8proc_category(static_cast<utf8proc_int32_t>(c)) == UTF8PROC_CATEGORY_CC ||
                c == 0x2028 || c == 0x2029)
                return false;
            encodedBytes += c >= 0x80 ? (c < 0x800 ? 6 : (c < 0x10000 ? 9 : 12)) : 1;
        }
        if (encodedBytes > 8192)
            return false;
    } catch (...) {
        return false;
    }
    const auto colon = value.find(':');
    auto path = value.find_first_of("/?#");
    bool scheme = colon != value.npos && (path == value.npos || colon < path);
    if (absolute && !scheme)
        return false;
    if (scheme) {
        if (colon == 0 || !std::isalpha(static_cast<unsigned char>(value[0])))
            return false;
        for (auto c : value.substr(1, colon - 1))
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '+' && c != '-' && c != '.')
                return false;
    }
    for (std::size_t i = 0; i < value.size(); ++i)
        if (value[i] == '%') {
            if (i + 2 >= value.size() || !std::isxdigit(static_cast<unsigned char>(value[i + 1])) ||
                !std::isxdigit(static_cast<unsigned char>(value[i + 2])))
                return false;
            i += 2;
        }
    auto rest = scheme ? value.substr(colon + 1) : value;
    if (rest.starts_with("//")) {
        auto authority = rest.substr(2);
        authority = authority.substr(0, authority.find_first_of("/?#"));
        if (authority.empty())
            return scheme && value.substr(0, colon) == "file";
        auto host =
            authority.substr(authority.rfind('@') == authority.npos ? 0 : authority.rfind('@') + 1);
        if (host.empty())
            return false;
        std::string_view port;
        if (host.starts_with('[')) {
            auto end = host.find(']');
            if (end == host.npos || end == 1 || host.substr(1, end - 1).find(':') == host.npos)
                return false;
            if (end + 1 < host.size()) {
                if (host[end + 1] != ':')
                    return false;
                port = host.substr(end + 2);
            }
        } else {
            if (host.find_first_of("[]\\") != host.npos)
                return false;
            auto sep = host.find(':');
            if (sep != host.npos) {
                if (sep == 0)
                    return false;
                port = host.substr(sep + 1);
            }
        }
        if (!port.empty()) {
            unsigned n{};
            auto parsed = std::from_chars(port.data(), port.data() + port.size(), n);
            if (parsed.ec != std::errc{} || parsed.ptr != port.data() + port.size() || n > 65535)
                return false;
        }
    }
    return true;
}
bool withoutUserInfo(std::string_view url) {
    auto marker = url.find("://");
    if (marker == url.npos)
        return false;
    auto authority = url.substr(marker + 3);
    authority = authority.substr(0, authority.find_first_of("/?#"));
    return !authority.empty() && authority.find('@') == authority.npos;
}
bool validTimeZone(const std::string &value) {
    if (value.empty())
        return true;
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
    try {
        (void)std::chrono::locate_zone(value);
        return true;
    } catch (const std::exception &) {
    }
#endif
    auto path = std::filesystem::path(value);
    if (!path.is_absolute() && !path.has_root_name()) {
        bool safe = true;
        for (const auto &part : path)
            if (part == ".." || part == ".")
                safe = false;
        if (safe)
            for (const auto *root :
                 {"/usr/share/zoneinfo", "/usr/share/lib/zoneinfo", "/var/db/timezone/zoneinfo"}) {
                std::error_code error;
                if (std::filesystem::is_regular_file(std::filesystem::path(root) / path, error))
                    return true;
            }
    }
    if (value == "UTC" || value == "GMT")
        return true;
    if (std::regex_match(value, std::regex("UTC[+-][0-9]{2}:[0-9]{2}")))
        return digits(std::string_view(value).substr(4, 2)) <= 14 &&
               digits(std::string_view(value).substr(7, 2)) < 60;
    return false;
}
std::string origin(std::string value) {
    require(validUri(value) && value.starts_with("https://") &&
            value.find_first_of("@?#", 8) == std::string::npos);
    auto slash = value.find('/', 8);
    require(slash == std::string::npos || slash == value.size() - 1);
    if (slash != std::string::npos)
        value.pop_back();
    require(value.size() > 8 && value.size() <= 2048);
    std::transform(value.begin() + 8, value.end(), value.begin() + 8,
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value.ends_with(":443"))
        value.resize(value.size() - 4);
    return value;
}
const std::map<std::string, std::size_t> detailLimits{
    {"fullName", 160},   {"givenName", 80},   {"additionalName", 80}, {"familyName", 80},
    {"pseudonym", 80},   {"biography", 4096}, {"organization", 160},  {"organizationId", 128},
    {"department", 160}, {"team", 160},       {"jobTitle", 160},      {"contactEmail", 254},
    {"phoneNumber", 64}, {"locale", 64},      {"timeZone", 128},      {"countryCode", 2},
    {"region", 160},     {"city", 160}};
Json profile(Json value) {
    require(value.dump().size() <= 131072);
    keys(value, {"schemaVersion", "account", "details", "attribution", "serviceOrigin",
                 "capturedAt", "device"});
    require(value.at("schemaVersion").is_number() && value.at("schemaVersion") == 1);
    const auto service = origin(text(value, "serviceOrigin", 2048, true));
    value["serviceOrigin"] = service;
    value["capturedAt"] = optionalDate(value, "capturedAt", true);
    auto account = value.at("account");
    keys(account, {"sub", "email", "displayName", "userId", "societyCloudMembership", "avatarUrl"});
    auto sub = text(account, "sub", 128, true);
    require(std::regex_match(sub, std::regex("[A-Za-z0-9_-]{1,128}")));
    auto mail = normalize(text(account, "email", 254, true), true, true);
    require(std::regex_match(mail, std::regex("[^\\s@]+@[^\\s@]+\\.[^\\s@]+")) &&
            mail.find('@') <= 64);
    auto user = text(account, "userId", 31);
    require(user.empty() || std::regex_match(user, std::regex("@[a-z0-9_]{3,30}")));
    auto membership = account.value("societyCloudMembership", std::string("Free"));
    require(membership == "Free" || membership == "Plus" || membership == "Pro" ||
            membership == "Enterprise");
    auto avatar = account.value("avatarUrl", Json(nullptr));
    if (!avatar.is_null()) {
        require(avatar.is_string());
        auto url = avatar.get<std::string>();
        if (url.starts_with('/'))
            url = service + url;
        require(url.starts_with(service + "/media/avatars/"));
        require(std::regex_match(url.substr(service.size()),
                                 std::regex("/media/avatars/[a-f0-9]{64}\\.webp")));
        avatar = url;
    }
    value["account"] = {{"sub", sub},
                        {"email", mail},
                        {"displayName", text(account, "displayName", 80)},
                        {"userId", user},
                        {"societyCloudMembership", membership},
                        {"avatarUrl", avatar}};
    auto details = value.value("details", Json::object());
    require(details.is_object() && details.dump().size() <= 65536);
    for (const auto &[key, item] : details.items()) {
        (void)item;
        require(detailLimits.contains(key) || key == "links" || key == "identifiers");
    }
    for (const auto &[key, maximum] : detailLimits)
        details[key] = text(details, key.c_str(), maximum, false, key == "biography");
    auto country = details["countryCode"].get<std::string>();
    require(country.empty() || std::regex_match(country, std::regex("[A-Z]{2}")));
    auto locale = details["locale"].get<std::string>();
    require(locale.empty() ||
            std::regex_match(locale, std::regex("[A-Za-z]{2,8}(-[A-Za-z0-9]{1,8})*")));
    require(validTimeZone(details["timeZone"].get<std::string>()));
    auto email = details["contactEmail"].get<std::string>();
    if (!email.empty()) {
        email = normalize(email, true, true);
        require(std::regex_match(email, std::regex("[^\\s@]+@[^\\s@]+\\.[^\\s@]+")));
        details["contactEmail"] = email;
    }
    for (const auto *name : {"links", "identifiers"}) {
        auto entries = details.value(name, Json::array());
        require(entries.is_array() && entries.size() <= 32);
        for (auto &entry : entries) {
            if (std::string_view(name) == "links") {
                keys(entry, {"relation", "label", "url"});
                entry["relation"] = text(entry, "relation", 40, true);
                entry["label"] = text(entry, "label", 160);
                auto url = text(entry, "url", 2048, true);
                require(validUri(url) && url.starts_with("https://") && withoutUserInfo(url));
                entry["url"] = url;
            } else {
                keys(entry, {"scheme", "value"});
                entry["scheme"] = text(entry, "scheme", 40, true);
                entry["value"] = text(entry, "value", 256, true);
            }
        }
        details[name] = entries;
    }
    value["details"] = details;
    auto attribution = value.value("attribution", Json::object());
    keys(attribution, {"roles", "credit", "copyrightNotice", "licenseIdentifier", "licenseUrl",
                       "documentId", "projectId", "workspaceId", "createdAt", "modifiedAt"});
    auto roles = attribution.value("roles", Json::array());
    require(roles.is_array() && roles.size() <= 16);
    std::set<std::string> unique;
    for (auto &role : roles) {
        require(role.is_string());
        auto name = clean(role.get<std::string>(), 40, true);
        require(unique.insert(name).second);
        role = name;
    }
    attribution["roles"] = roles;
    for (const auto *key : {"credit", "copyrightNotice", "licenseIdentifier", "licenseUrl",
                            "documentId", "projectId", "workspaceId"})
        attribution[key] = text(
            attribution, key,
            (std::string_view(key) == "licenseUrl"
                 ? 2048
                 : (std::string_view(key) == "credit" || std::string_view(key) == "copyrightNotice"
                        ? 1024
                        : 128)));
    auto license = attribution["licenseUrl"].get<std::string>();
    require(license.empty() ||
            (validUri(license) && license.starts_with("https://") && withoutUserInfo(license)));
    auto created = optionalDate(attribution, "createdAt"),
         modified = optionalDate(attribution, "modifiedAt");
    require(created.empty() || modified.empty() || created <= modified);
    attribution["createdAt"] = dateValue(created);
    attribution["modifiedAt"] = dateValue(modified);
    value["attribution"] = attribution;
    auto device = value.value("device", Json(nullptr));
    if (!device.is_null()) {
        keys(device, {"id", "type", "name", "platform", "osVersion", "appId", "appVersion"});
        const std::map<std::string, std::size_t> limits{
            {"id", 64},        {"type", 6},    {"name", 80},      {"platform", 40},
            {"osVersion", 80}, {"appId", 128}, {"appVersion", 40}};
        for (const auto &[key, maximum] : limits)
            device[key] = text(device, key.c_str(), maximum, true);
        require(std::regex_match(device.value("id", std::string()), std::regex("[a-f0-9]{64}")) &&
                (device.value("type", "") == "pc" || device.value("type", "") == "tablet"));
    }
    value["device"] = device;
    require(value.dump().size() <= 131072);
    return value;
}
std::string keyFor(const FileAuthor &author) {
    return sha256(author.metadata().serviceOrigin + '\n' + author.metadata().account.sub);
}
std::vector<FileLink> validatedLinks(const std::vector<FileLink> &links) {
    require(links.size() <= Authorship::MaximumLinks);
    std::vector<FileLink> result;
    for (const auto &link : links) {
        auto parsed = FileLink::create(link.name, link.url);
        require(parsed.has_value() &&
                std::find(result.begin(), result.end(), *parsed) == result.end());
        result.push_back(std::move(*parsed));
    }
    return result;
}
} // namespace
Timestamp utcNow() {
    using namespace std::chrono;
    auto now = floor<milliseconds>(system_clock::now());
    auto day = floor<days>(now);
    year_month_day date{day};
    hh_mm_ss time{now - day};
    char value[32];
    std::snprintf(value, sizeof(value), "%04d-%02u-%02uT%02lld:%02lld:%02lld.%03lldZ",
                  int(date.year()), unsigned(date.month()), unsigned(date.day()),
                  static_cast<long long>(time.hours().count()),
                  static_cast<long long>(time.minutes().count()),
                  static_cast<long long>(time.seconds().count()),
                  static_cast<long long>(time.subseconds().count()));
    return value;
}
std::string sha256(std::string_view bytes) {
    return detail::sha256(std::as_bytes(std::span(bytes)));
}
std::optional<FileAuthor> FileAuthor::fromJsonText(std::string_view snapshot) {
    try {
        require(snapshot.size() <= 131072);
        auto data = profile(Json::parse(snapshot));
        FileAuthor result;
        result.profile_ = data.dump();
        auto &a = data["account"];
        result.metadata_ = {
            {a["sub"], a["email"], a["displayName"], a["userId"], a["societyCloudMembership"],
             a["avatarUrl"].is_null() ? std::string() : a["avatarUrl"].get<std::string>()},
            data["serviceOrigin"],
            data["capturedAt"]};
        return result;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
std::optional<FileAuthor>
FileAuthor::fromIisaccAccount(const std::map<std::string, std::string> &account,
                              std::string service, Timestamp capturedAt) {
    Json data = {{"schemaVersion", 1},
                 {"account", account},
                 {"serviceOrigin", service},
                 {"capturedAt", capturedAt}};
    return fromJsonText(data.dump());
}
std::string FileAuthor::displayLabel() const {
    return metadata_.account.displayName.empty() ? metadata_.account.email
                                                 : metadata_.account.displayName;
}
std::optional<FileLink> FileLink::create(std::string name, std::string url) {
    try {
        name = clean(name, 160, true);
        require(name.find('|') == std::string::npos && validUri(url, false));
        return FileLink{std::move(name), std::move(url)};
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
std::optional<FileLink> FileLink::fromString(std::string_view entry) {
    auto sep = entry.find('|');
    if (!entry.starts_with('[') || !entry.ends_with(']') || sep == std::string_view::npos ||
        sep <= 1 || sep >= entry.size() - 2)
        return {};
    return create(std::string(entry.substr(1, sep - 1)),
                  std::string(entry.substr(sep + 1, entry.size() - sep - 2)));
}
Authorship::Authorship() { rebuildDump(); }
bool Authorship::isEmpty() const noexcept { return revision_ == 0 && authors_.empty(); }
void Authorship::clearActiveAuthor() noexcept { activeAuthor_.clear(); }
bool Authorship::hasActiveAuthor() const noexcept { return !activeAuthor_.empty(); }
std::optional<FileAuthor> Authorship::firstEditor() const {
    return authors_.empty() ? std::nullopt : FileAuthor::fromJsonText(authors_.front().profile);
}
std::vector<FileAuthor> Authorship::participants() const {
    std::vector<FileAuthor> result;
    for (std::size_t i = 1; i < authors_.size(); ++i)
        result.push_back(*FileAuthor::fromJsonText(authors_[i].profile));
    return result;
}
std::vector<FileLink> Authorship::links() const { return links_; }
void Authorship::rebuildDump() {
    Json authors = Json::array(), participants = Json::array(), links = Json::array();
    for (std::size_t i = 0; i < authors_.size(); ++i) {
        const auto &r = authors_[i];
        authors.push_back({{"key", r.key},
                           {"author", Json::parse(r.profile)},
                           {"firstChangedAt", r.firstChangedAt},
                           {"lastChangedAt", r.lastChangedAt}});
        if (i)
            participants.push_back(r.key);
    }
    for (const auto &l : links_)
        links.push_back({{"name", l.name}, {"url", l.url}});
    dump_ = Json{{"schemaVersion", 3},
                 {"revision", std::to_string(revision_)},
                 {"modifiedAt", dateValue(modifiedAt_)},
                 {"authors", authors},
                 {"firstEditor", authors_.empty() ? Json(nullptr) : Json(authors_.front().key)},
                 {"participants", participants},
                 {"links", links},
                 {"lastAuthor", dateValue(lastAuthor_)}}
                .dump();
    if (dump_.size() > MaximumBytes)
        throw std::length_error("Authorship metadata exceeds its size limit");
}
void Authorship::recordChange(Timestamp at) {
    at = timestamp(at);
    if (revision_ == UINT64_MAX)
        throw std::overflow_error("Authorship revision exhausted");
    auto next = *this;
    ++next.revision_;
    if (next.modifiedAt_.empty() || at > next.modifiedAt_)
        next.modifiedAt_ = at;
    next.lastAuthor_ = next.activeAuthor_;
    for (auto &r : next.authors_)
        if (r.key == next.activeAuthor_) {
            if (r.firstChangedAt.empty())
                r.firstChangedAt = next.modifiedAt_;
            r.lastChangedAt = next.modifiedAt_;
            break;
        }
    next.rebuildDump();
    *this = std::move(next);
}
bool Authorship::setAuthor(const FileAuthor &author, Timestamp at) {
    return setAuthor(author, links_, std::move(at));
}
bool Authorship::setAuthor(const FileAuthor &author, const std::vector<FileLink> &links,
                           Timestamp at) {
    auto next = *this;
    next.links_ = validatedLinks(links);
    auto key = keyFor(author);
    next.activeAuthor_ = key;
    for (auto &r : next.authors_)
        if (r.key == key) {
            if (r.profile == author.toJsonText() && next.links_ == links_) {
                activeAuthor_ = key;
                return false;
            }
            r.profile = author.toJsonText();
            next.recordChange(std::move(at));
            *this = std::move(next);
            return true;
        }
    require(next.authors_.size() < 256);
    next.authors_.push_back({key, author.toJsonText(), {}, {}});
    next.recordChange(std::move(at));
    *this = std::move(next);
    return true;
}
bool Authorship::setLinks(const std::vector<FileLink> &links, Timestamp at) {
    auto parsed = validatedLinks(links);
    if (parsed == links_)
        return false;
    auto next = *this;
    next.links_ = std::move(parsed);
    next.recordChange(std::move(at));
    *this = std::move(next);
    return true;
}
bool Authorship::setLinksFromStrings(const std::vector<std::string> &entries, Timestamp at) {
    std::vector<FileLink> links;
    for (const auto &entry : entries) {
        auto parsed = FileLink::fromString(entry);
        require(parsed.has_value());
        links.push_back(std::move(*parsed));
    }
    return setLinks(links, std::move(at));
}
std::optional<Authorship> Authorship::fromDump(std::string_view bytes) {
    try {
        require(bytes.size() <= MaximumBytes);
        auto value = Json::parse(bytes);
        require(value.at("schemaVersion").is_number());
        int schema = value.value("schemaVersion", 0);
        require(value["schemaVersion"] == schema && schema >= 1 && schema <= 3);
        if (schema == 1) {
            require(value.dump().size() <= 1024 * 1024);
            keys(value, {"schemaVersion", "revision", "modifiedAt", "authors", "lastAuthor"});
        } else if (schema == 2) {
            require(value.dump().size() <= 1024 * 1024 + 32 * 1024);
            keys(value, {"schemaVersion", "revision", "modifiedAt", "authors", "lastAuthor",
                         "firstEditor", "participants"});
        } else
            keys(value, {"schemaVersion", "revision", "modifiedAt", "authors", "lastAuthor",
                         "firstEditor", "participants", "links"});
        auto raw = text(value, "revision", 20, true);
        require(std::regex_match(raw, std::regex("0|[1-9][0-9]*")));
        Authorship result;
        auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), result.revision_);
        require(parsed.ec == std::errc{} && parsed.ptr == raw.data() + raw.size());
        result.modifiedAt_ = optionalDate(value, "modifiedAt", result.revision_ != 0);
        auto authors = value.at("authors");
        require(authors.is_array() && authors.size() <= 256);
        std::set<std::string> seen;
        for (auto &item : authors) {
            keys(item, {"key", "author", "firstChangedAt", "lastChangedAt"});
            auto author = FileAuthor::fromJsonText(item.at("author").dump());
            require(author.has_value());
            auto key = text(item, "key", 64, true);
            require(key == keyFor(*author) && seen.insert(key).second);
            auto first = optionalDate(item, "firstChangedAt", true),
                 last = optionalDate(item, "lastChangedAt", true);
            require(first <= last && last <= result.modifiedAt_);
            result.authors_.push_back({key, author->toJsonText(), first, last});
        }
        auto last = value.at("lastAuthor");
        require(last.is_null() || last.is_string());
        if (last.is_string()) {
            result.lastAuthor_ = last.get<std::string>();
            require(seen.contains(result.lastAuthor_));
        }
        if (schema == 3) {
            auto links = value.at("links");
            require(links.is_array() && links.size() <= MaximumLinks);
            for (auto &item : links) {
                keys(item, {"name", "url"});
                auto link = FileLink::create(item.at("name"), item.at("url"));
                require(link.has_value());
                result.links_.push_back(*link);
            }
            result.links_ = validatedLinks(result.links_);
        }
        require(result.revision_ != 0 ||
                (result.authors_.empty() && result.links_.empty() && result.modifiedAt_.empty()));
        result.rebuildDump();
        if (schema >= 2) {
            auto canonical = Json::parse(result.dump_);
            require(value.at("firstEditor") == canonical["firstEditor"] &&
                    value.at("participants") == canonical["participants"]);
        }
        return result;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}
} // namespace iiFileProvider::native
