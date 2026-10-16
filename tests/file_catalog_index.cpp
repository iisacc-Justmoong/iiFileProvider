#include <FileCatalog.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace iiFileProvider;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action, const char *message) {
    try { action(); } catch (const std::exception &) { return; }
    throw std::runtime_error(message);
}
int main() {
    const auto root = fs::path(CATALOG_TEST_DIRECTORY) / ("catalog-index-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        FileCatalog files(root);
        const auto campaign = files.createCollection("Campaign");
        fs::create_directory(root / "assets");
        std::vector<FileRegistration> batch;
        for (unsigned i = 0; i != 513; ++i) {
            const auto relative = "assets/asset-" + std::to_string(i) + ".bin";
            { std::ofstream out(root / relative, std::ios::binary); out << "fixture-" << i; }
            FileMetadata metadata;
            metadata.title = "Portrait " + std::to_string(i); metadata.description = "서울 fieldwork";
            metadata.keywords = {i % 2 ? "Photo" : "Video", "Campaign"}; metadata.rating = i % 6;
            metadata.color = i % 2 ? ColorLabel::Red : ColorLabel::Blue;
            metadata.fields = {{"project", "Autumn"}}; metadata.collections = {campaign.key};
            batch.push_back({relative, metadata});
        }
        const auto started = std::chrono::steady_clock::now();
        const auto registered = files.registerFiles(batch);
        require(registered.size() == 513 && files.count() == 513 && files.pendingMetadata() == 513, "bounded batch registration");
        require(!fs::exists(root / "assets/asset-0.bin.iisacc.json"), "batch unexpectedly published every companion");
        FileFilter filter; filter.text = "por 서울"; filter.pathPrefix = "assets";
        filter.allKeywords = {"CAMPAIGN"}; filter.anyKeywords = {"photo", "nonexistent"};
        filter.colors = {ColorLabel::Red}; filter.minimumRating = 3; filter.maximumRating = 5;
        filter.fields = {{"project", "Autumn"}}; filter.collections = {campaign.key};
        std::size_t expected = 0; for (unsigned i = 0; i != 513; ++i) if (i % 2 && i % 6 >= 3) ++expected;
        std::set<std::string> keys; std::optional<FileCursor> cursor;
        do {
            const auto page = files.search(filter, cursor, 31);
            require(page.files.size() <= 31, "search exceeded page bound");
            for (const auto &file : page.files) {
                require(keys.insert(file.key).second, "duplicate key across pages");
                require(file.color == ColorLabel::Red && file.rating >= 3 && file.title.starts_with("Portrait"), "incorrect filter projection");
            }
            cursor = page.next;
        } while (cursor);
        require(keys.size() == expected, "combined indexed search omitted files");
        auto first = files.search({}, {}, 1); require(first.next.has_value(), "missing continuation cursor");
        rejects([&] { (void)files.search(filter, first.next, 1); }, "cursor accepted another filter");
        auto altered = *first.next; altered.catalog = "wrong";
        rejects([&] { (void)files.search({}, altered, 1); }, "cursor accepted another catalog");
        const auto revision = registered.front().revision;
        auto edit = registered.front().metadata; edit.rating = 5;
        (void)files.updateMetadata(registered.front().key, revision, edit);
        rejects([&] { (void)files.search({}, first.next, 1); }, "changed catalog accepted stale cursor");
        require(files.publishMetadata(3) == 3, "companion batch ignored limit");
        require(files.pendingMetadata() == 509, "companion journal count incorrect");
        const auto count = files.count();
        { std::ofstream out(root / "unregistered.bin"); out << "new"; }
        rejects([&] { (void)files.registerFiles({{"unregistered.bin", {}}, {"missing.bin", {}}}); }, "partially invalid batch succeeded");
        require(files.count() == count && !files.lookupPath("unregistered.bin"), "failed batch left partial rows");
        require(!fs::exists(root / "unregistered.bin.iisacc.json"), "failed batch wrote a companion");
        FileFilter injection; injection.text = "\" OR * - nonexistent";
        require(files.search(injection).files.empty(), "plain-text search interpreted query operators");

        // A multi-gigabyte sparse payload must be indexed from filesystem
        // metadata and provided by range; allocating its logical size is a bug.
        { std::ofstream out(root / "large.bin", std::ios::binary); out << 'a'; }
#ifdef _WIN32
        constexpr std::uint64_t logicalSize = 2 * FileCatalog::maximumChunk + 17;
#else
        constexpr std::uint64_t logicalSize = (std::uint64_t(8) << 30) + 17;
#endif
        fs::resize_file(root / "large.bin", logicalSize);
        FileMetadata largeMetadata; largeMetadata.title = "Large original"; largeMetadata.keywords = {"Archive"};
        largeMetadata.description.assign(60000, 'x');
        const auto large = files.registerFile("large.bin", largeMetadata);
        require(large.metadataWritten && large.file.size == logicalSize, "large payload stat registration");
        FileFilter sizes; sizes.minimumSize = logicalSize; sizes.maximumSize = logicalSize;
        const auto page = files.search(sizes);
        require(page.files.size() == 1 && page.files.front().key == large.file.key, "64-bit size filter");
        const auto end = files.read(large.file.key, logicalSize - 2, 8, large.file.contentVersion);
        require(end.bytes.size() == 2 && end.eof && end.bytes[0] == std::byte(0), "64-bit bounded range provision");
        require(files.read(large.file.key, logicalSize, 8).bytes.empty(), "EOF range should be empty");
        rejects([&] { (void)files.read(large.file.key, 0, FileCatalog::maximumChunk + 1); }, "unbounded payload read accepted");
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        std::cout << "513-file transactional index, keyset search, rollback and " << logicalSize << "-byte logical payload verified in " << elapsed << " ms\n";
    } catch (const std::exception &error) { std::cerr << error.what() << "\nFixture: " << root << '\n'; return 1; }
    fs::remove_all(root); return 0;
}
