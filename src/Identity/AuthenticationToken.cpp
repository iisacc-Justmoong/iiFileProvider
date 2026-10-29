#include "AuthenticationToken.h"
#include "JsonContract.h"

#include <utility>

namespace iiFileProvider {
namespace {
QString kindName(AuthenticationTokenKind kind)
{
    switch (kind) {
    case AuthenticationTokenKind::IisaccSession: return "iisaccSession";
    case AuthenticationTokenKind::CognitoId: return "cognitoId";
    case AuthenticationTokenKind::CognitoAccess: return "cognitoAccess";
    case AuthenticationTokenKind::CognitoRefresh: return "cognitoRefresh";
    }
    throw detail::InvalidField{"token.kind"};
}
}

AuthenticationToken::AuthenticationToken(AuthenticationTokenInfo info, QByteArray secret)
    : m_info(std::move(info)), m_secret(std::move(secret)) {}

std::optional<AuthenticationToken> AuthenticationToken::create(
    AuthenticationTokenInfo info, QByteArray secret, QString* error)
{
    if (error) error->clear();
    try {
        using namespace detail;
        (void)kindName(info.kind);
        require(matches(info.subject, "\\A[A-Za-z0-9_-]{1,128}\\z"), "token.subject");
        info.serviceOrigin = serviceOrigin(info.serviceOrigin);
        require(!secret.isEmpty() && secret.size() <= 16384, "token.secret");
        for (unsigned char byte : secret)
            require(byte >= 33 && byte <= 126, "token.secret");
        if (info.kind == AuthenticationTokenKind::IisaccSession) {
            require(matches(QString::fromLatin1(secret), "\\A[A-Za-z0-9_-]{43}\\z"), "token.secret");
            require(!info.sessionId.isEmpty(), "token.sessionId");
        }
        require(info.sessionId.isEmpty() || matches(info.sessionId, "\\A[a-f0-9]{32}\\z"), "token.sessionId");
        info.issuer = cleanText(info.issuer, "token.issuer", 2048);
        info.audience = cleanText(info.audience, "token.audience", 256);
        require(info.scopes.size() <= 64, "token.scopes");
        QStringList scopes;
        for (const auto& scope : info.scopes) {
            const auto value = cleanText(scope, "token.scopes", 128, true);
            require(!value.contains(QRegularExpression("\\s")) && !scopes.contains(value), "token.scopes");
            scopes.append(value);
        }
        info.scopes = scopes;
        require(info.issuedAt.isValid() && info.expiresAt.isValid()
            && info.issuedAt < info.expiresAt, "token.validity");
        require(!info.notBefore.isValid() || (info.notBefore >= info.issuedAt
            && info.notBefore < info.expiresAt), "token.notBefore");
        info.issuedAt = info.issuedAt.toUTC();
        info.expiresAt = info.expiresAt.toUTC();
        if (info.notBefore.isValid()) info.notBefore = info.notBefore.toUTC();
        return AuthenticationToken(std::move(info), std::move(secret));
    } catch (const detail::InvalidField& invalid) {
        detail::finishError(error, invalid);
        return std::nullopt;
    }
}

const AuthenticationTokenInfo& AuthenticationToken::info() const noexcept { return m_info; }
QByteArray AuthenticationToken::secret() const { return m_secret; }
bool AuthenticationToken::isWithinValidityWindow(const QDateTime& at) const
{
    return at.isValid() && at >= m_info.issuedAt && at < m_info.expiresAt
        && (!m_info.notBefore.isValid() || at >= m_info.notBefore);
}
QJsonObject AuthenticationToken::toRedactedJson() const
{
    return {{"kind", kindName(m_info.kind)}, {"subject", m_info.subject},
        {"serviceOrigin", m_info.serviceOrigin.toString()}, {"sessionId", m_info.sessionId},
        {"issuer", m_info.issuer}, {"audience", m_info.audience},
        {"scopes", QJsonArray::fromStringList(m_info.scopes)},
        {"issuedAt", detail::dateJson(m_info.issuedAt)},
        {"notBefore", detail::dateJson(m_info.notBefore)},
        {"expiresAt", detail::dateJson(m_info.expiresAt)}, {"secret", "[REDACTED]"}};
}
QDebug operator<<(QDebug debug, const AuthenticationToken& token)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "AuthenticationToken(kind=" << kindName(token.info().kind)
        << ", secret=[REDACTED])";
    return debug;
}
} // namespace iiFileProvider
