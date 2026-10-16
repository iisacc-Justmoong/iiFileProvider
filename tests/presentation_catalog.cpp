#include <FileCatalog.h>
#include <iiFileProviderNative.h>
#include <iiFileProviderPresentation.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
using namespace iiFileProvider;
static void check(bool v, const char *message) { if (!v) throw std::runtime_error(message); }
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const auto root = fs::absolute(argv[1]) / ("presentation-catalog-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        {
            FileCatalog catalog(root);
            FileMetadata metadata; metadata.title = "Authored title"; metadata.keywords = {"work"}; metadata.rating = 4;
            const std::string bytes = "Original document content";
            const auto created = catalog.createFile("new.txt", {reinterpret_cast<const std::byte *>(bytes.data()), bytes.size()}, metadata);
            check(created.metadataWritten && !catalog.presentationDocument(created.file.key).empty(), "new catalog asset has an embedded presentation");
            const auto doc = nlohmann::json::parse(catalog.presentationDocument(created.file.key));
            check(doc.at("details").at("name") == "new.txt" && doc.at("preview").at("kind") == "text", "catalog exposes native detail/preview document");
            auto edit = metadata; edit.keywords.push_back("approved");
            const auto updated = catalog.updateMetadata(created.file.key, created.file.revision, edit);
            check(updated.metadataWritten && updated.file.metadata == edit, "embedding does not break strict catalog metadata revisions");
            const auto classification = nlohmann::json::parse(FileCatalog::classificationDocument(root / "new.txt"));
            check(classification.at("metadata").at("rating") == 4 && classification.at("metadata").at("keywords").size() == 2,
                "read-only asset inspection exposes the provider's editable classification");
            check(!classification.contains("path"), "portable inspection does not disclose a catalog-local path");
            check(catalog.read(created.file.key, 0, 1024, created.file.contentVersion).bytes.size() == bytes.size(), "catalog content version remains valid after attribute publication");
            check(native::File::read(root / "new.txt") == bytes, "source payload unchanged by enrichment and classification");
            FileMetadata invalid; invalid.collections = {"missing"};
            bool rejected = false;
            try { (void)catalog.createFile("rejected.txt", {}, invalid); } catch (...) { rejected = true; }
            check(rejected && !fs::exists(root / "rejected.txt") && !fs::exists(native::Presentation::companionPath(root / "rejected.txt")), "failed catalog registration rolls back its derived companion");
        }
        fs::remove_all(root / ".iisacc-file-provider");
        check(!FileCatalog::classificationDocument(root / "new.txt").empty(), "classification inspection needs no catalog database");
        check(!fs::exists(root / ".iisacc-file-provider"), "read-only inspection must not initialize an index");
        native::File::write(root / "foreign.txt", "external content");
        check(FileCatalog::classificationDocument(root / "foreign.txt").empty(), "unclassified external file has an explicit absent classification");
        native::File::write(root / "foreign.txt.iisacc.json", "{\"rating\":99}");
        bool malformedRejected = false;
        try { (void)FileCatalog::classificationDocument(root / "foreign.txt"); } catch (...) { malformedRejected = true; }
        check(malformedRejected, "malformed annotations must not become trusted file information");
        native::Presentation::cancelPending(root); fs::remove_all(root);
        std::cout << "Native presentation, classification edits, strict content versions and rollback integration passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
