# iiFileProvider

## C++23 Society object store

`iiFileProvider::Objects` provides a Qt-independent transactional object package:
stable object/index keys, directory-path projections, SHA-256, attribution,
versioned binary diffs, mutation journals and work sessions. See
[the object-store contract and integration boundaries](docs/OBJECT_STORE.md).
The existing Qt APIs below are preserved; this new target does not depend on them.

A dynamic library, version 0.5.0, using C++20 and Qt 6.8.3 Core. `File` handles format-independent file CRUD, and `Database` handles SQLite storage transactions. `FileAuthor` extends the iisacc.com account model and records a file author's identity, detailed profile, contributions, and authoring device. `Authorship` keeps one initial editor and the subsequent editing-participant list as persistent metadata. `FileLink` represents an optional file link as a name/URL pair, while `AuthenticationToken` holds a separate runtime authentication token. Metadata value objects do not perform file I/O directly. Login HTTP requests and JWT signature verification are outside this SDK's role.

The public repository is [iisacc-Justmoong/iiFileProvider](https://github.com/iisacc-Justmoong/iiFileProvider). On 2026-09-07, SDK identifiers in headers, namespaces, the CMake package, shared library, and installation paths were unified as `iiFileProvider`. Consumers must use the new headers and CMake target below and reconfigure their existing build caches.

<a id="공개-api"></a>

## Public API

Using `<src/iiFileProvider.h>`, the author model, file links, and authentication tokens can be used. The public headers are `src/FileAuthor.h`, `src/AuthenticationToken.h`, `src/Authorship.h`, and `src/FileLink.h`. `Authorship` stores the distinction between the first editor and participants, the first and recent contribution times per author, file links, and change numbers, and updates the JSON dump immediately after changes. Classes are C++ value objects, not QObject, and do not depend on the lifespan of Qt Network or account managers.

```cpp
#include <iiFileProvider.h>

QString error;
auto author = iiFileProvider::FileAuthor::fromIisaccAccount(
    accountJson, QUrl("https://iisacc.com"), QDateTime::currentDateTimeUtc(), &error);
if (author) {
    auto metadata = author->metadata();
    metadata.details.organization = "Example Studio";
    metadata.attribution.roles = {"creator", "editor"};
    // Specify the actual file creation time known to the host.
    metadata.attribution.createdAt = actualFileCreationTime;
    if (author->setMetadata(metadata, &error)) {
        const QJsonObject fileMetadata = author->toJson(); // Excludes tokens and login sessions
    }
}
```

`fromIisaccAccount()` receives the account object, and `fromIisaccAppSession()` receives the `{account, session, ...}` app response. The latter also checks the app device and session time. `fromJson()` reads file author metadata with a specified version. All return errors with `std::nullopt` and value not included.

Specify token type, account subject, service origin, issue/expiration time, and token raw text at `AuthenticationToken::create()`. The created token is linked to the author object at `setAuthenticationToken()`. Only `secret()` returns the raw text, and `toJson()` and debug output do not include the raw text. `isWithinValidityWindow(at)` is a time range check and does not prove authentication success or access rights. iisacc .com app's token is in HttpOnly cookie, not JSON response, so the host's authentication layer must manage it separately.

Field list, observed server files, validation conditions, and examples are in [file author contract](docs/FILE_AUTHOR_CONTRACT.md).

<a id="최초-편집자와-편집-참여자"></a>

### First editor and editing participants

The first successful `setAuthor()` fixes one first editor. Subsequently, the first appearing account is added once at the end of the participant list. Account distinction is a combination of service origin and `sub`, and changing display name or email does not create a new participant. The first editor is not duplicated in the participant list.

```cpp
iiFileProvider::Authorship history;
history.setAuthor(firstAuthor, firstEditTime); // Host-provided FileAuthor and actual recording time
history.setAuthor(collaborator, nextEditTime);

const std::optional<iiFileProvider::FileAuthor> original = history.firstEditor();
const QList<iiFileProvider::FileAuthor> participants = history.participants();
history.clearActiveAuthor(); // Clears only the current editing context and retains the participant list.

const QByteArray metadata = history.dump(); // The host stores this in the file metadata.
auto restored = iiFileProvider::Authorship::fromDump(metadata);
```

Query results are copies without authentication tokens. APIs for deleting, resetting, or swapping roles in the list do not exist, and profile updates for the same account also preserve the first editor and participant order and initial contribution time. Up to 256 entries (including the first editor) and full metadata 2 MiB limit are reached, existing records are not deleted, and new registrations are atomically rejected. Lists filling up to the existing limit of schema 1 · 2 are also transformed while preserving full records. Files with only unspecified author changes have no first editor and an empty participant list.

Save schema 3 stores the first editor key or null at `firstEditor`, subsequent participant key list at `participants`, each key's profile and contribution time at `authors`, and optional name and URL list at `links`. Schema 1 · 2 input reads preserving the existing list and revision and adding an empty link list. Consumers must rebuild with iiFileProvider 0.4.0 or higher headers and libraries. Shared library ABI identifier also changed to `0.4` as per link save member addition. Preservation contract applies to APIs using the same file's `Authorship` and save/restore paths, and is not a function to prevent external file tampering or replacement with separate empty values.

<a id="이름과-url-파일-메타데이터"></a>

### Name and URL file metadata

Create links with `FileLink::fromString("[Name|URL]")` or `FileLink::create(name, QString/QUrl)` and pass as an additional argument during author registration. HTTP (S) specific restrictions do not exist, and Society address, local files, SMB · IPFS · URN ·app scheme, and relative URLs can be received. URLs received as strings preserve case and encoding original text and are queried with `urlText()`.

```cpp
auto address = iiFileProvider::FileLink::fromString("[Society original|society:document-1]");
if (address) {
    history.setAuthor(author, {*address}, actualEditTime);
}
// The entire file-link list can also be updated separately from author registration.
history.setLinksFromStrings({"[Original|file:///files/original.png]", "[Reference|../reference.svg]"});
const auto fileLinks = history.links();
const auto metadata = history.dump();
```

Existing `setAuthor(author, at)` with omitted additional arguments maintains the link. Explicit empty link lists empty only URL metadata while maintaining the permanent editor list. Links do not automatically execute or query registered URLs. Supported formats, encodings, atomic updates, and version contracts are in the [file link document](docs/FILE_LINKS.md).

The following bootstrap API is also maintained for compatibility with existing consumers.

```cpp
#include <iiFileProvider.h>

const QString message = iiFileProvider::helloWorld();
```

`[[nodiscard]] QString iiFileProvider::helloWorld()` returns `Hello world!` on every call. Public headers and implementation are placed together in the source root. External dependencies are the existing Qt 6.8.3 Core, and no new external libraries were introduced. Qt usage and distribution conditions follow the installed Qt license.

<a id="빌드-테스트-설치"></a>

## Build, test, install

CMake 3.24 or higher, C++20 compiler, and Qt 6.8.3 are required. macOS automatically adds `/Volumes/Storage/Qt/6.8.3/macos` to the search path if it exists.

```sh
./install.sh
```

When configured as a standalone project, the default installation path is set, so the installation path of the parent project included via `add_subdirectory()` is maintained.

The script executes Release build and CTest from `build/`, installs to the default path `~/.local/SDK/iiFileProvider`, and builds and tests a separate executable that uses only the installed CMake package from `build/consumer/build/`. The bootstrap test checks return strings, C++20 compile settings, Qt 6.8.3 header version, and runtime version. The author contract test checks account mapping, detailed metadata round-trip, Unicode, invalid formats, session expiration, token binding, original exclusion, and atomic updates from both source and installed consumers. The `Authorship` test checks initial editor fixation, participant order, duplicate prevention, profile update, query copy, file restore after save, schema 1, 2 compatibility, invalid role reference, and record preservation on failure and limit exceedance from both sides. `FileLink` tests verify atomic recording of various schemes, relative addresses, string/JSON/file round-trips, encoding, additional arguments, link omission, clearing, and list preservation. Only Qt Test is used for test builds.

The installer consumer configuration explicitly specifies the package directory of the current installation path, so changing `INSTALL_PREFIX` and re-running does not reuse the previous package cache.

Settings are passed as environment variables instead of command-line arguments. `INSTALL_PREFIX` must be an absolute path, and `CMAKE_PREFIX_PATH` receives additional search paths separated by semicolons or colons. The number of parallel builds is specified as `CMAKE_BUILD_PARALLEL_LEVEL`, with a default of 2.

```sh
QT_PREFIX_PATH="/Volumes/Storage/Qt/6.8.3/macos" \
INSTALL_PREFIX="$HOME/.local/SDK/iiFileProvider" \
./install.sh
```

Even in manual execution, the build directory uses `build/`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="/Volumes/Storage/Qt/6.8.3/macos"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release
cmake -S tests/consumer -B build/consumer/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/.local/SDK/iiFileProvider;/Volumes/Storage/Qt/6.8.3/macos"
cmake --build build/consumer/build --config Release
ctest --test-dir build/consumer/build -C Release --output-on-failure
```

<a id="설치-결과와-소비"></a>

## Installation results and consumption

The default installation path generates `include/` umbrella, author, file link, authentication token, and export header, `lib/` shared libraries, `lib/cmake/iiFileProvider/` CMake package, and `share/iiFileProvider/` README and contract documents. Private `src/JsonContract.h` is not installed. The Windows shared library executable is installed to `bin/`. Consumers are informed of C++20 and `Qt6::Core` link requirements. Qt is not bundled and copied, and the installed Qt runtime is required. The shared library installation RPATH includes the external library paths used in the link.

```cmake
find_package(iiFileProvider 0.5.0 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE iiFileProvider::iiFileProvider)
```

Includes `CMAKE_PREFIX_PATH` installation path and SDK path at Qt. Provides only build, test, and installation, with no commit, remote upload, or deployment stages.

## License

SPDX-License-Identifier: AGPL-3.0-only

Self-written code and documents of iiFileProvider are distributed exclusively under the GNU Affero General Public License v3.0. The full terms follow [LICENSE](LICENSE).

External libraries including Qt and third-party code with separate notices maintain their own licenses. This project's license declaration does not replace the corresponding third-party license.

<a id="계정-프로필-동기화"></a>

## Account profile synchronization

`fromIisaccAccount()` and `fromIisaccAppSession()` reflect iisacc.com's `account.authorDetails` to the file author's `metadata().details`. `toIisaccProfileUpdate()` explicitly exports only the display name allowed by the web service and the author profile. Authentication tokens and per-file ownership information are not included in the account update.

```cpp
const auto update = author->toIisaccProfileUpdate();
// The host authenticates and sends update as variables.input of the updateAccountAuthor GraphQL mutation.
```

The account-side model and API are the service's `docs/ACCOUNT_AUTHORS.md`, and the file model is
Defined in [FILE_AUTHOR_CONTRACT.md](docs/FILE_AUTHOR_CONTRACT.md).

<a id="파일-crud"></a>

## File CRUD

Since 0.5, this SDK owns file creation, reading, updating, deletion, and SQLite storage transactions. It does not reference other iisacc SDKs and takes bytes, streams, and schemas as input. It follows the [complete contract](docs/FILE_CRUD.md). The ABI of the existing 0.4 value types is retained.

## Source layout

Implementation files and their headers live together under `src/`. Existing feature and platform subdirectories retain their responsibilities. Build configuration, tests, documentation, resources, and maintenance scripts remain at the project root. Configure and build using the repository-local `build/` directory.
