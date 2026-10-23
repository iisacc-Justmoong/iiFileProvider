# 파일 journal 및 RAW 래핑 검증 기록

이 문서는 온톨로지 파일 시스템 추가 전 스키마 4의 검증 기록이다. 스키마 5와 현재 통합 결과는 [온톨로지 파일 시스템 검증](ONTOLOGY_VALIDATION.md)을 참조한다.

검증일은 2026-10-22이며 대상은 `SDK/iiFileProvider`이다. 환경은 macOS 27.0.1 arm64, Apple Clang 21.0.0, CMake 4.4.4이다. Objects 타겟의 실제 컴파일 플래그 `-std=c++23`를 확인했다. SDK 소스와 다른 실제 사용자 저장소를 임의로 이전하지 않고 `build/` 아래 검증 전용 패키지만 생성했다.

## 결과

| 검증 | 결과 | 근거 |
| --- | --- | --- |
| 새 공개 API의 최초 실패 확인 | 예상대로 컴파일 실패 | `build/journal-red-contract.log`의 미구현 타입·메서드 오류 |
| 전체 Release 빌드 | 성공 | `build/journal-configure.log`, `build/journal-build.log` |
| 모듈 전체 CTest | 42개 항목의 최종 결과 통과 | 최초 병렬 40/42, 시간 초과 2개를 동일 조건에서 순차 재검증하여 2/2 통과 |
| Qt·Native·CoreIO 없는 독립 빌드 | 성공, 관련 테스트 6/6 통과 | `build/journal-qt-free-*.log` |
| 설치 헤더·라이브러리만 사용하는 별도 소비자 | 4개 항목의 최종 결과 통과 | 최초 3/4, 시간 초과 1개를 순차 재검증하여 1/1 통과 |
| 번들 SQLite 빌드·설치·재배치 | 성공, 내부 소비자 3/3 통과 | `build/journal-portable-ctest.log`, `build/portable-sqlite/consumer/build/Testing/Temporary/LastTest.log` |
| Qt 없는 실행 파일 의존성 | 확인 | 독립 테스트와 설치 소비자 `otool -L`에 시스템 SQLite·libc++·libSystem만 존재 |
| SDK 의존성 방향 | 통과 | 전체 CTest의 `iiFileProvider.dependency_direction` |
| SDK·Product 파일 생성 경계 감사 | 통과 | 전체 CTest의 `iiFileProvider.workspace_creation` |

독립 빌드의 6개 항목은 `object_journal`, `object_journal_storage`, `object_metadata_limits`, `object_read_only`, `object_index`, `object_store`이다. 설치 소비자는 `objects.installed`와 앞의 신규 회귀 테스트 3개를 실행한다. 번들 SQLite 소비자는 Catalog 분류·검색·영속성, RAW·diff·journal·세션의 설치 API, 스키마 3→4 이전·이력 보존을 각각 실행한다.

## 검증한 동작

- 1 MiB 청크 경계를 넘는 NUL·비 UTF-8 바이너리 RAW와 빈 RAW를 원본 그대로 보관하고 바이트가 같은 파일로 추출했다. 원본 내용과 수정 시각이 바뀌지 않았음을 확인했다.
- 원본을 삭제한 뒤에도 메타데이터를 편집하고, 콘텐츠 SHA-256·크기·청크가 유지되는지 확인했다. RAW 객체의 일반 페이로드 수정은 거부했다.
- 제목·키워드·등급·색상·컬렉션·사용자 필드를 버전별로 조회하고 JSON Pointer의 이스케이프와 값 추가·삭제를 확인했다.
- 메타데이터 no-op, 오래된 버전, 동시 CAS 경쟁, 실패한 배치, 잘못된 입력과 취소에 불필요한 버전·journal이 생기지 않음을 확인했다.
- 일반 파일의 가져오기·수정·이동·삭제 및 RAW 메타데이터 편집을 같은 journal에서 조회했다. 객체·세션 필터, 페이지 커서, 재개방·VACUUM 후 지속성을 확인했다.
- 작업 세션의 앱·버전·프로젝트·문서·장치·작성자·시작·종료 정보를 저장·조회했다. 종료된 세션의 수정 거부, active 필터, 세션 커서와 문맥 변조 감지를 확인했다.
- 스키마 3 패키지의 읽기 전용 호환, 잘못된 컨테이너 키의 이전 차단, 쓰기 연결의 스키마 4 이전·journal 백필·기존 검증 키 보존을 확인했다. 기존 스키마 1·2 회귀도 전체 테스트에 포함된다.
- 읽기 전용 RAW 래핑 실패 시 스냅샷·lease 파일을 새로 생성하지 않음을 확인했다.

## 최초 실패와 재검증

초기 Qt 어댑터 테스트에서 `iiLocalDiffusion` 설치 경로에 복제된 Native 0.8.1 동적 라이브러리가 먼저 로드되어 `SourceAccess::lock()` 심볼을 찾지 못했다. 현재 빌드의 Native 0.8.3 경로를 우선하는 `DYLD_LIBRARY_PATH="$PWD/build/Native:$PWD/build"`로 전체 Qt 어댑터를 실행하여 통과했다. 새 Objects 기능과 설치 소비자는 Qt나 Native 동적 라이브러리를 로드하지 않는다.

최초 전체 병렬 검증에서는 `object_store`가 CTest의 60초 제한을 넘었고, `file_catalog_cli`는 내부 프로세스의 45초 제한을 넘었다. 당시 다른 검증들도 병행 중이었으며 동일 바이너리·동일 제한을 유지한 순차 재검증에서는 각각 21.21초·13.16초로 통과했다. 설치 소비자의 `object_journal_storage`도 최초 120초 제한을 넘었으나 같은 조건의 순차 재검증에서 통과했다. 이 결과는 시간 초과가 순차 실행에서 재현되지 않았다는 증거이며, 모든 저장 장치에서 특정 지연 시간을 보장한다는 뜻은 아니다. 테스트의 의미 조건이나 시간 제한을 완화하지 않았다.

원래 실패 로그는 `build/journal-full-ctest.log`와 `build/journal-consumer-ctest.log`에 보존했다. 재검증 로그는 `build/journal-retry-ctest.log`, `build/journal-consumer-retry-ctest.log`이며 각각 JUnit 결과 `build/journal-retry.xml`, `build/journal-consumer-retry.xml`도 남겼다.

## 설치 소비 범위

검증용 설치 위치는 `build/journal-stage`이며 소비자 빌드 위치는 `build/journal-consumer/build`이다. 소비자는 `find_package(iiFileProviderObjects CONFIG REQUIRED)`로 설치된 타겟만 링크하고 Qt 패키지 탐색을 비활성화했다. 번들 SQLite는 별도의 `build/portable-sqlite/relocated` 설치를 소비하여 재배치 가능성을 확인했다. 실제 사용자 설치 경로 `~/.local/SDK/iiFileProvider`와 실행 중인 앱은 이번 검증 대상으로 교체하지 않았다.

이번 실제 실행 증거는 macOS arm64에 해당한다. 새 기능은 C++23과 기존 네이티브 저장 계층을 사용하며, Windows·Linux·모바일·WASM에서 이번 변경분을 실제로 실행했다는 증거는 포함하지 않는다. 원본 카메라 RAW 디코딩이나 외부 프로그램의 임의 편집 자동 추적도 이 기능의 검증 범위에 포함되지 않는다.
