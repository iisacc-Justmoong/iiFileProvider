# iiFileProvider native component

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
provider's existing AGPL-3.0-only license; dependency licenses are installed.
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
