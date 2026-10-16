#include <FileCatalog.h>
#include <ObjectPackager.h>
#include <TemporaryLease.h>
#include "file_fixture.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace iiFileProvider;
constexpr std::size_t expectedEntries = IIFILEPROVIDER_TEST_PRESENTATION ? 4 : 3;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main() {
    const auto root = fs::path(CATALOG_TEST_DIRECTORY) / ("catalog-objects-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        {
            FileCatalog catalog(root / "library");
            FileMetadata metadata; metadata.title = "Portable original"; metadata.keywords = {"Archive"}; metadata.rating = 5;
            const auto created = catalog.createFileWith("original.bin", [&](std::ostream &output) {
                output << "original payload";
                const auto live = ObjectPackager::inventory({{"Files", root / "library", {}}});
                require(live.entries.empty() && live.issues.empty(), "live provider stage/control metadata leaked into inventory");
            }, metadata);
            require(created.metadataWritten, "portable metadata publication");
            // Public hidden data is still inventoried. Only the reserved
            // provider state is excluded, never hidden files as a category.
            { std::ofstream out(root / "library/.public-data"); out << "visible document"; }
            test_support::settle_tree(root / "library");
            const auto inventory = ObjectPackager::inventory({{"Files", root / "library", {}}});
            require(inventory.entries.size() == expectedEntries && inventory.issues.empty(), "payload/classification/optional embedded-preview/public hidden scope");
            ObjectStore store(root / "package", "catalog-integration", true);
            const auto session = store.beginSession({"local:test", "fixture", "Fixture"}, "test", "classified payloads");
            ObjectPackager packager(store);
            const auto packaged = packager.package(inventory, session.key, {}, {}, [&](const auto &) -> std::optional<ObjectMetadata> {
                TemporaryLease cleaner(root / "package/staging", TemporaryLease::Mode::TryExclusive);
                require(!cleaner.locked(), "packaging preparation is protected before individual snapshots exist");
                return {};
            }, ObjectStore::maximumBatchFiles);
            require(packaged.imported == expectedEntries && packaged.issues.empty(), "catalog/object packaging integration");
            require(store.lookupPath("Files/original.bin.iisacc.json").has_value(), "portable metadata missing from object package");
            if constexpr (IIFILEPROVIDER_TEST_PRESENTATION) require(store.lookupPath("Files/.original.bin.iisacc-preview").has_value(), "embedded preview document missing from object package");
            require(packager.audit(inventory).verified == expectedEntries, "classified payload audit");
            store.endSession(session.key);
        }
        fs::remove_all(root); std::cout << "Catalog payloads and companions package independently of private provider state\n";
    } catch (const std::exception &error) { std::cerr << error.what() << "\nFixture: " << root << '\n'; return 1; }
}
