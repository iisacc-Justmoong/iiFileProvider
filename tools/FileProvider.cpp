#include <FileCatalog.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <set>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace {
using namespace iiFileProvider;
using Json = nlohmann::json;
constexpr std::array<const char *, 8> colors{"none", "red", "orange", "yellow", "green", "blue", "purple", "gray"};
std::uint64_t number(const std::string &input) {
    std::uint64_t value{}; const auto parsed = std::from_chars(input.data(), input.data() + input.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != input.data() + input.size()) throw std::runtime_error("Expected an unsigned integer");
    return value;
}
std::uint64_t jsonNumber(const Json &value, std::uint64_t maximum = INT64_MAX) {
    if ((!value.is_number_unsigned() && !value.is_number_integer()) || (value.is_number_integer() && !value.is_number_unsigned() && value.get<std::int64_t>() < 0))
        throw std::runtime_error("Expected a nonnegative JSON integer");
    const auto result = value.get<std::uint64_t>();
    if (result > maximum) throw std::runtime_error("JSON integer exceeds limit"); return result;
}
void keys(const Json &value, const std::set<std::string> &allowed) {
    if (!value.is_object()) throw std::runtime_error("Expected a JSON object");
    for (const auto &[key, item] : value.items()) if (!allowed.contains(key)) throw std::runtime_error("Unknown request field: " + key);
}
ColorLabel color(const std::string &name) {
    const auto at = std::find(colors.begin(), colors.end(), name);
    if (at == colors.end()) throw std::runtime_error("Unknown color label"); return ColorLabel(at - colors.begin());
}
Json request() {
    std::string text; std::array<char, 4096> buffer;
    while (std::cin) {
        std::cin.read(buffer.data(), buffer.size()); const auto n = std::cin.gcount();
        // A continuation contains both the filter and its serialized cursor.
        // Each annotation/filter is separately checked by the catalog.
        if (text.size() + std::size_t(n) > 4 * FileCatalog::maximumMetadataBytes) throw std::runtime_error("Request exceeds one MiB");
        text.append(buffer.data(), std::size_t(n));
    }
    if (!std::cin.eof()) throw std::runtime_error("Cannot read request");
    return text.empty() ? Json::object() : Json::parse(text);
}
FileMetadata annotation(const Json &j) {
    keys(j, {"title", "description", "keywords", "rating", "color", "fields", "collections"});
    FileMetadata m; m.title = j.value("title", std::string{}); m.description = j.value("description", std::string{});
    m.keywords = j.value("keywords", std::vector<std::string>{});
    if (j.contains("rating")) m.rating = static_cast<unsigned>(jsonNumber(j.at("rating"), 5));
    m.color = color(j.value("color", std::string("none"))); m.fields = j.value("fields", std::map<std::string, std::string>{});
    m.collections = j.value("collections", std::vector<std::string>{}); return m;
}
FileFilter filter(const Json &j) {
    keys(j, {"text", "pathPrefix", "allKeywords", "anyKeywords", "colors", "collections", "fields", "minimumRating", "maximumRating", "minimumSize", "maximumSize"});
    FileFilter f; f.text = j.value("text", std::string{}); f.pathPrefix = j.value("pathPrefix", std::string{});
    f.allKeywords = j.value("allKeywords", std::vector<std::string>{}); f.anyKeywords = j.value("anyKeywords", std::vector<std::string>{});
    for (const auto &c : j.value("colors", std::vector<std::string>{})) f.colors.push_back(color(c));
    f.collections = j.value("collections", std::vector<std::string>{}); f.fields = j.value("fields", std::map<std::string, std::string>{});
    if (j.contains("minimumRating")) f.minimumRating = static_cast<unsigned>(jsonNumber(j.at("minimumRating"), 5));
    if (j.contains("maximumRating")) f.maximumRating = static_cast<unsigned>(jsonNumber(j.at("maximumRating"), 5));
    if (j.contains("minimumSize")) f.minimumSize = jsonNumber(j.at("minimumSize"));
    if (j.contains("maximumSize")) f.maximumSize = jsonNumber(j.at("maximumSize")); return f;
}
Json summary(const CatalogFileSummary &f) {
    return {{"key", f.key}, {"index", f.index}, {"revision", f.revision}, {"path", f.path}, {"size", f.size},
        {"modifiedAtNs", f.modifiedAtNs}, {"contentVersion", f.contentVersion}, {"title", f.title},
        {"rating", f.rating}, {"color", colors[static_cast<unsigned>(f.color)]}, {"metadataPending", f.metadataPending}};
}
Json page(const FilePage &p) {
    Json j{{"files", Json::array()}, {"next", nullptr}};
    for (const auto &file : p.files) j["files"].push_back(summary(file));
    if (p.next) j["next"] = {{"catalog", p.next->catalog}, {"revision", p.next->revision}, {"afterIndex", p.next->afterIndex}, {"filter", p.next->filter}};
    return j;
}
Json written(FileCatalog &files, const MetadataWrite &result) {
    auto j = Json::parse(files.metadataDocument(result.file.key)); j["metadataWritten"] = result.metadataWritten;
    j["metadataError"] = result.metadataError; j["size"] = result.file.size; j["contentVersion"] = result.file.contentVersion; return j;
}
int run(const std::vector<std::string> &args) {
    if (args.size() < 3) throw std::runtime_error("Usage: ii-file-provider ROOT init|create|import|annotate|metadata|presentation|query|collection|collections|save-filter|search-filter|recover|read ...");
    const auto &command = args[2];
    const std::map<std::string, std::size_t> arity{{"init",3},{"create",4},{"import",5},{"annotate",5},{"metadata",4},{"presentation",4},{"query",3},{"collection",4},{"collections",3},{"save-filter",4},{"search-filter",4},{"recover",3},{"read",6}};
    if (!arity.contains(command) || args.size() != arity.at(command)) throw std::runtime_error("Unknown command or argument count");
    const bool readOnly = command == "metadata" || command == "presentation" || command == "query" || command == "collections" || command == "search-filter" || command == "read";
    FileCatalog files(std::filesystem::path(std::u8string(args[1].begin(), args[1].end())), readOnly ? FileCatalog::Access::ReadOnly : FileCatalog::Access::Create);
    Json output;
    if (command == "init") output = {{"files", files.count()}, {"pendingMetadata", files.pendingMetadata()}};
    else if (command == "create") {
        // Stream input directly into provider-owned staging, without materializing
        // an entire generated file or introducing a script runtime.
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        const auto created = files.createFileWith(args[3], [&](std::ostream &out) {
            std::vector<char> block(FileCatalog::maximumChunk);
            while (std::cin) { std::cin.read(block.data(), static_cast<std::streamsize>(block.size())); const auto n = std::cin.gcount(); if (n) out.write(block.data(), n); if (!out) throw std::runtime_error("Cannot write generated data"); }
            if (!std::cin.eof()) throw std::runtime_error("Cannot read generated data");
        }); output = written(files, created);
    }
    else if (command == "import") output = written(files, files.importFile(std::filesystem::path(std::u8string(args[3].begin(), args[3].end())), args[4]));
    else if (command == "annotate") output = written(files, files.updateMetadata(args[3], number(args[4]), annotation(request())));
    else if (command == "metadata") output = Json::parse(files.metadataDocument(args[3]));
    else if (command == "presentation") { const auto document = files.presentationDocument(args[3]); output = document.empty() ? Json(nullptr) : Json::parse(document); }
    else if (command == "query") {
        const auto j = request(); keys(j, {"filter", "cursor", "limit"}); std::optional<FileCursor> cursor;
        if (j.contains("cursor") && !j.at("cursor").is_null()) { const auto &c = j.at("cursor"); keys(c, {"catalog", "revision", "afterIndex", "filter"}); cursor = FileCursor{c.at("catalog"), jsonNumber(c.at("revision")), static_cast<std::int64_t>(jsonNumber(c.at("afterIndex"))), c.at("filter")}; }
        const auto limit = j.contains("limit") ? jsonNumber(j.at("limit"), FileCatalog::maximumPage) : 256;
        output = page(files.search(filter(j.value("filter", Json::object())), cursor, std::size_t(limit)));
    }
    else if (command == "collection") { const auto c = files.createCollection(args[3]); output = {{"key", c.key}, {"name", c.name}}; }
    else if (command == "collections") { output = Json::array(); for (const auto &c : files.collections()) output.push_back({{"key", c.key}, {"name", c.name}}); }
    else if (command == "save-filter") { const auto saved = files.saveFilter(args[3], filter(request())); output = {{"key", saved.key}, {"name", saved.name}}; }
    else if (command == "search-filter") output = page(files.searchSaved(args[3]));
    else if (command == "recover") { const auto n = files.publishMetadata(); output = {{"published", n}, {"pendingMetadata", files.pendingMetadata()}}; }
    else if (command == "read") {
        const auto length = number(args[5]); if (!length || length > FileCatalog::maximumChunk) throw std::runtime_error("Read exceeds chunk limit");
        const auto chunk = files.read(args[3], number(args[4]), std::size_t(length));
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        std::cout.write(reinterpret_cast<const char *>(chunk.bytes.data()), static_cast<std::streamsize>(chunk.bytes.size())); return std::cout ? 0 : 1;
    }
    std::cout << output.dump() << '\n'; return std::cout ? 0 : 1;
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) { const auto n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr); if (!n) return 2; std::string text(std::size_t(n), '\0'); WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, text.data(), n, nullptr, nullptr); text.pop_back(); args.push_back(std::move(text)); }
#else
int main(int argc, char **argv) {
    std::vector<std::string> args(argv, argv + argc);
#endif
    try { return run(args); }
    catch (const std::exception &error) { std::cerr << Json{{"error", error.what()}}.dump() << '\n'; return 1; }
}
