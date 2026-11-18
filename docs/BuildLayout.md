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

## CTest 파일 계약 검사

기존 저자·링크·저자 계약 검사는 CTest가 실행하는 C++ 테스트이며 Qt Test를
찾거나 링크하지 않는다. 제품의 Qt Core API는 해당 검사의 대상으로 유지한다.
각 검증 실패는 위치와 케이스를 출력하고 실행 파일의 실패 종료 상태에 반영한다.
기존 데이터 행은 모두 독립적으로 실행하며 한 행의 실패가 다른 행을 생략하지 않는다.
소스와 설치 소비자가 같은 C++ 계약을 실행하며 기존 리소스 작업 공간 생성·정리와
계정 fixture의 경계도 유지한다.
