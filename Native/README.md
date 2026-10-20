# iiFileProvider native component

Version 0.8.3 adds `SourceAccess`, an in-process native-identity access token for
in-place writers and read/copy scopes. Nested same-thread reads and hard links
share access; unrelated files remain independent. Idle streams do not hold it.
Domain renderers acquire access only for their source reads, then render detached
data. Presentation releases its intake scope across a renderer callback, reacquires
it before publication, and rejects changed source versions. A regression pauses a
renderer, accepts a concurrent scoped write and rejects its obsolete preview.
Concurrent requests for the same unchanged source can reuse the first completed
projection. Normalized provider attribute changes are distinguished from external
edits; they do not cause a valid second preview request to fail.

The C++23 build uses headers and translation units, with named-module scanning
disabled. This supports Android NDK toolchains that do not ship clang-scan-deps;
it does not change the language level or runtime behavior.

Version 0.8.1 retains `File::create`, `File::update` and `File::remove` for the Qt-free
iiCSMIDI document consumer. `create` writes complete staged bytes, then publishes
with an exclusive same-directory hard link: an existing file/symlink/directory
is never replaced. Filesystems lacking hard links fail explicitly; no partial or
direct-write fallback is used. `update` checks expected bytes and atomically
replaces the file using the existing publication path. It does not lock external
writers across checking/publication. `remove` removes one file/symlink, refuses
directories, and drains presentation work/removes its companion. These functions
accept native filesystem paths and preserve binary NUL in file content while
rejecting NUL in paths. FileCode distinguishes existing destinations, conflicts,
invalid paths, limits and I/O failure. Provider presentation is enqueued for
visible completed files; private/hidden staging trees remain excluded. CRUD,
preservation, conflict and presentation regression checks run in native_test.cpp.
Core/JSON remain outside the Native dependency graph.

C++23 byte-oriented file CRUD, atomic staged publication, scoped temporary directories,
credential-free file-author snapshots and schema-v1/v2/v3 authorship ledgers. Public
interfaces contain only standard types. Existing Qt adapters and object-store services
remain separate. JSON uses pinned nlohmann-json and Unicode normalization uses utf8proc;
full notices are preserved. SHA-256 is first-party standard C++ with preserved provenance.

Configure `Native` into `build/native` and install alongside iiFileProvider. Consumers use
`find_package(iiFileProviderNative 0.8 CONFIG REQUIRED)` and `iiFileProvider::Native`.
Old schema-v3 profiles, nested details, attribution, device and named links are retained;
reading a ledger clears its active identity. Rejected mutations leave its dump unchanged.
Physical writes use reserved files in the destination directory and preserve permissions.
The native component shares the existing temporary lease with storage cleanup services.

`iiFileProviderPresentation.h` provides native detail extraction, PNG thumbnails,
UTF-8 text cards, PCM waveform previews, domain renderer registration, stable
thumbnail change tokens and recursive intake. Apple builds use Image I/O and
Quick Look Thumbnailing; the portable image backend uses SHA-256-pinned stb
sources already reviewed in iiPaintEngine. No Qt, Python, VM or higher SDK is
linked into this component. See `../docs/FILE_PRESENTATION.md` in the source tree.


The authored code is C++23 with extensions disabled. nlohmann/json 3.12.0 and
utf8proc 2.11.3 sources/licenses are vendored and SHA-256 checked during CMake
configuration. utf8proc retains its upstream C source. The component keeps the
provider's existing MIT license; dependency licenses are installed.
New POSIX files use ordinary `0666 & umask` permissions; replacement preserves
the destination's permissions. Directory cleanup is scoped to each owned random
directory, and staged files participate in the provider's shared temporary lease.

`Native.AuthorLinks` verifies relative/file/custom/signed URL text, malformed
URLs, Unicode email lowercase behavior, strict schema types, legacy ledgers and
UTC offset normalization. `IIFILEPROVIDER_NATIVE_SANITIZERS=ON` enables address and
undefined-behavior instrumentation in a build directory such as
`build/native-sanitized`. The module never resolves Qt during configuration.

## Native storage adapters (0.6)

`iiFileProviderStorage.h` provides C++23 output/read-write streams, native
descriptor-relative operations and SQLite opening without Qt. The C ABI in
`FileProviderC.h` supports existing Python workers with typed error codes,
suffix-preserving staging and final publication. Native dependencies include
SQLite >= 3.27 and the pinned JSON/Unicode code already vendored here.
The native target is static for iOS/WASM and shared for desktop/Android.
Use the root SDK installation to obtain Native, Catalog, Objects, Temporary,
Python, Swift and Android support together.
The native package exports `iiFileProviderNative_PYTHON_DIRECTORY` and
`iiFileProviderNative_LICENSE_DIRECTORY` for runtime packaging. Existing 0.5
native consumers remain compatible; new storage adapters require 0.6.

On MSVC, each exported `Authorship` compile-time limit has its own declaration.
`Native.AuthorLinks` checks the byte and link limits at compile time as well as
exercising author metadata through the imported native library.
The relocated bundled-SQLite consumer test prepends its own installed `bin/`
directory on Windows so DLL loading also exercises the relocated package.
Unicode filesystem fixtures use UTF-8 path literals explicitly on Windows.
Native and device contract executables print caught failures before returning a
nonzero status, so Windows test logs retain the original error information.
Windows native read handles share deletion, allowing an atomic replacement while
existing readers retain the old file. The native contract checks both old-reader
bytes and replacement bytes. Existing Windows files use `ReplaceFileW` to retain
that behavior with a live reader. Windows writes drain pending presentation work for
the destination before replacement so NTFS metadata streams cannot block commit.
The presentation contract compares source and derived-file Windows DACLs; POSIX
builds retain the owner-only mode check. Windows permission bits do not represent
NTFS access-control entries.
The catalog safety test verifies Windows denial when an open SQLite connection
prevents renaming its state directory and checks that the original catalog stays
intact. Filesystems that allow the rename retain the foreign-directory identity
rejection check.
Windows stream renames also drain pending presentation work for both paths; the
native contract closes an OutputFile and immediately replaces an existing file.
The public storage header forward-declares NT syscall pointer types. Only the
implementation includes winternl.h, so consumers can include Dokan's NT types.
Windows installations also colocate an external SQLite DLL with the native
provider when it is available beside the imported SQLite library. The installed
runtime test loads a staged Native DLL with Python's normal safe DLL search mode.
## Coordinated source access

`SourceAccess` is a native C++23 access token for an existing regular file. A
document owner uses `std::lock_guard<SourceAccess>` across an in-place authoring
transaction; provider `ReadFile` operations and `Presentation::prepare/attach`
use the same source identity. Idle open streams do not retain a lock. Hard
links share ownership, nested reads on one thread are allowed, and unrelated
files use independent locks. Weak identity entries are retired after their last
token, keeping the registry bounded by live access tokens.

The scope does not replace operating-system database locks or coordinate other
processes. Document adapters must opt into it for their writes; a provider read
scope alone cannot coordinate an unadapted external writer. This prevents the
provider's own preview reads and descriptor closes from overlapping an opted-in
authoring transaction. SQLite documents why raw descriptor closes during an open
transaction are unsafe in its [locking guidance](https://sqlite.org/howtocorrupt.html#posix_advisory_locks_canceled_by_a_separate_thread_doing_close).

`iiFileProvider.Native.SourceAccess` exercises blocked same-file reads,
independent other-file reads, reentrancy, hard-link identity and completion after
the authoring scope is released. It runs without Qt.
