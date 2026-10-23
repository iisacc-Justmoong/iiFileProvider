# 파일 journal, 작업 세션, 원본 RAW 래핑

`iiFileProvider::Objects`의 C++23 공개 API에 파일 수정 이력, 작업 세션 문맥, 원본 바이트를 보존하는 메타데이터 래핑을 제공한다. Qt나 다른 SDK 모듈에 의존하지 않는다. 공개 헤더는 구현 옆의 `src/ObjectStore.h`이며 설치 소비자는 `<ObjectStore.h>`와 `iiFileProvider::Objects`를 사용한다. `FileMetadata` 값 타입을 재사용하지만 Catalog 라이브러리에 링크하지 않는다.

## 저장 모델과 원본 보존

`wrapRaw(source, logicalPath, metadata, session, options)`는 안정된 `ObjectSource` 스냅샷을 얻고 원본 바이트를 1 MiB 청크로 저장한다. 카메라 RAW, 이미지, 음성, 문서 등 모든 일반 파일의 바이트를 보관할 수 있다. 원본 경로·내용·수정 시각을 덮어쓰지 않는다. symlink·변경 중인 소스·중복 논리 경로·취소는 기존 저장 계층의 규칙에 따라 거부한다. 스냅샷은 패키지 디렉터리 안의 provider 소유 임시 경로에 두며 해당 작업만 정리한다. 대용량 파일 전체를 메모리에 적재하지 않지만 청크 참조, SQLite 캐시 및 스냅샷 디스크 공간은 별도 필요하다.

래핑 결과는 별도 단일 파일 확장자가 아닌 `ObjectStore` 패키지이다. `objects.sqlite3`에 원본 청크, 버전 매니페스트, RAW 메타데이터, 세션과 journal을 함께 보관한다. WAL 연결이 열려 있는 동안 DB 파일 하나만 복사하면 안 되며, 일관된 SQLite 백업 또는 모든 연결 종료 후 복사를 사용해야 한다.

`RawMetadata`는 원본 basename, MIME 유형, `FileMetadata`를 가진다. 원본명과 MIME 유형은 최초 래핑 후 고정된다. `FileMetadata`에는 제목·설명·키워드·0~5 등급·색상 라벨·사용자 필드·컬렉션 키가 들어간다. 사용자 필드에는 앱이 해석하는 노출 보정이나 편집 레시피 같은 문자열도 보관할 수 있다. RAW 디코딩·현상·영상 렌더링은 이 API의 기능 범위에 포함되지 않는다.

`reviseMetadata(key, metadata, expectedVersion, session)`는 메타데이터만 새 버전으로 기록하고 기존 청크 참조를 재사용한다. 원본 파일이 이동·삭제된 후에도 작동한다. 원본 청크를 읽거나 복사하거나 재삽입하지 않는다. 버전 체인 검증과 청크 참조 처리는 수행하므로 비용이 이력 길이·청크 수와 완전히 무관한 것은 아니다. 동일 메타데이터는 버전이나 journal을 추가하지 않는다. 모든 수정은 현재 버전 일치를 요구하며 충돌한 작성자는 최신 버전을 읽어 병합한 뒤 다시 제출해야 한다.

RAW 객체에 대한 `reviseFile()`은 원본 변경을 막기 위해 거부한다. 새 원본은 새 RAW 객체로 래핑한다. 경로 이동과 tombstone 삭제는 허용하며 이전 메타데이터와 원본 이력을 유지한다. `rawEnvelope(key, version)`는 해당 버전의 원본 정보와 메타데이터를 반환하며, 버전 0은 현재 버전이다. 삭제 이력의 메타데이터도 조회할 수 있다. `extract()`는 살아 있던 특정 버전의 원본 바이트를 검증하여 새 경로에 독점 생성한다. 기존 파일 덮어쓰기와 tombstone 버전 추출은 거부한다.

과거 분류 상태로 되돌릴 때는 `rawEnvelope(key, oldVersion).metadata.annotations`를 현재 버전과 함께 `reviseMetadata()`에 제출하면 된다. 과거 이력을 덮어쓰지 않고 복원 작업 자체를 새 버전과 journal로 남긴다.

## 바이너리 diff와 메타데이터 diff

| API | 반환 값 | 의미 |
| --- | --- | --- |
| `history(key, afterVersion, limit)` | `ObjectRecord` 목록 | 객체별 버전·작업자·세션·검증 체인 |
| `diff(key, version)` | `ObjectChange` 목록 | 직전 버전 대비 청크 오프셋, 전후 해시·크기 |
| `metadataDiff(key, version)` | `MetadataChange` 목록 | 직전 버전 대비 분류 메타데이터 변경 |
| `journal(query)` | `ObjectJournalEntry` 목록 | 모든 객체의 영속 작업 순서와 조회 커서 |

바이너리 diff는 최소 텍스트 줄 차이가 아닌 가역 청크 차이이다. 메타데이터 편집이나 이동의 바이너리 diff는 비어 있다. `MetadataChange::pointer`는 `FileMetadata` JSON 루트 기준 JSON Pointer이며 필드 이름의 `~`와 `/`를 각각 `~0`, `~1`로 이스케이프한다. `beforeJson`·`afterJson`은 JSON 값의 문자열 표현이고 `std::nullopt`는 해당 값의 부재이다. 문자열 `"null"`이나 JSON `null`과 부재를 혼동하면 안 된다. 배열은 요소 인덱스 편집 대신 배열 전체의 전후 값을 반환한다. 첫 버전은 빈 문서와 비교하므로 분류 필드의 최초 추가를 반환한다.

## 작업 journal과 페이지 조회

`ObjectJournalQuery`는 `afterSequence`, `limit`, 선택적인 `objectKey`, `sessionKey` 필터를 받는다. 필터를 함께 지정하면 모두 일치하는 항목만 반환한다. 페이지 크기는 1~1024이며 기본 256이다. 다음 페이지의 `afterSequence`에는 마지막 반환 항목의 `sequence`를 넣는다. 비어 있는 결과는 그 조회 시점에 남은 일치 항목이 없다는 뜻이다. 이후 작업이 커밋되면 같은 커서로 계속 조회할 수 있다. 페이지별로 커밋된 상태를 읽으며 여러 페이지를 하나의 동결된 스냅샷으로 만들지는 않는다.

journal sequence는 SQLite의 명시적 `INTEGER PRIMARY KEY AUTOINCREMENT` 커서이다. 재개방·VACUUM 후에도 유지되며 시스템 시각의 정렬에 의존하지 않는다. 시퀀스에 연속 번호를 가정하면 안 된다. 기존 이력의 최초 백필은 저장된 작업 시각과 기존 행 순서로 정렬한다. 이후 순서는 커밋된 쓰기 트랜잭션의 발행 순서를 따른다.

가져오기·RAW 래핑·일반 파일 수정·메타데이터 수정·이동·삭제가 모두 같은 트랜잭션의 트리거로 journal에 기록된다. 청크나 헤드만 커밋되고 journal이 빠지는 부분 성공을 허용하지 않는다. 실패한 배치·취소·버전 충돌·메타데이터 no-op에는 새 journal이 생기지 않는다. 조회는 작은 journal 투영과 해당 리비전만 읽으며 페이로드·큰 작성자 프로필·메타데이터 JSON을 적재하지 않는다. 객체·세션별 커서 조회를 위한 복합 인덱스가 있다. journal은 조회 투영이며 외부 공격에 대한 독립된 인증 원장이 아니다.

## 작업 세션 문맥

기존 `beginSession(author, device, description)`은 유지한다. 네 번째 인자로 `ObjectSessionContext`를 전달하면 앱·버전·프로젝트·문서·사용자 속성을 불변 문맥으로 보관한다. `workSession(key)`는 세션과 문맥, 영속 sequence를 반환한다. `sessions(query)`는 전체 또는 종료되지 않은 세션을 sequence로 조회한다. 페이지 크기는 1~128, 기본 64이며 직렬화된 문맥과 작성자 정보의 합계는 페이지당 16 MiB로 제한된다. 따라서 요청 limit보다 작은 페이지도 마지막 반환 sequence부터 계속 조회해야 한다.

명시적인 `endSession()`은 종료 시각을 기록하고 이후 수정을 거부한다. 정상 종료를 자동 추정하지 않으므로 프로세스 비정상 종료 시 `endedAtNs == 0`인 미종료 세션이 남는다. 이미 종료된 세션의 문맥과 수정 기록은 유지된다. 문맥과 원본 메타데이터는 리비전 검증 키에 함께 결합되며 내용 변경을 검출한다. 검증 키는 암호학적 서명이나 계정 인증 수단이 아니다. 앱의 로그인·프로젝트 권한 확인은 호출자가 담당한다.

문자열은 정규화하지 않고 UTF-8 표기를 보존한다. 잘못된 UTF-8과 NUL은 거부한다. 앱 이름·프로젝트·문서는 각 4096바이트, 버전은 256바이트, 속성은 최대 64개이며 키 256바이트·값 16384바이트, 문맥 전체는 256 KiB이다. `token`, `password`, `authorization`, `secret`, `access_token` 등 알려진 인증 키는 속성 이름으로 거부한다. 이 검사는 임의 문자열 안의 모든 비밀 정보를 판별하는 기능은 아니므로 호출자가 자격 증명을 넣으면 안 된다.

분류 메타데이터의 JSON 크기는 256 KiB로 제한된다. 제목 4096바이트, 설명 64 KiB, 키워드 최대 256개·항목당 256바이트, 컬렉션 최대 128개·항목당 128바이트, 사용자 필드 최대 64개·키 256바이트·값 64 KiB이다. 비어 있는 키워드·컬렉션·사용자 키, 0~5 밖의 등급, 정의되지 않은 색상 라벨은 거부한다. 원본명은 경로 구분자 없는 basename으로 최대 4096바이트, MIME 문자열은 최대 256바이트이다. 실제 형식 판별이나 RAW 코덱 검증을 수행하지 않는다.

## 공개 API 예시

```cpp
#include <ObjectStore.h>

using namespace iiFileProvider;

ObjectStore store(packageDirectory, "project-store", true);
ObjectSessionContext context;
context.application = "Dreamscapes";
context.applicationVersion = "1.0";
context.project = "project-42";
context.document = "document-7";
auto session = store.beginSession(
    {"https://iisacc.com", "account-subject", "Editor"},
    "device-1", "RAW classification", context);

RawMetadata raw;
raw.originalName = "capture.nef";
raw.mediaType = "image/x-nikon-nef";
raw.annotations.title = "Original capture";
auto captured = store.wrapRaw(sourcePath, "Files/capture.nef", raw, session.key);

auto annotations = raw.annotations;
annotations.rating = 5;
annotations.color = ColorLabel::Green;
annotations.fields["exposure"] = "+1.0";
auto edited = store.reviseMetadata(captured.key, annotations, captured.version, session.key);
auto changes = store.metadataDiff(captured.key, edited.version);

ObjectJournalQuery query;
query.sessionKey = session.key;
query.limit = 64;
auto page = store.journal(query);
if (!page.empty()) query.afterSequence = page.back().sequence;

auto original = store.rawEnvelope(captured.key, 1);
store.extract(captured.key, edited.version, destinationPath);
store.endSession(session.key);
```

상기 계정 문자열은 이미 호출자가 검증한 작업자 식별값을 제공하는 예시이다. 가져온 사람을 원본의 역사적 저작자로 자동 기록하지 않는다. 기존 `ObjectWriteOptions::authorship`로 원래 저작권·기여 이력을 별도로 제공할 수 있다.

## 형식 이전 및 적용 범위

파일 journal·RAW 래핑·작업 세션 문맥은 데이터베이스 스키마 4에서 도입되었고, 현재 새 데이터베이스는 [온톨로지 파일 시스템](ONTOLOGY_FILESYSTEM.md)을 포함한 스키마 5이다. 기존 스키마 1~4는 올바른 컨테이너 키로 쓰기 연결을 열 때 하나의 트랜잭션으로 이전한다. 기존 청크·버전·검증 키를 변경하지 않으며 알 수 없는 과거 앱·프로젝트 정보는 빈 문맥으로 남긴다. 최초 journal 백필은 기존 리비전의 조회·정렬 비용을 수반한다. 이전 형식만 아는 바이너리는 새 스키마를 거부하므로 소비자 업데이트를 함께 배포해야 한다. 이전을 자동 수행하는 범위는 호출자가 연 패키지이다. 데이터베이스 형식, 기존 `ObjectRecord::schemaVersion` 1·2, RAW·문맥 JSON 문서의 `schemaVersion` 1은 서로 다른 버전 계약이다.

읽기 전용 연결은 스키마 3~5의 기존 파일 API를 계속 사용할 수 있다. RAW·journal·세션 문맥 API는 스키마 4 이상이 필요하며 스키마 3에서는 명시적으로 거부한다. 읽기 전용 연결은 형식 이전이나 RAW 스냅샷 생성을 수행하지 않는다. 쓰기 연결과 별도의 읽기 연결은 WAL을 통해 커밋된 상태를 병행 조회할 수 있다.

이력은 `ObjectStore` API를 통과한 작업에 적용된다. 임의의 외부 프로그램이 원본을 편집한 내역이나 모든 `Native::OutputFile` 호출을 자동 수집하는 기능은 아니다. 앱은 저장소와 세션을 명시적으로 연결해야 한다. Catalog의 검색 인덱스·파일 companion·프리뷰를 자동 재생성하는 작업도 별도 호출 계약을 유지한다.

## 검증 방법

`iiFileProvider.object_journal`은 청크 경계의 바이너리 RAW 보존, 원본 제거 후 메타데이터 편집, 버전별 메타데이터, diff, CAS 경쟁, 재개방·추출·변조 감지를 검증한다. `object_journal_storage`는 스키마 3의 읽기 전용 호환, 형식 이전·백필·기존 해시 보존, 세션 커서, journal 필터, 배치 롤백, WAL 동시 조회, VACUUM 후 커서와 세션 문맥 변조를 검증한다. `object_metadata_limits`는 UTF-8·NUL·크기·색상·인증 키 제한, JSON Pointer, 빈 RAW, 읽기 전용 쓰기 차단과 임시 파일 미생성을 검증한다.

`iiFileProvider.portable_sqlite`는 번들 SQLite로 Qt 없는 패키지를 빌드·설치·재배치한 뒤 별도 소비자로 Catalog와 RAW·journal·세션 API 및 스키마 이전을 실행한다. SDK 소스의 include 경로나 빌드 라이브러리를 직접 지정하지 않고 재배치된 설치 타겟을 사용한다.

```sh
cmake -S . -B build
cmake --build build --parallel 6
ctest --test-dir build --output-on-failure

cmake -S . -B build/journal-qt-free/build \
  -DIIFILEPROVIDER_BUILD_QT_API=OFF -DIIFILEPROVIDER_BUILD_NATIVE_API=OFF \
  -DIIFILEPROVIDER_BUILD_CORE_IO=OFF -DIIFILEPROVIDER_BUILD_TOOLS=OFF \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE -DCMAKE_BUILD_TYPE=Release
cmake --build build/journal-qt-free/build --parallel 6
ctest --test-dir build/journal-qt-free/build -R 'object_(journal|metadata)' --output-on-failure

cmake --install build --prefix "$PWD/build/journal-stage"
cmake -S tests/objects_consumer -B build/journal-consumer/build \
  -DCMAKE_PREFIX_PATH="$PWD/build/journal-stage" \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE -DCMAKE_BUILD_TYPE=Release
cmake --build build/journal-consumer/build --parallel 6
ctest --test-dir build/journal-consumer/build --output-on-failure
```

macOS에서 다른 SDK에 복제된 구버전 Native 동적 라이브러리가 먼저 로드되면 전체 Qt 어댑터 테스트는 현재 빌드 경로를 우선하는 `DYLD_LIBRARY_PATH="$PWD/build/Native:$PWD/build"` 환경에서 수행한다. Qt 없는 Objects 소비자는 시스템 SQLite·표준 라이브러리·플랫폼 스레드 및 자체 Temporary 정적 라이브러리만 사용한다. 실제 검증 결과와 로그 경로는 [검증 기록](FILE_JOURNAL_VALIDATION.md)에 기록한다.
