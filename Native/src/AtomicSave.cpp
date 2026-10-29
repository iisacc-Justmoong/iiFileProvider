#include "iiFileProviderNativeDevice.h"
#include "iiFileProviderPresentation.h"
#include "AtomicPublication.h"
#include "TemporaryLease.h"
#include <mutex>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace iiFileProvider::native {
namespace {
using atomic_detail::checked;
using atomic_detail::moveFile;
std::filesystem::perms newPermissions(const std::filesystem::path& stage) {
#ifdef _WIN32
    (void)stage;return std::filesystem::perms(0666);
#else
    // Read the actual umask-filtered mode without changing the process umask.
    auto probe=stage;probe+=".permission-probe";
    int fd=::open(probe.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0666);
    if(fd<0)throw FileError("Cannot reserve permission probe");
    struct stat info{};const auto status=fstat(fd,&info);const auto removed=unlink(probe.c_str());const auto closed=::close(fd);
    if(status||removed||closed)throw FileError("Cannot inspect/retire permission probe");
    return std::filesystem::perms(info.st_mode&0777);
#endif
}
}
PublishResult publishFile(const std::filesystem::path& stageName,const std::filesystem::path& destinationName,
                         bool overwrite,std::optional<std::filesystem::perms> permissions) {
    try {
        const auto stage=checked(stageName),destination=checked(destinationName);
        const auto source=std::filesystem::symlink_status(stage);
        if(stage==destination||!std::filesystem::is_regular_file(source))return {false,"invalid_source","Publication requires a distinct regular staged file"};
        std::error_code error;const auto target=std::filesystem::symlink_status(destination,error);
        if(error&&error!=std::errc::no_such_file_or_directory)return {false,"destination_status_failed",error.message()};
        if(std::filesystem::is_symlink(target)||(std::filesystem::exists(target)&&!std::filesystem::is_regular_file(target)))
            return {false,"invalid_path","Destination must be a regular file, not a link or directory"};
        if(!overwrite&&std::filesystem::exists(target))return {false,"already_exists","Destination already exists"};
#ifdef _WIN32
        Presentation::cancelPending(stage);Presentation::cancelPending(destination);
#endif
        std::unique_ptr<SourceAccess> access;
        if(std::filesystem::exists(target))access=std::make_unique<SourceAccess>(destination);
        auto lock=access?std::unique_lock<SourceAccess>(*access):std::unique_lock<SourceAccess>();
        const auto mode=permissions?*permissions:std::filesystem::exists(target)?target.permissions():newPermissions(stage);
        std::filesystem::permissions(stage,mode,std::filesystem::perm_options::replace,error);
        if(error)return {false,"temporary_permissions_failed",error.message()};
        error=moveFile(stage,destination,overwrite);
        if(error)return {false,error==std::errc::file_exists?"already_exists":"atomic_commit_failed",error.message()};
        // Publication has already happened. A deferred preview failure must
        // never report that the original file is still in place.
        try{Presentation::enqueue(destination);}catch(...){}
        return {true,{},{}};
    }catch(const std::exception& error){return {false,"atomic_commit_failed",error.what()};}
}
PublishResult renameFileExclusive(const std::filesystem::path& sourceName,const std::filesystem::path& destinationName) {
    try {
        const auto source=checked(sourceName),destination=checked(destinationName);
        const auto status=std::filesystem::symlink_status(source);
        if(source==destination||(!std::filesystem::is_regular_file(status)&&!std::filesystem::is_symlink(status)))
            return {false,"invalid_source","Rename requires a distinct file or link"};
        if(std::filesystem::exists(std::filesystem::symlink_status(destination)))
            return {false,"already_exists","Destination already exists"};
#ifdef _WIN32
        Presentation::cancelPending(source);Presentation::cancelPending(destination);
#endif
        const auto error=moveFile(source,destination,false);
        if(error==std::errc::cross_device_link) {
            // Cross-volume rename is copy + exclusive publication + removal.
            // Preserve the source on every failure before complete publication.
            if(std::filesystem::is_symlink(status))std::filesystem::create_symlink(std::filesystem::read_symlink(source),destination);
            else {
                Device input(false);input.open(source,{.read=true});
                AtomicSave output;output.open(destination);
                std::vector<char> bytes(1024*1024);
                for(;;){const auto count=input.read(bytes.data(),bytes.size());if(!count)break;output.device().write(bytes.data(),count);}
                output.setPermissions(status.permissions());const auto published=output.commit(false);
                if(!published.succeeded)return published;
            }
            std::error_code removed;std::filesystem::remove(source,removed);
            if(removed)return {false,"source_cleanup_failed",removed.message()};
        } else if(error)return {false,error==std::errc::file_exists?"already_exists":"rename_failed",error.message()};
        try{Presentation::removeCompanion(source);Presentation::enqueue(destination);}catch(...){}
        return {true,{},{}};
    }catch(const std::exception& error){return {false,"rename_failed",error.what()};}
}
struct AtomicSave::Impl {
    Device output{false};
    std::filesystem::path destination,stage;
    std::unique_ptr<iiFileProvider::TemporaryLease> lease;
    std::optional<std::filesystem::perms> permissions;
    bool cancelled=false,committed=false;
    void discard() noexcept {
        try{output.close();}catch(...){}
        if(!stage.empty()){std::error_code ignored;std::filesystem::remove(stage,ignored);stage.clear();}
        lease.reset();
    }
};
AtomicSave::AtomicSave():impl_(std::make_unique<Impl>()){}
AtomicSave::~AtomicSave(){impl_->discard();}
Device& AtomicSave::device()noexcept{return impl_->output;}
const std::filesystem::path& AtomicSave::stagedPath()const noexcept{return impl_->stage;}
void AtomicSave::open(const std::filesystem::path& name) {
    if(impl_->output.isOpen())throw FileError("Atomic output is already open");
    impl_->discard();impl_->destination=checked(name);impl_->cancelled=false;impl_->committed=false;impl_->permissions.reset();
    const auto parent=impl_->destination.parent_path();
    if(!std::filesystem::is_directory(parent))throw FileError("Atomic output parent does not exist",FileCode::InvalidPath);
    const auto status=std::filesystem::symlink_status(impl_->destination);
    if(std::filesystem::is_symlink(status)||(std::filesystem::exists(status)&&!std::filesystem::is_regular_file(status)))
        throw FileError("Atomic output must be a regular file",FileCode::InvalidPath);
    impl_->lease=std::make_unique<iiFileProvider::TemporaryLease>(std::filesystem::canonical(parent),iiFileProvider::TemporaryLease::Mode::Shared);
    try{auto pattern=parent/std::filesystem::path(".iifile-stage-XXXXXX");pattern+=impl_->destination.extension();impl_->output.openTemporary(pattern);impl_->stage=impl_->output.path();}
    catch(...){impl_->discard();throw;}
}
void AtomicSave::cancel()noexcept{impl_->cancelled=true;impl_->discard();}
bool AtomicSave::setPermissions(std::filesystem::perms value){if(!impl_->output.setPermissions(value))return false;impl_->permissions=value;return true;}
PublishResult AtomicSave::commit(bool overwrite) {
    if(impl_->committed)return {false,"already_published","Atomic output was already published"};
    if(impl_->cancelled||!impl_->output.isOpen()||impl_->output.failed()){impl_->discard();return {false,"cancelled_or_failed","Atomic output was cancelled, closed or failed"};}
    try{
        impl_->output.flush(true);impl_->output.close();
        auto result=publishFile(impl_->stage,impl_->destination,overwrite,impl_->permissions);
        if(result.succeeded){impl_->committed=true;impl_->stage.clear();impl_->lease.reset();}
        else impl_->discard();return result;
    }catch(const std::exception& error){impl_->discard();return {false,"write_failed",error.what()};}
}
} // namespace iiFileProvider::native
