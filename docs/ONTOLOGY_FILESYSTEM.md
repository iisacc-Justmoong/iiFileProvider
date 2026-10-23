# 온톨로지 파일 시스템

`OntologyFileSystem`은 ObjectStore 패키지의 파일을 업무 개념과 관계로 구성하는 C++23 의미 기반 파일 시스템이다. 파일 내용은 기존 ObjectStore가 소유하고 클래스·개체·속성·관계·가상 디렉터리는 같은 SQLite 데이터베이스에 저장한다. Qt 및 상향 SDK 의존성은 없다. 헤더와 구현은 `src/OntologyFileSystem.h`와 `.cpp`에 함께 둔다. 설치 소비자는 기존 `iiFileProvider::Objects` 타겟을 사용한다.

## 파일과 의미 구조

| 구성 | 역할 |
| --- | --- |
| `OntologyClass` | Asset·Photo·Person·Project 같은 개념과 다중 부모 계층 |
| `OntologyResource::file(objectKey)` | 기존 파일의 안정된 객체 식별자 |
| `OntologyResource::entity(key)` | 인물·프로젝트·장소 같은 바이트 파일이 아닌 업무 개체 |
| `OntologyProperty` | 속성 종류, 주체 클래스, 관계 대상 클래스, 단일 값·비순환 제약 |
| `OntologyTypeAssignment` | 파일 또는 개체가 특정 클래스의 인스턴스라는 명시적 분류 |
| `OntologyAssertion` | 개체 사이의 관계 또는 문자열·정수·실수·불리언 속성 값 |
| `OntologyDirectory` | 클래스와 속성 조건으로 정의하여 저장하는 가상 폴더 |

클래스·인스턴스·속성의 구분과 상위 클래스에 대한 분류 전파는 [W3C RDF Schema](https://www.w3.org/TR/rdf-schema/)의 개념을 참고했다. 이 API는 자체 저장·질의 계약이다. domain/range를 명시적 입력 검증 제약으로 사용하며, 클래스 계층의 순환은 거부한다. RDF 전체 추론, OWL, SPARQL 또는 RDF 구문 호환성을 구현한 것으로 간주하면 안 된다.

업무 키는 UTF-8 문자열이며 URN·도메인 식별자 등을 사용할 수 있다. `OntologyResource`는 파일과 개체를 구분하므로 같은 문자열을 가진 업무 개체가 파일과 충돌하지 않는다. 파일 관계는 경로 대신 ObjectStore 키에 연결한다. 파일을 이동하거나 RAW 메타데이터를 수정해도 관계가 유지되며, 조회 시에는 최신 파일 경로·버전·콘텐츠 해시를 반환한다.

기존 파일 등급·색상·키워드와 프리뷰는 FileMetadata·Catalog·Presentation의 계약을 유지한다. 온톨로지는 업무 개념과 의미 관계를 명시적으로 구축하는 별도 층이다. 파일 메타데이터의 모든 키를 자동으로 업무 클래스나 사실로 추정하지 않는다.

## 객체 수명과 저장소

```cpp
#include <ObjectStore.h>
#include <OntologyFileSystem.h>

iiFileProvider::ObjectStore store(packageDirectory, "project-store", true);
iiFileProvider::OntologyFileSystem ontology(store);
```

생성자는 원본 ObjectStore의 경로·컨테이너 키·읽기 전용 여부를 복사하여 독립된 SQLite 연결을 소유한다. 원래 ObjectStore가 먼저 소멸해도 사용할 수 있다. 하나의 인스턴스는 자신의 연결을 직렬화하고, 여러 인스턴스는 SQLite WAL로 읽기와 쓰기를 조정한다. 모든 온톨로지 변경은 같은 패키지 안의 트랜잭션에 포함된다. 원본 파일 청크나 파일 버전을 다시 만들지 않는다.

파일 생성·RAW 래핑·파일 버전 수정은 ObjectStore API를 사용한다. 온톨로지 연결은 그 파일 키를 분류한다. 파일 바이트 가져오기와 후속 의미 분류는 각각의 API 트랜잭션이며 하나의 합성 트랜잭션으로 주장하지 않는다. `extractFile()`은 ObjectStore의 검증·독점 생성 경로를 이용하여 파일 원본을 제공한다.

## 원자적 변경과 제약

`apply(changes, expectedRevision, sessionKey, cancellation)`는 최대 256개 변경을 입력 순서대로 처리한다. 최초 그래프 revision은 0이며 모든 변경은 정확한 revision 일치를 요구한다. 새 클래스·부모·속성·업무 개체 정의는 처음 사용하기 전에 같은 배치의 앞부분에 두어야 한다. 하나 이상의 실제 변경이 있으면 배치당 revision이 한 번 증가하고, 완전히 동일한 선언·분류·사실은 새 revision이나 journal을 만들지 않는다.

클래스의 이름·설명·부모 계층과 속성 정의는 갱신할 수 있다. `OntologyTypeAssignment::present=false`와 `OntologyAssertion::present=false`는 분류·사실을 해제한다. 단일 값 속성을 교체할 때는 이전 사실 해제와 새 사실 추가를 같은 배치에 넣는다. 저장된 가상 폴더는 다시 선언하여 갱신하거나 `OntologyRemoveDirectory`로 제거한다. 클래스·업무 개체·속성 정의 자체의 물리 삭제 API는 제공하지 않는다.

문자열, 정수, 실수, 불리언, 관계 대상은 서로 다른 값 종류이다. `OntologyValue::text/integer/real/boolean/resource` 팩터리를 사용하여 문자열 리터럴의 불리언 변환 같은 모호함을 피한다. 실수 NaN·무한대, 잘못된 UTF-8·NUL, 없는 파일·개체·클래스·속성, 잘못된 domain/range, 단일 값 위반을 거부한다. `acyclic=true`는 관계형 속성에만 사용할 수 있으며 해당 속성의 순환을 거부한다. 일반 관계는 순환을 가질 수 있다.

배치 마지막 상태에서 모든 제약을 검사한다. 뒤늦은 제약 위반, 저장된 폴더와 호환되지 않는 속성 타입 변경, 취소, SQLite 오류는 전체 배치와 journal을 되돌린다. 보통의 사실·분류 변경은 인덱스로 연결된 주체·대상·속성만 재검증한다. 기존 클래스의 부모 계층 변경은 모든 상속 제약에 영향을 줄 수 있어 전체 사실 검증을 수행한다. 관계의 비순환 검증도 연결된 그래프 크기에 따른 비용이 들 수 있다. 이 작업에는 취소 토큰을 전달할 수 있다.

## 의미 기반 폴더와 검색

`OntologyQuery`는 선택적 클래스, 하위 클래스 포함 여부, 최대 16개 속성 조건, 삭제된 파일 포함 여부를 받는다. 속성 조건은 AND이며 값 생략은 해당 속성의 존재를 뜻한다. 값 종류를 구분하여 정확히 비교한다. 상위 Asset 클래스를 조회하면 Photo 같은 하위 클래스에 분류된 파일도 포함한다. 기본 조회는 살아 있는 파일만 반환하며 조건이 없으면 분류 여부와 관계없이 전체 파일 인덱스를 조회한다.

```cpp
using namespace iiFileProvider;
auto session = store.beginSession({"https://iisacc.com", "account-subject", "Editor"},
                                  "device-1", "Ontology classification");
auto project = OntologyResource::entity("project:portrait");
auto file = OntologyResource::file(captured.key); // importFile()/wrapRaw() 결과

OntologyQuery portraits;
portraits.classKey = "Photo";
portraits.conditions = {{"belongsTo", OntologyValue::resource(project)}};

ontology.apply({
    OntologyClass{"Asset", "Asset", "", {}},
    OntologyClass{"Photo", "Photo", "", {"Asset"}},
    OntologyClass{"Project", "Project", "", {}},
    OntologyProperty{"belongsTo", "Project", OntologyValueKind::Resource,
                     "Asset", "Project", true},
    OntologyEntity{"project:portrait", "Portrait project", ""},
    OntologyTypeAssignment{project, "Project"},
    OntologyTypeAssignment{file, "Photo"},
    OntologyAssertion{file, "belongsTo", OntologyValue::resource(project)},
    OntologyDirectory{"Projects/Portraits", "Portrait files", portraits}
}, ontology.revision(), session.key);

auto page = ontology.listDirectory("Projects/Portraits");
if (page.next) page = ontology.listDirectory("Projects/Portraits", *page.next);
store.endSession(session.key);
```

`queryFiles()`와 `listDirectory()`는 기본 256개, 최대 1024개 결과를 반환한다. 속성·클래스 후보와 파일 키에 인덱스를 사용하며 파일 페이로드·RAW 메타데이터·작성자 프로필을 읽지 않는다. 키셋 커서는 조회 조건·컨테이너·그래프 revision·파일 journal sequence에 결합된다. 파일 또는 그래프가 변경되면 다음 페이지의 오래된 커서를 거부한다. 호출자는 첫 페이지부터 새로 조회해야 하며, 이 계약은 여러 페이지의 조용한 중복·누락을 허용하는 것과 구분된다. 계속 열린 다중 페이지 DB 스냅샷을 유지하는 방식은 아니다.

가상 폴더는 상대 논리 경로로 저장하며 `..`, 절대 경로, 역슬래시, 콜론을 거부한다. 실제 OS 디렉터리나 원본 파일을 복제하지 않는다. `directories(prefix, afterPath, limit)`는 저장된 경로를 문자열 접두와 키셋으로 열거한다. 하위 경로 경계가 필요하면 `Projects/`처럼 `/`를 포함한 접두를 사용한다. 이 기능은 iiFileProvider API에서 제공하는 의미 기반 파일 시스템이며 Finder·Explorer·Linux 커널에 마운트하는 드라이버가 아니다.

## 관계 탐색과 이력

`traverse(resource, property, direction, maximumDepth, limit, includeDeleted, cancellation)`는 정방향·역방향으로 관계를 탐색한다. 기본 깊이는 8, 최대 32이며 결과는 최단 발견 거리와 키로 정렬한다. 동일 개체는 한 번 반환하고 순환 관계도 종료한다. 내부 재귀 행은 10,000개로 제한하며 결과나 내부 행 제한에 도달하면 `truncated`를 반환한다. 최대 깊이는 호출자가 정한 탐색 범위이다. 기본 탐색은 삭제된 파일 노드를 제외한다.

`statements()`는 개체별 현재 사실을 영속 sequence로 페이지 조회한다. `journal()`은 작업별 이전·이후 JSON, 그래프 revision, 작업 세션 키, 시각을 조회한다. `beforeJson`·`afterJson`의 빈 문자열은 해당 값의 부재를 뜻한다. 세션 필터와 영속 커서를 지원한다. 파일·RAW의 journal과 온톨로지 journal은 수정 이유가 다른 별도 스트림이며 같은 작업 세션으로 연결한다. `store.workSession(event.sessionKey)`로 앱·프로젝트·문서 문맥을 얻는다. 이 journal은 서명된 인증 원장이 아니다.

파일 삭제는 온톨로지 이력을 파괴하지 않는다. 기본 가상 폴더에서는 사라지며 `includeDeleted=true`인 질의에서는 삭제된 파일에 남아 있는 분류·관계를 조회할 수 있다. 특정 과거 그래프 revision 전체를 재현하는 시점 질의 API는 제공하지 않는다. 삭제된 파일을 대상으로 새 분류·사실을 추가하지 못하지만 기존 사실·분류의 해제는 허용한다. 추출은 살아 있던 파일 버전을 지정해야 한다.

## 크기·취소·형식 이전

키 1024바이트, 이름 4096바이트, 설명 16384바이트, 클래스의 직접 부모 최대 32개, 텍스트 속성 65536바이트를 허용한다. 저장된 쿼리는 JSON 256 KiB이며 한 배치의 이전·이후 journal JSON 합계는 4 MiB이다. journal 한 페이지는 8 MiB 또는 요청 개수 중 먼저 도달한 경계에서 멈추므로 마지막 반환 sequence부터 계속 조회한다. 취소는 실행 전·배치 사이·SQL 진행 중·커밋 전에 확인한다.

데이터베이스 스키마는 5이다. ObjectStore 쓰기 연결이 스키마 1~4를 한 번의 트랜잭션으로 이전하며 기존 파일·RAW·세션·검증 키를 보존한다. 새 그래프는 비어 있는 revision 0으로 시작한다. 읽기 전용 ObjectStore는 기존 스키마 3·4의 파일 API도 지원하지만 OntologyFileSystem은 스키마 5를 요구하고 직접 이전하지 않는다. 이전 형식만 지원하는 앱은 새 SDK로 재빌드해야 한다. 실제 패키지를 옮길 때는 WAL을 포함한 일관된 백업 또는 모든 연결 종료 후 복사를 사용한다.

검증 절차와 실제 결과는 [온톨로지 검증 기록](ONTOLOGY_VALIDATION.md)에 기록한다.
