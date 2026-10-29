#pragma once

#include "FileAuthor.h"
#include "FileLink.h"
#include <QtCore/QJsonArray>

namespace iiFileProvider {

// Copyable file metadata. Runtime credentials are not copied; explicit link URLs are persisted.
class IIFILEPROVIDER_EXPORT Authorship final {
public:
    static constexpr int SchemaVersion = 3;
    static constexpr qsizetype MaximumBytes = 2 * 1024 * 1024;
    static constexpr qsizetype MaximumLinks = 64;
    static constexpr const char* MetadataKey = "iisacc:authorship";
    Authorship();
    [[nodiscard]] static std::optional<Authorship> fromJson(const QJsonObject&, QString* error = nullptr);
    [[nodiscard]] static std::optional<Authorship> fromDump(const QByteArray&, QString* error = nullptr);
    // Returns false only for an identical profile; still selects that author for future changes.
    // Stored profiles are sanitized through FileAuthor::toJson(). Failure throws before mutation.
    // The first identity remains the first editor; later identities are appended once.
    // Profile refreshes never remove, replace or reorder the recorded identities.
    bool setAuthor(const FileAuthor&, const QDateTime& at = QDateTime::currentDateTimeUtc());
    // Explicit links replace the file-level list in the same atomic change/revision.
    // The original overload preserves links. An explicitly empty list clears only links.
    bool setAuthor(const FileAuthor&, const QList<FileLink>& links,
        const QDateTime& at = QDateTime::currentDateTimeUtc());
    [[nodiscard]] QList<FileLink> links() const;
    bool setLinks(const QList<FileLink>&, const QDateTime& at = QDateTime::currentDateTimeUtc());
    bool setLinksFromStrings(const QStringList&, const QDateTime& at = QDateTime::currentDateTimeUtc());
    // Only clears runtime editing context, never the permanent editor roster.
    void clearActiveAuthor() noexcept;
    [[nodiscard]] bool hasActiveAuthor() const noexcept;
    // Credential-free snapshots. Editing these copies cannot change the stored roster.
    [[nodiscard]] std::optional<FileAuthor> firstEditor() const;
    [[nodiscard]] QList<FileAuthor> participants() const; // Excludes the first editor, in registration order.
    // Call synchronously at a successful mutation boundary, never for a rejected edit/no-op.
    void recordChange(const QDateTime& at = QDateTime::currentDateTimeUtc());
    [[nodiscard]] quint64 revision() const noexcept;
    [[nodiscard]] bool isEmpty() const noexcept;
    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] QByteArray dump() const; // Already serialized by the last mutation.
private:
    quint64 m_revision = 0;
    QDateTime m_modifiedAt;
    QJsonArray m_authors; // Append-only identities: first editor, then distinct participants.
    QJsonArray m_links; // Optional, independently replaceable file-level references.
    QString m_lastAuthor;
    QString m_activeAuthor; // Local editing context, never restored from a file.
    QByteArray m_dump;
    void rebuildDump();
};
} // namespace iiFileProvider
