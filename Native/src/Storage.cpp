#include "iiFileProviderStorage.h"
#include "iiFileProviderPresentation.h"
#include <sqlite3.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#endif
namespace iiFileProvider::native {
namespace {
// ios_base storage keeps the existing public stream object layout unchanged.
int presentationSlot() { static const int slot = std::ios_base::xalloc(); return slot; }
void streamEvent(std::ios_base::event event, std::ios_base &stream, int slot) {
    auto *p = static_cast<std::filesystem::path *>(stream.pword(slot));
    if (event == std::ios_base::erase_event) { delete p; stream.pword(slot) = nullptr; }
    else if (event == std::ios_base::copyfmt_event && p) stream.pword(slot) = new std::filesystem::path(*p);
}
void remember(std::ios_base &stream, const std::filesystem::path &path) {
    auto &slot = stream.pword(presentationSlot());
    if (!slot) { slot = new std::filesystem::path; stream.register_callback(streamEvent, presentationSlot()); }
    *static_cast<std::filesystem::path *>(slot) = std::filesystem::absolute(path);
}
void completed(std::ios_base &stream) noexcept {
    auto *path = static_cast<std::filesystem::path *>(stream.pword(presentationSlot()));
    if (path && !path->empty()) { Presentation::enqueue(*path); path->clear(); }
}
}
OutputFile::OutputFile(const std::filesystem::path &path, std::ios::openmode mode) { open(path, mode); }
OutputFile::~OutputFile() { try { if (is_open()) close(); else completed(*this); } catch (...) {} }
void OutputFile::open(const std::filesystem::path &path, std::ios::openmode mode) { std::ofstream::open(path, mode); if (is_open() && !fail()) remember(*this, path); }
void OutputFile::close() { const bool ok = bool(*this); std::ofstream::close(); if (ok && !fail()) completed(*this); }
ReadWriteFile::ReadWriteFile(const std::filesystem::path &path, std::ios::openmode mode) { open(path, mode); }
ReadWriteFile::~ReadWriteFile() { try { if (is_open()) close(); else completed(*this); } catch (...) {} }
void ReadWriteFile::open(const std::filesystem::path &path, std::ios::openmode mode) { std::fstream::open(path, mode); if (is_open() && !fail() && (mode & std::ios::out)) remember(*this, path); }
void ReadWriteFile::close() { const bool ok = bool(*this); std::fstream::close(); if (ok && !fail()) completed(*this); }
FILE *openStream(const char *path, const char *mode) { return std::fopen(path, mode); }
int openDatabase(const char *path,sqlite3 **database,int flags,const char *vfs) { return sqlite3_open_v2(path,database,flags,vfs); }
bool createDirectory(const std::filesystem::path &p) { return std::filesystem::create_directory(p); }
bool createDirectory(const std::filesystem::path &p, std::error_code &e) { return std::filesystem::create_directory(p,e); }
bool createDirectories(const std::filesystem::path &p) { return std::filesystem::create_directories(p); }
bool createDirectories(const std::filesystem::path &p, std::error_code &e) { return std::filesystem::create_directories(p,e); }
bool copyFile(const std::filesystem::path &s, const std::filesystem::path &d, std::filesystem::copy_options o) { const bool copied=std::filesystem::copy_file(s,d,o); if (copied) Presentation::enqueue(d); return copied; }
bool copyFile(const std::filesystem::path &s, const std::filesystem::path &d, std::filesystem::copy_options o, std::error_code &e) { const bool copied=std::filesystem::copy_file(s,d,o,e); if (copied && !e) Presentation::enqueue(d); return copied; }
void renameFile(const std::filesystem::path &s,const std::filesystem::path &d) { std::filesystem::rename(s,d); Presentation::enqueue(d); }
void renameFile(const std::filesystem::path &s,const std::filesystem::path &d,std::error_code &e) { std::filesystem::rename(s,d,e); if (!e) Presentation::enqueue(d); }
void createHardLink(const std::filesystem::path &s,const std::filesystem::path &d) { std::filesystem::create_hard_link(s,d); }
#ifndef _WIN32
int openFile(const char *p,int f,mode_t m) { return ::open(p,f,m); }
int openFileAt(int d,const char *p,int f,mode_t m) { return ::openat(d,p,f,m); }
int makeDirectory(const char *p,mode_t m) { return ::mkdir(p,m); }
int makeDirectoryAt(int d,const char *p,mode_t m) { return ::mkdirat(d,p,m); }
#else
HANDLE createFile(const wchar_t *p,DWORD a,DWORD s,LPSECURITY_ATTRIBUTES q,DWORD c,DWORD f,HANDLE t) { return CreateFileW(p,a,s,q,c,f,t); }
BOOL createDirectoryWindows(const wchar_t *p,LPSECURITY_ATTRIBUTES a) { return CreateDirectoryW(p,a); }
BOOL moveFile(const wchar_t *s,const wchar_t *d,DWORD f) { const auto moved=MoveFileExW(s,d,f); if (moved) Presentation::enqueue(std::filesystem::path(d)); return moved; }
NTSTATUS createFileNt(PHANDLE h,ACCESS_MASK a,POBJECT_ATTRIBUTES o,PIO_STATUS_BLOCK s,
    PLARGE_INTEGER z,ULONG attrs,ULONG share,ULONG disposition,ULONG options,PVOID ea,ULONG length) {
    return NtCreateFile(h,a,o,s,z,attrs,share,disposition,options,ea,length);
}
#endif
}
