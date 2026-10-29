#include "Authorship.h"
#include "JsonContract.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QJsonDocument>
#include <QtCore/QSet>
#include <limits>
#include <stdexcept>

namespace iiFileProvider {
namespace {
QString keyFor(const FileAuthor& author)
{
    const auto& metadata = author.metadata();
    return QString::fromLatin1(QCryptographicHash::hash(
        metadata.serviceOrigin.toEncoded() + '\n' + metadata.account.sub.toUtf8(),
        QCryptographicHash::Sha256).toHex());
}
QJsonArray linkRecords(const QList<FileLink>& links)
{
    if (links.size() > Authorship::MaximumLinks) throw std::length_error("Too many file links");
    QJsonArray result;
    for (const auto& link : links) {
        const auto validated = FileLink::create(link.name(), link.urlText());
        if (!validated) throw std::invalid_argument("Invalid file link");
        const auto record = validated->toJson();
        if (result.contains(record)) throw std::invalid_argument("Duplicate file link");
        result.append(record);
    }
    return result;
}
}
Authorship::Authorship() { rebuildDump(); }
quint64 Authorship::revision() const noexcept { return m_revision; }
bool Authorship::isEmpty() const noexcept { return m_revision == 0 && m_authors.isEmpty(); }
void Authorship::clearActiveAuthor() noexcept { m_activeAuthor.clear(); }
bool Authorship::hasActiveAuthor() const noexcept { return !m_activeAuthor.isEmpty(); }
std::optional<FileAuthor> Authorship::firstEditor() const
{
    if (m_authors.isEmpty()) return std::nullopt;
    return FileAuthor::fromJson(m_authors.first().toObject()["author"].toObject());
}
QList<FileAuthor> Authorship::participants() const
{
    QList<FileAuthor> result;
    for (qsizetype index = 1; index < m_authors.size(); ++index)
        result.append(FileAuthor::fromJson(m_authors[index].toObject()["author"].toObject()).value());
    return result;
}
QList<FileLink> Authorship::links() const
{
    QList<FileLink> result;
    for (const auto& item : m_links)
        result.append(FileLink::fromJson(item.toObject()).value());
    return result;
}
bool Authorship::setLinks(const QList<FileLink>& links, const QDateTime& at)
{
    const auto records = linkRecords(links);
    if (records == m_links) return false;
    auto next = *this;
    next.m_links = records;
    next.recordChange(at);
    *this = std::move(next);
    return true;
}
bool Authorship::setLinksFromStrings(const QStringList& entries, const QDateTime& at)
{
    if (entries.size() > MaximumLinks) throw std::length_error("Too many file links");
    QList<FileLink> links;
    for (const auto& entry : entries) {
        auto link = FileLink::fromString(entry);
        if (!link) throw std::invalid_argument("Invalid named URL format");
        links.append(std::move(*link));
    }
    return setLinks(links, at);
}
QByteArray Authorship::dump() const { return m_dump; }
QJsonObject Authorship::toJson() const
{
    // Derive both roles from the one append-only roster so they cannot drift apart.
    const auto first = m_authors.isEmpty() ? QJsonValue(QJsonValue::Null)
        : m_authors.first().toObject()["key"];
    QJsonArray participants;
    for (qsizetype index = 1; index < m_authors.size(); ++index)
        participants.append(m_authors[index].toObject()["key"]);
    return {{"schemaVersion", SchemaVersion}, {"revision", QString::number(m_revision)},
        {"modifiedAt", detail::dateJson(m_modifiedAt)}, {"authors", m_authors},
        {"firstEditor", first}, {"participants", participants}, {"links", m_links},
        {"lastAuthor", m_lastAuthor.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(m_lastAuthor)}};
}
void Authorship::rebuildDump()
{
    auto serialized = QJsonDocument(toJson()).toJson(QJsonDocument::Compact);
    if (serialized.size() > MaximumBytes) throw std::length_error("Authorship metadata exceeds the size limit");
    m_dump = std::move(serialized);
}
bool Authorship::setAuthor(const FileAuthor& author, const QDateTime& at)
{
    return setAuthor(author, links(), at);
}
bool Authorship::setAuthor(const FileAuthor& author, const QList<FileLink>& links, const QDateTime& at)
{
    const auto records = linkRecords(links);
    const auto key = keyFor(author);
    const auto profile = author.toJson();
    auto next = *this;
    next.m_activeAuthor = key;
    next.m_links = records;
    for (qsizetype index = 0; index < next.m_authors.size(); ++index) {
        auto record = next.m_authors[index].toObject();
        if (record["key"].toString() != key) continue;
        if (record["author"].toObject() == profile && m_links == records) {
            m_activeAuthor = key;
            return false;
        }
        record["author"] = profile;
        next.m_authors[index] = record;
        next.recordChange(at);
        *this = std::move(next);
        return true;
    }
    if (next.m_authors.size() >= 256) throw std::length_error("Authorship exceeds 256 authors");
    next.m_authors.append(QJsonObject{{"key", key}, {"author", profile}});
    next.recordChange(at);
    *this = std::move(next);
    return true;
}
void Authorship::recordChange(const QDateTime& at)
{
    if (!at.isValid()) throw std::invalid_argument("Invalid authorship change time");
    if (m_revision == std::numeric_limits<quint64>::max())
        throw std::overflow_error("Authorship revision is exhausted");
    auto next = *this;
    ++next.m_revision;
    next.m_modifiedAt = m_modifiedAt.isValid() && at < m_modifiedAt ? m_modifiedAt : at.toUTC();
    next.m_lastAuthor = m_activeAuthor;
    // Contribution time belongs to this ledger, not a guessed source-file creation time.
    for (qsizetype index = 0; index < next.m_authors.size(); ++index) {
        auto record = next.m_authors[index].toObject();
        if (record["key"].toString() != m_activeAuthor) continue;
        if (!record.contains("firstChangedAt")) record["firstChangedAt"] = detail::dateJson(next.m_modifiedAt);
        record["lastChangedAt"] = detail::dateJson(next.m_modifiedAt);
        next.m_authors[index] = record;
        break;
    }
    next.rebuildDump();
    *this = std::move(next);
}
std::optional<Authorship> Authorship::fromDump(const QByteArray& bytes, QString* error)
{
    if (error) error->clear();
    if (bytes.size() > MaximumBytes) { if (error) *error = "Invalid authorship size."; return std::nullopt; }
    QJsonParseError parseError;
    const auto parsed = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
        if (error) *error = "Invalid authorship JSON.";
        return std::nullopt;
    }
    return fromJson(parsed.object(), error);
}
std::optional<Authorship> Authorship::fromJson(const QJsonObject& json, QString* error)
{
    if (error) error->clear();
    try {
        using namespace detail;
        const auto byteSize = QJsonDocument(json).toJson(QJsonDocument::Compact).size();
        require(byteSize <= MaximumBytes, "authorship.size");
        const auto schema = json["schemaVersion"];
        require(schema.isDouble() && (schema.toDouble() == 1 || schema.toDouble() == 2
            || schema.toDouble() == SchemaVersion),
            "authorship.schemaVersion");
        if (schema.toInt() == 1) require(byteSize <= 1024 * 1024, "authorship.size");
        if (schema.toInt() == 2) require(byteSize <= 1024 * 1024 + 32 * 1024, "authorship.size");
        auto fields = QStringList{"schemaVersion", "revision", "modifiedAt", "authors", "lastAuthor"};
        if (schema.toInt() >= 2) fields.append({"firstEditor", "participants"});
        if (schema.toInt() == SchemaVersion) fields.append("links");
        knownKeys(json, fields, "authorship.fields");
        const auto revision = text(json, "revision", 20, true);
        require(matches(revision, "\\A(?:0|[1-9][0-9]*)\\z"), "authorship.revision");
        Authorship result;
        bool valid = false;
        result.m_revision = revision.toULongLong(&valid);
        require(valid, "authorship.revision");
        result.m_modifiedAt = timestamp(json, "modifiedAt", result.m_revision != 0);
        require(json["authors"].isArray() && json["authors"].toArray().size() <= 256, "authorship.authors");
        QSet<QString> keys;
        for (const auto& item : json["authors"].toArray()) {
            require(item.isObject(), "authorship.author");
            auto record = item.toObject();
            knownKeys(record, {"key", "author", "firstChangedAt", "lastChangedAt"}, "authorship.author.fields");
            const auto author = FileAuthor::fromJson(object(record, "author"));
            require(author.has_value(), "authorship.author");
            const auto key = text(record, "key", 64, true);
            require(key == keyFor(*author) && !keys.contains(key), "authorship.author.key");
            keys.insert(key);
            const auto first = timestamp(record, "firstChangedAt", true);
            const auto last = timestamp(record, "lastChangedAt", true);
            require(first <= last && last <= result.m_modifiedAt, "authorship.author.timestamps");
            record["author"] = author->toJson();
            record["firstChangedAt"] = dateJson(first);
            record["lastChangedAt"] = dateJson(last);
            result.m_authors.append(record);
        }
        const auto last = json["lastAuthor"];
        require(last.isNull() || last.isString(), "authorship.lastAuthor");
        if (last.isString()) {
            result.m_lastAuthor = last.toString();
            require(keys.contains(result.m_lastAuthor), "authorship.lastAuthor");
        }
        if (schema.toInt() == SchemaVersion) {
            require(json["links"].isArray() && json["links"].toArray().size() <= MaximumLinks,
                "authorship.links");
            for (const auto& item : json["links"].toArray()) {
                require(item.isObject(), "authorship.link");
                const auto link = FileLink::fromJson(item.toObject());
                require(link.has_value(), "authorship.link");
                const auto record = link->toJson();
                require(!result.m_links.contains(record), "authorship.link.duplicate");
                result.m_links.append(record);
            }
        }
        require(result.m_revision != 0 || (result.m_authors.isEmpty() && result.m_links.isEmpty()
            && !result.m_modifiedAt.isValid()), "authorship.empty");
        if (schema.toInt() >= 2) {
            const auto canonical = result.toJson();
            require(json["firstEditor"] == canonical["firstEditor"], "authorship.firstEditor");
            require(json["participants"].isArray() && json["participants"] == canonical["participants"],
                "authorship.participants");
        }
        // Version 1 already stored authors in first-registration order. Preserve that
        // order and every profile/timestamp; do not infer roles by sorting wall-clock times.
        result.rebuildDump();
        return result;
    } catch (const detail::InvalidField& invalid) {
        detail::finishError(error, invalid);
        return std::nullopt;
    } catch (const std::length_error&) {
        if (error) *error = "Invalid authorship size.";
        return std::nullopt;
    }
}
} // namespace iiFileProvider
