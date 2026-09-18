#include "FileAuthor.h"
#include "JsonContract.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QTimeZone>

#include <utility>

namespace iiFileProvider {
namespace {
using namespace detail;

const QStringList membershipNames{"Free", "Plus", "Pro", "Enterprise"};
QString membershipName(SocietyCloudMembership membership)
{
    const auto index = static_cast<int>(membership);
    require(index >= 0 && index < membershipNames.size(), "societyCloudMembership");
    return membershipNames[index];
}

IisaccAccount readAccount(const QJsonObject& json, const QUrl& origin, bool strict)
{
    if (strict) knownKeys(json, {"sub", "email", "displayName", "userId", "societyCloudMembership", "avatarUrl"}, "account.fields");
    IisaccAccount account;
    // Stable identity is never guessed from a handle, email, name or a token payload.
    require(json.value("sub").isString(), "sub");
    account.sub = json.value("sub").toString();
    require(matches(account.sub, "\\A[A-Za-z0-9_-]{1,128}\\z"), "sub");
    account.email = email(text(json, "email", 254, true), "email");
    account.displayName = text(json, "displayName", 80);
    account.userId = text(json, "userId", 31);
    require(account.userId.isEmpty() || matches(account.userId, "\\A@[a-z0-9_]{3,30}\\z"), "userId");
    const auto name = json.contains("societyCloudMembership")
        ? text(json, "societyCloudMembership", 16, true) : QString("Free");
    const auto index = membershipNames.indexOf(name);
    require(index >= 0, "societyCloudMembership");
    account.societyCloudMembership = static_cast<SocietyCloudMembership>(index);
    const auto avatar = json.value("avatarUrl");
    if (!avatar.isNull() && !avatar.isUndefined()) {
        require(avatar.isString(), "avatarUrl");
        const auto raw = avatar.toString();
        QUrl candidate(raw, QUrl::StrictMode);
        // Stored metadata uses the resolved URL; the server's native field is relative.
        if (strict && !candidate.isRelative()) {
            require(candidate.isValid() && candidate.adjusted(QUrl::RemovePath) == origin, "avatarUrl");
        } else {
            require(raw.startsWith('/') && !raw.startsWith("//"), "avatarUrl");
            candidate = origin.resolved(candidate);
        }
        require(candidate.isValid() && !candidate.hasQuery() && !candidate.hasFragment()
            && candidate.userInfo().isEmpty()
            && matches(candidate.path(QUrl::FullyEncoded), "\\A/media/avatars/[a-f0-9]{64}\\.webp\\z"), "avatarUrl");
        account.avatarUrl = candidate;
    }
    return account;
}

QJsonObject accountJson(const IisaccAccount& account)
{
    return {{"sub", account.sub}, {"email", account.email}, {"displayName", account.displayName},
        {"userId", account.userId}, {"societyCloudMembership", membershipName(account.societyCloudMembership)},
        {"avatarUrl", account.avatarUrl.isEmpty() ? QJsonValue(QJsonValue::Null)
            : QJsonValue(account.avatarUrl.toString(QUrl::FullyEncoded))}};
}

struct DetailField { const char* key; QString AuthorDetails::*member; int maximum; };
const DetailField detailFields[] = {
    {"fullName", &AuthorDetails::fullName, 160}, {"givenName", &AuthorDetails::givenName, 80},
    {"additionalName", &AuthorDetails::additionalName, 80}, {"familyName", &AuthorDetails::familyName, 80},
    {"pseudonym", &AuthorDetails::pseudonym, 80}, {"biography", &AuthorDetails::biography, 4096},
    {"organization", &AuthorDetails::organization, 160}, {"organizationId", &AuthorDetails::organizationId, 128},
    {"department", &AuthorDetails::department, 160}, {"team", &AuthorDetails::team, 160},
    {"jobTitle", &AuthorDetails::jobTitle, 160}, {"contactEmail", &AuthorDetails::contactEmail, 254},
    {"phoneNumber", &AuthorDetails::phoneNumber, 64}, {"locale", &AuthorDetails::locale, 64},
    {"timeZone", &AuthorDetails::timeZone, 128}, {"countryCode", &AuthorDetails::countryCode, 2},
    {"region", &AuthorDetails::region, 160}, {"city", &AuthorDetails::city, 160}
};

QJsonObject detailsJson(const AuthorDetails& details);
AuthorDetails readDetails(const QJsonObject& json)
{
    require(QJsonDocument(json).toJson(QJsonDocument::Compact).size() <= 65536, "details.size");
    AuthorDetails details;
    QStringList keys{"links", "identifiers"};
    for (const auto& field : detailFields) {
        keys.append(field.key);
        details.*(field.member) = text(json, field.key, field.maximum, false, QString(field.key) == "biography");
    }
    knownKeys(json, keys, "details.fields");
    if (!details.contactEmail.isEmpty()) details.contactEmail = email(details.contactEmail, "contactEmail");
    require(details.locale.isEmpty() || matches(details.locale, "\\A[A-Za-z]{2,8}(?:-[A-Za-z0-9]{1,8})*\\z"), "locale");
    require(details.countryCode.isEmpty() || matches(details.countryCode, "\\A[A-Z]{2}\\z"), "countryCode");
    require(details.timeZone.isEmpty() || QTimeZone(details.timeZone.toUtf8()).isValid(), "timeZone");
    for (const auto* key : {"links", "identifiers"}) {
        const auto value = json.value(key);
        if (value.isUndefined()) continue;
        require(value.isArray() && value.toArray().size() <= 32, key);
        for (const auto& entry : value.toArray()) {
            require(entry.isObject(), key);
            const auto item = entry.toObject();
            if (QString(key) == "links") {
                knownKeys(item, {"relation", "label", "url"}, "links.fields");
                const auto url = httpsUrl(text(item, "url", 2048, true), "links.url", true);
                require(url.toString(QUrl::FullyEncoded).size() <= 2048, "links.url");
                details.links.append({text(item, "relation", 40, true), text(item, "label", 160),
                    url});
            } else {
                knownKeys(item, {"scheme", "value"}, "identifiers.fields");
                details.identifiers.append({text(item, "scheme", 40, true), text(item, "value", 256, true)});
            }
        }
    }
    require(QJsonDocument(detailsJson(details)).toJson(QJsonDocument::Compact).size() <= 65536, "details.size");
    return details;
}

QJsonObject detailsJson(const AuthorDetails& details)
{
    QJsonObject json;
    for (const auto& field : detailFields) json[field.key] = details.*(field.member);
    QJsonArray links, identifiers;
    for (const auto& link : details.links)
        links.append(QJsonObject{{"relation", link.relation}, {"label", link.label}, {"url", link.url.toString(QUrl::FullyEncoded)}});
    for (const auto& identifier : details.identifiers)
        identifiers.append(QJsonObject{{"scheme", identifier.scheme}, {"value", identifier.value}});
    json["links"] = links;
    json["identifiers"] = identifiers;
    return json;
}

FileAttribution readAttribution(const QJsonObject& json)
{
    knownKeys(json, {"roles", "credit", "copyrightNotice", "licenseIdentifier", "licenseUrl",
        "documentId", "projectId", "workspaceId", "createdAt", "modifiedAt"}, "attribution.fields");
    FileAttribution attribution;
    const auto roles = json.value("roles");
    if (!roles.isUndefined()) {
        require(roles.isArray() && roles.toArray().size() <= 16, "roles");
        for (const auto& role : roles.toArray()) {
            require(role.isString(), "roles");
            const auto name = cleanText(role.toString(), "roles", 40, true);
            require(!attribution.roles.contains(name), "roles");
            attribution.roles.append(name);
        }
    }
    attribution.credit = text(json, "credit", 1024);
    attribution.copyrightNotice = text(json, "copyrightNotice", 1024);
    attribution.licenseIdentifier = text(json, "licenseIdentifier", 128);
    attribution.licenseUrl = httpsUrl(text(json, "licenseUrl", 2048), "licenseUrl");
    attribution.documentId = text(json, "documentId", 128);
    attribution.projectId = text(json, "projectId", 128);
    attribution.workspaceId = text(json, "workspaceId", 128);
    attribution.createdAt = timestamp(json, "createdAt");
    attribution.modifiedAt = timestamp(json, "modifiedAt");
    require(!attribution.createdAt.isValid() || !attribution.modifiedAt.isValid()
        || attribution.modifiedAt >= attribution.createdAt, "attribution.timestamps");
    return attribution;
}

QJsonObject attributionJson(const FileAttribution& attribution)
{
    return {{"roles", QJsonArray::fromStringList(attribution.roles)}, {"credit", attribution.credit},
        {"copyrightNotice", attribution.copyrightNotice}, {"licenseIdentifier", attribution.licenseIdentifier},
        {"licenseUrl", attribution.licenseUrl.toString(QUrl::FullyEncoded)},
        {"documentId", attribution.documentId}, {"projectId", attribution.projectId},
        {"workspaceId", attribution.workspaceId}, {"createdAt", dateJson(attribution.createdAt)},
        {"modifiedAt", dateJson(attribution.modifiedAt)}};
}

AuthorDevice readDevice(const QJsonObject& json, bool strict)
{
    if (strict) knownKeys(json, {"id", "type", "name", "platform", "osVersion", "appId", "appVersion"}, "device.fields");
    AuthorDevice device;
    device.id = text(json, "id", 64, true);
    device.type = text(json, "type", 6, true);
    require(matches(device.id, "\\A[a-f0-9]{64}\\z"), "device.id");
    require(device.type == "pc" || device.type == "tablet", "device.type");
    device.name = text(json, "name", 80, true);
    device.platform = text(json, "platform", 40, true);
    device.osVersion = text(json, "osVersion", 80, true);
    device.appId = text(json, "appId", 128, true);
    device.appVersion = text(json, "appVersion", 40, true);
    return device;
}

QJsonObject deviceJson(const AuthorDevice& device)
{
    return {{"id", device.id}, {"type", device.type}, {"name", device.name},
        {"platform", device.platform}, {"osVersion", device.osVersion},
        {"appId", device.appId}, {"appVersion", device.appVersion}};
}

QJsonObject metadataJson(const AuthorMetadata& data)
{
    return {{"schemaVersion", FileAuthor::SchemaVersion}, {"account", accountJson(data.account)},
        {"details", detailsJson(data.details)}, {"attribution", attributionJson(data.attribution)},
        {"serviceOrigin", data.serviceOrigin.toString(QUrl::FullyEncoded)}, {"capturedAt", dateJson(data.capturedAt)},
        {"device", data.device ? QJsonValue(deviceJson(*data.device)) : QJsonValue(QJsonValue::Null)}};
}

AuthorMetadata readMetadata(const QJsonObject& json)
{
    require(QJsonDocument(json).toJson(QJsonDocument::Compact).size() <= 131072, "metadata.size");
    knownKeys(json, {"schemaVersion", "account", "details", "attribution", "serviceOrigin", "capturedAt", "device"}, "metadata.fields");
    require(json.value("schemaVersion").isDouble()
        && json.value("schemaVersion").toDouble() == FileAuthor::SchemaVersion, "schemaVersion");
    AuthorMetadata data;
    data.serviceOrigin = serviceOrigin(QUrl(text(json, "serviceOrigin", 2048, true), QUrl::StrictMode));
    data.capturedAt = timestamp(json, "capturedAt", true);
    data.account = readAccount(object(json, "account"), data.serviceOrigin, true);
    data.details = readDetails(object(json, "details", false));
    data.attribution = readAttribution(object(json, "attribution", false));
    const auto device = json.value("device");
    if (!device.isNull() && !device.isUndefined()) data.device = readDevice(object(json, "device"), true);
    return data;
}
}

FileAuthor::FileAuthor(AuthorMetadata metadata) : m_metadata(std::move(metadata)) {}

std::optional<FileAuthor> FileAuthor::fromIisaccAccount(const QJsonObject& account,
    const QUrl& origin, const QDateTime& capturedAt, QString* error)
{
    if (error) error->clear();
    try {
        require(QJsonDocument(account).toJson(QJsonDocument::Compact).size() <= 131072, "account.size");
        AuthorMetadata data;
        data.serviceOrigin = serviceOrigin(origin);
        require(capturedAt.isValid(), "capturedAt");
        data.capturedAt = capturedAt.toUTC();
        data.account = readAccount(account, data.serviceOrigin, false);
        data.details = readDetails(object(account, "authorDetails", false));
        return FileAuthor(std::move(data));
    } catch (const InvalidField& invalid) {
        finishError(error, invalid);
        return std::nullopt;
    }
}

std::optional<FileAuthor> FileAuthor::fromIisaccAppSession(const QJsonObject& response,
    const QUrl& origin, const QDateTime& capturedAt, QString* error)
{
    if (error) error->clear();
    try {
        require(QJsonDocument(response).toJson(QJsonDocument::Compact).size() <= 196608, "response.size");
        require((!response.contains("error") || response.value("error").isNull())
            && (!response.contains("challenge") || response.value("challenge").isNull()), "response.state");
        auto author = fromIisaccAccount(object(response, "account"), origin, capturedAt, error);
        if (!author) return std::nullopt;
        const auto json = object(response, "session");
        AuthorLoginSession session;
        session.id = text(json, "id", 32, true);
        require(matches(session.id, "\\A[a-f0-9]{32}\\z"), "session.id");
        session.client = text(json, "client", 3, true);
        require(session.client == "app" && json.value("current").isBool()
            && json.value("current").toBool(), "session.current");
        session.device = readDevice(object(json, "device"), false);
        session.createdAt = timestamp(json, "createdAt", true);
        session.lastSeenAt = timestamp(json, "lastSeenAt", true);
        session.expiresAt = timestamp(json, "expiresAt", true);
        require(session.createdAt <= session.lastSeenAt && session.lastSeenAt <= capturedAt
            && capturedAt < session.expiresAt, "session.timestamps");
        author->m_metadata.device = session.device;
        author->m_session = std::move(session);
        return author;
    } catch (const InvalidField& invalid) {
        finishError(error, invalid);
        return std::nullopt;
    }
}

std::optional<FileAuthor> FileAuthor::fromJson(const QJsonObject& json, QString* error)
{
    if (error) error->clear();
    try { return FileAuthor(readMetadata(json)); }
    catch (const InvalidField& invalid) { finishError(error, invalid); return std::nullopt; }
}

const AuthorMetadata& FileAuthor::metadata() const noexcept { return m_metadata; }
QJsonObject FileAuthor::toIisaccProfileUpdate() const
{
    return {{"displayName", m_metadata.account.displayName}, {"authorDetails", detailsJson(m_metadata.details)}};
}
bool FileAuthor::setMetadata(const AuthorMetadata& metadata, QString* error)
{
    if (error) error->clear();
    try {
        auto replacement = readMetadata(metadataJson(metadata));
        const bool changedBinding = replacement.account.sub != m_metadata.account.sub
            || replacement.serviceOrigin != m_metadata.serviceOrigin || replacement.device != m_metadata.device;
        m_metadata = std::move(replacement);
        if (changedBinding) { m_token.reset(); m_session.reset(); }
        return true;
    } catch (const InvalidField& invalid) { finishError(error, invalid); return false; }
}
QString FileAuthor::displayLabel() const
{
    if (!m_metadata.account.displayName.isEmpty()) return m_metadata.account.displayName;
    if (!m_metadata.account.userId.isEmpty()) return m_metadata.account.userId;
    return m_metadata.account.email;
}
QJsonObject FileAuthor::toJson() const { return metadataJson(m_metadata); }
const std::optional<AuthorLoginSession>& FileAuthor::loginSession() const noexcept { return m_session; }
const std::optional<AuthenticationToken>& FileAuthor::authenticationToken() const noexcept { return m_token; }
bool FileAuthor::setAuthenticationToken(AuthenticationToken token, QString* error)
{
    if (error) error->clear();
    const auto& info = token.info();
    if (info.subject != m_metadata.account.sub || info.serviceOrigin != m_metadata.serviceOrigin
        || (m_session && info.sessionId != m_session->id)) {
        if (error) *error = "Invalid token.binding.";
        return false;
    }
    m_token = std::move(token);
    return true;
}
void FileAuthor::clearAuthenticationToken() { m_token.reset(); }
} // namespace iiFileProvider
