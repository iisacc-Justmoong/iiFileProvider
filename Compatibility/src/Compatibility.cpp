#include <array>
#include <charconv>
#include <fstream>
#include <iiFileProviderCompatibility.h>
#include <iiFileProviderNative.h>
#include <limits>
#include <sstream>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace iiFileProvider::native {
namespace fs = std::filesystem;
namespace {
std::string utf8(const fs::path &path) {
  auto bytes = path.u8string();
  return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}
std::string host_name() {
#ifdef _WIN32
  std::array<wchar_t, 256> name{};
  DWORD size = name.size();
  if (!GetComputerNameExW(ComputerNameDnsHostname, name.data(), &size))
    throw iiFileProvider::native::FileError("Cannot identify the lock host");
  return utf8(fs::path(std::wstring(name.data(), size)));
#else
  std::array<char, 256> name{};
  if (::gethostname(name.data(), name.size() - 1) != 0)
    throw iiFileProvider::native::FileError("Cannot identify the lock host");
  return name.data();
#endif
}
std::uint64_t process_id() {
#ifdef _WIN32
  return GetCurrentProcessId();
#else
  return ::getpid();
#endif
}
std::string process_name() {
  fs::path path;
#ifdef _WIN32
  std::vector<wchar_t> buffer(32768);
  const auto size = GetModuleFileNameW(nullptr, buffer.data(), buffer.size());
  if (size && size < buffer.size())
    path = std::wstring(buffer.data(), size);
#elif defined(__APPLE__)
  std::uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buffer(size);
  if (_NSGetExecutablePath(buffer.data(), &size) == 0)
    path = buffer.data();
#else
  std::error_code link_error;
  path = fs::read_symlink("/proc/self/exe", link_error);
#endif
  if (path.empty())
    throw iiFileProvider::native::FileError("Cannot identify the lock process");
  std::error_code ec;
  const auto resolved = fs::weakly_canonical(path, ec);
  return utf8((ec ? path : resolved).filename());
}
bool dead_owner(std::istream &input, const std::string &host) {
  std::string pid_text, application, owner_host;
  if (!std::getline(input, pid_text) || !std::getline(input, application) ||
      !std::getline(input, owner_host) || owner_host != host)
    return false;
  std::uint64_t pid = 0;
  const auto parsed =
      std::from_chars(pid_text.data(), pid_text.data() + pid_text.size(), pid);
  if (parsed.ec != std::errc{} ||
      parsed.ptr != pid_text.data() + pid_text.size() || pid == 0 ||
      pid > std::numeric_limits<std::int32_t>::max())
    return false;
#ifdef _WIN32
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               static_cast<DWORD>(pid));
  if (!process)
    return GetLastError() == ERROR_INVALID_PARAMETER;
  DWORD result = STILL_ACTIVE;
  const bool exited =
      GetExitCodeProcess(process, &result) && result != STILL_ACTIVE;
  CloseHandle(process);
  return exited;
#else
  return ::kill(static_cast<pid_t>(pid), 0) == -1 && errno == ESRCH;
#endif
}
bool dead_local_owner(const fs::path &lock, const std::string &host) {
  std::error_code ec;
  if (fs::is_symlink(lock, ec) || !fs::is_regular_file(lock, ec) || ec ||
      fs::file_size(lock, ec) > 4096 || ec)
    return false;
  std::ifstream input(lock);
  return dead_owner(input, host);
}
// Reclaim only a proven dead local owner. Active Qt/native owners retain a
// platform lock/handle, and identity checks prevent unlinking a replaced path.
bool reclaim(const fs::path &path, const std::string &host) {
  if (!dead_local_owner(path, host))
    return false;
#ifdef _WIN32
  HANDLE file =
      CreateFileW(path.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr,
                  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  std::array<char, 4097> bytes{};
  DWORD size = 0;
  const bool read =
      ::ReadFile(file, bytes.data(), bytes.size(), &size, nullptr);
  std::istringstream contents(std::string(bytes.data(), size));
  FILE_DISPOSITION_INFO disposition{TRUE};
  const bool removed =
      read && size < bytes.size() && dead_owner(contents, host) &&
      SetFileInformationByHandle(file, FileDispositionInfo, &disposition,
                                 sizeof(disposition));
  CloseHandle(file);
  return removed;
#else
  const int file = ::open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
  if (file < 0)
    return false;
  struct stat opened{}, current{};
  const bool removed =
      ::flock(file, LOCK_EX | LOCK_NB) == 0 && ::fstat(file, &opened) == 0 &&
      ::lstat(path.c_str(), &current) == 0 && opened.st_dev == current.st_dev &&
      opened.st_ino == current.st_ino && dead_local_owner(path, host) &&
      ::unlink(path.c_str()) == 0;
  ::close(file);
  return removed;
#endif
}
} // namespace
struct FileWriteLock::Private final {
  fs::path path;
#ifdef _WIN32
  HANDLE file = INVALID_HANDLE_VALUE;
#else
  int file = -1;
#endif
  bool owned = false;
  ~Private() {
#ifdef _WIN32
    if (file != INVALID_HANDLE_VALUE) {
      // Mark this exact open file for deletion before closing its handle.
      if (owned) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        SetFileInformationByHandle(file, FileDispositionInfo, &disposition,
                                   sizeof(disposition));
      }
      CloseHandle(file);
    }
#else
    if (file >= 0) {
      if (owned) {
        struct stat opened{}, current{};
        if (::fstat(file, &opened) == 0 &&
            ::lstat(path.c_str(), &current) == 0 &&
            opened.st_dev == current.st_dev && opened.st_ino == current.st_ino)
          ::unlink(path.c_str());
      }
      ::close(file);
    }
#endif
  }
};
FileWriteLock::FileWriteLock(const fs::path &destination)
    : p_(std::make_unique<Private>()) {
  if (destination.empty() || destination.filename().empty() ||
      destination.native().find(fs::path::value_type{}) !=
          fs::path::string_type::npos)
    throw iiFileProvider::native::FileError("An output filename is required");
  p_->path = fs::absolute(destination);
  p_->path += ".iisacc-lock";
  const auto host = host_name();
  const auto metadata = std::to_string(process_id()) + '\n' + process_name() +
                        '\n' + host + "\n\n\n";
  for (int attempt = 0; attempt < 2; ++attempt) {
#ifdef _WIN32
    p_->file =
        CreateFileW(p_->path.c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ,
                    nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (p_->file != INVALID_HANDLE_VALUE)
#else
    p_->file =
        ::open(p_->path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (p_->file >= 0)
#endif
    {
      p_->owned = true;
#ifdef _WIN32
      DWORD written = 0;
      if (!::WriteFile(p_->file, metadata.data(), metadata.size(), &written,
                       nullptr) ||
          written != metadata.size())
        throw iiFileProvider::native::FileError("Cannot write lock ownership");
#else
      if (::flock(p_->file, LOCK_EX | LOCK_NB) != 0)
        throw iiFileProvider::native::FileError("Cannot lock the output guard");
      std::string_view remaining(metadata);
      while (!remaining.empty()) {
        const auto written =
            ::write(p_->file, remaining.data(), remaining.size());
        if (written < 0 && errno == EINTR)
          continue;
        if (written <= 0)
          throw iiFileProvider::native::FileError(
              "Cannot write lock ownership");
        remaining.remove_prefix(static_cast<std::size_t>(written));
      }
#endif
      return;
    }
    if (attempt || !reclaim(p_->path, host))
      break;
  }
  throw iiFileProvider::native::FileError(
      "Another file operation owns this output path");
}
FileWriteLock::~FileWriteLock() = default;
void writeCompatible(const fs::path &destination, std::string_view bytes) {
  if (!destination.parent_path().empty())
    iiFileProvider::native::File::createDirectories(destination.parent_path());
  FileWriteLock lock(destination);
  iiFileProvider::native::File::write(destination, bytes);
}
} // namespace iiFileProvider::native
