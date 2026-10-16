# Native file catalog and classification

Version 0.6.0 adds `iiFileProvider::Catalog`: native C++23 file creation, streaming
import, bounded provision, editable metadata, collections and persistent search.
It depends on SQLite 3.27+ with FTS5, the standard library, provider TemporaryLease,
and the JSON/utf8proc sources already vendored under `Native/third_party`. Their
pinned hashes and licenses are verified during configuration. No Qt, upper iisacc
SDK, renderer, account, network service or interpreter runtime is required.

## Storage contract

Open an absolute local directory with `FileCatalog(root)`. `Access::Create`
creates missing storage, `ReadWrite` requires an existing catalog, and `ReadOnly`
rejects provider mutations and never creates a missing root. SQLite may use its
ordinary WAL/shared-memory coordination files for an existing read-only connection.

- Original payload bytes remain unchanged by annotation.
- `<filename>.iisacc.json` holds UTF-8 portable metadata: stable file key, revision,
  title, description, keywords, rating, color, custom fields, collection keys/names.
- `.iisacc-file-provider/catalog.sqlite3` holds the transactional index, saved
  filters, collections, filesystem versions and pending-publication journal.
  Its schema is independent of Society object packages.
- `.iisacc-temporary.lock` coordinates provider-owned staging and cleanup.

ObjectPackager excludes the reserved `.iisacc-file-provider` directory,
`.iisacc-temporary.lock`, `.iifile-stage-*` and `.society-extract-*` control names
at every tree depth. Public payloads and `.iisacc.json` companions are packaged
together. Reserved controls and companion suffixes are compared without ASCII
case sensitivity. Other hidden files retain the ordinary inventory policy.

Keep companions with originals when transporting files. An arbitrary image,
video, archive or model cannot safely accept generic metadata inserted into its
payload. This schema is not automatically recognized as EXIF, IPTC, XMP, ID3 or
an OS filesystem tag. Format-specific embedding stays in a codec supplied with
a provider-owned stream or staging path. Classification does not authenticate
authors; existing `Authorship`, `FileAuthor` and `FileLink` contracts are preserved.

## Build and consume

```sh
cmake -S . -B build -DIIFILEPROVIDER_BUILD_QT_API=OFF \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/build/install"
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --install build
```

```cmake
find_package(iiFileProviderCatalog 0.6.0 CONFIG REQUIRED)
target_link_libraries(application PRIVATE iiFileProvider::Catalog)
```

The public header is `<FileCatalog.h>`. No vendored dependency type appears in
its interface. The existing Qt package re-exports the Catalog target. The native
CLI installs as `bin/ii-file-provider`.

## Creation and editing

```cpp
#include <FileCatalog.h>
#include <ostream>

iiFileProvider::FileCatalog files(absoluteLibraryDirectory);
auto campaign = files.createCollection("Autumn campaign");
iiFileProvider::FileMetadata metadata;
metadata.title = "Seoul portrait";
metadata.description = "Approved original";
metadata.keywords = {"Portrait", "Seoul"};
metadata.rating = 5;
metadata.color = iiFileProvider::ColorLabel::Purple;
metadata.fields = {{"project", "Campaign 2026"}};
metadata.collections = {campaign.key};

auto created = files.createFileWith("originals/portrait.bin",
    [&](std::ostream &output) { encodeOriginal(output); }, metadata);
// Inspect created.metadataWritten and metadataError before reporting export complete.
auto edit = created.file.metadata;
edit.rating = 4;
auto updated = files.updateMetadata(created.file.key, created.file.revision, edit);
auto chunk = files.read(updated.file.key, 0, 256 * 1024);
auto next = files.read(updated.file.key, chunk.bytes.size(), 256 * 1024,
                       chunk.contentVersion);
```

`createFile` accepts a byte span. `createFileWith` streams an encoder into staging
and publishes after successful completion/flush. An encoder exception publishes
nothing. Creation/import reject an existing payload or companion without replacing
it, before running an expensive encoder/copy, then recheck before publication.
Metadata updates replace classification, never payload content.

`importFile(source, relativeDestination)` copies with a 1 MiB maximum buffer and
checks the source version before/after copying. A recognized source companion
supplies metadata and collections, while the copy receives a new file key.
Explicit metadata replaces inherited classification. An unknown companion is an
error, never permission to overwrite it.

`registerFile(relativePath)` reads filesystem metadata, not payload content.
Existing indexed paths retain classification; new paths import a recognized
companion and stable key. Index rebuilding can therefore recover file-owned
metadata/memberships. Saved filters and empty collections remain catalog-owned
and require an index backup. `registerFiles` commits up to 1,024 entries and
16 MiB of annotations atomically; a failure rolls back the complete batch.
Batch registration defers companion writes to `publishMetadata(limit)`.

Do not copy only the SQLite main file while WAL writers are active. Back up a
consistent SQLite snapshot, or close all catalog connections before copying the
index and before any explicit rebuild. Do not discard a pending journal before
its companions have been published.

Each instance serializes calls. Independent instances/processes use SQLite WAL
and writer transactions, with up to 30 seconds of writer contention waiting for
durable external-storage flushes. Encoders run inside their instance's operation;
use a separate catalog connection for reads made by an encoder callback.
`updateMetadata` requires the caller's last-read revision;
stale edits fail without replacing the current data. Metadata arguments are complete
replacements: load the current record, edit desired fields, submit its revision.

Ratings range from 0 (unrated) to 5. Colors are None, Red, Orange, Yellow, Green,
Blue, Purple and Gray. Keywords use NFC normalization and Unicode case folding
for duplicate detection/exact filtering, preserving the first display spelling.
Custom fields use NFC and exact matching. Collections are virtual memberships;
they never move or duplicate a payload.

## Search and saved filters

```cpp
iiFileProvider::FileFilter filter;
filter.text = "서울 por";
filter.pathPrefix = "originals";
filter.allKeywords = {"Portrait"};
filter.minimumRating = 4;
filter.colors = {iiFileProvider::ColorLabel::Purple};
filter.collections = {campaign.key};
filter.fields = {{"project", "Campaign 2026"}};
auto saved = files.saveFilter("Approved portraits", filter);
auto page = files.searchSaved(saved.key, {}, 128);
if (page.next) page = files.searchSaved(saved.key, page.next, 128);
```

Text consists of Unicode word prefixes with AND semantics across path, title,
description, keywords and custom fields. It is never raw SQL/FTS syntax. FTS5
uses `unicode61` and 2/3/4-character prefix indexes. This is token-prefix search,
not arbitrary substring matching or morphological analysis. See
[SQLite FTS5](https://www.sqlite.org/fts5.html).
The minimum SQLite version follows the `remove_diacritics=2` option added in
[SQLite 3.27.0](https://www.sqlite.org/releaselog/3_27_0.html).

Filter groups combine with AND. `allKeywords` requires every keyword;
`anyKeywords`, `colors` and `collections` use OR within their own group.
Rating/size ranges are inclusive. Path prefixes match the path and descendants,
not unrelated names sharing initial letters. A deleted collection is an empty
match in saved filters; removing it never broadens a filter to all files.

Pages contain up to 1,024 `CatalogFileSummary` values: key, index, revision, path,
64-bit size, modification time, content version, title, rating, color and pending
status. Search does not load full descriptions, fields, memberships or original
contents. `lookup(key)` retrieves full metadata. Keyset cursors bind catalog ID,
index revision and normalized filter. Classification/index mutations expire earlier cursors;
restart search instead of silently skipping/duplicating results.

Saved filters upsert by normalized name and keep stable keys. Collection names
are unique after normalization/case folding. Renaming a collection queues member
companions; publish the updates in bounded batches.

## Recovery and boundaries

The index and pending journal commit together before companion publication.
Single-file methods return `MetadataWrite` with the committed file,
`metadataWritten` and `metadataError`. A blocked companion leaves durable metadata
and a retry entry. Correct the blockage, then call `publishMetadata`; it throws
on the first failure and leaves unprocessed entries pending. Check
`pendingMetadata()` for remaining work. Repeating a publication after a crash
between atomic replacement and journal acknowledgement is safe at the same revision.

Staging resides on the destination filesystem. Native exclusive publication
preserves existing payloads. Companion replacements use atomic native rename,
file flush and directory flush where supported. SQLite uses `synchronous=FULL`.
Payload publication and SQLite are separate commits; a power failure between them
can leave an unindexed original. Register that original to recover it.

Managed paths are canonical root-relative UTF-8. Absolute/parent paths,
backslashes, colons, reserved provider control names, companion names, Windows-reserved
characters/device names, control characters and trailing spaces/dots are rejected.
The device/character rules follow [Microsoft's filename conventions](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file).
Ordinary hidden files such as `.config/app.json` are supported; hidden names are
not excluded as a category.
Components have a 220-byte limit to reserve suffix
space. Existing symbolic links/reparse points and replaced root/state/database
identities are rejected. The same hard-linked file cannot be registered twice.
This is coordination for cooperating local applications, not authorization
against a hostile process racing path replacement. Remote access must retain its
transport's confinement and authority checks.

There is no recursive disk scan, background watcher, network server or automatic
mount. The owner decides which files to register. Externally changed files require
explicit registration refresh. External file relocation requires owner-managed
index maintenance; this release adds no catalog move/delete operation.

Limits: 256 KiB encoded companion, 4 KiB title, 64 KiB description, 128 keywords,
128 memberships, 64 custom fields, 4,096 collections/saved filters per catalog,
1 MiB provision chunks, 32 text-search prefixes.

## Native CLI

Use an absolute `ROOT`. Metadata requests/results use UTF-8 JSON stdin/stdout.
Failures return JSON stderr and a nonzero exit code. `create` consumes binary
stdin; `read` emits binary stdout. Commands:
JSON requests are bounded to 1 MiB, including serialized continuation cursors.

```text
ii-file-provider ROOT init
ii-file-provider ROOT create RELATIVE
ii-file-provider ROOT import SOURCE RELATIVE
ii-file-provider ROOT annotate KEY EXPECTED_REVISION
ii-file-provider ROOT metadata KEY
ii-file-provider ROOT query
ii-file-provider ROOT collection NAME
ii-file-provider ROOT collections
ii-file-provider ROOT save-filter NAME
ii-file-provider ROOT search-filter KEY
ii-file-provider ROOT recover
ii-file-provider ROOT read KEY OFFSET LENGTH
```

`annotate` consumes a full metadata object. `save-filter` consumes a filter.
`query` consumes `{filter, limit, cursor}`; pass its returned `next` object as the
next cursor. `search-filter` currently exposes the first page; use the C++ API
or `query` with the same conditions for subsequent pages. Unknown request fields
and fractional/negative/overflowing integer values are rejected.

Native tests cover payload preservation, Unicode, conflicts, restart, filters,
pagination, 513-file batches, rollback, sparse multi-gigabyte provision, portable
import, independent concurrent connections, encoder failure, foreign state,
redirected roots, recovery, CLI and installed consumers. Qt adapters and Society
export have separate tests. Host execution evidence is macOS; other targets
require native toolchains and target execution. Browser storage requires an
explicit filesystem/SQLite adapter; this release does not provide it.

On macOS, packaging fixtures prime their first native snapshot, then establish
one second of stable native metadata before capturing their asserted inventory.
New-file provenance attributes can
otherwise arrive asynchronously and change ctime during an unrelated test phase.
Production source identity/conflict checks remain unchanged and include ctime.
The quiet-fixture helper is confined to the new catalog/object integration test;
existing packaging regression fixtures retain their original behavior. Their
metadata-change failures are recorded separately from classification validation.
Packaging and audit hold a shared staging lease across the complete operation,
including intervals between individual snapshots, so cleanup cannot enter a gap.
Native temporary admission retries a transient concurrent-first-create ENOENT
at most eight times, only while the pinned parent still matches the live path.
Invalid, redirected, replaced or removed parents retain their failure behavior.
The regression exercises eight simultaneous writers in 32 fresh directories.
