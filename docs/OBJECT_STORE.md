# Society object package — database schema 3

`iiFileProvider::Objects` is a C++23/SQLite target with no Qt or upward SDK
dependency. It is additive to the existing Qt author/file adapters. Consumers use
`find_package(iiFileProviderObjects CONFIG REQUIRED)` and `<ObjectStore.h>`.
The installed CMake package resolves both SQLite and the platform thread target;
consumers do not need to add thread linker flags manually.
Configure `-DIIFILEPROVIDER_BUILD_QT_API=OFF` for a Qt-free build.

## Identity and layout

`ObjectStore(directory, containerKey, create)` owns `objects.sqlite3` and its SQLite
WAL sidecars. Creating a package must be explicit. Unknown databases, another
container's package, redirected paths and nonempty unknown package directories
are rejected. Do not copy an open database without its WAL: use a consistent
SQLite backup or close all connections first. The original directory tree is not
rewritten by these APIs.

An object has a random opaque key independent of its logical relative path, a
stable monotonically allocated integer index key, an increasing version, source
SHA-256, byte count, author attribution, a work-session reference, an operation,
a timestamp, a parent validation key and a Society schema validation key.
`move` changes the classification/path without changing object identity. Two
separate files with identical content still have different object keys.

WAL durability remains `synchronous=FULL` with `fullfsync=ON`. A connection uses
SQLite's default 1,000-frame auto-checkpoint unless a caller supplies a different
page threshold through the four-argument `ObjectStore` constructor. The package
CLI exposes this as `--wal-autocheckpoint-pages`; `0` disables auto-checkpointing.
Raising the threshold reduces how often a commit also checkpoints the main DB,
but allows the WAL sidecar to grow to the threshold (or to a single transaction's
size) before checkpointing. This is a durability-preserving scheduling choice,
not a free-space optimization: select it only when the package volume has room
for the expected WAL. CLI default remains 1,000 frames.
The threshold controls commit-triggered automatic checkpoints only. SQLite still
attempts a final checkpoint when the last connection closes; setting the threshold
to zero does not make shutdown free of disk synchronization. A CLI summary is
emitted before connection destruction, so require the process's exit status as
well as its summary before reopening the package for another operation.
The installed-only consumer regression writes with an explicit threshold and
reopens with the original constructor, checking that both exported call forms
link and preserve the same validated object and compact index.

The current index is separate from immutable version records. `scan` uses
keyset pagination on index keys, not recursive directory traversal or OFFSET.
Separate database connections can read committed heads while packaging writes
under WAL. One ObjectStore serializes its own connection. Pages are bounded to
10,000 records. Concurrent changes may appear on later pages; a scan is not a
frozen multi-page snapshot.

`index` returns a compact projection of keys, path, version, content digest,
size, validation/session keys, tombstone and actor identity. It does not load
payload, history or potentially large author documents. Use `lookup` for full
metadata on demand. Both APIs use the same monotonic keyset boundary and live/
deleted policy; concurrent mutations do not provide a frozen multi-page snapshot.
The projection is stored physically in `current_index`, ordered by its integer
primary key. Traversal is a single range query with no revision/session joins
or temporary sort. A database trigger updates each head's projection in the same
transaction as import, revision, move or deletion; a failed batch also rolls back
its projected rows. This trades one small current-state row per object for fewer
disk-page lookups. It does not eliminate the storage device's first-read latency.
The projection is a traversal cache, not an independent integrity authority;
`validate` continues to verify the immutable revision chain and content.
The `object_index` regression compares every projected field to authoritative
heads through import/revise/move/delete, failed-batch rollback, keyset pagination
and reopen. It checks the single-table query plan, schema-2 backfill, rejection of
a mismatched container, and complete rollback when a migration fails after
backfill. The legacy fixture additionally checks schema-1 index preservation.

## Package payload, diff and journal

Each imported/revised file is streamed in 1 MiB chunks with bounded working
memory. The package stores immutable content-addressed chunk bytes, deduplicated
across files and revisions. Version manifests retain chunk order. A revision,
its chunks and its current-index update commit in one SQLite transaction. Failed
imports/conflicts roll back and never rewrite the source file.
Source ingestion and CLI source audits request a 1 MiB file-stream buffer before
opening the file. In the inspected Apple libc++ implementation the default is
only 4 KiB, independently of the caller's `read()` size. This avoids repeated
small buffer refills on image-backed storage; it does not promise that the OS
will complete an individual read within a fixed time. Boundary/tail sentinel
tests verify that buffering does not omit or duplicate bytes.

For small-file ingestion, `importFiles` accepts at most 256 files and 16 MiB total
payload per transaction. A separate 16 MiB author/provenance JSON budget counts
the recording actor's profile once per file plus each supplied authorship document.
This aggregate metadata budget is checked before payload reads or object writes.
It reuses the same import/validation path, assigns an
independent object/index key to every file and publishes all records together.
Any missing source, conflicting path, invalid metadata, limit violation or
cancellation rolls back the entire batch. This amortizes durable synchronization
without disabling WAL durability or exposing partial objects.
Batch payloads are read by a bounded standard-C++ worker pool (hardware thread
count by default, at most 64 workers and never more workers than files). All
source sizes are inspected before reading: the combined captured payload stays
within 16 MiB. Caller-provided stream buffers are capped at 1 MiB and at source size,
so they add at most another 16 MiB plus a byte per empty worker input; SQLite,
metadata and result allocations have separate overhead. Workers do not access
SQLite. The owning thread hashes/stores the prepared bytes in input order inside
one transaction. Source identities are checked after reads and again before
publishing each object. Any read failure, detected source change or cancellation
rejects the batch; all started workers are joined before their buffers are freed.
Large individual files retain the bounded streaming path instead of being read
entirely into RAM. This overlaps small-file I/O latency; actual storage throughput
still requires measurement and is not guaranteed by the worker count.

The packager also captures and destroys small-batch `ObjectSource` snapshots with
bounded workers (hardware concurrency, capped at 64 and the batch count). Metadata
callbacks and candidate selection remain on the owning thread. Every request is
checked against its inventoried identity before and after acquisition. An
expected-identity overload checks the acquired descriptor before copying or
cloning; copy fallback rejects growth before writing beyond the acquired size. An
acquisition failure rejects the complete selected batch, waits for all workers,
and cleans its owned snapshots; per-item issues identify every rejected member.
Earlier metadata-budget boundaries still publish their prepared prefix. Committed
progress is emitted before snapshot cleanup; cleanup joins before the next batch.
It never removes unrelated staging files or original sources. Failure to start
cleanup workers falls back to the caller instead of abandoning owned snapshots.
Fallback-copy scratch buffers are heap allocated, capped at 1 MiB and source size
(one byte for empty sources), so small native thread stacks cannot overflow.
The snapshot regression covers serial/parallel equivalence, source-edit isolation,
stale/missing/redirected inputs, cancellation, size/count bounds and owned cleanup.
The private `IIFILEPROVIDER_TEST_COPY_SNAPSHOTS` compile definition forces the
existing copy fallback in standalone tests; normal Apple builds still prefer clones.

`history` is the append-only per-object mutation journal. Each entry records the
actor/session/operation and a hash chain. `diff` reports changed binary chunks
with byte offsets and before/after hashes and sizes. The hashes refer to retained
bytes, so `extract` can reconstruct and verify any non-deleted version. This is
a reversible chunk diff, not a minimal textual line diff. A deletion appends a
tombstone, retaining previous versions and freeing the live logical path.

An explicit work session records its author, device, description, start and end.
Closed sessions cannot mutate files. Attribution records are domain metadata,
not proof of identity. Existing files must not be silently attributed to their
importer as their historical creator; the integration must distinguish unknown
provenance and the actor performing the import.

`ObjectAuthor::profile` preserves the exact credential-free JSON from the legacy
FileAuthor model (`iiFileProvider.FileAuthor/1`). `ObjectRecord::authorship`
independently preserves the original author/contributor roster and links from
`Authorship::dump` (`iiFileProvider.Authorship/3`). Empty metadata means unknown;
it never means the importer was the historical creator. An omitted authorship
write option preserves the previous document; explicit empty metadata clears it.
The packager accepts an optional source-metadata callback; the CLI does not guess
missing original authors from operating-system file ownership.
For existing objects the callback is evaluated before the unchanged-source skip.
An omitted document preserves recorded provenance; an explicitly empty document
clears it. A different supplied document creates a new revision on the same
object/index key even when the source bytes are unchanged. Old provenance stays
in history and the validation key changes; unchanged bytes produce no chunk diff.
Identical documents remain idempotent. Readers may be invoked again during retry
or batch lookahead and should only read/resolve metadata, not mutate source data.
Actor profiles and original-authorship rosters are not interchangeable formats;
an import or revision receiving a FileAuthor document in the roster slot fails
without allocating a new object or advancing its version.

Metadata validation uses SQLite JSON functions: object root, schema version,
known top-level fields, required basic shapes, duplicate-field rejection and
recursive credential-key rejection. Actor profile identity must match its
session identity. The existing FileAuthor/Authorship models remain responsible
for their complete domain semantics before serialization. The Qt-free storage
layer does not duplicate every legacy profile/timestamp/link semantic validator.
SQLite must supply JSON functions for nonempty metadata; unavailable functions
fail closed. Full documents are bounded to 2 MiB and included in validation keys.

The validation key binds the container, object/index keys, version, path, content
digest/size, attribution, immutable session data, timestamp, operation and parent
key. It detects inconsistency/corruption, but is **not a cryptographic signature,
authentication credential or authorization mechanism**. `validate` checks the
metadata chain and optionally the current version's payload/chunk digests.

Opening schema 1 or 2 with the correct container identity upgrades the database
to schema 3. Initialization or all required migration steps share one transaction;
schema 3 backfills current heads and installs the publication trigger together.
A first upgrade can take
time proportional to the existing object population and must complete before
traversal begins. Older binaries reject the new database schema. Existing
revisions retain their original schema-1 or schema-2
validation key and cannot acquire unbound metadata. New revisions use
`society-object-v2:` and bind actor profile, original authorship and source stamp.
Mixed-version history remains verifiable and extractable. A mismatched container
identity is rejected before migration.

## Safety and current boundaries

Sources remain ordinary files. Path traversal and symlinks are rejected. Direct
`importFile`/`reviseFile` still require a stable source from the caller. Use
`ObjectSource` or `ObjectPackager` for external filesystem sources:

- Source handles reject redirected ancestors and non-regular final entries.
- POSIX inventory reads metadata through a no-follow parent handle and `fstatat`;
  it does not open every payload merely to collect identity. Payload acquisition
  is deferred to snapshotting, preventing unnecessary provider hydration.
- macOS uses descriptor-based APFS cloning where available; fallback copies
  stream from the acquired handle with cancellation and identity checks.
- Source stamps bind canonical path, volume/file identity, size and nanosecond
  change/modification times (plus creation time where available), not just mtime.
- Handle and current-path identities are compared before/after capture. The
  snapshot is then independent of subsequent original-file changes.
- The portable copy fallback protects against ordinary concurrent modification,
  not privileged writers deliberately restoring all checked metadata. Windows
  uses a read handle denying write/delete sharing; that path still needs runtime
  platform validation.

SHA-256 uses the operating system's CommonCrypto implementation on Apple and a
portable C++ implementation elsewhere, with identical standard digest output.
All new database/files use owner-only permissions. Extraction validates first
and publishes via an exclusive hard link; existing destinations are never
overwritten. A filesystem without hard-link support rejects extraction safely.

The initial package retains a copy of source content (deduplication only saves
identical chunks). A real-drive migration requires a capacity inventory and a
recoverable plan; this API must not be deployed as an unbounded startup copy.
No real Society drive is migrated by the SDK tests.

## Resumable tree packaging and CLI

### Reader/writer access boundary

Consumers that only query objects should explicitly open
`ObjectStore(directory, container, ObjectStore::Access::ReadOnly)`. The connection
uses SQLite `READONLY` and `query_only`, checks the container identity and requires
schema 3. It neither creates a package nor runs migrations, sets writer WAL
checkpoint policy or changes persistent journal mode. Older schemas must first
be opened by a writer for their existing transactional migration. The existing
boolean constructors retain their previous create/open-for-writing behavior;
`Access::ReadWrite` and `Access::Create` are explicit alternatives.

The same lookup, session, index, history, diff and validation APIs work for a
reader. Database mutation methods reject writes. Each completed query can see
later committed state; this is not a single frozen snapshot across many index
pages. SQLite may use WAL shared-memory sidecars, so read-only **database access**
is not a claim that the filesystem has no auxiliary I/O. Explicit `extract`
still writes its caller-selected output, and source audit still owns temporary
snapshots under package staging. The CLI `--index` and `--audit` now use this
reader access mode. Normal packaging and `--end-session` remain writer actions.

`iiFileProvider.object_read_only` verifies opening and reading while another
connection owns a WAL write transaction, every mutation entry point rejecting
writes, committed-state visibility, missing-package non-creation, container
identity, and rejection of schema 2 without migration. An explicit writer then
upgrades that fixture and preserves its object identity.

### Command-line policy

CLI policy is isolated in `tools/ObjectPackageOptions.{h,cpp}`. Its pure
`parseObjectPackageOptions` function returns typed options before inventory,
database access, signal observation or session creation. The executable owns
process lifetime and JSONL output; `ObjectPackager` owns source reconciliation
and ingestion; `ObjectStore` owns persistence and integrity. Library users do
not depend on command-line parsing, and changing CLI validation does not require
changing storage code or its public API.

The parser rejects conflicting modes, duplicate namespaces, malformed mappings,
unmapped/nested exclusions and missing required arguments. Checkpoint counts must
be complete nonnegative decimal integers within `int` range: signs, whitespace,
suffixes and overflow are rejected instead of accepting a numeric prefix.
Exclusions can precede their mapping, and UTF-8 paths/spaces are preserved.
Filesystem existence, canonical confinement and source identity remain runtime
SDK checks, not parser responsibilities. `iiFileProvider.object_package_options`
tests these rules without opening source files or a database; the existing
`iiFileProvider.object_package_cli` regression verifies real-process wiring,
crash/resume, audit, revision identity and index behavior.

`ObjectPackager::inventory` traverses explicitly mapped namespaces. It rejects
overlapping/redirected roots, duplicate namespaces, symlinks and special files.
Exclusions are explicit top-level names per mapping, never a blanket exclusion
of hidden files. Any incomplete inventory prevents publication. Logical paths
retain their section/directory tree even when section roots live on other volumes.
Metadata acquisition uses a bounded worker pool (hardware concurrency by default,
at most 64; explicit concurrency is accepted for constrained callers). Parallel
and single-worker inventories have the same sorted paths, stamps and totals.
This does not run multiple SQLite writers against the same package; file commits
remain serial and recoverable.

Each file is snapshotted and ingested. By default the library commits each file
independently; callers can opt into batches of up to 256 new small files. A stored source
stamp makes unchanged committed files skippable after restart; changed sources
receive a new revision of the same path's object. Cancellation rolls back the
in-flight transaction and leaves previous commits intact. Publication uses expected
version checks; conflicting concurrent changes are reported, not overwritten.
Capacity is checked per file with a conservative `3 * bytes + 256 MiB` reserve
for snapshot, payload and journal. No original file is removed or rewritten.

The CLI `iiFileProviderObjectPackage` accepts repeated
`--map Namespace=/absolute/source` and optional `--exclude Namespace/name`:

```
iiFileProviderObjectPackage --package /absolute/package --container ID \
  --map Files=/absolute/tree --map Photos=/absolute/photos
```

Normal mode creates/opens the package and writes JSONL per-item results and a
summary. It schedules every inventoried file by ascending byte size, breaking
ties by logical path, so a lexically early large model cannot delay all small
objects. This changes processing order, not scope, identity, validation, or the
requirement to finish every mapped file. Inventory and audit retain path order;
library callers retain their supplied inventory order. The CLI groups consecutive
new files into batches of at most 256 files /
16 MiB payload and 16 MiB author/provenance JSON. Metadata-heavy input is split
at the budget boundary without dropping the next file. Larger files and revisions
keep independent transactions. Snapshots are
retained until the batch is committed and checked against the aggregate capacity
reserve. Per-item success events are emitted only after the entire batch commits;
a cancellation arriving from a success callback cannot undo that committed batch.
Batch errors report every rolled-back member. `--inventory` is read-only with
respect to the package. Per-item committed/skipped events include the independent
index key, SHA-256, validation key, recorded session and recording actor, as well
as an `authorshipKnown` flag. The recording actor is not a claim about the original
creator. These committed-record fields allow progress inspection without opening
another database connection during ingestion; full provenance documents and payloads
are not copied into the progress log. `--index` scans
the compact current index three times, reporting counts and elapsed milliseconds
without reading source files. `--wal-autocheckpoint-pages N` tunes the writer
connection's automatic WAL checkpoint threshold without changing
`synchronous=FULL`. Use the default for ordinary bounded writes; a larger value
can reduce repeated checkpoint stalls during large resumable packages at the cost
of a larger WAL and should be used only after checking free space. Process-kill/
resume tests exercise a raised threshold to ensure committed WAL content survives
recovery. `--end-session KEY` closes a known interrupted
session after its writer has been verified terminal; it does not remove the
session or its object history. `--audit` independently hashes fresh source
snapshots, checks source identity before/after, validates each current packaged
payload and its metadata ancestry, compares the compact index projection with
each current head, and compares live object/index counts to inventory.
This checks current bytes, not every historical version's payload. It is not an
atomic snapshot of the entire concurrently changing drive; rescan on changes.

Graceful SIGINT/SIGTERM cancels between chunks and closes the work session. A
forced kill can leave an unfinished session and uniquely owned staging snapshot;
the next run recovers SQLite transactions and skips committed files, but does
not blindly delete another process's staging directories. Orphan staging cleanup
requires a separate ownership/liveness check. Missing source files are not
automatically tombstoned and external renames are not inferred from content.

Pending end-to-end requirements: discovery of original author documents in real
sources, container lifecycle and filesystem-projection integration, external
rename/deletion reconciliation, installed consumer/platform tests, measured real
whole-index traversal, and packaging/auditing all real drive files. SDK tests
alone do not prove that requested end state.

## Regression test

`iiFileProvider.object_store` covers SHA-256 known vectors, streamed import,
path-independent identity, versions, reversible binary diff, journal/session
persistence, CAS conflicts, rollback, tombstones, path boundaries and source
preservation. It also checks independent identity for equal content, empty files,
keyset pages, competing connections, self-contained extraction after source
removal, and deliberate payload/ancestor-metadata corruption. New mutations
reject an inconsistent metadata chain rather than appending onto it.
Tests create their fixtures inside the repository `build/` working
directory. Run `ctest --test-dir build -R object_store --output-on-failure`.

`object_authorship` verifies exact FileAuthor/Authorship round trips while keeping
import actor and original creator distinct, rejecting malformed/credential JSON,
and rolling back cancellation. A captured schema-1 SQL fixture proves upgrade
compatibility without recomputing its expected validation key from new code.
`object_packager` covers snapshot isolation, source changes, cancellation/resume,
idempotent retry, explicit exclusions, incomplete-inventory rejection and metadata
budget rollover with exact provenance and source-stamp preservation.
`object_packager_metadata` covers provenance-only reconciliation, stable object
identity and payload digests, immutable attribution history, idempotent repeat,
omission versus explicit clearing, and invalid-document rejection on resume.
`object_batch` checks independent identities and all-or-none rollback for missing
sources, conflicts, invalid metadata, cancellation, size/count bounds and closed
sessions, including distinct source stamps for each member and aggregate metadata
budget rejection before any object is published.
It also extracts mixed prepared payloads across a binary chunk boundary and
checks the canonical empty-file digest. `object_batch_read` compares serial and
parallel reads byte-for-byte, including zero-length and chunk-tail cases, and
rejects missing/symlink inputs, cancellation and aggregate size/count overflow.
`object_package_cli` kills an actual packaging process after its first batch
commit, resumes it and checks all 256 identities/versions remain committed, full audit, stale-source
detection, compact index count and SQLite integrity. These fixtures stay beneath
the build working directory and never access real Society contents.
