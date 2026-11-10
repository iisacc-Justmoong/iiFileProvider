# Build and resource layout

Windows SDK의 `min`·`max` 매크로가 활성화된 소비자에서 파일 읽기의 기본
최대 크기가 매크로로 치환되지 않도록 표준 극한값 호출을 보호한다. Windows
테스트 빌드에 이 포함 순서의 컴파일 전용 검사를 두며, 이 변경의 실제 Windows
빌드나 설치는 아직 수행하지 않았다.

The project's `build/` directory contains the provider libraries, explicitly
configured tools and test binaries. Temporary file, database, catalog and
object-store fixtures belong under the project `resources/` directory. Host
tests reserve an isolated, visible `resources/test-workspaces/` directory through iiFileProvider,
point temporary-directory environment variables and fixture roots to it, and
remove it after the test. Visible path components allow the provider's normal
preview and metadata policies to run during the tests. When the shared native API is being built, the test
runner uses that library and this checkout's Python adapter. Static-only or
native-disabled host builds use an already installed iiFileProvider adapter.

Some older install, cross-platform and integration checks still stage files
under build paths or execute external applications. They retain their existing
behavior and are not included in the clean-layout, build-only verification until
they are migrated. Recovered object stores and other user data are retained in
ignored `resources/.recovery/`; other projects' package references can require
temporary compatibility links while those projects remain active.
