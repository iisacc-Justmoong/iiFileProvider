# iisacc file creation and provision ownership

From version 0.6.0, iiFileProvider is the common owner of iisacc file creation,
opening, streaming, staging and publication. Products and format SDKs delegate
these operations to provider APIs rather than independently opening final output
files. Format validation/serialization remains with each domain SDK.

| Caller need | Provider API | Caller responsibility |
| --- | --- | --- |
| Native classified files | `FileCatalog::createFileWith`, `createFile`, `importFile`, `registerFiles` | Metadata, collections, encoder, pending companion recovery |
| Classified search/provision | `FileCatalog::search`, `lookup`, `read` | UI, transport authority, bounded result consumption |
| Existing Qt codecs | `File::openRead`, `writeWith`, `create`, `update`, `copyToUrl` | Domain validation/serialization, optional catalog registration |
| Path-only external codecs | `StagedFile`, `File::publish` | Write borrowed temporary path, validate before publication |
| Society object packages | `iiFileProvider::Objects` | Domain objects, authors, sessions, synchronization authority |
| Domain SQLite schemas | `Database`, `Statement`, `Transaction` | Schema, record semantics, domain revision rules |

The direction is `product -> domain/transport SDK -> iiFileProvider`. Native
Catalog/Objects/Temporary have no Qt dependency; existing adapters retain Qt Core.
Provider never imports an upper SDK, model engine, account client or renderer.
Native APIs do not run application logic through an interpreter or VM.

Use Catalog for user-visible classified files. Temporary codec data, database
internals and transport caches do not acquire user collections merely because
bytes were written. The owner explicitly registers results; implicit recursive
access to unrelated directories is outside the contract.

The SharedCanvas, document, MIDI, bitmap and XML adapters already use the common
paths listed in [FILE_CRUD.md](FILE_CRUD.md). Society Helper media export now
delegates file opening and final URL publication to provider; media validation
stays with Helper. Android selected `content:` destinations use the OS-granted
document stream inside the adapter. Portable companion export needs a filesystem
destination supporting the payload/companion pair; the generic single-stream URL
API transfers only supplied bytes.

Repository-controlled SDK and Product runtime factories, including deprecated
product source and Python AI workers, use provider entry points. The workspace
gate is `tools/audit_file_creation.py --check`; unreadable source is reported.
Generated builds, bootstrap/development tools, fixtures and vendored engines are
counted separately. Shipping Python entry points, plural `runtimes/` workers and
the TLS FTP host are runtime scope even when kept under a tools directory. External codecs receive provider-owned streams, staging
files or private directories. The gate is a source boundary check, not a syscall
sandbox for arbitrary shell commands or OS-managed stores.

`tests/dependency_direction.py` rejects upward edges and SDK cycles, including
Native/Catalog/Temporary component dependencies. New file-producing features
must select a provider entry point and retain domain regression coverage.

Source tests, installed native/Qt consumers and integration tests are distinct
evidence. A local build alone does not imply product deployment or device proof.

## Native and platform adapters

| Need | Entry point | Behavior |
| --- | --- | --- |
| Qt-free streams and descriptors | `iiFileProvider::Native`, `iiFileProviderStorage.h` | C++23, native platform calls, append/seek/exclusive/no-follow flags retained |
| Complete native output | `native::StagedFile`, `native::File::write` | Suffix-preserving private path, publish on success, discard unsuccessful stages |
| Existing Qt devices | `FileDevice`, `SaveFile`, `Directory`, `TemporaryFile`, `TemporaryDirectory`, `LockFile` | Preserve Qt errors, permissions, attributes and stream behavior inside the provider |
| Image serialization | `File::saveImage` | Caller-owned image codec, provider-owned atomic stream |
| Existing Qt SQL schemas | `File::prepareDatabase` | Reserve an empty missing database without truncating existing bytes before the domain driver opens it |
| Native SQL schemas | `native::openDatabase` | Provider opens storage; caller retains schema and query semantics |
| Python model workers | `ii_file_provider` | Native C ABI creation/publication, owned streams and codec staging |
| Native Swift | `iiFileProviderSwift` | Compile `IisaccFileProvider.swift` from the installed SDK |
| Android services | `FileStorage`, `iiFileProvider_add_android_storage` | Existing OS bridge retains granted content URIs and descriptor flags |

The native API imports no Qt and introduces no common VM/interpreter for app
logic. Python support serves existing model workers through a compiled storage
library. Android Java support is an OS bridge for existing services. Core Values
and PhotoLibrary Albums retain their independent dependency boundaries.
The Swift package contains architecture-independent source. iOS helpers locate
it outside the target sysroot and compile it with the selected native toolchain.

## AI output and control files

PNG generation, video frames, PCM/WAV, JSON reports, checkpoint conversion and
model archives use owned streams or staging. Generation History stays PNG;
editor persistence owns IISC conversion. Sampling, encoding and validation remain
with model/format modules. Path-only encoders borrow the provider's output path.

```python
from ii_file_provider import staged_path
with staged_path(output_png, overwrite=False) as temporary:
    image.save(temporary, format="PNG")
```

`IIFILEPROVIDER_NATIVE_LIBRARY` selects the native library explicitly;
`IIFILEPROVIDER_PREFIX` selects its installed support files. Missing native
support is an error, never a fallback to an independent final-file writer.
Temporary-directory cleanup is idempotent and tolerates completed stages moving
to their final names. Staged destinations are resolved when the stage is reserved,
so an encoder changing the current directory cannot redirect publication.
The native CMake package exports its Python support and license directories.
AI runtime packages include that adapter and the native shared library; macOS
workers can discover a bundled versioned library in `Contents/Frameworks` without
relying on a developer's global SDK installation.

`.iisacc-temporary.lock`, `.iisacc-file-provider` and `.iifile-stage-*` are control
artifacts. Lease sentinels persist to preserve interprocess lock identity. Python
workers use `provider.iterdir`/`rglob` to omit them from media/model payloads.
Portable `.iisacc.json` companions remain transferable file metadata. Internal
logs and databases do not gain user collections implicitly; owners explicitly
register user-visible classified results in Catalog.
