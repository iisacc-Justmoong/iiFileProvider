#include <FileCatalog.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace iiFileProvider;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F fn, const char *message) {
    try { fn(); } catch (const std::exception &) { return; }
    throw std::runtime_error(message);
}
std::span<const std::byte> bytes(const std::string &s) { return {reinterpret_cast<const std::byte *>(s.data()), s.size()}; }
int main() {
    const auto root = fs::path(CATALOG_TEST_DIRECTORY) / ("classification-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        std::string key, collection, saved;
        {
            FileCatalog files(root);
            const auto group = files.createCollection("Campaign / 캠페인"); collection = group.key;
            FileMetadata metadata; metadata.title = "Seoul portrait"; metadata.description = "서울 야간 촬영";
            metadata.keywords = {"Portrait", "portrait", "Seoul", "cafe\xcc\x81"};
            metadata.rating = 5; metadata.color = ColorLabel::Purple;
            metadata.fields = {{"camera", "Synthetic camera"}, {"copyright", "Example Studio"}};
            metadata.collections = {collection};
            const std::string original("\x89PNG\r\n\x1a\nsynthetic-original\0bytes", 32);
            const auto created = files.createFile("image.png", bytes(original), metadata);
            require(created.metadataWritten, "file-owned metadata was not published"); key = created.file.key;
            require(created.file.metadata.keywords.size() == 3, "Unicode keyword normalization/deduplication");
            require(fs::is_regular_file(root / "image.png.iisacc.json"), "portable companion absent");
            const auto body = files.read(key); require(body.bytes == std::vector<std::byte>(bytes(original).begin(), bytes(original).end()), "payload changed by annotation");
            require(body.eof && body.size == original.size(), "bounded provider read");
            FileFilter filter; filter.text = "서울"; filter.minimumRating = 4;
            filter.colors = {ColorLabel::Purple}; filter.allKeywords = {"PORTRAIT", "caf\xc3\xa9"};
            filter.collections = {collection}; filter.fields = {{"camera", "Synthetic camera"}};
            require(files.search(filter).files.size() == 1, "combined indexed filter");
            saved = files.saveFilter("Best portraits", filter).key;
            const auto before = files.metadataDocument(key);
            auto invalid = created.file.metadata; invalid.rating = 6;
            rejects([&] { (void)files.updateMetadata(key, created.file.revision, invalid); }, "invalid rating accepted");
            require(files.metadataDocument(key) == before, "invalid update mutated metadata");
            auto updated = created.file.metadata; updated.description = "서울 approved selection";
            const auto edited = files.updateMetadata(key, created.file.revision, updated);
            require(edited.file.revision > created.file.revision && edited.metadataWritten, "metadata revision update");
            rejects([&] { (void)files.updateMetadata(key, created.file.revision, metadata); }, "stale writer accepted");
            require(files.read(key).bytes == body.bytes, "metadata edit rewrote original image");
            rejects([&] { (void)files.createFile("image.png", bytes("replacement")); }, "creation overwrote original");
            require(files.read(key).bytes == body.bytes, "collision damaged original");
            rejects([&] { (void)files.registerFile("../escape"); }, "root escape accepted");
            rejects([&] { (void)files.registerFile(".iisacc-file-provider/catalog.sqlite3"); }, "private state exposed");
            rejects([&] { (void)files.registerFile("image.png.iisacc.json"); }, "companion indexed as payload");
            rejects([&] { (void)files.search({}, {}, FileCatalog::maximumPage + 1); }, "unbounded search accepted");
        }
        {
            FileCatalog files(root, FileCatalog::Access::ReadWrite);
            require(files.lookup(key)->metadata.rating == 5 && files.searchSaved(saved).files.size() == 1, "restart persistence");
            FileCatalog second(root, FileCatalog::Access::ReadWrite);
            const auto old = second.lookup(key).value();
            auto edit = old.metadata; edit.rating = 4; (void)files.updateMetadata(key, old.revision, edit);
            rejects([&] { (void)second.updateMetadata(key, old.revision, old.metadata); }, "independent writer conflict not detected");
            files.renameCollection(collection, "Final selection");
            require(files.collections().front().name == "Final selection", "collection rename");
            const auto before = files.read(key);
            { std::ofstream output(root / "image.png", std::ios::binary | std::ios::app); output << 'x'; }
            rejects([&] { (void)files.read(key, 0, 100, before.contentVersion); }, "stale content range accepted");
            const auto refreshed = files.registerFile("image.png");
            require(refreshed.file.metadata.rating == 4, "registration discarded classification");
            // A companion failure leaves a recoverable journal, never lost metadata.
            fs::remove(root / "image.png.iisacc.json"); fs::create_directory(root / "image.png.iisacc.json");
            edit = refreshed.file.metadata; edit.title = "durably queued";
            const auto pending = files.updateMetadata(key, refreshed.file.revision, edit);
            require(!pending.metadataWritten && !pending.metadataError.empty() && files.pendingMetadata() == 1, "publication failure not journaled");
            fs::remove(root / "image.png.iisacc.json");
            require(files.publishMetadata() == 1 && files.pendingMetadata() == 0, "pending companion recovery");
            files.removeCollection(collection);
            require(files.lookup(key)->metadata.collections.empty(), "collection removal left dangling membership");
            require(files.searchSaved(saved).files.empty(), "deleted collection filter broadened access");
            files.removeFilter(saved); require(!files.savedFilter(saved), "saved-filter removal");
        }
        {
            FileCatalog reader(root, FileCatalog::Access::ReadOnly);
            require(reader.count() == 1, "read-only catalog unavailable");
            rejects([&] { (void)reader.createCollection("forbidden"); }, "read-only writer accepted");
        }
        // Portable metadata survives loss/rebuilding of the acceleration index.
        fs::remove_all(root / ".iisacc-file-provider");
        {
            FileCatalog restored(root);
            const auto adopted = restored.registerFile("image.png");
            require(adopted.file.key == key && adopted.file.metadata.title == "durably queued", "companion restoration");
        }
        fs::remove_all(root); std::cout << "Native metadata, collections, filters, persistence and bounded provision verified\n"; return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\nFixture preserved: " << root << '\n'; return 1;
    }
}
