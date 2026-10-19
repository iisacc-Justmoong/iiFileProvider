#include "TemporaryLease.h"
#include "iiFileProviderNative.h"
#include "iiFileProviderPresentation.h"
#include <algorithm>
#include <fstream>
#include <limits>
#include <random>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace iiFileProvider::native {
namespace {
std::string nonce() {
    std::random_device random;
    return std::to_string(random()) + "-" + std::to_string(random());
}
std::filesystem::path parentOf(const std::filesystem::path &path) {
    return path.parent_path().empty() ? std::filesystem::current_path() : path.parent_path();
}
bool reserve(const std::filesystem::path &path) {
#ifdef _WIN32
    auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(handle);
    return true;
#else
    auto fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd < 0)
        return false;
    ::close(fd);
    return true;
#endif
}
std::filesystem::path checkedPath(const std::filesystem::path &path) {
    if (path.empty() || path.filename().empty() ||
        path.native().find(std::filesystem::path::value_type{})!=std::filesystem::path::string_type::npos)
        throw FileError("A nonempty output filename without NUL is required",FileCode::InvalidPath);
    return std::filesystem::absolute(path);
}
void writeStaged(const std::filesystem::path &path,std::string_view bytes) {
    std::ofstream output(path,std::ios::binary|std::ios::trunc);
    if (!output) throw FileError("Cannot open staged output");
    if (bytes.size()>static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        throw FileError("Output exceeds stream limits",FileCode::LimitExceeded);
    output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) throw FileError("Cannot write staged output");
    output.close();
    if (!output) throw FileError("Cannot close staged output");
}
} // namespace
struct ReadFile::Impl {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
    ~Impl() { if (handle!=INVALID_HANDLE_VALUE) CloseHandle(handle); }
#else
    std::ifstream stream;
#endif
    std::uint64_t size{}, position{};
};
ReadFile::ReadFile(const std::filesystem::path &path) : impl_(std::make_unique<Impl>()) {
    const auto target=checkedPath(path);
    std::error_code error;
    auto size = std::filesystem::file_size(target, error);
    if (error)
        throw FileError("Cannot inspect input file: " + error.message());
    impl_->size = size;
#ifdef _WIN32
    impl_->handle=CreateFileW(target.c_str(),GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (impl_->handle==INVALID_HANDLE_VALUE) throw FileError("Cannot open input file");
#else
    impl_->stream.open(target, std::ios::binary);
    if (!impl_->stream)
        throw FileError("Cannot open input file");
#endif
}
ReadFile::~ReadFile() = default;
std::int64_t ReadFile::read(char *data, std::int64_t count) {
    if (count < 0)
        return -1;
    auto amount = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(count),
        static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()));
#ifdef _WIN32
    DWORD read=0;
    if (!::ReadFile(impl_->handle,data,static_cast<DWORD>(std::min<std::uint64_t>(amount,MAXDWORD)),&read,nullptr)) return -1;
#else
    impl_->stream.read(data, static_cast<std::streamsize>(amount));
    auto read = impl_->stream.gcount();
    if (impl_->stream.bad())
        return -1;
#endif
    impl_->position += static_cast<std::uint64_t>(read);
    return static_cast<std::int64_t>(read);
}
std::string ReadFile::read(std::int64_t count) {
    if (count < 0)
        throw FileError("Invalid input read length");
    std::string data(static_cast<std::size_t>(count), '\0');
    auto bytes = read(data.data(), count);
    if (bytes < 0)
        throw FileError("Input file read failed");
    data.resize(static_cast<std::size_t>(bytes));
    return data;
}
bool ReadFile::seek(std::uint64_t offset) {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
        return false;
#ifdef _WIN32
    LARGE_INTEGER target{};target.QuadPart=static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(impl_->handle,target,nullptr,FILE_BEGIN)) return false;
#else
    impl_->stream.clear();
    impl_->stream.seekg(static_cast<std::streamoff>(offset));
    if (!impl_->stream)
        return false;
#endif
    impl_->position = offset;
    return true;
}
std::uint64_t ReadFile::size() const noexcept { return impl_->size; }
std::uint64_t ReadFile::pos() const noexcept { return impl_->position; }
std::unique_ptr<ReadFile> File::openRead(const std::filesystem::path &path) {
    return std::make_unique<ReadFile>(path);
}
std::string File::read(const std::filesystem::path &path, std::uint64_t maximum) {
    auto input = openRead(path);
    if (input->size() > maximum ||
        input->size() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        throw FileError("Input file exceeds the configured byte limit", FileCode::LimitExceeded);
    auto result = input->read(static_cast<std::int64_t>(input->size()));
    if (result.size() != input->size())
        throw FileError("Input file changed or ended during reading");
    return result;
}
void File::createDirectories(const std::filesystem::path &path) {
    if (path.empty())
        return;
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error)
        throw FileError("Cannot create directory: " + error.message());
}
PublishResult File::publish(const std::filesystem::path &temporary,
                            const std::filesystem::path &destination) {
    try {
#ifdef _WIN32
        Presentation::cancelPending(temporary);
        Presentation::cancelPending(destination);
#endif
        if (std::filesystem::weakly_canonical(parentOf(temporary)) !=
            std::filesystem::weakly_canonical(parentOf(destination)))
            return {false, "cross_directory",
                    "Staged files must be published in their original directory"};
        auto sourceStatus = std::filesystem::symlink_status(temporary);
        if (!std::filesystem::is_regular_file(sourceStatus))
            return {false, "invalid_staging", "Staging input is not a regular file"};
        auto status = std::filesystem::symlink_status(destination);
        if (std::filesystem::is_symlink(status) || std::filesystem::is_directory(status))
            return {false, "invalid_destination", "Cannot replace a symlink or directory"};
        if (std::filesystem::exists(status))
            std::filesystem::permissions(temporary, status.permissions());
#ifdef _WIN32
        const bool replaced=std::filesystem::exists(status)
            ? ReplaceFileW(destination.c_str(),temporary.c_str(),nullptr,0,nullptr,nullptr)
            : MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
        if (!replaced)
            return {false, "rename_failed", "Native file replacement failed: "+std::system_category().message(static_cast<int>(GetLastError()))};
#else
        if (::rename(temporary.c_str(), destination.c_str()) != 0)
            return {false, "rename_failed", std::system_category().message(errno)};
#endif
        Presentation::enqueue(destination);
        return {true, {}, {}};
    } catch (const std::exception &error) {
        return {false, "commit_failed", error.what()};
    }
}
struct StagedFile::Impl {
    std::filesystem::path path, destination;
    std::unique_ptr<iiFileProvider::TemporaryLease> lease;
    bool published{};
};
StagedFile::StagedFile(const std::filesystem::path &destination) : impl_(std::make_unique<Impl>()) {
    if (destination.empty() || destination.filename().empty()
        || destination.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos)
        throw FileError("A nonempty output filename without NUL is required");
    impl_->destination = std::filesystem::absolute(destination);
    auto parent = parentOf(impl_->destination);
    File::createDirectories(parent);
    impl_->lease = std::make_unique<iiFileProvider::TemporaryLease>(
        parent, iiFileProvider::TemporaryLease::Mode::Shared);
    for (int attempt = 0; attempt < 128; ++attempt) {
        auto name = std::filesystem::path(".iifile-stage-" + nonce());
        name += impl_->destination.extension();
        auto path = parent / name;
        if (reserve(path)) {
            impl_->path = std::move(path);
            return;
        }
    }
    throw FileError("Cannot reserve a native staged file");
}
StagedFile::~StagedFile() {
    if (impl_ && !impl_->published) {
        std::error_code error;
        std::filesystem::remove(impl_->path, error);
    }
}
const std::filesystem::path &StagedFile::path() const noexcept { return impl_->path; }
PublishResult StagedFile::publish() {
    if (impl_->published)
        return {false, "already_published", "Staged file was already published"};
    auto result = File::publish(impl_->path, impl_->destination);
    impl_->published = result.succeeded;
    return result;
}
void File::write(const std::filesystem::path &destination, std::string_view bytes) {
    StagedFile staged(destination);
    writeStaged(staged.path(),bytes);
    auto published = staged.publish();
    if (!published.succeeded)
        throw FileError(published.message);
}
void File::write(const std::filesystem::path &destination, std::span<const std::byte> bytes) {
    write(destination,
          std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}
void File::create(const std::filesystem::path &destination,std::string_view bytes) {
    const auto target=checkedPath(destination);
    StagedFile staged(target);
    writeStaged(staged.path(),bytes);
    // A same-directory hard link publishes complete bytes and cannot replace an
    // existing destination. Filesystems without hard links fail explicitly.
#ifdef _WIN32
    if (!CreateHardLinkW(target.c_str(),staged.path().c_str(),nullptr)) {
        const auto error=GetLastError();
        throw FileError("Exclusive file publication failed: "+std::system_category().message(int(error)),
            error==ERROR_ALREADY_EXISTS || error==ERROR_FILE_EXISTS ? FileCode::AlreadyExists : FileCode::IoFailure);
    }
#else
    if (::link(staged.path().c_str(),target.c_str())!=0) {
        const auto error=errno;
        throw FileError("Exclusive file publication failed: "+std::system_category().message(error),
            error==EEXIST ? FileCode::AlreadyExists : FileCode::IoFailure);
    }
#endif
    // Retire the staging link before preview identity/ctime is sampled.
    std::error_code cleanup;
    std::filesystem::remove(staged.path(),cleanup);
    Presentation::enqueue(target);
}
void File::update(const std::filesystem::path &destination,std::string_view expected,std::string_view replacement) {
    const auto target=checkedPath(destination);
    std::error_code error;
    const auto status=std::filesystem::symlink_status(target,error);
    if (error==std::errc::no_such_file_or_directory)
        throw FileError("Update destination was removed",FileCode::Conflict);
    if (error) throw FileError("Cannot inspect update target: "+error.message());
    if (!std::filesystem::is_regular_file(status))
        throw FileError("Update destination must be a regular file",FileCode::Conflict);
    try {
        const auto maximum=expected.size()==SIZE_MAX ? UINT64_MAX : std::uint64_t(expected.size())+1;
        if (read(target,maximum)!=expected) throw FileError("File changed before update",FileCode::Conflict);
    } catch (const FileError &error) {
        if (error.code()==FileCode::LimitExceeded) throw FileError("File size changed before update",FileCode::Conflict);
        throw;
    }
    write(target,replacement);
}
bool File::remove(const std::filesystem::path &destination) {
    const auto target=checkedPath(destination);
    Presentation::cancelPending(target);
    std::error_code error;
    const auto status=std::filesystem::symlink_status(target,error);
    if (error==std::errc::no_such_file_or_directory) return false;
    if (error) throw FileError("Cannot inspect removal target: "+error.message());
    if (!std::filesystem::exists(status)) return false;
    if (std::filesystem::is_directory(status)) throw FileError("File removal does not remove directories");
    const bool removed=std::filesystem::remove(target,error);
    if (error) throw FileError("Cannot remove file: "+error.message());
    if (removed) Presentation::removeCompanion(target);
    return removed;
}
TemporaryDirectory::TemporaryDirectory(const std::filesystem::path &parent) {
    File::createDirectories(parent);
    for (int attempt = 0; attempt < 128; ++attempt) {
        auto path = std::filesystem::absolute(parent) / (".iisacc-native-directory-" + nonce());
        std::error_code error;
        if (std::filesystem::create_directory(path, error)) {
            std::filesystem::permissions(path, std::filesystem::perms::owner_all);
            path_ = std::move(path);
            return;
        }
        if (error && error != std::errc::file_exists)
            break;
    }
    throw FileError("Cannot reserve a native temporary directory");
}
TemporaryDirectory::~TemporaryDirectory() {
    Presentation::cancelPending(path_);
    if (!path_.empty()) {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
}
} // namespace iiFileProvider::native
