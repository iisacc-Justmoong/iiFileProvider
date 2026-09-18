#pragma once

#include "AuthenticationToken.h"

#include <QtCore/QList>

namespace iiFileProvider {

enum class SocietyCloudMembership { Free, Plus, Pro, Enterprise };

// Identity fields of iisacc.com's account snapshot. authorDetails maps separately
// to AuthorMetadata::details to keep the file schema unchanged. No authentication claim.
struct IisaccAccount
{
    QString sub;
    QString email;
    QString displayName;
    QString userId;
    SocietyCloudMembership societyCloudMembership = SocietyCloudMembership::Free;
    QUrl avatarUrl;
};

struct AuthorLink { QString relation; QString label; QUrl url; };
struct AuthorIdentifier { QString scheme; QString value; };

// Optional author-supplied information; never inferred from email, sub or displayName.
struct AuthorDetails
{
    QString fullName;
    QString givenName;
    QString additionalName;
    QString familyName;
    QString pseudonym;
    QString biography;
    QString organization;
    QString organizationId;
    QString department;
    QString team;
    QString jobTitle;
    QString contactEmail;
    QString phoneNumber;
    QString locale;
    QString timeZone;
    QString countryCode;
    QString region;
    QString city;
    QList<AuthorLink> links;
    QList<AuthorIdentifier> identifiers;
};

struct FileAttribution
{
    QStringList roles;
    QString credit;
    QString copyrightNotice;
    QString licenseIdentifier;
    QUrl licenseUrl;
    QString documentId;
    QString projectId;
    QString workspaceId;
    QDateTime createdAt;
    QDateTime modifiedAt;
};

struct AuthorDevice
{
    QString id;
    QString type;
    QString name;
    QString platform;
    QString osVersion;
    QString appId;
    QString appVersion;
    bool operator==(const AuthorDevice&) const = default;
};

struct AuthorLoginSession
{
    QString id;
    QString client;
    AuthorDevice device;
    QDateTime createdAt;
    QDateTime lastSeenAt;
    QDateTime expiresAt;
};

struct AuthorMetadata
{
    IisaccAccount account;
    AuthorDetails details;
    FileAttribution attribution;
    QUrl serviceOrigin;
    QDateTime capturedAt;
    std::optional<AuthorDevice> device;
};

// A file author's persistent metadata and separate, optional runtime credential.
// Parsing a local snapshot cannot prove identity, ownership, membership or login.
class IIFILEPROVIDER_EXPORT FileAuthor final
{
public:
    static constexpr int SchemaVersion = 1;

    [[nodiscard]] static std::optional<FileAuthor> fromIisaccAccount(
        const QJsonObject& account, const QUrl& serviceOrigin,
        const QDateTime& capturedAt, QString* error = nullptr);
    [[nodiscard]] static std::optional<FileAuthor> fromIisaccAppSession(
        const QJsonObject& response, const QUrl& serviceOrigin,
        const QDateTime& capturedAt, QString* error = nullptr);
    [[nodiscard]] static std::optional<FileAuthor> fromJson(
        const QJsonObject& metadata, QString* error = nullptr);

    [[nodiscard]] const AuthorMetadata& metadata() const noexcept;
    // Validated, atomic replacement. Changed identity/device clears runtime state.
    [[nodiscard]] bool setMetadata(const AuthorMetadata& metadata, QString* error = nullptr);
    [[nodiscard]] QString displayLabel() const;
    // Schema v1 contains no credentials, session registry IDs or authentication claims.
    [[nodiscard]] QJsonObject toJson() const;
    // Editable account fields only, for PATCH /Account/Profile/Author. This is an
    // explicit payload export; it does not authenticate, upload or alter any file.
    [[nodiscard]] QJsonObject toIisaccProfileUpdate() const;
    [[nodiscard]] const std::optional<AuthorLoginSession>& loginSession() const noexcept;
    [[nodiscard]] const std::optional<AuthenticationToken>& authenticationToken() const noexcept;
    [[nodiscard]] bool setAuthenticationToken(AuthenticationToken token, QString* error = nullptr);
    void clearAuthenticationToken();

private:
    explicit FileAuthor(AuthorMetadata metadata);
    AuthorMetadata m_metadata;
    std::optional<AuthorLoginSession> m_session;
    std::optional<AuthenticationToken> m_token;
};

} // namespace iiFileProvider
