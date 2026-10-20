# Repeated author metadata validation

Native author/profile, timestamp and revision validation reuses immutable,
function-local regular expressions. Previously each save or metadata import
compiled the same expressions again. The patterns and validation order remain
unchanged; no accepted syntax, limits, account checks or filesystem confinement
checks are relaxed. Locale, normalization and time-zone checks retain their
existing implementations.

The expressions have thread-safe static initialization and are subsequently
used only by const matches. Concurrent profile-import tests verify independent
results, and malformed profile fixtures exercise each reused expression.
Presentation intake already uses a bounded queue, duplicate suppression,
serialized decoding, source-change stamps and bounded directory slices; those
contracts are preserved rather than replaced with a second scheduler.

## Stable object snapshots

Object packaging captures POSIX snapshot bytes through an already pinned read
descriptor, with a buffer capped at 1 MiB. APFS extent cloning was observed to
change source ctime after snapshot use/retirement, invalidating unchanged source
stamps and causing false revisions and audit failures. The copy path preserves
the existing identity/size/mtime/ctime checks, no-follow ancestor checks,
cancellation and source-change rejection. It trades clone-based storage sharing
for stable source identity and bounded extra memory.

Disposable snapshots do not call fsync before being read into the object store.
They are never durable user output and are deleted after use. Published payloads
remain protected by the store's existing durable transaction and synchronization
settings. This removes a redundant disk barrier per copied file without changing
the final commit contract. Windows retains its existing share-locked copy path.

The batch/large-file audit, resume, cancellation and changed-source tests cover
the complete packaging flow; source identity must survive snapshot retirement.

## Workspace creation audit

The development audit reads candidate source files in bounded native-scanner
batches instead of starting a scanner process for each file. UTF-8, exact line
boundaries, empty files and unreadable-source rejection are preserved. This
reduces process startup overhead across SDK/Product checks without changing the
creation-policy patterns, exclusions or runtime/tool/fixture classifications.
The full SDK/Product boundary gate has a 600-second execution budget on external
storage. It is an exhaustive correctness audit, not a 120-second performance
threshold; unreadable files and policy violations still fail the check.
