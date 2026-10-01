#include "ObjectPackageOptions.h"
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

using namespace iiFileProvider::cli;

namespace {
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
ObjectPackageOptions parse(std::initializer_list<std::string_view> arguments) {
#ifdef _WIN32
    // Keep the fixture paths readable while providing the root name required
    // by Windows path::is_absolute(). No fixture directory is ever created.
    std::vector<std::string> native;
    for (const auto argument : arguments) {
        std::string value(argument);
        const auto pathStart = value.starts_with('/') ? 0 : value.find("=/");
        if (pathStart != std::string::npos)
            value.insert(pathStart == 0 ? 0 : pathStart + 1, "C:");
        native.push_back(std::move(value));
    }
    std::vector<std::string_view> views(native.begin(), native.end());
    return parseObjectPackageOptions(views);
#else
    return parseObjectPackageOptions({arguments.begin(), arguments.size()});
#endif
}
void rejects(std::initializer_list<std::string_view> arguments) {
    bool rejected = false;
    try { (void)parse(arguments); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "invalid options must be rejected before filesystem I/O");
}
}

int main() {
    try {
        const auto package = parse({"--exclude", "Files/.Trashes", "--package", "/not-created/package",
            "--container", "fixture", "--map", "Files=/not-created/files",
            "--map", "Photos=/not-created/photos", "--wal-autocheckpoint-pages", "262144"});
        require(package.mode == PackageMode::Package && package.container == "fixture"
            && package.package.relative_path() == "not-created/package", "package identity");
        require(package.mappings.size() == 2 && package.mappings[0].prefix == "Files"
            && package.mappings[0].excludedTopLevel == std::vector<std::string>{".Trashes"}
            && package.mappings[1].excludedTopLevel.empty(), "exclusions bind by namespace in any argument order");
        require(package.walAutoCheckpointPages == 262144, "configured threshold");
        const auto unicode = parse({"--inventory", "--map", "Generation History=/not-created/생성 이력"});
        require(unicode.mappings[0].prefix == "Generation History"
            && unicode.mappings[0].root.relative_path().generic_u8string() == u8"not-created/생성 이력", "UTF-8 and spaces are preserved");

        const auto inventory = parse({"--inventory", "--map", "Files=/not-created/files"});
        require(inventory.mode == PackageMode::Inventory && inventory.package.empty()
            && inventory.walAutoCheckpointPages == iiFileProvider::ObjectStore::defaultWalAutoCheckpointPages,
            "inventory needs no package and uses the SDK default");
        require(parse({"--audit", "--package", "/not-created/package", "--container", "fixture",
            "--map", "Files=/not-created/files"}).mode == PackageMode::Audit, "audit mode");
        require(parse({"--index", "--package", "/not-created/package", "--container", "fixture"})
            .mode == PackageMode::Index, "index requires no source tree");
        const auto end = parse({"--end-session", "session-1", "--container", "fixture",
            "--package", "/not-created/package"});
        require(end.mode == PackageMode::EndSession && end.sessionToEnd == "session-1", "session close mode");

        for (const auto &value : std::vector<std::string>{"0", "1", std::to_string(std::numeric_limits<int>::max())}) {
            const auto result = parse({"--inventory", "--map", "Files=/not-created/files",
                "--wal-autocheckpoint-pages", value});
            require(result.walAutoCheckpointPages == std::stoll(value), "threshold boundaries");
        }
        for (const auto value : {"", "-1", "-0", "+1", "1tail", " 1", "1 ", "1.5", "2147483648", "999999999999999999999999"})
            rejects({"--inventory", "--map", "Files=/not-created/files", "--wal-autocheckpoint-pages", value});
        rejects({});
        rejects({"--inventory"});
        rejects({"--inventory", "--index", "--map", "Files=/not-created/files"});
        rejects({"--index", "--end-session", "session-1", "--package", "/not-created/package", "--container", "fixture"});
        rejects({"--index", "--package", "/not-created/package"});
        rejects({"--audit", "--package", "/not-created/package", "--container", "fixture"});
        rejects({"--inventory", "--map"});
        rejects({"--inventory", "--wal-autocheckpoint-pages"});
        rejects({"--end-session", ""});
        rejects({"--unknown", "value"});
        rejects({"--inventory", "--map", "Files"});
        rejects({"--inventory", "--map", "= /not-created/files"});
        rejects({"--inventory", "--map", "Files="});
        rejects({"--inventory", "--map", "Files=relative/path"});
        rejects({"--inventory", "--map", "Files=/not-created/files", "--map", "Files=/other"});
        rejects({"--inventory", "--map", "Files=/not-created/files", "--exclude", "Photos/.previews"});
        rejects({"--inventory", "--map", "Files=/not-created/files", "--exclude", "Files/"});
        rejects({"--inventory", "--map", "Files=/not-created/files", "--exclude", "Files/folder/nested"});
        rejects({"--inventory", "--map", "Files=/not-created/files", "--exclude", "/name"});
        std::cout << "Package command modes, mappings and strict numeric options passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
