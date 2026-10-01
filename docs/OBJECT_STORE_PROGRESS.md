# Object-storage goal ledger

The goal remains active. This ledger is not a declaration of completed Society
drive migration. Existing file-loading/pairing changes in other repositories
were preserved.

## Current authoritative handoff — 2026-09-29 resumed goal

### Actual-package read-only index measurement

- After the new read-only path passed functional and installed-consumer checks,
  opened the actual package once with that path while keeping sole writer **563**
  running. This supersedes the earlier blanket avoidance of opening diagnostic
  connections: do not use a writer-opening constructor/CLI for live inspection.
  Read-only access bypasses migration, journal-mode changes and writer checkpoint
  configuration; the concurrent-write regression passed before this measurement.
- Command: build CLI `--package '/Volumes/Society Data/.society-objects'
  --container 41e92f9a-7836-4cca-aba6-409752c56296 --index`. Handle **88645** is
  terminal, exit 0. All three runs enumerated **10,279 current index rows**.
  First traversal: **50,119.5 ms**; subsequent traversals: **5.86175 ms** and
  **5.77892 ms**. Total process: **52.03 s** (0.02 user / 0.01 system).
- Evidence: `build/object-package-live-readonly-index-20260929.jsonl` and
  `.stderr` (the latter contains timing, not an application error). First scan
  here means this process's first scan under ongoing packaging I/O, not an
  experimentally controlled cold-cache benchmark. Warm traversal is fast; the
  first-access delay is not acceptable evidence of uniformly fast startup.
- The reader exited naturally and did not replace/restart the writer. These
  results cover currently committed objects only. The final post-package index
  stage, full fresh-source audit and SQLite integrity checks remain mandatory;
  do not use this partial population measurement as full-drive completion.
- Subsequent live check at writer elapsed **1:25:23** confirmed another commit:
  `Models/LoRA/harustyle_v1.5.safetensors`, **228,587,776 bytes**, index **10,280**.
  Writer **563** and pipeline handle **91090** remain live; stderr is empty and
  no terminal package summary exists. WAL was 976,176,352 bytes. Keep the same
  pipeline and do not confuse the earlier 10,279-row measurement with final
  whole-drive results. The remaining mapped large models include hidden import
  and rejected-import files; the original all-file scope has not been reduced.

### Latest increment — reader/writer access boundary (functional checks passed)

- Added `ObjectStore::Access::{ReadOnly,ReadWrite,Create}` while retaining both
  boolean constructor symbols. Read-only mode uses SQLite READONLY/query_only,
  validates identity, requires schema 3 and bypasses migration/checkpoint setup.
  CLI `--index` and `--audit` now choose this mode; source-audit staging is still
  filesystem work, not a claim of zero auxiliary I/O. This prepares a consumer
  boundary but does not yet integrate Society's native browsers or mutations.
- Added `object_read_only` regression for concurrent WAL writer/read access,
  every nonempty mutation entry point, missing-package non-creation, committed
  state visibility, schema-2 rejection and explicit-writer migration. TDD red
  showed the expected missing constructor symbol. Library, CLI and regression
  targets built successfully. First Make invocation regenerated the build files
  but did not recognize the new target until the subsequent successful invocation.
- CTest handle **9966** is terminal, exit 8, **1/4 passed** in 836.07 seconds.
  **Read-only timed out at 181.60 s; index at 305.94 s; store at 63.47 s.** These
  bounded-gate failures remain recorded. The updated CLI crash/resume/audit/index
  regression **passed in 280.50 s**, including the new read-only inspection path.
- Isolated read-only test, handle **39234**, passed all phase checks and exited 0
  in **158.65 s** (0.00 user / 0.01 system). Full log:
  `build/readonly-isolated-20260929.log`. No durability setting, assertion or test
  scope was removed; this run observed actual completion without CTest's timeout.
- Sequential isolated remainder, execution handle **24182**, is terminal, exit 0. Store contract
  test passed in **247.69 s** (0.04 user / 0.06 system), recorded in
  `build/object-store-isolated-20260929.log`. Full index regression passed in
  **188.54 s** (0.01 user / 0.06 system). Staged installed-only consumer CTest
  passed **1/1**, test time **127.36 s**, total **134.23 s**, under
  `set -euo pipefail`. Logs: `build/object-index-isolated-20260929.log` and
  `build/readonly-consumer-20260929.log`. These complete functional runs do not
  erase the earlier bounded CTest failures or establish real-drive performance.
- Timed-out read-only fixture is
  `build/object-read-only-524696949399208`. A later read-only inspection found
  schema 3, one object, and a nonzero ended-session timestamp. This is partial
  evidence only. Added/flushed phase messages and rebuilt the read-only test for
  the next isolated run so a repeated timeout can be located precisely.
- `build/object-index-test-io.sample` observed the existing boolean constructor
  in SQLite commit `unixSync`/`fcntl`, not waiting on a reader's SQL writer lock.
  System memory pressure reported 78% available. Actual volume is mounted,
  writable APFS with about 2.59 TB free and reported SMART Verified; this does
  not exclude transient I/O trouble. Do not weaken durability or cancel the
  actual writer to turn the tests green.
- Staged installation and installed-only consumer configure/build passed,
  handle **69697** terminal 0. Stage: `build/readonly-stage`; consumer:
  `build/objects-consumer-readonly`, with explicit staged package config path.
  Consumer runtime verification passed as recorded above. Its compile includes
  and static link path point only to this staged object SDK, not repository
  headers or the build archive. Staged header/CLI byte comparisons passed;
  `otool -L` lists only SQLite, libc++ and libSystem. Canonical SDK/app/device
  installs were not changed. The consumer exercises old and new constructors.
- Actual pipeline remains handle **91090**, writer **563**, confirmed live at
  1:15:39 elapsed. Another 168,120,878-byte model committed as index **10,279**.
  Final package/audit/index/integrity results are still outstanding.

- The user resumed the full goal after the earlier process-close request. The
  former writer and inspection process were confirmed absent before resuming.
- Read-only preflight exited successfully: schema 3, container
  `41e92f9a-7836-4cca-aba6-409752c56296`, 10,228 objects, 10,228 compact index
  rows, zero open sessions. This is not a full SQLite integrity check.
- Execution handle **91090**, parent shell PID **561**, package writer PID
  **563**. Check the handle and current process state before taking action;
  process identity here is a checkpoint, not proof that it remains live later.
- Sole resumed writer uses the tested build with
  `--wal-autocheckpoint-pages 262144`, all nine original namespaces, and only
  the original two Files OS-directory exclusions. Work session:
  `session-b67c9e2d9a4805a9a6259e59655ee5c7`.
- Fresh inventory: **10,295 files / 152,727,550,345 bytes / zero issues**.
  Source inventory changed since the cancelled run; require the fresh complete
  source audit rather than interpreting previous counts as current completion.
- Runtime logs (all in `build/`):
  - `object-package-checkpoint-resume-20260929.jsonl` and `.stderr`;
  - `object-package-checkpoint-audit-20260929.jsonl` and `.stderr`;
  - `object-package-checkpoint-index-20260929.jsonl` and `.stderr`;
  - `object-package-checkpoint-integrity-20260929.log`.
  The parent runs package, audit, index and SQLite checks sequentially under
  `set -e`; a failed stage stops the pipeline. Later logs need not exist until
  their stage begins. Inspect the actual outputs, not only the pipeline exit.
- The final SQLite checkpoint may outlive the CLI summary. Wait for actual
  process exit before opening the DB or starting another writer. Do not cancel
  or restart merely because an observation window expires. Full real-drive
  completion, audit, integrity and complete-index timing remain outstanding.
- At 7:08 writer elapsed, the resumed JSONL contained 10,227 skips, 45 imports
  and 1 revision, with zero error events and empty stderr. These cover
  1,706,421,892 visited source bytes; most of the 152.73 GB payload is still
  outstanding. The writer was confirmed live at PID 563, not inferred from logs.
- SDK delivery before the CLI-options extraction below: rebuilt
  `iiFileProviderObjects` and `iiFileProviderObjectPackage`
  successfully, then installed the current API, static library, CLI and docs to
  `/Users/ymy/.local/SDK/iiFileProvider`. Installed header and CLI match the source
  header/build CLI; all eight archive member payloads match the build archive
  (the install-time archive symbol-table timestamp differs).
- Updated installed-only consumer regression writes through the configurable
  constructor and reopens with the original constructor. Fresh configure/build
  and `ctest --test-dir build/objects-consumer-current --output-on-failure`
  passed **1/1 in 12.64 seconds**, using the canonical installed CMake package.
  `otool -L` lists only SQLite, libc++ and libSystem. Documentation also now
  explains why the last connection's checkpoint can outlive a CLI summary.

### Maintainability close-out: independent CLI policy

- Added private `ObjectPackageOptions` with a typed mode/options value and a pure
  parser. CLI validation has no inventory, database or session side effects.
  Runtime execution/JSONL stays in `ObjectPackage.cpp`; existing library APIs,
  persistence schema and current-record contracts remain unchanged. No generic
  framework or speculative storage interface was introduced.
- Parsing covers all five modes, UTF-8 paths, namespace-bound exclusions and
  strict complete integer conversion. Invalid options are now also tested at
  the real-process boundary before any package directory can be created.
- TDD red was an expected unresolved parser symbol; after registering the
  implementation, the regular CMake CLI/options build passed. An independent
  C++23 build with `-Wall -Wextra -Wpedantic` and the pure test also passed.
- `ctest --test-dir build -R '^iiFileProvider.object_package_(options|cli)$'
  passed **2/2**, exit 0: pure options 0.32 seconds, real-process crash/resume/
  audit 227.35 seconds, total 228.13 seconds. Execution handle **29207** is
  terminal. The process regression includes rejected options before storage,
  crash recovery, session closure, unchanged-object identity, changed-file
  revisions, source/payload audit, index scan and fixture SQLite integrity.
  This latest CLI refactor is built in `build/` but has not been reinstalled
  into the canonical SDK prefix. App/device reinstallation was not performed.
- The actual writer remains separate and was confirmed live at 34:47 elapsed.
  Last confirmed head is index 10,278; stderr is empty. Large-file WAL writes are
  still in progress. These observations do not establish final package/audit or
  integrity completion. Keep the existing pipeline, without another writer.

### Completion-scope check — same live pipeline

- Revalidated handle **91090** and writer **563** at 39:13 elapsed. The process
  sample `build/object-package-checkpoint-stage-20260929-followup.sample` shows
  `ObjectStore::importFile` committing through SQLite WAL `guarded_pwrite_np`.
  WAL size advanced from 478,838,792 to 561,242,912 bytes. This is a verified
  live-write wait, not a terminal/stalled-job declaration or permission to restart.
- A read-only check of committed/skipped JSONL events found **10,278 records**,
  **10,278 distinct object keys**, **10,278 distinct index keys** and **10,278
  distinct logical paths**. No missing/malformed required key, positive version,
  SHA-256, validation-key, session-key or recording-actor fields were found.
  Observed source bytes total **1,999,347,335**; no error events or terminal
  summary exist. This is a log-contract check of the committed subset only,
  not a DB payload/history audit or proof of all 152.73 GB being packaged.
- Source inspection confirms the compact index is an ordered keyset query on
  `current_index`, without payload/history joins. The full-drive timing still
  awaits the post-package stage. Metadata-chain validation compares each
  revision with its session and parent validation key; the independent audit
  also hashes fresh sources and validates current payloads and index projections.
- Current `iiSocietyContainer` CMake/storage sources do not yet consume
  `iiFileProvider::Objects`. Existing `FileOperations` still relies on downstream
  indexing after native mutations. Container lifecycle/native-browser adoption
  therefore remains unproven and must not be claimed from CLI packaging tests.

## Historical handoff — superseded by the current state above

- Sole actual writer: **32997 / PID 27785**, log
  `build/object-package-live-all-parallel-snapshots.jsonl`, canonically installed
  build hash `d9f308632e8326da742469a8716f434a56f446dda308131876b65678937851c5`.
- At 10:40 elapsed: **2,173 new imports + 1,409 reused objects = 3,582 cumulative
  confirmed objects**. No terminal summary. Full mapped scope: 10,258 inventoried
  files / 198,342,091,658 bytes across all nine namespaces. This is not byte/time
  progress, a full-payload audit, or a completed goal.
- Native/forced-copy TSan, integrated packager, CLI recovery/audit and fresh
  installed-consumer gates passed. Schema 3 is installed and the actual package
  was migrated. The only actual index measurement covers the earlier 1,409-object
  population immediately after backfill, not a controlled cold/full-drive scan.
- Do not restart this writer for an observation timeout; inspect its same handle.
  All earlier actual writers and the no-writer migration/index process are terminal.
- Rechecking this later handoff found no live `iiFileProviderObjectPackage` process
  and its old execution handle had expired. The JSONL ends at committed index
  key 4504 with **3,095 imports + 1,409 skips = 4,504 confirmed current heads**,
  but no final summary. Do not call this normal completion or zero errors. The
  current CLI source now includes `ObjectAudit.cpp`; its source timestamp is later
  than the canonical executable and it was not in the binary that stopped.
- An independent read-only SQLite check of the Society object package was started
  with `PRAGMA integrity_check`, `foreign_key_check`, schema/object/index counts and
  unfinished-session count. Await its terminal output before reopening the package.
  Package staging currently contains leftover `snapshot-*/payload` pairs from the
  terminated process. Leave them untouched until integrity and exact stale-owner
  scope are established; do not confuse these files with committed object content.
- New `ObjectPackager::audit` implementation/test files appeared after the
  canonical install. Integrate them into a rebuilt/staged SDK, run their focused
  regression, and use the bounded source/payload/index audit against the actual
  Society inventory after confirming object-database integrity. Preserve the
  active goal; the actual mapped tree still has more files than committed objects.

## Current increment: bounded snapshot acquisition and cleanup

- The previous turn was progress: schema-3 compact-index source and all delivery
  gates completed, plus another 256 actual imports. Actual writer **1627 / 14995**
  was then live and unchanged, with **1,409** cumulative confirmed objects.
- New private `ObjectSnapshotBatch` overlaps acquisition and destruction of
  small-batch snapshots, hardware concurrency capped at 64 and request count.
  Metadata callbacks and SQLite publication remain on the owning thread.
  Requests retain the 256-file / 16 MiB payload cap; metadata bounds are unchanged.
  Worker acquisition errors reject the whole selected batch, join all workers
  and clean only owned snapshots. Cleanup-worker launch failure drains remaining
  items on the caller and joins any workers already started.
- `ObjectSource` retains its existing three-argument constructor and adds an
  expected-identity overload to reject stale descriptors before copy/clone.
  Fallback-copy buffers moved from a 1 MiB stack array to bounded heap storage;
  growth beyond the acquired source size is rejected before excess bytes are
  written. This avoids overflowing smaller native worker stacks.
- New tests cover serial/parallel order and byte equality (empty and 2 MiB +
  17-byte boundary/tail content), source-edit isolation, stale/missing/symlink
  inputs, cancellation, bounds and cleanup preserving unrelated staging files.
  The integrated packager test additionally changes a source between metadata
  preparation and snapshot capture, expecting a complete rejected batch, no
  committed prefix, clean staging and a successful fresh-inventory retry.
- Standalone forced-copy ThreadSanitizer build/run **passed**, exit 0, **64.26 s**
  (0.49 user / 0.25 system), handle **31347**. Logs:
  `object-snapshot-batch-copy-tsan-build.log` and
  `object-snapshot-batch-copy-tsan-tests.log`. The private test compile definition
  disables Apple cloning only for this test, exercising the actual copy fallback.
- Initial new-test link failed on missing `ObjectSnapshotBatch` symbols (handle
  **52541**, exit 2, `object-snapshot-batch-red-build.log`). Configuration had
  captured the old source list while source registration was being edited.
  An overlapping full-build process group **21949** was stopped (handle **77944**,
  exit 143) to avoid concurrent CMake writers; the actual drive writer was not
  signalled. A fresh explicitly serialized configure/build/test pipeline is now
  running in **75297**. It runs the native snapshot test, integrated packager
  regression and full CLI process-recovery/source audit in that order. Logs use
  `build/object-snapshot-batch-final-*`, `object-snapshot-batch-tests.log`,
  `object-snapshot-batch-packager-tests.log`, and `object-snapshot-batch-cli-tests.log`.
  No canonical installation of these changes has happened.
- Fresh configure completed (19.5 s configure / 152.3 s generation), and the full
  SDK build passed. Native snapshot CTest **passed in 3.82 s**, total 4.59 s.
  The same delivery pipeline **75297** is now running the integrated packager
  regression; no later-stage success should be inferred yet.
- A transient `Ts` process state was observed for actual writer 14995. Polling
  the same **1627** handle and re-reading the process immediately showed `Us`
  disk wait again. No restart or signal was sent; the process remains live.
- The installed-only consumer fixture now also compiles and invokes the public
  expected-identity snapshot overload before its existing store/inventory checks.
  Its new staging build/run remains a delivery gate, not a completed result.
- Integrated packager regression **passed in 280.56 s** (0.10 user / 0.16 system),
  including complete batch rejection and fresh-inventory retry after a source
  changes during preparation. Pipeline **75297** has advanced to CLI crash/recovery
  and source/payload audit. Staging installation and a fresh installed-only
  consumer are being validated independently; no actual writer replacement yet.
- All delivery gates completed: pipeline **75297 exit 0**, full 256-object CLI
  process-kill/resume/source-payload audit **passed 256.54 s**. Staging/consumer
  pipeline **55212 exit 0**, installed-only consumer **passed 52.58 s** (CTest
  52.83 s), including the new expected-identity constructor.
- Build and staging executable SHA-256 matches:
  `d9f308632e8326da742469a8716f434a56f446dda308131876b65678937851c5`.
  After these gates, the actual writer **14995 / 1627** was sent **SIGINT** for
  the planned validated upgrade. Normal shutdown and terminal completion must
  be verified before installing canonically or opening the actual package.
  No force kill, remount, original-file deletion or second writer was used.
- Old actual writer **1627 / 14995 terminated with exit 130**. Final summary:
  imported=256, skipped=1152, revised/errors=0, cancelled=true, **3286.54 s**.
  The close sample reached SQLite WAL checkpoint `pwrite`; process disappearance
  and terminal status were both checked, not just the summary. Cumulative
  committed objects remain 1,409.
- Container identifier and both source volume UUIDs still match the prior run;
  approximately 2.4 TiB is available on the mapped volumes. Canonical installation
  and a no-writer actual schema-3 index measurement have now been started. Logs:
  `object-snapshot-batch-install.log`, `object-index-live-schema3.jsonl`,
  `object-index-live-schema3-time.log`. The measurement population is the existing
  committed subset; it is not proof of a complete drive migration or cold-cache
  latency after a just-completed schema backfill.
- Canonical install completed and matches the tested build hash `d9f308...7851c5`.
  Actual schema-3 index measurement **6784 completed with exit 0**: all three scans
  returned **1,409** objects, in **1.381 / 0.9785 / 0.985667 ms**. Entire command,
  including migration/open/close: **46.19 s** (0.01 user / 0.02 system). These are
  post-backfill traversal timings, not a controlled cold-cache comparison or
  full-drive population benchmark. Existing full migration remains incomplete.
- Only after that measurement terminated, the upgraded CLI was launched against
  the same package/container and all nine original mappings. New live log:
  `build/object-package-live-all-parallel-snapshots.jsonl`. This run combines
  compact schema-3 indexing, bounded payload reads and bounded snapshot lifecycle
  workers; the old writer is terminal, not running in parallel.
- Upgraded actual run is **handle 32997 / PID 27785**. Inventory completed with
  **10,258 files / 198,342,091,658 bytes**, zero inventory issues, **0.774664 s**.
  This is a later observed inventory, not a controlled speed comparison; the
  source set's byte total changed. Background scheduling was removed with
  `taskpolicy -B -p 27785`. Revalidate this same handle before any other actual
  writer or database observer; previous handle 1627 is terminal.
- All **1,408** resumed Photos entries exactly match the union of prior committed
  logs for path, object/index key, version, bytes, SHA-256, validation key,
  session, recording actor and provenance-known flag. The Files object still
  sorts later. Source logs contain 1,409 prior imported objects in total.
- At **4:08 elapsed**, upgraded run **32997 / 27785** had committed **1,536 new
  objects** (six complete batches), bringing cumulative confirmed objects to
  **2,945**. No final summary yet; zero emitted error events does not prove zero
  accumulated issues. Do not translate object counts to byte/time completion.
- The native sample `object-package-snapshots-live-sample.txt` observed ten
  simultaneous `readObjectBatch` payload workers and the owning thread joining
  them. That sample caught payload reading, not snapshot-worker execution; the
  snapshot implementation has separate native and forced-copy/TSan tests.
  Launch/cache/storage conditions differ from the old run, so no controlled
  throughput multiplier is claimed.
- At **7:29 elapsed**, the same writer had imported **1,917** new objects and
  skipped **1,409** existing objects; cumulative confirmed count is **3,326**.
  It has progressed beyond small descriptors into JPEG preview payloads. The
  latest observed event was index 3326, `Photos/.previews/cd13552a3a6214e58c38879358ab160ecddb9e6ea60b03fd13638e7a00f1bf8f.jpg`,
  38,874 bytes. No summary yet: all-drive packaging and final source/payload
  audit remain outstanding. The first Files object has now also been reused.
- Source SHA-256 of newly committed Photos descriptor `b61053...706c.societyphoto`
  independently matched its event (`736326c1be7aa72e2046410447ce734bfa11442c9994358aef5d66f3c9cd47f6`).
  This is a source comparison, not the final package extraction audit.
- The JPEG preview source at index 3326 also independently matched its committed
  SHA-256: `902e02077ee590515de2e81a9b200322d5f5177b55b9904db15d1dfa2f6a7f2c`.
- Final prior-log comparison covers all 1,409 reused objects with an evidence
  distinction: all 1,408 Photos entries match every current event field. The
  oldest Files import event predates expanded progress logging and contains only
  path/key/version/bytes; those fields match exactly (`object-1859fc4b59c1a7fae70d281f54cdf0c0`,
  version 1, 8,196 bytes). Missing historical digest/actor fields are not a mismatch
  and must not be presented as independently compared old fields.

## Current handoff: persistent compact-index increment

- Sole actual drive writer: handle **1627**, PID **14995**, installed parallel-read
  CLI, log `build/object-package-live-all-parallel-read.jsonl`. All nine namespaces
  remain included. Latest inventory: **10,258 files / 198,341,676,161 bytes**,
  zero inventory issues, 348.247 s. It reused 1,152 Photos objects; all identity,
  index, version, content hash, validation and session fields in the previous
  256-object committed batch matched exactly. The earlier Files object sorts
  later. The continuing run has now committed another 256 objects, bringing the
  confirmed cumulative total to **1,409**; it remains live, not a finished run.
- The new live sample found a serial staging-source `openat` wait in
  `ObjectSource`, before the parallel payload-read stage. Thus worker support is
  not evidence of an observed end-to-end throughput gain. Sample:
  `build/object-package-parallel-read-live-sample.txt`. No second actual writer
  or live database observer was started.
- The observed 139-second first index traversal motivates physically separating
  current traversal rows from revision/session/payload storage. Database schema 3
  adds a compact integer-keyed `current_index`; its publication trigger shares
  each mutation's atomic transaction. Index traversal no longer joins revisions.
  Schema 1/2 migration backfills the projection without changing validation keys.
  This is currently a source/build change, **not installed into the live writer**.
- New Qt-free index regression: old code failed `compact index requires schema 3`
  in 29.19 s, exit 1 (`build/object-index-red-tests.log`). The initial build attempt
  needed CMake regeneration before the new target existed; after configure it
  built and produced the expected contract failure.
- Updated full build succeeded (`build/object-index-build.log`); the first
  regression passed in **148.56 s**, handle **49005**, terminal exit 0, output
  `build/object-index-tests.log`. It checks
  all projected fields against authoritative heads, seven-row keyset pages,
  imports/revision/move/tombstone, rollback, reopen, schema-2 backfill and a
  single-table range query plan.
- Initialization and the complete schema migration now share one transaction,
  avoiding extra durable commits per schema step. A forced trigger-name conflict
  regression checks rollback after projection backfill. Fixture roots are pinned
  to `build/`. Final delivery validation is running in handle **34266**: rebuild,
  final compact-index CTest, streaming/legacy regression, actual-process CLI
  crash/resume/audit, staging installation and installed-only Qt-free consumer.
  Logs use `build/object-index-final-*`, `object-index-store-tests.log`,
  `object-index-cli-tests.log`, `object-index-stage.log` and
  `object-index-consumer-*`. No canonical/live writer replacement is authorized
  by a stage start alone; inspect terminal outcomes before replacing it.
- No actual-drive first-read improvement is yet established, and all-drive
  migration/audit remains incomplete.
- The continuing parallel-read writer subsequently committed **256 new objects**,
  index keys **1154–1409**, work session
  `session-1c6c008a13b8b0ab9640a4332bce5282`. Cumulative confirmed imports across
  runs are now **1,409**. The first new key is
  `object-dedab4f221ec366071547404bc34af6c`; the last is
  `object-7e7a574ce12a4f64045b943bdc636fd8`. This is positive real commit evidence,
  not a full-drive summary, controlled speed comparison or byte-progress metric.
- The final compact-index CTest **passed in 146.54 s**, total CTest time 149.57 s,
  including the failed-migration rollback case. The full final build also passed.
  Delivery handle **34266** is now running the streaming/legacy store regression;
  the CLI recovery, staging and installed-consumer gates follow only on success.
  Do not infer completion of those later stages from the compact-index pass.
- An independent SHA-256 read of the last newly imported source,
  `Photos/6aa552a352a2b71e40dca2fc172c5cc7a9e7de171753653d3e8a6975a427e5d0.societyphoto`,
  matched its committed event:
  `155f73ce0d712d4136f1616f68846167ca4501b0316dd1661649cb59f961a1c5`.
  This verifies that source digest, not full-package payload extraction or the
  complete mapped-drive audit. The actual writer remains the schema-2 binary;
  the schema-3 index source is not yet canonically installed.
- The final streaming/legacy ObjectStore regression passed in **269.63 s**
  (0.04 user / 0.05 system). A native sample found WAL `unixSync` / `fcntl`
  waits during import, not CPU saturation. Delivery handle **34266** advanced
  to the CLI process-kill/resume/audit gate; staging and installed-consumer
  verification still follow only if that gate succeeds.
- Final delivery chain **34266 completed with exit 0**. The unchanged full
  256-object process-kill/resume/source-payload audit passed in **294.11 s**.
  Staging installation succeeded under `build/object-index-stage`; a fresh
  installed-only consumer configured/built and passed **42.68 s** (CTest 43.74 s).
  Its linked libraries are SQLite, libc++ and libSystem only, no Qt.
- Build/staged executable SHA-256 matches:
  `0470b84b022acfc121bac73e6bfa80fd306af90859c7521e027362b11f4a4ff0`.
  Canonical installed executable remains the live schema-2 version:
  `f027231370e6cc634d5106b910ab0e56e0669001988cc5e215d8f1394cddb704`.
  No new Society app bundle or physical-iPhone execution is established here.
- A later sample of the same actual writer found **serial snapshot cleanup**
  after its successful batch, in `ObjectSource::~ObjectSource` / `remove` /
  `unlink`, log `build/object-package-parallel-second-batch-sample.txt`.
  Together with the earlier serial snapshot `openat`, snapshot creation and
  cleanup are measured remaining bottlenecks outside parallel payload reads.
  Bounded snapshot lifecycle concurrency is a relevant next optimization, not
  an implemented or tested feature. Do not interrupt/restart the real writer
  just because a poll interval elapsed; no completion summary has appeared.

## Bounded parallel payload-read increment

- The existing installed 256-file writer (handle 70850 / PID 5649) finally
  committed its first additional 256 objects without errors. Combined with the
  prior 897 objects, the observed committed total was 1,153. It was then live;
  completion of a first batch is not completion of the real drive migration.
  The CLI currently emits accumulated per-item failures only after `package`
  returns; no error events during a live run is **not** proof of zero accumulated
  issues. Use committed events for positive progress and the final summary for
  the whole-run error count. Live failure reporting still needs improvement.
- Its native sample found serial payload `read` waits inside `importFiles`.
  The new internal `ObjectBatchRead` input stage overlaps those reads using
  hardware concurrency, capped at 64 and at the number of files. Every source
  size is checked before payload allocation; captured data is capped at 16 MiB
  plus bounded caller-owned input buffers. SQLite is used only by the owning
  thread, publishing in original order inside the same atomic transaction.
- Source identities are checked after the read and before each publication.
  Read errors/cancellation reject the batch, and every started worker is joined
  before buffers are destroyed. Individual large-file streaming stays intact.
  This changes concurrency, not durability, scope, key or validation formats.
- Red build: the new reader contract initially failed on the missing private
  header (`build/object-batch-read-red-build.log`). The full updated build then
  succeeded; the serial/parallel binary-content contract passed in 10.23 seconds.
  A standalone ThreadSanitizer build/run passed with no reported races
  (`build/object-batch-read-tsan-build.log`, `object-batch-read-tsan-tests.log`).
- The integrated batch test now mixes a binary chunk-boundary/tail file, an empty
  file and repeated ordinary content, then extracts and verifies the result.
- The targeted run finished **3/4**: reader 10.23 s, integrated atomic batch
  107.64 s, and provenance reconciliation 85.98 s passed. CLI kill/resume/audit
  exceeded its 300-second CTest limit (300.04 s); do not call this a green suite.
  Output: `build/object-batch-read-tests.log`, terminal handle 43684 / exit 8.
- The final reader regression also exercised actual payload read denial after
  successful metadata inspection and passed in 0.93 s. The platform Threads
  target is exported through the installed CMake package for non-Qt consumers.
- Delivery validation is running in handle **64243**: final rebuild, verbose
  reader test, refreshed TSan build, staging install under
  `build/object-batch-read-stage`, fresh installed-only consumer build/test,
  refreshed TSan run, standalone full CLI retry (same 256-file scope and its
  unchanged per-call timeout), then the streaming ObjectStore regression.
  Logs use `build/object-batch-read-*`; the standalone CLI log is
  `object-batch-read-cli-retry.log`. Final build/reader/staging steps completed;
  the fresh installed-only consumer passed in 92.60 s and the refreshed TSan run
  also passed, explicitly exercising worker read denial. The standalone CLI
  retry passed in **244.46 s** (0.72 user / 1.81 system), preserving all 256
  committed objects and completing the source/payload/revision audit. This does
  not erase the earlier CTest timeout. The streaming-store regression is now
  running (PID 13121 at observation). Inspect each stage and terminal exit;
  later stages have not necessarily run. Only a staging prefix is written. Do
  not install over or duplicate the live actual writer while these run.
- After the passing CLI gate, the actual old writer was revalidated and sent
  SIGINT to prepare the parallel-read upgrade. Handle **70850** / PID **5649**
  then completed with **exit 130**. Its final summary reports imported=256,
  skipped=896, revised/errors=0, cancelled=true, 2483.63 seconds. The cumulative
  committed count is 1,153 including the first Files object. Close completion,
  not the earlier signal or summary alone, was checked before replacement.
- The streaming ObjectStore regression passed in **292.78 s** (0.04 user /
  0.06 system); handle **64243** completed with exit 0. Its sampled wait was
  SQLite constructor/journal `unixSync`, and the old writer's shutdown sample
  showed `endSession` / WAL `unixSync`, not CPU saturation.
- The container identifier and both source volume UUIDs were revalidated;
  approximately 2.59 TB remained available. Only after the actual writer and
  test chain terminated was canonical installation started, followed by a
  no-writer index measurement of the current actual package. Logs:
  `object-batch-read-install.log`, `object-index-live-before-parallel.jsonl`,
  `object-index-live-before-parallel-time.log`. This index population is the
  committed subset, not all 10,256 currently inventoried source files.
- Canonical installation completed; built/installed CLI hashes match:
  `f027231370e6cc634d5106b910ab0e56e0669001988cc5e215d8f1394cddb704`.
- The actual current 1,153-object index completed all three scans: **139,055 ms**
  first, **1.37692 ms** second, **1.05725 ms** third. Whole command: 148.37 s
  (0.01 user / 0.03 system), exit 0, handle 66098. A live sample located the slow
  first traversal inside `ObjectStore::index` / SQLite B-tree page `pread`, not
  just connection initialization. This was an observed first traversal, not a
  controlled OS cold-cache test. Warm traversal is fast at this population;
  first-use latency and full-drive population performance remain unproven/poor.
- After the index process finished, the newly installed parallel-read CLI was
  launched against the same package/container and all nine mappings. Live log:
  `build/object-package-live-all-parallel-read.jsonl`, execution handle **1627**,
  PID **14995**. It has emitted `inventory-start`; its background scheduling
  policy was removed with `taskpolicy -B`. Launch is not completion. Revalidate
  this handle before any additional actual writer or observer is started.

## Provenance-only reconciliation increment

- Inspection found that an unchanged source stamp caused the packager to skip
  an existing object before consulting the authorship callback. Later discovery
  of original provenance could therefore never enrich already imported files.
- A new Qt-free regression reproduced this: the old code failed
  `unchanged bytes must reconcile newly supplied provenance` in 64.35 seconds
  (`build/object-provenance-red-tests.log`).
- Existing objects now consult the supplied metadata before the skip decision.
  A different explicit document creates a revision on the same object/index key;
  nullopt or no reader preserves known provenance, explicit empty clears it,
  identical input stays idempotent, and invalid JSON cannot silently skip or
  advance the version. Old documents remain in immutable history. The current
  implementation snapshots/revalidates the content for these revisions.
- The full SDK rebuild succeeded. The new regression passed in 154.48 seconds
  (`build/object-provenance-build.log`, `build/object-provenance-tests.log`),
  covering stable hashes/source stamps, an empty payload diff, changed validation
  keys and complete historical documents. A separate staging installation
  completed under `build/object-provenance-stage`; it does not replace the live
  canonical CLI. The running real migration has no authorship callback, so it
  keeps its already tested behavior and is not restarted for this change.
  The new regression executable links only SQLite, libc++ and libSystem;
  `build/object-provenance-linkage.log` confirms no Qt dependency.
- Live handle **70850**, PID **5649**, was revalidated. It had skipped all 896
  prior small objects, reported no errors, and had not yet committed its first
  new 256-file batch. `build/object-package-live-256-first-batch-sample.txt`
  located it inside batch ingestion's `ifstream/read/fread/__read_nocancel`,
  with peak footprint 12.4 MiB. Read latency is an additional observed bottleneck,
  not just commit synchronization; this is not evidence of successful new imports.

## Current increment: bounded 256-file batches

- The same all-nine-section writer completed inventory with 10,263 files /
  198,338,701,645 bytes, zero issues, in 123.838 seconds. This is a later live
  inventory, not a controlled speed comparison with the earlier scan.
- At the pre-upgrade observation, its JSONL contained 896 committed imports and zero
  errors, in addition to the earlier Files object. PID 98369 / handle 91645 was
  then the sole actual writer. Counts are observations, not a final migration
  summary. The source SHA-256 of one committed Photos descriptor independently
  matched its event; this does not replace package-payload or full-drive audit.
- A native sample found the active ingest waiting in SQLite WAL commit /
  `unixSync` / `fcntl`, with low CPU use. The new implementation expands the
  small-file count limit from 32 to 256 while retaining the 16 MiB payload cap.
  It also caps aggregate actor-profile and provenance JSON at 16 MiB (counting
  the actor profile for every member). Durability settings remain unchanged.
- The direct-import API preflights metadata totals; the packager splits at the
  budget boundary without losing the next source. Tests cover atomic 256-object
  publication, 257-file rejection, metadata overflow and rollover, plus actual
  process kill/resume/audit. The initial red build failed on the old 32-file limit.
- The SDK build completed. The targeted CTest run passed batch (86.92 s), actual
  CLI crash/resume/audit (214.41 s), and authorship (150.16 s), but packager hit
  its 240-second limit (reported 241.73 s). This was **3/4**, not a green suite.
  A native sample located the packager in a new ObjectStore constructor's SQLite
  commit / `unixSync` / `fcntl`, not in a failing metadata-budget assertion.
  Logs: `object-batch-256-build.log`, `object-batch-256-tests.log`,
  `object-batch-256-packager-sample.txt` under `build/`.
- The rollover fixture now reuses its already-open store/session instead of
  initializing a third durable database. It still verifies all 17 metadata-heavy
  objects, the 16+1 commit boundary, exact provenance and source stamps, and
  staging cleanup. The rebuilt standalone retry **passed**, exit 0, in 273.94 s
  (0.10 user / 0.09 system); output is in
  `build/object-batch-256-packager-retry.log`. This does not erase the earlier
  CTest timeout or establish a passing 240-second performance bound. The retry
  sample reached `endSession` / WAL commit / `unixSync` / `fcntl`.
- A separately staged installation and installed-only consumer passed (79.54 s).
  `otool -L build/objects-256-consumer/objects_installed` shows system SQLite,
  libc++ and libSystem only, no Qt. Logs use `object-batch-256-consumer-*`.
- Following the passing CLI recovery/installed-consumer gates, SIGINT was sent
  to PID 98369 to prepare the batching upgrade. Handle 91645 subsequently ended
  with **exit 130**. Its final summary reports imported=896, revised/skipped/errors=0,
  cancelled=true, 2072.41 seconds; database close finished later. The earlier
  Files object is separate, so 897 objects have committed across the actual runs.
- Only after terminal exit was confirmed was canonical SDK installation started
  (`build/object-batch-256-install.log`). All nine mappings remain required on
  resume; no full-drive completion or measured 256-batch speedup is claimed.
- Canonical installation completed. The built and installed CLI SHA-256 values
  match (`fdc8721a228ff647f3842c7cea201f647079fb86931c3a1485f24dfdedfc990e`),
  and the installed header exposes the 256-file limit. The new installed CLI was
  launched with the same package/container and **all nine section mappings**;
  only the existing explicit Files OS-directory exclusions remain. Live output:
  `build/object-package-live-all-256.jsonl`, execution handle **70850**, PID **5649**.
  Its new inventory completed: **10,256 files / 198,341,151,568 bytes**, zero
  issues, 87.2267 seconds. The live tree differs from the earlier 10,263-file
  observation; this is not a fixed-dataset benchmark. It has skipped the 896
  previously committed small objects without errors; no new import was observed
  at that checkpoint. Resume is working, but migration remains incomplete.
  Comparing all 896 old imported events with their resumed skipped events found
  zero changes to the key/index/version/hash/validation/session/actor fields.
  Revalidate this process/handle before starting anything else. Its background
  policy was removed with `taskpolicy -B`; no measured speedup is inferred.

## Small-first ingestion and first real object

- The previous turn was progress: buffered/batched storage passed targeted tests
  and installed; the old unproductive ingest was closed safely. No package process
  was still live when this increment began.
- The actual container ID and both mapped volume UUIDs were revalidated. A first
  Files-only attempt failed with SQLite `locking protocol` while a read-only
  diagnostic connection was still recovering/reading the package. That diagnostic
  subsequently exited; the package had zero objects. The next attempt succeeded.
- `build/object-package-live-files-retry.jsonl` records the first committed actual
  object: `Files/.DS_Store`, 8,196 bytes, version 1,
  `object-1859fc4b59c1a7fae70d281f54cdf0c0`, in session
  `session-34c66de6e982a2f51fbdab3eef06ebd4`. Its final summary reports imported=1,
  errors=0, cancelled=false, 192.317 s; the process later exited 0 after database
  close. This is filesystem metadata, not proof that user images/models are done.
- The new CLI schedules the full inventory smallest-first with path tie-breaking.
  A red test demonstrated that the earlier lexical order committed a 128 MiB file
  before all small objects; the changed CLI passed the complete kill/resume/audit
  regression in 124.61 s. Scope is not reduced; all mapped files remain required.
- Per-item event coverage now requires index key, content hash, validation key,
  recorded session and actor to survive resume unchanged, with an explicit unknown
  original-authorship flag. Its initial red run failed on missing `indexKey`.
  The added fields report committed records without repeated observer DB opens;
  final green/install results are recorded separately when available.
- The completed record-event regression passed in 141.03 s. It independently
  checks SHA-256 values, preserved index/session/actor/validation fields on resume,
  and new hash/validation key with the same index key on revision. Build and
  canonical SDK/tool installation completed (`object-record-events-build.log`,
  `object-record-events-tests.log`, `object-small-first-install.log`).
- The updated installed tool was then launched for **all nine sections**, including
  Photos and the separately mounted Files volume. It resumes the same actual
  package; no second actual writer is running. Live output is
  `build/object-package-live-all-small-first.jsonl`. Launch is not completion;
  inventory, individual commits and the final audit still need inspection.
  At the handoff observation the live process was PID 98369, execution handle
  91645, and had emitted `inventory-start`. Revalidate that same handle/PID before
  deciding it stopped; do not launch another writer on an observation timeout.
- A live sample of the real one-file run blocked in `beginSession -> SQLite WAL
  commit -> unixSync -> fcntl`. This is a device synchronization wait, not evidence
  of hashing consuming all CPU. Durability settings were not weakened.

## Latest observed state: bounded batches and buffered reads

- The complete nine-section inventory finished: 10,262 files, 198,338,058,004
  bytes, zero traversal issues, 2,260.27 seconds. This supersedes the older
  10,259-file inventory below; live source files can change.
- A separate eight-section ingestion (Photos not included yet) inventoried 198
  files / 196,810,872,202 bytes in 49.653 seconds. It created a real work session
  and the package database. At the last read-only count, zero objects had committed;
  growing WAL bytes are not proof of successful imports or a completed migration.
- Bounded small-file batches now preserve individual object identities while
  committing at most 32 files / 16 MiB atomically. The actual CLI crash test kills
  after the first callback and verifies all 32 committed members survive/resume.
- The final buffered build passed four targeted CTest suites (batch, CLI,
  packager, authorship): 366.51 seconds total. The independent ObjectStore
  boundary/tail contract passed in 272.57 seconds (0.03 user / 0.04 system).
  Logs: `object-buffer-tests.log`, `object-buffer-boundary-tests.log` in `build/`.
- The earlier batch full suite passed 11/12; ObjectStore exceeded its unchanged
  60-second CTest limit. Its isolated rerun passed in 200.92 seconds. This remains
  a timeout failure in that full-suite run, not a green full suite.
- A live ingestion sample blocked in libc++ filebuf/fread/read. The installed
  libc++ header defaults that buffer to 4 KiB. Ingest and source-audit streams now
  request a 1 MiB input buffer, with chunk-boundary/tail correctness coverage.
  A separate native 1 MiB-read probe of 16 MiB took 88.75 seconds (189,041 B/s),
  demonstrating severe underlying storage latency, not a proven app speedup.
- The live ingestion process was started before these changes. Installing new
  SDK files does not update an already-running process. Do not start a concurrent
  replacement writer or claim that its live speed reflects this implementation.
- That old ingestion was subsequently cancelled normally to reduce foreground
  storage contention: exit 130, imported/revised/skipped/errors all zero,
  cancelled=true, 2,750.2 seconds in its final summary. SQLite close itself stalled
  in WAL `ftruncate` before final process exit. No replacement ingestion is running;
  the all-nine-section packaging requirement remains unfinished.
- A temporary blank RAM-device diagnostic could not mount without elevated
  credentials. It ran no tests, was detached, and its empty scratch directory
  was removed. No real Society volume was detached or source file removed.

## Requirement audit

| Requirement | Current authoritative evidence | Remaining work |
| --- | --- | --- |
| Logical objects independent of directory classification | ObjectStore key/path separation; move keeps key and index | Integrate the ordinary directory projection and native file lifecycle |
| Per-file unique key, index key, version | SQLite object heads and immutable revisions; CAS/concurrent-writer tests | Assign and verify these values for every real drive file |
| Society validation key and content hash | SHA-256 known vectors, metadata hash chain, payload and ancestor tamper tests | Full real-drive audit; standard-key policy must not be misrepresented as authentication |
| Author | Schema 2 preserves exact FileAuthor actor profile and independent Authorship document; existing-model round-trip test | Discover available source provenance during real ingestion; unknown creators remain explicitly unknown |
| Modification diff and journal | Reversible chunk references, append-only mutation history, source snapshots and original-removal extraction test | Capture application/Finder/external changes and reconcile renames/deletions |
| Work-session records | Explicit start/end, device/description, closed-session mutation rejection | Bind application editing/ingestion sessions to the shared record |
| Very fast complete index traversal | Compact keyset index excludes payload/full author documents; page and tombstone tests | Real large-index measurements and actual Society consumer adoption |
| iiFileProvider owns the feature | New C++23 Objects target; no Qt/upward SDK dependency | Full integration with existing value models and container/sync layers |
| Package Society drive files | Capacity checks, source snapshots, per-file atomic commits; cancellation and real-process kill/resume tests | Run migration of all mapped sections and full source/payload verification |

## Verified in this increment

- Red test first failed with missing ObjectStore symbols (`build/object-store-red.log`).
- Full SDK build and CTest: 8/8 targets passed (`build/object-store-build.log`,
  `build/object-store-tests.log`).
- Qt-disabled standalone build and object-store test passed
  (`build/object-store-standalone-*.log`).
- Installed-package-only consumer passed (`build/object-store-consumer-tests.log`).
  `otool -L build/objects-consumer/objects_installed` lists only system SQLite,
  libc++ and libSystem, not Qt.
- Installed to the canonical `~/.local/SDK/iiFileProvider` prefix
  (`build/object-store-install.log`). This does not update Society's running
  executable or migrate its files.
- Real container identity was read from `/Volumes/Society Data/.society-drive.json`.
  The Files section maps by its volume UUID to `/Volumes/Society`; other sections
  are under `/Volumes/Society Data`. Do not omit the separately mounted Files tree.
- No actual Society file content, container manifest, sync catalogue or author
  history was modified by this increment. Inventory is read-only.
- Completed inventory: 10,259 regular files / 198,336,695,850 logical bytes;
  2,589,102,186,496 bytes available at observation time. No traversal errors or
  symlinks were reported. OS-managed `.Trashes` and `.fseventsd` roots on the Files
  volume were explicitly excluded; this is a scoped inventory, not a forensic
  filesystem image. Section counts: Deleted 8, Files 1, Generation History 94,
  Models 92, Photos 10,064; remaining four sections empty.

## Next sequence

1. Use `build/object-store-drive-inventory.json` as the initial inventory;
   revalidate volume identities, free space and the changing file set before writes.
2. Preserve available source FileAuthor/Authorship without inventing historical creators.
3. Run the resumable packager, inspect explicit per-item outcomes, and perform
   source/payload audit. Keep originals and account for any crash orphan staging.
4. Integrate object identity with SocietyDrive/FileOperations, sync and native
   projections, then adopt indexed queries in relevant consumers.
5. Measure large/full index traversal, run cross-platform and installed-consumer
   gates, package all real drive files and audit every mapped item and metadata
   requirement before marking the goal complete.

## Schema-2 and packager increment

- Metadata red test rejected the then-unimplemented malformed-document case
  (`build/object-metadata-red.log`); round-trip tests now retain original
  creator/contributors separately from the work-session actor.
- Captured schema-1 fixture migrates without rewriting old validation keys;
  subsequent schema-2 revisions, old extraction and mixed chains validate.
- `ObjectSource` captures descriptor-based APFS clones or checked copy fallbacks;
  modified originals cannot change captured snapshots. Cancellation rolls back
  only the in-flight object.
- `ObjectPackager` accepts all mapped section roots, explicit exclusions, stable
  stamps, per-file capacity checks, authorship callback and resumable CAS writes.
- CLI regression kills an actual child after the first committed object. Restart
  preserves its key/version, adds the remaining object once, audits source and
  payload, then revises exactly the changed file. SQLite integrity is checked.
- Full SDK tests passed 11/11, Qt-disabled release tests passed 3/3 and installed
  consumer passed 1/1 before the final metadata-only/worker-pool optimization.
  Updated builds/test logs use the `build/object-packager-*` prefix; recheck these
  logs for the final optimization rather than treating earlier passes as proof.
- Installed consumer links only SQLite, libc++ and libSystem (no Qt).
- A read-only real-drive probe showed prolonged `openat` waits on the preview
  tree. POSIX inventory now uses no-follow `fstatat` metadata instead of opening
  every payload, with bounded hardware-concurrency workers. The old read-only
  probe was gracefully cancelled after this implementation change; it did not
  create a package. Do not misreport this as a completed whole-drive inventory.
- The metadata-only probe also encountered a kernel `getdirentries64` wait while
  enumerating `Photos/.previews` (`build/packager-parallel-inventory-sample.txt`).
  A kernel-log capture (`build/object-package-disk-diagnostics.log`) records APFS
  disk13 transaction synchronization of about 24 seconds and flush preparation
  of about 40 seconds. This is evidence of filesystem latency, not evidence of
  corruption or a completed file scan. Do not attempt destructive repair or
  stop unrelated generation workers on this evidence alone.
- The worker-pool version passed 11/11 main, 3/3 Qt-free and 1/1 installed tests.
  An additional red test then exposed FileAuthor accepted in the Authorship slot;
  the format-specific validation was fixed. Its latest main-suite run hit the
  object-store test's 60-second timeout during this I/O-latency interval, so a
  serialized rerun is required before claiming final all-pass proof.
- Final format-specific source: the serialized main SDK suite passed 11/11 in
  68.02 seconds (`build/object-packager-tests-serial.log`). The Qt-disabled
  suite still hit its 60-second object-store timeout; an isolated executable run
  is retained for diagnosis, not counted as a pass until it exits successfully.
- The tested main build was installed to the canonical SDK prefix and staging
  prefix; the installed-only consumer was rebuilt and its CTest completed with
  exit 0 (`build/object-packager-final-install.log`,
  `build/object-packager-consumer-tests.log`). This updates the SDK, not Society's
  bundled executable, and does not make the real-drive migration complete.
- The actual drive inventory remains in flight with only `inventory-start` in
  `build/object-package-live-inventory.jsonl`. No real package creation command
  has been issued. Do not infer completion from the earlier inventory totals.
- Society's live sample (`build/society-storage-wait-sample.txt`) showed
  `SyncWorker::refreshWatches` eagerly collecting `QDir::entryInfoList` from
  `Photos/.previews`. During normal quit the GUI waits in
  `Controller::closeAndWait` for that worker. The queued watch-count limit is
  applied only after collection, so it does not bound enumeration cost. A future
  fix must preserve ownership-handoff guarantees, not simply drop the wait.
- Normal quit timed out. Only the known installed Society process was terminated
  and a restart requested; no drive files, journals, mounts or separate model
  worker were removed/reset/stopped. Screen recovery is not yet confirmed.
- Real container and Files-volume UUID mappings were revalidated before writes;
  `.society-objects` did not yet exist at that check. The drive still has not been
  packaged by this increment as of this ledger entry.

The current source has only macOS build/runtime proof. iOS/Windows/Linux builds,
real-drive migration, external lifecycle synchronization and application-level
index adoption remain unverified. Crash recovery is now tested on macOS fixtures,
not yet on real data or every platform.

## 2026-09-28 resumed live package

- After verifying the former writer had terminated and closing its stale session
  through the CLI, the canonical package writer was resumed once using the
  current `build/iiFileProviderObjectPackage` binary and the nine revalidated
  drive mappings above. No concurrent writer or destructive source operation was
  started.
- The current JSONL is
  `build/object-package-live-resume-20260928.jsonl`. At the latest read-only
  checkpoint it contains 2,124 imports, 5 revisions and 4,499 unchanged objects
  (6,628 current objects accounted for), with no `error` event. The writer is
  still running; this is partial progress, not completion.
- Before the resumed writer began, the package database passed SQLite
  `integrity_check` and `foreign_key_check` (`ok`, no violations) with 4,504
  objects, 4,504 current-index entries and 4,504 live objects. The previous
  stale packaging session was closed only after its writer process was verified
  absent. Do not inspect the SQLite database while the resumed writer is live.
- The complete source inventory most recently verified 10,292 files,
  153,069,688,624 bytes and zero path/read issues across the nine mappings. A
  full post-package audit is still required after the writer exits normally.
- Live revalidation on 2026-09-28 found the same writer, PID 58432, still active
  against the same package and mappings; do not open SQLite or start a competing
  writer. Its JSONL now records 5,665 imports, 4,499 unchanged skips and 5
  revisions, reaching current index key 10,169, with no error or terminal summary.
  Those outcomes cover 10,169 visited paths / 1,534,585,289 source bytes so far;
  the largest visited object is still only 883,276 bytes, so most source payload
  bytes have not yet been packaged.
  Read-only process samples during quiet intervals showed the main thread in
  SQLite's automatic WAL checkpoint (`guarded_pwrite_np`, then `unixSync -> fcntl`);
  subsequent JSONL events confirmed the writer resumed. Treat this as a storage-sync
  wait, not CPU-bound hashing or a completed package. After normal exit, rebuild/install
  the newer audit-capable CLI if needed, then run a fresh all-mapping source/payload/index
  audit before deciding whether a delta packaging pass is required.

## 2026-09-29 WAL checkpoint tuning follow-up

- Added a configurable SQLite WAL auto-checkpoint page threshold to the object
  store and packager CLI, retaining `synchronous=FULL` and `fullfsync=ON`. The
  source-compatible default is 1,000 pages; `--wal-autocheckpoint-pages` can
  raise or disable automatic checkpoints. Documentation describes the WAL-space
  tradeoff. Build targets `iiFileProviderObjectPackage` and
  `iiFileProvider_object_batch` built successfully; the batch test passed in
  103.82 seconds.
- The crash/resume CLI regression was extended to run with a high threshold and
  assert its emitted setting. It did not complete: CTest timed out the test at
  304.19 seconds while a separate real Society writer was simultaneously blocked
  in SQLite's full-durability WAL checkpoint path. A sample of the fixture CLI
  showed `sqlite3Close -> sqlite3WalCheckpoint -> unixSync -> fcntl`; this
  confirms that suppressing intermediate auto-checkpoints does not suppress the
  required close-time checkpoint. The timed-out test is not a passing regression.
- The real Society writer (PID 58432) is still the only database writer. Its
  JSONL contains 5,724 imports, 4,499 skips and 5 revisions, reaching index key
  10,228 / 10,228 visited paths with no error event, but it has emitted neither
  a terminal summary nor a close event. Source bytes are still only about 1.51
  GB of the 153.07 GB inventory. It remains in `sqlite3WalDefaultHook ->
  sqlite3_wal_checkpoint_v2 -> sqlite3WalCheckpoint -> unixSync -> fcntl`.
  SIGINT was requested; the process remains in uninterruptible kernel I/O, so
  cancellation is not yet confirmed and the database must not be reopened.
- The mapped volume is an APFS sparsebundle disk image at
  `/Volumes/Storage/Society.sparsebundle`; it has ample reported free space. This
  establishes where the nested-volume sync is occurring, but not a media fault
  or corruption diagnosis. No destructive repair or remount was attempted.
- Next safe gate: wait for PID 58432 to exit and inspect its JSONL terminal state;
  only then close any unfinished session and resume once with the rebuilt CLI's
  higher threshold. Run focused crash/recovery tests without concurrent real
  drive I/O, followed by a complete mapped-source/payload/index audit and SQLite
  integrity checks. The package goal remains incomplete.
- Follow-up on the next check: PID 58432 exited and the log ended with
  `{"event":"summary","imported":5724,"revised":5,"skipped":4499,"errors":0,"cancelled":true,...}`.
  The session was already closed by that cancellation path; an explicit second
  `--end-session` correctly reported `object work session is closed`. No package
  writer remains active. The full SQLite `integrity_check` was attempted after
  exit but interrupted after several minutes blocked in `pread` against the
  nested volume, so integrity remains unverified (do not describe this as a
  pass).
- After the real writer exited, the focused crash/resume CLI regression was
  rerun alone and passed 1/1 in 171.29 seconds. It exercises a killed child,
  session closure, resume with the configured checkpoint threshold, payload and
  source audit, revision identity, and SQLite fixture integrity. The earlier
  304.19-second timeout occurred during concurrent real-volume I/O and remains
  recorded as a failed run; the serialized pass is the current test result.
- The user explicitly requested that the process be closed. Therefore the real
  drive writer was not restarted with the higher threshold. Packaging remains
  partial at 10,228 / 10,292 visited paths (5,724 imports, 4,499 skips, 5
  revisions), approximately 1.61 GB of source payload bytes visited out of
  153.07 GB. Full audit, DB integrity, complete package and index timing are
  outstanding; user-requested stop has been honored.
