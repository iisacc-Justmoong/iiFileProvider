#pragma once

#include "ObjectPackager.h"
#include <span>
#include <string_view>

namespace iiFileProvider::cli {

enum class PackageMode { Package, Inventory, Audit, Index, EndSession };

struct ObjectPackageOptions {
    PackageMode mode = PackageMode::Package;
    std::filesystem::path package;
    std::string container;
    std::vector<ObjectTreeMapping> mappings;
    std::string sessionToEnd;
    int walAutoCheckpointPages = ObjectStore::defaultWalAutoCheckpointPages;
};

// Parse arguments after argv[0] without opening files, stores, or work sessions.
// Filesystem identity and confinement remain ObjectPackager/ObjectStore duties.
ObjectPackageOptions parseObjectPackageOptions(std::span<const std::string_view> arguments);

} // namespace iiFileProvider::cli
