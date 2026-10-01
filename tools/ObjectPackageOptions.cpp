#include "ObjectPackageOptions.h"
#include <algorithm>
#include <charconv>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace iiFileProvider::cli {
namespace {
[[noreturn]] void invalid(const std::string &message) {
    throw std::invalid_argument(message);
}

std::filesystem::path utf8Path(std::string_view value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

int checkpointPages(std::string_view value) {
    if (value.empty() || value.front() < '0' || value.front() > '9')
        invalid("invalid --wal-autocheckpoint-pages value: expected a nonnegative decimal integer");
    int result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        invalid("invalid --wal-autocheckpoint-pages value: expected a nonnegative decimal integer");
    return result;
}
} // namespace

ObjectPackageOptions parseObjectPackageOptions(std::span<const std::string_view> arguments) {
    ObjectPackageOptions options;
    std::optional<PackageMode> selectedMode;
    std::set<std::string> namespaces;
    std::vector<std::pair<std::string, std::string>> exclusions;
    const auto selectMode = [&](PackageMode mode) {
        if (selectedMode && *selectedMode != mode) invalid("choose one inspection mode");
        selectedMode = mode;
        options.mode = mode;
    };

    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto argument = arguments[i];
        if (argument == "--inventory") { selectMode(PackageMode::Inventory); continue; }
        if (argument == "--audit") { selectMode(PackageMode::Audit); continue; }
        if (argument == "--index") { selectMode(PackageMode::Index); continue; }
        const auto value = [&]() -> std::string_view {
            if (i + 1 == arguments.size() || arguments[i + 1].empty())
                invalid("missing value for " + std::string(argument));
            return arguments[++i];
        };
        if (argument == "--package") options.package = utf8Path(value());
        else if (argument == "--container") options.container = value();
        else if (argument == "--wal-autocheckpoint-pages") options.walAutoCheckpointPages = checkpointPages(value());
        else if (argument == "--end-session") {
            selectMode(PackageMode::EndSession);
            options.sessionToEnd = value();
        } else if (argument == "--map") {
            const auto mapping = value();
            const auto equal = mapping.find('=');
            if (equal == std::string_view::npos || equal == 0 || equal + 1 == mapping.size())
                invalid("--map requires Namespace=absolute-directory");
            const std::string prefix(mapping.substr(0, equal));
            const auto root = utf8Path(mapping.substr(equal + 1));
            if (!root.is_absolute()) invalid("--map requires an absolute directory");
            if (!namespaces.insert(prefix).second) invalid("duplicate logical tree");
            options.mappings.push_back({prefix, root, {}});
        } else if (argument == "--exclude") {
            const auto exclusion = value();
            const auto slash = exclusion.find('/');
            if (slash == std::string_view::npos || slash == 0 || slash + 1 == exclusion.size()
                || exclusion.find('/', slash + 1) != std::string_view::npos)
                invalid("--exclude requires Namespace/top-level-name");
            exclusions.emplace_back(exclusion.substr(0, slash), exclusion.substr(slash + 1));
        } else invalid("unknown option: " + std::string(argument));
    }

    if (options.mode != PackageMode::Inventory && (options.package.empty() || options.container.empty()))
        invalid("--package and --container are required");
    if ((options.mode == PackageMode::Package || options.mode == PackageMode::Inventory
            || options.mode == PackageMode::Audit) && options.mappings.empty())
        invalid("at least one --map is required");
    if (options.mode == PackageMode::EndSession && options.sessionToEnd.empty())
        invalid("--end-session requires a session key");
    for (const auto &[prefix, name] : exclusions) {
        const auto mapping = std::find_if(options.mappings.begin(), options.mappings.end(),
            [&](const auto &entry) { return entry.prefix == prefix; });
        if (mapping == options.mappings.end()) invalid("excluded namespace is not mapped");
        mapping->excludedTopLevel.push_back(name);
    }
    return options;
}
} // namespace iiFileProvider::cli
