#include "iiFileProviderNativeDevice.h"
#include "iiFileProviderPresentation.h"
#include "PipeSignal_p.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <limits>
#include <mutex>
#include <random>
#include <system_error>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#endif

namespace iiFileProvider::native {
namespace {
[[noreturn]] void ioError(const char* operation, int error = errno) {
    throw FileError(std::string(operation) + ": " + std::generic_category().message(error),
                    error == EEXIST ? FileCode::AlreadyExists : FileCode::IoFailure);
}
int descriptor(FILE* stream) {
#ifdef _WIN32
    return _fileno(stream);
#else
    return fileno(stream);
#endif
}
struct Mapping { char* visible; void* base; std::size_t bytes; };
}
struct Device::Impl {
    int fd = -1;
    FILE* stream = nullptr;
    bool owner = false, broken = false, sequential = false, notify;
    OpenOptions options;
    std::filesystem::path path;
    std::unique_ptr<SourceAccess> access;
    std::vector<Mapping> mappings;
    explicit Impl(bool value) : notify(value) {}
    auto lock() { return access ? std::unique_lock<SourceAccess>(*access) : std::unique_lock<SourceAccess>(); }
    void opened() {
#ifdef _WIN32
        struct _stat64 info{}; if (_fstat64(fd, &info)) ioError("Inspect descriptor");
        sequential = (info.st_mode & _S_IFMT) != _S_IFREG;
#else
        struct stat info{}; if (fstat(fd, &info)) ioError("Inspect descriptor");
        if (S_ISDIR(info.st_mode)) throw FileError("A directory is not a file device",FileCode::InvalidPath);
        sequential = !S_ISREG(info.st_mode) && !S_ISBLK(info.st_mode);
#endif
        broken = false;
    }
};
Device::Device(bool notify) : impl_(std::make_unique<Impl>(notify)) {}
Device::~Device() { try { close(); } catch (...) {} }
bool Device::isOpen() const noexcept { return impl_->fd >= 0; }
bool Device::failed() const noexcept { return impl_->broken; }
bool Device::isSequential() const noexcept { return impl_->sequential; }
int Device::handle() const noexcept { return impl_->fd; }
const std::filesystem::path& Device::path() const noexcept { return impl_->path; }
void Device::open(const std::filesystem::path& name, OpenOptions options) {
    if (isOpen()) throw FileError("Device is already open");
    if (name.empty() || name.native().find(std::filesystem::path::value_type{}) != name.native().npos ||
        (!options.read && !options.write) || (options.exclusive && options.existing) ||
        (!options.write && (options.append || options.truncate || options.exclusive)))
        throw FileError("Invalid file path or open options", FileCode::InvalidPath);
    impl_->path = std::filesystem::absolute(name); impl_->options = options;
    impl_->access.reset();
    if (std::filesystem::is_regular_file(impl_->path)) impl_->access = std::make_unique<SourceAccess>(impl_->path);
    auto lock = impl_->lock();
#ifdef _WIN32
    DWORD access = (options.read ? GENERIC_READ : 0) | (options.write ? GENERIC_WRITE : 0);
    const DWORD disposition = options.exclusive ? CREATE_NEW : options.existing ?
        (options.truncate ? TRUNCATE_EXISTING : OPEN_EXISTING) : options.write ?
        (options.truncate ? CREATE_ALWAYS : OPEN_ALWAYS) : OPEN_EXISTING;
    if (options.noFollow && std::filesystem::is_symlink(std::filesystem::symlink_status(name)))
        throw FileError("Refusing a file link", FileCode::InvalidPath);
    auto handle = CreateFileW(name.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        throw FileError("Open file: " + std::system_category().message(int(error)),
            error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ? FileCode::AlreadyExists : FileCode::IoFailure);
    }
    const int mode = _O_BINARY | _O_NOINHERIT | (options.read && options.write ? _O_RDWR : options.write ? _O_WRONLY : _O_RDONLY);
    impl_->fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), mode);
    if (impl_->fd < 0) { CloseHandle(handle); ioError("Adopt native file handle"); }
#else
    int flags = options.read && options.write ? O_RDWR : options.write ? O_WRONLY : O_RDONLY;
    if (options.write && !options.existing) flags |= O_CREAT;
    if (options.exclusive) flags |= O_EXCL;
    if (options.truncate) flags |= O_TRUNC;
    if (options.append) flags |= O_APPEND;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    if (options.noFollow) flags |= O_NOFOLLOW;
#endif
    do { impl_->fd = ::open(name.c_str(), flags, mode_t(options.permissions) & 0777); } while (impl_->fd < 0 && errno == EINTR);
    if (impl_->fd < 0) ioError("Open file");
#endif
    impl_->owner = true;
    try { impl_->opened(); if (!impl_->access && std::filesystem::is_regular_file(impl_->path)) impl_->access = std::make_unique<SourceAccess>(impl_->path); if (options.append && !impl_->sequential) seek(size()); }
    catch (...) { impl_->broken = true; close(); throw; }
}
void Device::attach(int fd, bool owner, OpenOptions options) {
    if (isOpen() || fd < 0 || (!options.read && !options.write)) throw FileError("Invalid borrowed descriptor");
    impl_->fd = fd; impl_->owner = owner; impl_->options = options; impl_->path.clear(); impl_->access.reset();
    try { impl_->opened(); } catch (...) { impl_->fd = -1; throw; }
}
void Device::attach(FILE* stream, bool owner, OpenOptions options) {
    if (!stream) throw FileError("Null stream");
    attach(descriptor(stream), owner, options); impl_->stream = stream;
}
void Device::openTemporary(const std::filesystem::path& pattern) {
    if (isOpen()) throw FileError("Device is already open");
    auto raw = pattern.native(); using Char = std::filesystem::path::value_type;
    const auto token = std::filesystem::path("XXXXXX").native(); auto at = raw.rfind(token);
    if (at == raw.npos) { raw += std::filesystem::path(".XXXXXX").native(); at = raw.size() - 6; }
    std::random_device random;
    for (int attempt = 0; attempt < 128; ++attempt) {
        auto name = raw; static constexpr char digits[] = "0123456789abcdef";
        std::basic_string<Char> suffix(24, Char{});
        for (std::size_t i = 0; i < suffix.size(); i += 8) { auto bits = random(); for (int j = 0; j < 8; ++j) { suffix[i+j] = Char(digits[bits & 15]); bits >>= 4; } }
        name.replace(at, 6, suffix);
        try { open(std::filesystem::path(name), {.read=true,.write=true,.exclusive=true,.noFollow=true,.permissions=std::filesystem::perms::owner_read | std::filesystem::perms::owner_write}); return; }
        catch (const FileError& error) { if (error.code() != FileCode::AlreadyExists) throw; }
    }
    throw FileError("Cannot reserve a temporary file");
}
void Device::close() {
    if (!isOpen()) return;
    auto lock = impl_->lock();
    while (!impl_->mappings.empty()) unmap(impl_->mappings.back().visible);
    int status = 0;detail::PipeSignal signal(impl_->sequential);
    if (impl_->stream && impl_->options.write && std::fflush(impl_->stream)) { impl_->broken = true; status = errno; if(status==EPIPE)signal.broken(); }
    if (impl_->owner) {
        int closed;
        if (impl_->stream) closed = std::fclose(impl_->stream);
#ifdef _WIN32
        else closed = _close(impl_->fd);
#else
        else closed = ::close(impl_->fd); // Never retry close: the descriptor may already have been reused.
#endif
        if (closed) { impl_->broken = true; status = errno; if(status==EPIPE)signal.broken(); }
    }
    impl_->fd = -1; impl_->stream = nullptr;
    if (impl_->notify && impl_->options.write && !impl_->broken && impl_->access) {
        try{Presentation::enqueue(impl_->path);}catch(...){}
    }
    if (status) {if(status==EPIPE)signal.broken();ioError("Close file", status);}
}
std::int64_t Device::read(char* bytes, std::int64_t count) {
    if (!isOpen() || !impl_->options.read || count < 0 || (!bytes && count)) throw FileError("Invalid read");
    if (!count) return 0; auto lock = impl_->lock(); std::int64_t result;
    if (impl_->stream) { result = std::fread(bytes, 1, std::size_t(std::min<std::int64_t>(count, INT_MAX)), impl_->stream); if (!result && std::ferror(impl_->stream)) result = -1; }
    else {
#ifdef _WIN32
        result = _read(impl_->fd, bytes, unsigned(std::min<std::int64_t>(count, INT_MAX)));
#else
        do { result = ::read(impl_->fd, bytes, std::size_t(std::min<std::int64_t>(count, SSIZE_MAX))); } while (result < 0 && errno == EINTR);
#endif
    }
    if (result < 0) { impl_->broken = true; ioError("Read file"); } return result;
}
std::int64_t Device::write(const char* bytes, std::int64_t count) {
    if (!isOpen() || !impl_->options.write || count < 0 || (!bytes && count)) { impl_->broken = true; throw FileError("Invalid write"); }
    auto lock = impl_->lock(); std::int64_t done = 0;detail::PipeSignal signal(impl_->sequential);
    while (done < count) {
        std::int64_t result; const auto part = unsigned(std::min<std::int64_t>(count-done, INT_MAX));
        if (impl_->stream) { if (impl_->options.append && !impl_->sequential) seek(size()); result = std::fwrite(bytes+done, 1, part, impl_->stream); }
        else {
#ifdef _WIN32
            if (impl_->options.append) {
                OVERLAPPED end{}; end.Offset = end.OffsetHigh = MAXDWORD; DWORD written=0;
                if(!WriteFile(reinterpret_cast<HANDLE>(_get_osfhandle(impl_->fd)), bytes+done, part, &written, &end)) {
                    impl_->broken=true;throw FileError("Write file: "+std::system_category().message(int(GetLastError())));
                }
                result = written;
                if (result >= 0) _lseeki64(impl_->fd, 0, SEEK_END);
            } else result = _write(impl_->fd, bytes+done, part);
#else
            do { result = ::write(impl_->fd, bytes+done, part); } while (result < 0 && errno == EINTR);
#endif
        }
        if (result <= 0) { impl_->broken = true; if(errno==EPIPE)signal.broken();ioError("Write file", result == 0 ? ENOSPC : errno); } done += result;
    }
    return done;
}
bool Device::seek(std::int64_t offset) {
    if (!isOpen() || offset < 0 || impl_->sequential) return false;
    auto lock = impl_->lock();
#ifdef _WIN32
    const bool ok = impl_->stream ? _fseeki64(impl_->stream,offset,SEEK_SET)==0 : _lseeki64(impl_->fd,offset,SEEK_SET)>=0;
#else
    const bool ok = impl_->stream ? fseeko(impl_->stream,off_t(offset),SEEK_SET)==0 : lseek(impl_->fd,off_t(offset),SEEK_SET)>=0;
#endif
    if (!ok) ioError("Seek file"); return true;
}
std::int64_t Device::pos() const {
    if (!isOpen()) return 0; auto lock=impl_->lock();
#ifdef _WIN32
    const auto result=impl_->stream ? _ftelli64(impl_->stream) : _lseeki64(impl_->fd,0,SEEK_CUR);
#else
    const auto result=impl_->stream ? ftello(impl_->stream) : lseek(impl_->fd,0,SEEK_CUR);
#endif
    if (result<0 && !impl_->sequential) ioError("File position"); return result<0?0:result;
}
std::int64_t Device::size() const {
    if (!isOpen()) return 0; auto lock=impl_->lock();
#ifdef _WIN32
    struct _stat64 info{}; if (_fstat64(impl_->fd,&info)) ioError("File size");
#else
    struct stat info{}; if (fstat(impl_->fd,&info)) ioError("File size");
#endif
    return info.st_size;
}
void Device::flush(bool durable) {
    if (!isOpen()) throw FileError("Device is closed"); auto lock=impl_->lock();detail::PipeSignal signal(impl_->sequential);
    if (impl_->stream && impl_->options.write && std::fflush(impl_->stream)) {impl_->broken=true;if(errno==EPIPE)signal.broken();ioError("Flush stream");}
    if (durable && impl_->options.write && !impl_->sequential) {
#ifdef _WIN32
        const auto result=_commit(impl_->fd);
#else
        int result; do {result=fsync(impl_->fd);} while(result && errno==EINTR);
#endif
        if (result) {impl_->broken=true;ioError("Sync file");}
    }
}
bool Device::resize(std::int64_t count) {
    if (!isOpen() || !impl_->options.write || count<0) return false;
    auto lock=impl_->lock(); flush();const auto before=pos();
#ifdef _WIN32
    if (const auto error=_chsize_s(impl_->fd,count)) {impl_->broken=true;ioError("Resize file",int(error));}
#else
    if (ftruncate(impl_->fd,off_t(count))) {impl_->broken=true;ioError("Resize file");}
#endif
    return seek(std::min(before,count));
}
std::filesystem::perms Device::permissions() const {
    if (!isOpen()) return std::filesystem::perms::unknown;
#ifdef _WIN32
    struct _stat64 info{};if (_fstat64(impl_->fd,&info)) ioError("Read permissions");
#else
    struct stat info{};if (fstat(impl_->fd,&info)) ioError("Read permissions");
#endif
    return std::filesystem::perms(info.st_mode & 0777);
}
bool Device::setPermissions(std::filesystem::perms perms) {
    if (!isOpen()) return false;auto lock=impl_->lock();
#ifdef _WIN32
    FILE_BASIC_INFO info{};auto handle=reinterpret_cast<HANDLE>(_get_osfhandle(impl_->fd));
    if (!GetFileInformationByHandleEx(handle,FileBasicInfo,&info,sizeof(info))) throw FileError("Read file attributes failed");
    if ((perms & std::filesystem::perms::owner_write)==std::filesystem::perms::none) info.FileAttributes|=FILE_ATTRIBUTE_READONLY;
    else info.FileAttributes&=~FILE_ATTRIBUTE_READONLY;
    if (!info.FileAttributes) info.FileAttributes=FILE_ATTRIBUTE_NORMAL;
    if (!SetFileInformationByHandle(handle,FileBasicInfo,&info,sizeof(info))) throw FileError("Write file attributes failed");
#else
    if (fchmod(impl_->fd,mode_t(perms)&0777)) ioError("Set permissions");
#endif
    return true;
}
char* Device::map(std::int64_t offset,std::int64_t bytes,bool privateCopy) {
    if (!isOpen() || offset<0 || bytes<=0 || std::uint64_t(bytes)>SIZE_MAX || offset>INT64_MAX-bytes) throw FileError("Invalid mapping");
    auto lock=impl_->lock();flush();
    const auto lengthAvailable=size();
    if(offset>lengthAvailable||bytes>lengthAvailable-offset)throw FileError("Mapping exceeds file size");
#ifdef _WIN32
    SYSTEM_INFO system{};GetSystemInfo(&system);const auto alignment=std::uint64_t(system.dwAllocationGranularity);
#else
    const auto page=sysconf(_SC_PAGESIZE);if(page<=0)throw FileError("Page size unavailable");const auto alignment=std::uint64_t(page);
#endif
    const auto start=std::uint64_t(offset)/alignment*alignment,delta=std::uint64_t(offset)-start;
    if (std::uint64_t(bytes)>SIZE_MAX-delta) throw FileError("Mapping exceeds address space");
    const auto length=std::size_t(bytes)+std::size_t(delta);void* base;
#ifdef _WIN32
    auto mapping=CreateFileMappingW(reinterpret_cast<HANDLE>(_get_osfhandle(impl_->fd)),nullptr,privateCopy?PAGE_WRITECOPY:impl_->options.write?PAGE_READWRITE:PAGE_READONLY,0,0,nullptr);
    if(!mapping)throw FileError("Cannot create file mapping");
    base=MapViewOfFile(mapping,privateCopy?FILE_MAP_COPY:impl_->options.write?FILE_MAP_WRITE:FILE_MAP_READ,DWORD(start>>32),DWORD(start),length);CloseHandle(mapping);
    if(!base)throw FileError("Cannot map file");
#else
    base=mmap(nullptr,length,PROT_READ|((privateCopy||impl_->options.write)?PROT_WRITE:0),privateCopy?MAP_PRIVATE:MAP_SHARED,impl_->fd,off_t(start));
    if(base==MAP_FAILED)ioError("Map file");
#endif
    auto* visible=static_cast<char*>(base)+delta;
    try{impl_->mappings.push_back({visible,base,length});}catch(...){
#ifdef _WIN32
        UnmapViewOfFile(base);
#else
        munmap(base,length);
#endif
        throw;
    }return visible;
}
bool Device::unmap(char* address) {
    auto found=std::find_if(impl_->mappings.begin(),impl_->mappings.end(),[&](const auto& map){return map.visible==address;});if(found==impl_->mappings.end())return false;
#ifdef _WIN32
    if(!UnmapViewOfFile(found->base))throw FileError("Unmap file failed");
#else
    if(munmap(found->base,found->bytes))ioError("Unmap file");
#endif
    impl_->mappings.erase(found);return true;
}
} // namespace iiFileProvider::native
