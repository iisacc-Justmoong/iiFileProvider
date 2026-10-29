#include "FileProviderC.h"
#include "iiFileProviderStorage.h"
#include "iiFileProviderNative.h"
#include "iiFileProviderPresentation.h"
#include <cerrno>
#include <algorithm>
#ifdef _WIN32
#include <io.h>
#endif
namespace {
thread_local std::string error;
thread_local int errorNumber = 0;
auto path(const char *p) { if (!p || !*p) throw std::invalid_argument("Empty file path"); return std::filesystem::u8path(p); }
template<class F> int status(F f) noexcept {
    try { f(); error.clear(); errorNumber=0; return 0; }
    catch (const std::filesystem::filesystem_error &e) { error=e.what(); errorNumber=e.code().default_error_condition().value(); return -1; }
    catch (const std::exception &e) { error=e.what(); errorNumber=EIO; return -1; }
}
struct Stage {
    iiFileProvider::native::StagedFile file;
    std::filesystem::path destination;
    std::string text;
    explicit Stage(const char *p) : file(path(p)), destination(std::filesystem::absolute(path(p))) {
        const auto utf8=file.path().u8string(); text.assign(utf8.begin(),utf8.end());
    }
};
}
extern "C" {
const char *iifp_last_error() { return error.c_str(); }
int iifp_last_errno() { return errorNumber; }
int iifp_open(const char *p,int f,int m) {
    try {
        auto name=path(p);
#ifdef _WIN32
        return _wopen(name.c_str(),f,m);
#else
        return iiFileProvider::native::openFile(name.c_str(),f,static_cast<mode_t>(m));
#endif
    } catch (const std::exception &e) { error=e.what(); errno=EINVAL; return -1; }
}
int iifp_mkdir(const char *p,int recursive) { return status([&] {
    if (recursive) iiFileProvider::native::createDirectories(path(p));
    else if (!iiFileProvider::native::createDirectory(path(p)))
        throw std::filesystem::filesystem_error("Directory already exists", std::make_error_code(std::errc::file_exists));
}); }
int iifp_copy(const char *s,const char *d,int overwrite) { return status([&] {
    iiFileProvider::native::StagedFile stage(path(d));
    std::filesystem::copy_file(path(s),stage.path(),std::filesystem::copy_options::overwrite_existing);
    if (!overwrite) {
        std::filesystem::create_hard_link(stage.path(),path(d));
    } else {
        auto result=stage.publish(); if (!result.succeeded) throw std::runtime_error(result.message);
    }
    iiFileProvider::native::Presentation::enqueue(path(d));
}); }
int iifp_publish(const char *s,const char *d,int overwrite) { return status([&] {
    if (overwrite) {
        // Publication across private staging subdirectories on one filesystem
        // retains os.replace semantics used by existing model pipelines.
        iiFileProvider::native::renameFile(path(s),path(d));
    } else { std::filesystem::create_hard_link(path(s),path(d)); }
    iiFileProvider::native::Presentation::enqueue(path(d));
}); }
void *iifp_stage(const char *p) {
    try { auto result=new Stage(p); error.clear(); return result; }
    catch (const std::exception &e) { error=e.what(); return nullptr; }
}
const char *iifp_stage_path(void *p) { return p ? static_cast<Stage *>(p)->text.c_str() : ""; }
int iifp_stage_publish(void *p,int overwrite) { return status([&] {
    if (!p) throw std::invalid_argument("Missing stage");
    auto &stage=*static_cast<Stage *>(p);
    if (!overwrite) std::filesystem::create_hard_link(stage.file.path(),stage.destination);
    else { const auto result=stage.file.publish(); if (!result.succeeded) throw std::runtime_error(result.message); }
    iiFileProvider::native::Presentation::enqueue(stage.destination);
}); }
void iifp_stage_discard(void *p) { delete static_cast<Stage *>(p); }
void iifp_present_later(const char *p) { try { iiFileProvider::native::Presentation::enqueue(path(p)); } catch (...) {} }
int iifp_present(const char *p) { return status([&] { (void)iiFileProvider::native::Presentation::prepare(path(p)); }); }
const char *iifp_presentation(const char *p) {
    thread_local std::string document;
    try { auto value=iiFileProvider::native::Presentation::load(path(p)); if (!value) return nullptr; document=value->document; return document.c_str(); }
    catch (...) { return nullptr; }
}
int iifp_presentation_wait(int timeout) { return iiFileProvider::native::Presentation::waitForIdle(std::chrono::milliseconds(std::max(0,timeout))) ? 1 : 0; }
void iifp_presentation_cancel(const char *p) { try { iiFileProvider::native::Presentation::cancelPending(path(p)); } catch (...) {} }
const char *iifp_cache_change_token(const char *p) {
    thread_local std::string token;
    if (status([&] { token=iiFileProvider::native::Presentation::cacheChangeToken(path(p)); }) < 0) return nullptr;
    return token.c_str();
}
const char *iifp_normalize_change_time(uint64_t device, uint64_t index, int64_t seconds, int64_t nanoseconds) {
    thread_local std::string token;
    if(status([&] { token = iiFileProvider::native::Presentation::normalizeChangeTime(device, index, seconds, nanoseconds); }) < 0) return nullptr;
    return token.c_str();
}
}
