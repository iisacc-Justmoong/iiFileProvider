#include <FileCatalog.h>
#include <sqlite3.h>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
using namespace iiFileProvider;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action, const char *message) {
    try { action(); } catch (const std::exception &) { return; }
    throw std::runtime_error(message);
}
std::span<const std::byte> bytes(const std::string &text) { return {reinterpret_cast<const std::byte *>(text.data()), text.size()}; }
std::string contents(const fs::path &path) { std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), {}}; }
int main() {
    const auto root = fs::path(CATALOG_TEST_DIRECTORY) / ("catalog-safety-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        // Process-independent initialization and revisions use the same SQLite
        // locking path. Every worker owns a distinct provider connection.
        std::barrier start(4); std::mutex errorsMutex; std::vector<std::string> errors;
        std::vector<std::jthread> writers;
        for (unsigned i = 0; i != 4; ++i) writers.emplace_back([&, i] {
            start.arrive_and_wait();
            try { FileCatalog files(root / "concurrent"); (void)files.createFile("worker-" + std::to_string(i), bytes("parallel")); }
            catch (const std::exception &error) { std::lock_guard lock(errorsMutex); errors.push_back(error.what()); }
        });
        writers.clear();
        for (const auto &error : errors) std::cerr << error << '\n';
        require(errors.empty(), "concurrent catalog initialization/writes failed");
        { FileCatalog files(root / "concurrent"); require(files.count() == 4, "concurrent writer lost a file"); }

        FileCatalog source(root / "source");
        const auto collection = source.createCollection("Client work");
        FileMetadata annotation; annotation.title = "Native archive"; annotation.keywords = {"Client", "原本"};
        annotation.color = ColorLabel::Green; annotation.rating = 4; annotation.collections = {collection.key};
        const auto original = source.createFileWith("original.bin", [](std::ostream &output) { output << "original"; }, annotation);
        const auto before = contents(root / "source/original.bin");
        FileCatalog destination(root / "destination");
        const auto imported = destination.importFile(root / "source/original.bin", "copy.bin");
        require(imported.file.key != original.file.key, "copied payload reused original identity");
        require(imported.file.metadata == original.file.metadata && destination.collections().front().name == "Client work", "import lost portable annotation or collection");
        require(contents(root / "destination/copy.bin") == before, "import altered bytes");
        bool encodedCollision = false;
        rejects([&] { (void)destination.createFileWith("copy.bin", [&](std::ostream &) { encodedCollision = true; }); }, "existing output accepted");
        require(!encodedCollision, "existing destination ran an expensive encoder");
        for (const std::string name : {"NUL.txt", "sub/CoM1.bin", "LPT².dat", "bad*.bin", "control\x01.bin"})
            rejects([&] { (void)destination.createFile(name, bytes("invalid")); }, "nonportable native path accepted");
        for (const std::string name : {".IISACC-FILE-PROVIDER/catalog.sqlite3", ".IISACC-TEMPORARY.LOCK", "copy.bin.IISACC.JSON"})
            rejects([&] { (void)destination.registerFile(name); }, "case variant exposed private provider state");
        rejects([&] { (void)destination.createFileWith("failed.bin", [](std::ostream &out) { out << "partial"; throw std::runtime_error("encoder failed"); }); }, "encoder failure was swallowed");
        require(!fs::exists(root / "destination/failed.bin") && destination.count() == 1, "failed encoder published data");
        for (const auto &entry : fs::directory_iterator(root / "destination"))
            require(!entry.path().filename().string().starts_with(".iifile-stage-"), "abandoned encoder stage");
        // A foreign companion is preserved and the committed edit remains
        // visible in the durable retry journal.
        const auto companion = root / "destination/copy.bin.iisacc.json";
        fs::remove(companion); { std::ofstream out(companion); out << "foreign data"; }
        auto changed = imported.file.metadata; changed.rating = 5;
        const auto queued = destination.updateMetadata(imported.file.key, imported.file.revision, changed);
        require(!queued.metadataWritten && queued.file.metadataPending && contents(companion) == "foreign data", "foreign companion was overwritten");
        fs::remove(companion); require(destination.publishMetadata() == 1, "foreign companion recovery failed");
        require(contents(root / "source/original.bin") == before, "classification changed source bytes");

#ifndef _WIN32
        fs::create_directory_symlink(root / "source", root / "destination/redirect");
        rejects([&] { (void)destination.registerFile("redirect/original.bin"); }, "redirected parent accepted");
        rejects([&] { (void)destination.createFile("redirect/new.bin", bytes("bad")); }, "creation escaped through symlink");
        require(!fs::exists(root / "source/new.bin"), "symlink escape modified outside data");
#endif
        const auto state = root / "destination/.iisacc-file-provider";
        fs::rename(state, root / "destination/saved-state"); fs::create_directory(state);
        rejects([&] { (void)destination.search(); }, "replaced catalog directory accepted");
        fs::remove(state); fs::rename(root / "destination/saved-state", state);

        {
            FileCatalog settings(root / "settings");
            const auto hidden = settings.createFile(".config/app.json", bytes("{}"));
            require(hidden.metadataWritten && settings.lookupPath(".config/app.json").has_value(), "ordinary hidden application files must use the native provider too");
        }

        // An unrelated SQLite file must never be initialized as a catalog.
        fs::create_directories(root / "foreign/.iisacc-file-provider");
        const auto foreignPath = root / "foreign/.iisacc-file-provider/catalog.sqlite3";
        sqlite3 *db = nullptr; require(sqlite3_open(foreignPath.string().c_str(), &db) == SQLITE_OK, "fixture DB open");
        require(sqlite3_exec(db, "CREATE TABLE customer_data(value TEXT); INSERT INTO customer_data VALUES('keep');", nullptr, nullptr, nullptr) == SQLITE_OK, "fixture DB create");
        sqlite3_close(db); const auto foreignBytes = contents(foreignPath);
        rejects([&] { FileCatalog foreign(root / "foreign"); }, "foreign database adopted");
        require(contents(foreignPath) == foreignBytes, "foreign SQLite database modified");
        rejects([&] { FileCatalog missing(root / "missing", FileCatalog::Access::ReadOnly); }, "read-only created missing root");
        require(!fs::exists(root / "missing"), "read-only access created a root");
        std::cout << "Concurrent connections, portable import, encoder rollback, foreign state and redirected roots verified\n";
    } catch (const std::exception &error) { std::cerr << error.what() << "\nFixture: " << root << '\n'; return 1; }
    fs::remove_all(root); return 0;
}
