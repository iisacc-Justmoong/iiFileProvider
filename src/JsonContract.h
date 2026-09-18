#pragma once

// Private parsing helpers shared by the author and credential implementations.
#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>

namespace iiFileProvider::detail {

struct InvalidField { QString field; };
inline void require(bool valid, const QString& field)
{
    if (!valid) throw InvalidField{field};
}
inline bool matches(const QString& text, const char* pattern)
{
    return QRegularExpression(QString::fromLatin1(pattern)).match(text).hasMatch();
}
inline QString cleanText(QString text, const QString& field, qsizetype maximum,
                         bool required = false, bool multiline = false)
{
    require(text.isValidUtf16(), field);
    text = text.normalized(QString::NormalizationForm_C).trimmed();
    require((!required || !text.isEmpty()) && text.toUcs4().size() <= maximum, field);
    for (char32_t code : text.toUcs4()) {
        const auto category = QChar::category(code);
        require((multiline && code == '\n') || (category != QChar::Other_Control
            && category != QChar::Separator_Line && category != QChar::Separator_Paragraph), field);
    }
    return text;
}
inline QString text(const QJsonObject& object, const QString& key, qsizetype maximum,
                    bool required = false, bool multiline = false)
{
    const auto value = object.value(key);
    if (value.isUndefined() && !required) return {};
    require(value.isString(), key);
    return cleanText(value.toString(), key, maximum, required, multiline);
}
inline QJsonObject object(const QJsonObject& parent, const QString& key, bool required = true)
{
    const auto value = parent.value(key);
    if (value.isUndefined() && !required) return {};
    require(value.isObject(), key);
    return value.toObject();
}
inline void knownKeys(const QJsonObject& value, const QStringList& keys, const QString& field)
{
    for (auto it = value.begin(); it != value.end(); ++it)
        require(keys.contains(it.key()), field); // Do not reflect unknown input keys in errors.
}
inline QDateTime timestamp(const QJsonObject& object, const QString& key, bool required = false)
{
    const auto value = object.value(key);
    if ((value.isNull() || value.isUndefined()) && !required) return {};
    require(value.isString(), key);
    const auto raw = value.toString();
    require(matches(raw, "\\A[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(?:\\.[0-9]{1,3})?(?:Z|[+-][0-9]{2}:[0-9]{2})\\z"), key);
    auto parsed = QDateTime::fromString(raw, Qt::ISODateWithMs);
    require(parsed.isValid(), key);
    return parsed.toUTC();
}
inline QJsonValue dateJson(const QDateTime& date)
{
    return date.isValid() ? QJsonValue(date.toUTC().toString(Qt::ISODateWithMs)) : QJsonValue(QJsonValue::Null);
}
inline QUrl httpsUrl(const QString& raw, const QString& field, bool required = false)
{
    if (raw.isEmpty() && !required) return {};
    QUrl url(raw, QUrl::StrictMode);
    require(raw.size() <= 2048 && url.isValid() && url.scheme() == "https"
        && !url.host().isEmpty() && url.userInfo().isEmpty(), field);
    return url;
}
inline QUrl serviceOrigin(const QUrl& value)
{
    auto url = httpsUrl(value.toString(QUrl::FullyEncoded), "serviceOrigin", true);
    require((url.path().isEmpty() || url.path() == "/") && !url.hasQuery() && !url.hasFragment(), "serviceOrigin");
    url.setPath({});
    if (url.port() == 443) url.setPort(-1);
    return url;
}
inline QString email(QString value, const QString& field)
{
    value = value.normalized(QString::NormalizationForm_KC).toLower();
    require(value.size() >= 3 && value.size() <= 254
        && matches(value, "\\A[^\\s@]+@[^\\s@]+\\.[^\\s@]+\\z")
        && value.section('@', 0, 0).size() <= 64, field);
    return value;
}
inline void finishError(QString* error, const InvalidField& invalid)
{
    if (error) *error = "Invalid " + invalid.field + ".";
}
} // namespace iiFileProvider::detail
