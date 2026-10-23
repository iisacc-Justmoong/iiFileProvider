# 온톨로지 파일 시스템 검증 기록

검증일은 2026-10-22이다. 대상은 `SDK/iiFileProvider`이며 실행 환경은 macOS 27.0.1 arm64, Apple Clang 21.0.0, CMake 4.4.4이다. Objects 타겟과 독립 소비자의 C++23 컴파일을 확인했다. 실제 사용자 패키지·활성 staging·설치 앱을 대상으로 형식 이전이나 교체를 수행하지 않고 `build/` 아래 소유한 fixture만 사용했다.

## 실행 결과

| 검증 | 결과 | 로그 |
| --- | --- | --- |
| API 미구현 상태 확인 | 테스트의 헤더 탐색 실패를 먼저 기록 | `build/ontology-red-contract.log` |
| 전체 Release 빌드 | 성공 | `build/ontology-configure.log`, `build/ontology-build.log` |
| 전체 모듈 회귀 | 45/45 통과, 240.82초 | `build/ontology-full-ctest.log` |
| 최종 보강 후 온톨로지 회귀 | 3/3 통과, 21.51초 | `build/ontology-final-contract-ctest.log` |
| Qt·Native·CoreIO 없는 독립 빌드 | 성공, 관련 회귀 9/9 통과 | `build/ontology-qt-free-*.log` |
| 설치된 헤더·라이브러리만 사용하는 별도 소비자 | 7/7 통과, 90.25초 | `build/ontology-consumer-*.log` |
| 최종 소스의 번들 SQLite 빌드·설치·재배치 | 소비자 5/5 통과, 외부 검증 71.33초 | `build/ontology-portable-final-ctest.log` |
| Qt 없는 실행 파일의 동적 의존성 | 시스템 SQLite·libc++·libSystem만 확인 | `otool -L`로 독립 테스트와 설치 소비자 확인 |
| SDK 의존성 방향·SDK/Product 파일 생성 경계 | 통과 | 전체 회귀의 `dependency_direction`, `workspace_creation` |

전체 회귀 후 실수 ±0의 동일 값 처리, 검증 중 동일 속성·타입 판정 재사용, SQL 조회 계획 확인을 보강했다. 해당 최종 소스는 온톨로지 3개 회귀, 독립 빌드 9개, 설치 소비자 7개, 번들 SQLite 소비자 5개로 다시 검증했다. 이미 통과한 다른 기능의 회귀는 불필요하게 반복하지 않았다. 전체 Qt 어댑터 회귀는 현재 빌드 라이브러리를 우선하는 `DYLD_LIBRARY_PATH="$PWD/build/Native:$PWD/build"`에서 수행했다.

마지막으로 두 부모와 다이아몬드 상속 경로에서 파일 결과가 중복되지 않는 양성 사례를 추가했다. 이 테스트는 독립 빌드·설치 소비자·번들 SQLite에서 각각 1/1로 통과했으며 `build/ontology-diamond-ctest.log`, `build/ontology-installed-diamond-ctest.log`, `build/ontology-bundled-diamond-ctest.log`에 결과를 보존했다.

독립 빌드의 9개 항목은 신규 `ontology_filesystem`, `ontology_storage`, `ontology_boundaries`와 기존 `object_journal`, `object_journal_storage`, `object_metadata_limits`, `object_read_only`, `object_index`, `object_store`이다. 설치 소비자는 통합 `objects.installed` 및 신규·기존 journal 회귀 6개를 실행한다. 번들 SQLite 검증은 Catalog, 설치 API 통합 소비자, 파일 journal 이전, 온톨로지 동작, 온톨로지 이전을 각각 실행한다.

## 검증한 계약

- 상위 Asset·하위 Photo 분류, 다중 부모 모델의 탐색 구조, 명시적 타입과 domain/range 검증을 확인했다. 클래스·속성 정의를 갱신할 때 기존 사실이나 저장된 폴더를 무효화하면 전체 변경을 거부한다.
- 업무 개체와 파일 개체, 관계·정수·실수·불리언·문자열 값의 구분을 확인했다. 비유한 실수·잘못된 UTF-8·NUL·없는 참조·잘못된 타입·단일 값 위반을 거부한다. +0.0과 -0.0은 중복 사실을 만들지 않는다.
- 클래스·관계·타입·가상 폴더 변경의 원자성, 정확한 revision 비교, 두 연결의 동시 CAS 경쟁, 지연된 제약 오류의 완전 롤백, 세션 종료 후 수정 거부를 확인했다.
- 클래스·관계 조건의 가상 디렉터리, 정방향·역방향 탐색, 허용된 순환 관계의 종료, 결과 상한과 취소를 확인했다. 금지한 비순환 속성에 순환을 넣는 작업은 거부한다.
- 파일 이동 후 동일한 키와 최신 경로 유지, 파일 삭제 후 기본 폴더에서 제외, 삭제된 파일의 남은 분류·관계 조회, 이전 파일 버전의 원본 제공을 확인했다. 의미 구조 변경으로 RAW 청크·파일 버전·파일 검증 키가 바뀌지 않는다.
- 파일이나 그래프 변경 시 오래된 파일 페이지 커서와 다른 질의의 커서를 거부한다. 조회 계획에 분류 후보 인덱스와 파일 키 인덱스를 사용하며 RAW 문서·청크를 스캔하지 않는지 확인했다.
- 스키마 4의 파일·RAW·journal 읽기 전용 호환과 온톨로지의 이전 요구, 스키마 5 쓰기 이전, DDL 중간 실패 시 완전 롤백, 기존 RAW 검증 키 보존을 확인했다. 기존 스키마 1·2·3 이전 회귀도 전체 실행에 포함된다.
- WAL 읽기 동시성, 원래 ObjectStore 소멸 이후 facade 사용, 읽기 전용 능력 유지, VACUUM·재개방 후 journal 커서 유지, 변경 개수·journal 바이트 제한의 원자적 거부를 확인했다.

## 빌드·설치 소비자 재현

```sh
cmake -S . -B build
cmake --build build --parallel 5
DYLD_LIBRARY_PATH="$PWD/build/Native:$PWD/build" \
  ctest --test-dir build --output-on-failure --parallel 2

cmake -S . -B build/ontology-qt-free/build \
  -DIIFILEPROVIDER_BUILD_QT_API=OFF -DIIFILEPROVIDER_BUILD_NATIVE_API=OFF \
  -DIIFILEPROVIDER_BUILD_CORE_IO=OFF -DIIFILEPROVIDER_BUILD_TOOLS=OFF \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE -DCMAKE_BUILD_TYPE=Release
cmake --build build/ontology-qt-free/build --parallel 5
ctest --test-dir build/ontology-qt-free/build \
  -R '^iiFileProvider\.(ontology_.*|object_journal.*|object_metadata_limits|object_store|object_index|object_read_only)$' \
  --output-on-failure --parallel 1

cmake --install build/ontology-qt-free/build --prefix "$PWD/build/ontology-stage"
cmake -S tests/objects_consumer -B build/ontology-consumer/build \
  -DCMAKE_PREFIX_PATH="$PWD/build/ontology-stage" \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE \
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/ontology-consumer/build --parallel 5
ctest --test-dir build/ontology-consumer/build --output-on-failure --parallel 1

ctest --test-dir build -R '^iiFileProvider.portable_sqlite$' --output-on-failure
```

독립 소비자는 SDK 소스의 include 경로나 빌드 정적 라이브러리를 직접 지정하지 않고 설치된 CMake 타겟만 링크한다. 번들 SQLite 검증은 설치 경로를 `build/portable-sqlite/relocated`로 옮긴 뒤 소비한다. 검증용 설치 위치는 `build/ontology-stage`이다. 파일 시스템 계약과 예시는 [ONTOLOGY_FILESYSTEM.md](ONTOLOGY_FILESYSTEM.md)에 명시한다.

이번 실제 실행 증거는 macOS arm64이다. Windows·Linux·Android·iOS·WASM에서 이번 변경분을 실행했다는 증거는 포함하지 않는다. OS 커널 마운트, RDF/OWL 전체 처리, 자연어 온톨로지 생성, 특정 과거 그래프 revision 전체의 재현은 제공 범위에 포함되지 않는다.
