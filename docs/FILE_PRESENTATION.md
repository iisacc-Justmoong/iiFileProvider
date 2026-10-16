# File details and embedded previews

iiFileProvider 0.7 owns the persistent presentation of an iisacc file asset as
well as its filesystem creation and delivery. An asset retains its original
data fork and filename extension. The provider never converts an imported file
to another document format merely to attach a preview. Generated images remain
PNG; a separately saved editor canvas remains IISC.

## Representation

`iiFileProvider.Presentation`, schema 1, embeds the following in one JSON document:

- Source identity/version and a native change token.
- Filename, extension, detected MIME type, byte size, nanosecond modification time
  and format-specific properties. Apple images also expose available camera,
  capture-time, exposure, ISO, orientation and color-model properties.
- Preview kind, MIME type, dimensions, base64 bytes and SHA-256 of those bytes.

The native file additionally carries the document in
`com.iisacc.file.presentation` on Apple, `user.iisacc.file.presentation` on Linux /
Android, or the `iisacc.presentation` NTFS stream on Windows where supported.
`PresentationRecord::embedded` reports the actual result rather than assuming
that every filesystem supports attributes or a document of that size.

The hidden sibling `.<filename>.iisacc-preview` is the portable asset companion.
It contains the preview bytes themselves, not a machine-local cache pathname.
Retain it when transporting an asset through a service that drops file attributes.
The object packager includes it and its existing classification companion in the
ordinary source inventory. User-facing Society trees and native Files projections
hide provider control documents even when public hidden files are requested.

The portable document journals the **resulting** native change token after an
attribute write. This avoids an attribute/ctime invalidation loop and detects
same-size edits that restore mtime, including changes outside the bounded head /
tail fingerprint samples. Missing/corrupt/stale companions are refused as cache
hits and regenerated. Preview checksums detect damaged derived bytes; they are
not signatures or authorization credentials. IISC source versions also account
for the committed WAL when present.
On Apple filesystems, canonically equivalent composed/decomposed Unicode names
share the same verified presentation; the native identity still pins the file.
Per-file publication locks and thumbnail token bookkeeping also use native file
identity, so these alternate path spellings cannot split an enrichment request.
Pinned native readers use `normalizeChangeTime` with their own stat/handle
identity and timestamps. It performs no path lookup, preserves external ctime
changes and normalizes only the provider's recorded attribute publication.
Replication hashing and chunk reads therefore retain content-version checks
without restarting when this process merely embeds a preview.

Derived companions are published with owner-only read/write permission so their
embedded content does not broaden filesystem access. The original source bytes
and permission bits are retained. Native metadata
publication can change ctime/change-time. Single-file Catalog creation/import/
registration therefore enriches **before** recording the physical catalog
version. Annotation edits keep the existing strict revision/content checks.
Editable keywords, ratings, labels, collections and custom annotations remain in
the independent `.iisacc.json` classification document and its catalog journal.

## Content support and limits

| Backend | Content preview |
| --- | --- |
| Portable C++23 | PNG/JPEG/BMP/GIF and supported stb raster inputs, with 16-Mpixel allocation admission; UTF-8 text/JSON; PCM16 and float32 WAVE waveforms |
| Apple native | Image I/O aspect-fit thumbnails with orientation; other available system PDF/movie/office thumbnail providers through Quick Look Thumbnailing |
| Swift source-only bridge | Native Image I/O thumbnails and UTF-8 text/information cards using the same portable schema |
| iiSocietyHelper domain adapter | Bounded, read-only IISC frame-0 composites through the existing iiSharedCanvas renderer and PNG encoder |
| Unsupported, corrupt or over-budget content | Explicit `information` card; never labeled as a rendered image/document |

Previews fit within 512 × 512, the document is at most 2 MiB and encoded preview
bytes at most 1 MiB. Image/document decoding is admitted only up to 32 MiB source
size; file details and information cards also work for larger ordinary files
without reading the whole payload. Text cards use a bounded leading excerpt.
Waveforms describe whether they cover all samples or only the leading sample
window. Apple Quick Look requests are cancelled after a three-second wait.
Format availability depends on the actual platform codecs; unsupported formats
keep their real original bytes and truthful information-card state.
An information card written by the source-only Swift bridge receives one native
decoder attempt when opened by a C++ consumer; usable previews remain reusable.

## Native API and creation lifecycle

```cpp
#include <iiFileProviderNative.h>
#include <iiFileProviderPresentation.h>
using iiFileProvider::native::File;
using iiFileProvider::native::Presentation;

File::write(destination, encodedBytes);       // atomic save, then automatic enqueue
auto record = Presentation::prepare(destination); // explicit synchronous result
auto cached = Presentation::load(destination);    // read-only, rejects stale data
```

Use `find_package(iiFileProviderNative 0.7 CONFIG REQUIRED)` and link
`iiFileProvider::Native`. The component is compiled C++23, with native Apple/JNI
adapters where applicable, and has no Qt or higher SDK dependency. Its existing
Python worker bridge calls this compiled component; it is not a new application
runtime or an interpreter implementation of presentation.

Completed `File::write`/publication, native output-stream close/destruction,
copy/rename, Qt `File`/`StagedFile`, `FileDevice::close`, `SaveFile::commit`, C
staging/copy/publication, and Python buffered-stream close notify the owner.
Swift write/create/copy/exclusive-document creation and descriptor finalization
do the same through the compiled Swift source adapter. Android output stream,
writable ParcelFileDescriptor close and AtomicFile completion notify the native
JNI component. The Android adapter exposes the same `isControlFile` rule without loading JNI for
read-only name queries. The native library is loaded on the first completed write.
Granted content URIs remain subject to their document provider; Society's local
backing-file close callback supplies the native source path.

Low-level borrowed descriptors/`FILE*` cannot signal a caller's arbitrary later
close automatically. Their consumer calls `Presentation::enqueue` /
`iifp_present_later` after finalization, or uses a monitored Society root.
Private staging, provider control state, hidden internal trees, logs and database
bookkeeping do not create recursive previews through generic stream hooks.
Explicit prepare and managed Society intake can still describe user `.log` and
database files as assets. An encoder failure before publication queues no final
asset and preserves an existing destination.

The C++ worker coalesces pending paths, bounds its queue at 4096 with producer
backpressure, and drains accepted writes at normal shutdown. Use
`waitForIdle(timeout)` / Python `wait_for_presentations()` for a deterministic
completion boundary. `cancelPending(path)` is the lifecycle barrier before
deleting an owned temporary tree. Provider temporary directories and Python
temporary-directory cleanup use it. `File::remove` removes its owned preview
companion after cancelling work. Unrelated companion files are never overwritten
or removed. Swift callers have `waitForPresentations()` for an explicit completion
boundary before disposing of externally managed fixture/output directories.

`FileCatalog::presentationDocument(key)` and the compiled CLI's
`presentation KEY` command expose verified detail/preview JSON. The metadata-only
`registerFiles()` batch retains its no-payload-read contract; use native intake or
enqueue completed assets when combining batch indexing with presentation.
Core writable file Channels notify on successful final close, and native
ServerHost uploads notify after publishing the complete destination. Atomic
Core publication already uses the notifying provider rename operation.

## Society intake and consumers

Opening a verified SocietyDrive registers the public sections, excluding Deleted,
with the native presentation monitor. It keeps the original native directory
identity and the expected manifest identity, rejects redirected roots and skips
private/control subtrees. Two stable observations precede external enrichment.
Traversal is sliced into 128-entry inspections with at most one decoder per slice;
unfinished traversals continue promptly, and completed passes repeat at two-second
intervals. The monitor uses one native worker, at most 128 roots and a bounded
observation table. It does not require an open Society window when a storage
consumer or daemon owns the selected drive.

The native Apple Files and FSKit bridges also notify the Swift provider after
content replacement and descriptor write/close, including publication by rename.
The Qt preview cache consumes provider-generated PNGs. Its thumbnail key uses a
native change token normalized across the same process's own attribute writes,
so a newly embedded thumbnail does not invalidate the other queued requests for
the same file. A later external edit still changes the key.

iiSocietyHelper registers its actual IISC decoder through
`Presentation::setRenderer(".iisc", renderer)`. The provider never imports or links
iiSharedCanvas upward. The callback reads a detached bounded preview and supplies
PNG bytes. The existing Helper preview URL remains a private runtime cache URL,
now populated from the provider's embedded result. Canvas edits enqueue a fresh
projection after closing their working file.

Society's file inspector reads these details asynchronously. In the file list or
grid, Space opens/closes the LVRS preview surface; Escape closes it and Open
retains normal file activation. Only a validated selected local child is read;
inspection does not download a remote original, and late results from an older
selection cannot replace the current details. This keyboard interaction is in
Society. System Finder support for custom document types additionally requires a
registered Quick Look extension; the common presentation document alone does not
register one.

## Read-only classification

`FileCatalog::classificationDocument(absoluteAssetPath)` exposes the published
classification without opening or creating a catalog database and without
reading the original payload. The bounded companion is parsed and validated by
iiFileProvider itself. The result includes the persistent file key, annotation
revision, title, description, keywords, rating, color, custom fields and collection
keys/names. Catalog-local paths and unknown document fields are omitted. An empty
string means unclassified; malformed, oversized, redirected or concurrently
changed data throws. Pending annotation writes still belong to the author's
catalog journal; this read-only API reports the portable, published copy.

Society's `SharedStorage::fileInformation` combines this projection with the
verified presentation. Its explicit `ready` / `absent` / `invalid` classification
state prevents a damaged annotation from hiding an otherwise readable preview.
Details of the native, application and authenticated host interfaces are in
[Society's file-information contract](../../iiSocietyContainer/docs/FilePresentation.md).

## Verification

Native tests cover actual raster-to-PNG conversion, float32 PCM details/waveform,
domain renderer registration, external recursive intake, XML escaping, Unicode,
same-size/restored-mtime edits, portable embedding, source-byte preservation and
invalid-preview rejection. Catalog integration tests cover annotation edits,
strict content versions and rollback of derived companions. Source ownership,
package consumers, Python, Swift, platform bridges, Society regressions and their
exact evidence boundaries are recorded in `FILE_PRESENTATION_VALIDATION.md`.
