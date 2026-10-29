#include "iiFileProviderNativeDirectory.h"
#include "AtomicPublication.h"

#include <exception>

namespace iiFileProvider::native {
PublishResult publishDirectoryExclusive(const std::filesystem::path &stageName,
                                        const std::filesystem::path &destinationName)
{
    try {
        const auto stage = atomic_detail::checked(stageName);
        const auto destination = atomic_detail::checked(destinationName);
        if (stage == destination || !std::filesystem::is_directory(std::filesystem::symlink_status(stage)))
            return {false, "invalid_source", "Publication requires a distinct real staged directory"};
        if (std::filesystem::canonical(stage.parent_path()) != std::filesystem::canonical(destination.parent_path()))
            return {false, "cross_directory", "Staged directories must be published in their original parent"};
        std::error_code error;
        const auto target = std::filesystem::symlink_status(destination, error);
        if (error && error != std::errc::no_such_file_or_directory)
            return {false, "destination_status_failed", error.message()};
        if (target.type() != std::filesystem::file_type::not_found)
            return {false, "already_exists", "Destination already exists"};
        error = atomic_detail::moveDirectoryExclusive(stage, destination);
        if (error) {
            const bool exists = error == std::errc::file_exists || error == std::errc::directory_not_empty;
            const bool unsupported = error == std::errc::function_not_supported
                || error == std::errc::operation_not_supported || error == std::errc::invalid_argument;
            return {false, exists ? "already_exists" : unsupported ? "unsupported" : "atomic_commit_failed", error.message()};
        }
        return {true, {}, {}};
    } catch (const std::exception &error) {
        return {false, "atomic_commit_failed", error.what()};
    }
}
}
