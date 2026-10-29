# iiFileProvider Core I/O adapter

Version 0.1.1 is the maintenance release accompanying iiFileProvider 0.7.2
and Native 0.8.1. Its existing I/O and dependency contracts are unchanged.

This optional C++23 consumer project composes iiSDKCore 0.13 and iiFileProvider
Native 0.7. The application builds and links it separately; neither SDK imports
the adapter or the other SDK. It is no longer a subdirectory of the provider's
build. Qt is not used.

`iiFileProvider::core::open_file` owns file creation, exclusive creation,
truncation and regular-file validation. It transfers the native descriptor/handle
into Core's `NativeFile::adopt`. Read/write/wait and EventLoop/AsyncIO semantics
stay with Core. Successful writable close, including the last Channel owner,
notifies Native Presentation exactly once. Read-only opens do not schedule it.

`iiFileProvider::core::AtomicFile` owns temporary creation and atomic publication.
It retains the old Core transaction's flush, cancel and no-direct-write-fallback
contract. Storage errors use Core's I/O result and native error categories.

```cmake
find_package(iiFileProviderCoreIO 0.1 CONFIG REQUIRED)
target_link_libraries(MyApp PRIVATE iiFileProvider::CoreIO)
```

```cpp
#include <iiFileProviderCoreIO.h>
auto channel = iiFileProvider::core::open_file(path,
    {ii::io::Access::read_write, ii::io::Creation::create_new});
auto transaction = iiFileProvider::core::AtomicFile::create(target);
```

Build this directory after building/installing Core and Native into Workspace
staging prefixes. Use `build/` for outputs. The relocated I/O example and atomic
transaction assertions run here; they are not removed from coverage when Core
is tested without storage dependencies.

This is the explicit migration from Core 0.12's `ii::io::open_file` and
`ii::io::AtomicFile`. Link this component and change the include/namespace.
Core's rooted `File::open` accepts an opener callback, for example
`File::open(root, relative, iiFileProvider::core::open_file, options)`.

The 2026-10-16 macOS arm64 Release validation passed 2/2 tests (29 integration
checks and the moved loop-I/O example) and a separately installed consumer 1/1.
The base Native provider was configured with Core and Qt discovery disabled and
passed its own 3/3 tests. Results are in `../build/coreio-*.log` and
`../build/core-independent-*.log`. The declaration-graph check includes this
adapter above both lower libraries. Other target-platform execution is separate.

`NativeFile.h` is a platform integration extension, not a portable file-opening
API. Its intptr_t ownership transfer is reserved for native adapters. Common
application code receives a Channel and does not access a native handle.

<!-- workspace-docs:readme:start -->
## Documentation entry points

- Human overview: this English `README.md`.
- Agent instructions: [AGENT.md](AGENT.md).
- Human documentation: [documentation index](docs/README.md).
- Structured agent documentation: [agent index](docs/agent-index.json).
<!-- workspace-docs:readme:end -->
