#pragma once

#include "Export.h"

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QJsonObject>
#include <QtCore/QStringList>
#include <QtCore/QUrl>

#include <optional>

namespace iiFileProvider {

enum class AuthenticationTokenKind { IisaccSession, CognitoId, CognitoAccess, CognitoRefresh };

struct AuthenticationTokenInfo
{
    AuthenticationTokenKind kind = AuthenticationTokenKind::IisaccSession;
    QString subject;
    QUrl serviceOrigin;
    QString sessionId;
    QString issuer;
    QString audience;
    QStringList scopes;
    QDateTime issuedAt;
    QDateTime notBefore;
    QDateTime expiresAt;
};

// An opaque runtime credential. Creation validates structure, not authenticity.
class IIFILEPROVIDER_EXPORT AuthenticationToken final
{
public:
    [[nodiscard]] static std::optional<AuthenticationToken> create(
        AuthenticationTokenInfo info, QByteArray secret, QString* error = nullptr);
    [[nodiscard]] const AuthenticationTokenInfo& info() const noexcept;
    // Explicit access for a host's authenticated transport; never called by JSON export.
    [[nodiscard]] QByteArray secret() const;
    [[nodiscard]] bool isWithinValidityWindow(const QDateTime& at) const;
    [[nodiscard]] QJsonObject toRedactedJson() const;

private:
    AuthenticationToken(AuthenticationTokenInfo info, QByteArray secret);
    AuthenticationTokenInfo m_info;
    QByteArray m_secret;
};

IIFILEPROVIDER_EXPORT QDebug operator<<(QDebug debug, const AuthenticationToken& token);

} // namespace iiFileProvider
