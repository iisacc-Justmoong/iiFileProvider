#include "FileLink.h"
#include "JsonContract.h"

namespace iiFileProvider {
namespace {
QUrl readUrl(const QString& raw)
{
    detail::require(raw.isValidUtf16() && !raw.isEmpty()
        && raw.size() <= FileLink::MaximumUrlBytes
        && raw.toUtf8().size() <= FileLink::MaximumUrlBytes, "fileLink.url");
    // Do not normalize Unicode, resolve relative URLs or guess an HTTP/file scheme.
    for (const auto code : raw.toUcs4()) {
        const auto category = QChar::category(code);
        detail::require(category != QChar::Other_Control && category != QChar::Separator_Line
            && category != QChar::Separator_Paragraph, "fileLink.url");
    }
    const QUrl url(raw, QUrl::StrictMode);
    detail::require(url.isValid() && !url.isEmpty()
        && url.toEncoded().size() <= FileLink::MaximumUrlBytes, "fileLink.url");
    return url;
}
}

FileLink::FileLink(QString name, QString url) : m_name(std::move(name)), m_urlText(std::move(url)) {}

std::optional<FileLink> FileLink::create(const QString& name, const QUrl& url, QString* error)
{
    if (!url.isValid() || url.isEmpty()) {
        if (error) *error = "Invalid fileLink.url.";
        return std::nullopt;
    }
    return create(name, url.toString(QUrl::FullyEncoded), error);
}

std::optional<FileLink> FileLink::create(const QString& name, const QString& url, QString* error)
{
    if (error) error->clear();
    try {
        const auto cleanName = detail::cleanText(name, "fileLink.name", MaximumNameLength, true);
        detail::require(!cleanName.contains('|'), "fileLink.name");
        (void)readUrl(url);
        // Custom authority IDs and signed URLs can be case/encoding sensitive.
        // Validation must never replace their supplied text with QUrl's normalized form.
        return FileLink(cleanName, url);
    } catch (const detail::InvalidField& invalid) {
        detail::finishError(error, invalid);
        return std::nullopt;
    }
}

std::optional<FileLink> FileLink::fromString(const QString& namedUrl, QString* error)
{
    if (error) error->clear();
    try {
        const auto input = namedUrl.trimmed();
        detail::require(input.startsWith('[') && input.endsWith(']'), "fileLink.format");
        const auto separator = input.indexOf('|');
        detail::require(separator > 1 && separator < input.size() - 2, "fileLink.format");
        const auto name = input.mid(1, separator - 1);
        const auto url = input.mid(separator + 1, input.size() - separator - 2);
        return create(name, url, error);
    } catch (const detail::InvalidField& invalid) {
        detail::finishError(error, invalid);
        return std::nullopt;
    }
}

std::optional<FileLink> FileLink::fromJson(const QJsonObject& json, QString* error)
{
    if (error) error->clear();
    try {
        detail::knownKeys(json, {"name", "url"}, "fileLink.fields");
        detail::require(json["name"].isString() && json["url"].isString(), "fileLink.fields");
        return create(json["name"].toString(), json["url"].toString(), error);
    } catch (const detail::InvalidField& invalid) {
        detail::finishError(error, invalid);
        return std::nullopt;
    }
}

const QString& FileLink::name() const noexcept { return m_name; }
const QString& FileLink::urlText() const noexcept { return m_urlText; }
QUrl FileLink::url() const { return QUrl(m_urlText, QUrl::StrictMode); }
QString FileLink::toString() const { return '[' + m_name + '|' + m_urlText + ']'; }
QJsonObject FileLink::toJson() const { return {{"name", m_name}, {"url", m_urlText}}; }

} // namespace iiFileProvider
