# iiFileProvider 0.6.0 validation

This is the earlier metadata catalog snapshot. The subsequent
[workspace file-creation validation](FILE_CREATION_VALIDATION.md) records the
later source migration and updated full provider suite results.

The metadata/classification implementation and compiled CLI are validated on
macOS arm64 with AppleClang 21, C++23 and system SQLite 3.54.0. The catalog requires
SQLite 3.27 or newer with FTS5. Qt-free Release and the existing Qt 6.8.3 adapter
both build. The CLI is a native Mach-O arm64 executable linked to SQLite, libc++
and libSystem; Qt and an interpreter are absent from its runtime dependencies.

| Validation | Result |
| --- | --- |
| Native Release classification/provision/temporary admission | 6 / 6 passed |
| Provider full source suite | 21 / 25 passed; four existing packaging/snapshot checks remain unresolved |
| Installed Qt API and Catalog consumers | 7 / 7 passed |
| Independent installed native Catalog consumer | 1 / 1 passed |
| Independent installed native Objects consumer | 1 / 1 passed |
| Society Helper source suite | 11 / 11 passed |
| Society Helper installed consumers | 9 / 9 passed |
| Final Helper filesystem/export checks against updated provider | source 1 / 1; installed 1 / 1 passed |
| SDK dependency graph and protected storage boundaries | 32 SDK declarations form a DAG; no upward provider edges |

Functional evidence includes 513-file transactional registration, compound
filters and keyset pages, Unicode normalization, persistent collections/filters,
original byte preservation, stale-writer rejection, a sparse 8,589,934,609-byte
logical file with bounded range reads, portable annotation import, pending
companion recovery, foreign database/companion preservation, private path checks,
ordinary hidden configuration files, and native CLI operations. The sparse-file
fixture verifies 64-bit indexing and bounded provision, not a physical 8 GiB
copy-throughput benchmark.

Concurrent first temporary admission was reproduced as ENOENT while the pinned
parent inode still matched the live directory. A bounded retry preserves the
no-follow/identity checks. The regression now passes eight simultaneous shared
writers in each of 32 fresh directories. Catalog writers wait up to 30 seconds
through durable external-storage flush contention, then retain error/revision
checks rather than silently overwriting another edit.

## Remaining full-suite failures

- `iiFileProvider.object_audit`
- `iiFileProvider.object_snapshot_batch`
- `iiFileProvider.object_package_cli`
- `iiFileProvider.object_packager`

The original ObjectSource implementation and all four regression sources match
the task-start backup. Failures include changed-source stamps during snapshot or
audit and an extra same-content revision during resume. A separate synthetic
probe observed a delayed ctime change and a provenance attribute after an initial
native clone. This observation does not establish every failure's cause. The
runtime stamp checks and existing test expectations were preserved. The full
suite is not green; this report does not claim packaging health or whole-SDK
release readiness.

## Installation and execution boundary

Verified prefixes are `build/metadata-catalog/install`,
`build/metadata-catalog/native/install`, and the sibling Helper's
`build/file-provider-metadata/install`. Global `~/.local` installations, installed
product apps and live Society stores/processes were not replaced or restarted.
Windows/Linux/Android/iOS/WASM execution and Android document-picker URI writing
remain unverified. No all-platform runtime completion is claimed.

Machine-readable results, binary/source SHA-256 values and exact JUnit paths are
in [the verification record](../build/metadata-catalog/verification.json).
The API/storage contract is [FILE_CATALOG.md](FILE_CATALOG.md); ongoing file I/O
ownership is [FILE_OWNERSHIP.md](FILE_OWNERSHIP.md). The ownership policy and guarded
adapters do not constitute an exhaustive migration of every historical writer in
every Product/Service repository.
