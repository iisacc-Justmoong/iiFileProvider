#pragma once

#include "Export.h"
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <optional>

namespace iiFileProvider {

// A named file-level URL reference. No scheme allowlist, resolution or execution.
class IIFILEPROVIDER_EXPORT FileLink final {
public:
    static constexpr qsizetype MaximumNameLength = 160; // Unicode code points.
    static constexpr qsizetype MaximumUrlBytes = 16 * 1024; // Fully encoded URL.

    [[nodiscard]] static std::optional<FileLink> create(
        const QString& name, const QUrl& url, QString* error = nullptr);
    [[nodiscard]] static std::optional<FileLink> create(
        const QString& name, const QString& url, QString* error = nullptr);
    [[nodiscard]] static std::optional<FileLink> fromString(
        const QString& namedUrl, QString* error = nullptr); // [name|URL]
    [[nodiscard]] static std::optional<FileLink> fromJson(
        const QJsonObject&, QString* error = nullptr);
    [[nodiscard]] const QString& name() const noexcept;
    [[nodiscard]] const QString& urlText() const noexcept; // Exact text supplied by string/JSON input.
    [[nodiscard]] QUrl url() const; // Parsed convenience view; QUrl may normalize this view.
    [[nodiscard]] QString toString() const;
    [[nodiscard]] QJsonObject toJson() const;
    bool operator==(const FileLink&) const = default;

private:
    FileLink(QString name, QString url);
    QString m_name;
    QString m_urlText;
};

} // namespace iiFileProvider
