# Workspace file creation migration validation

Validated on 2026-10-15 on macOS arm64 with AppleClang 21, C++23 and Qt 6.8.3 for existing compatibility adapters. Native storage introduces no Qt dependency or common interpreter/VM for application logic.

## Scope and ownership

The migration covers 20 consumer SDKs, iiFileProvider itself, and seven products: Dreamscapes, Society, Congregation, Alright, TimeScopes, and archived Vincent/WhatSon. Native streams/descriptors, Qt devices, Python AI workers, Swift storage and Android document bridges delegate creation/publication to iiFileProvider. Format codecs and transport authority stay in their existing owners.

The final source audit counts 2463 runtime files, 60 bootstrap/development files, 1079 fixture files and 91 provider-owned files. It reports 0 direct creation violations and 0 unreadable files. Deprecated product source and nested model source are included. Generated builds, fixtures, vendored implementations and bootstrap tools are distinct scopes. The gate checks known source factories and is not a syscall sandbox for arbitrary commands, third-party internals or OS-managed stores.

`tests/dependency_direction.py` validates 32 SDK declarations as a DAG with no upward iiFileProvider dependency. Core Values and PhotoLibrary Albums retain their independent package boundaries. All 28 touched repositories passed `git diff --check`.

## SDK results

Focused corrections were rerun after fixing the relevant boundary. A row with a rerun means every listed case passed across the full run and its corrective rerun; it does not imply repeating unrelated hardware/GUI execution.

| Module | Passed/tested | Evidence scope |
| --- | --- | --- |
| LVRS | 56/56 | full source suite |
| iiAudioGen | 8/8 | full source suite |
| iiCalendar | 13/13 | full source suite |
| iiCameraIntegration | 21/21 | full source suite |
| iiFileProvider | 31/31 | full source suite; relocated worker discovery rerun |
| iiGraphicAdjust | 163/163 | full source suite |
| iiLicenseManager | 3/3 | full source suite |
| iiLocalDiffusion | 10/10 | focused output/video/checkpoint/archive/installer suite |
| iiLocalLLM | 34/34 | focused file/agent suite; two strict wire tests rerun on shorter socket paths |
| iiPaintEngine | 45/45 | full source suite |
| iiPhotoLibrary | 8/8 | full source suite |
| iiSDKBuilder | 12/12 | full source suite |
| iiSDKCore | 34/34 | full source suite |
| iiWebServer | 10/10 | full source suite |
| iiSharedCanvas | 63/63 | full source suite |
| iiSocietyContainer | 31/31 | full source suite; corrected iOS package rerun |
| iiSocietyGeneration | 1/1 | full source suite |
| iiSocietyHelper | 11/11 | full source suite |
| iiSocietyMonitor | 2/2 | full source suite |
| iiSocietySync | 12/12 | full source suite |
| iiUpdateManager | 5/5 | full source suite |

The original broad iiLocalDiffusion run had 17 failures in 107 tests. Publication collision errno, failed-output cleanup, moved temporary directories, checkpoint publication and installer fixture regressions were corrected and rerun. Remaining model-resource checks include incompatible fallback VAE family/class registration and missing `torch` in the selected test interpreter. Recent StandaloneImage/GenericDiffusers runs still report the VAE configuration error. Their strict expectations and model resource files were preserved. The complete AI suite is not green.

## Installed packages and products

- The independent installed Qt/Catalog consumer passed 7/7 tests. The native consumer passed 1/1 with Qt package discovery disabled.
- Provider Python tests verify append/exclusive creation, failed encoder rollback, existing output preservation, suffix retention, original relative destination retention after a working-directory change, idempotent cleanup, and versioned library discovery from SDK `lib/` and app `Contents/Frameworks/` without SDK/dyld overrides.
- A relocated installed AI worker published through its bundled provider adapter/native library with global SDK/dyld overrides removed. Runtime installation carries Python support, the native library and its licenses. The separately installed ModelPackaging component passed its 12-case CLI suite with the same overrides removed. TLS FTP has a native-backed filesystem adapter and an independent real network test for Unicode creation, append, rename and exclusive upload. External rclone internals remain outside the source-factory audit.
- 28 changed product C++ translation units compiled independently: Dreamscapes 6, Society 6, Congregation 4, WhatSon 9 and Vincent 3. WhatSon's maintained C++ regression gate was rebuilt and passed. No alternative Product app bundle was created for these checks.
- The Swift provider contract test passed on macOS, and Alright's changed core sources passed typechecking. TimeScopes' changed storage and shared models compiled to an arm64 iOS 17 object with the actual iOS SDK.
- Android provider and affected SDK/Society Java bridges compiled against Android SDK 35. This is Java bridge compilation, not APK/device execution.
- Society's iOS storage/FileProvider/Live Activity packaging regression compiled the native iOS targets without signing. This is cross-build evidence, not an iOS device run.

## Verification boundaries

The Native library is static for iOS/WASM and shared for desktop/Android. Actual Windows/Linux/WASM builds and Android/iOS device execution remain unverified. The AI tests use controlled fixtures and do not establish model/GPU quality. Changed product object compilation does not establish complete app packaging, deployment, device permission or hardware behavior.

All build/staging evidence is under repository-local `build/`. Global `~/.local` SDKs, installed applications and live Society stores were not replaced or restarted by this migration. Minimal-backend AI test packages were kept in staging.

The complete case-level evidence and paths are in [verification.json](../build/ownership-migration/verification.json). Ownership/API usage is described in [FILE_OWNERSHIP.md](FILE_OWNERSHIP.md). The earlier [metadata catalog report](FILE_CATALOG_VALIDATION.md) records a previous snapshot; this report provides the later ownership migration results.
