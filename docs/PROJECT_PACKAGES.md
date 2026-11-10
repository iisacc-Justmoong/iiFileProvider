# 디렉터리 프로젝트의 Catalog 등록

`FileCatalog::registerPackage`는 `.iiproject` 확장자의 실제 디렉터리와 그 안의 일반 파일 `manifest.json`을 확인하여 전체 패키지를 Catalog 한 항목으로 등록한다. 프로젝트의 JSON 문법과 의미 검증은 iiProjectManager가 담당하며 iiFileProvider는 그 SDK에 의존하지 않는다. Catalog는 에셋을 열거나 디렉터리를 재귀 탐색하지 않으므로 목록의 `size`는 패키지의 총 용량이 아닌 미측정 값 `0`이다.

`itemKind(key)`는 일반 `File`과 `Package`를 구분한다. 기존 `CatalogFile`과 `CatalogFileSummary`의 바이너리 구조는 변경하지 않는다. 프로젝트 패키지의 byte-range `read`는 거부한다. `resolvePackage(key, expectedContentVersion)`은 등록된 디렉터리의 실제 식별자와 manifest 버전이 일치할 때만 절대 디렉터리 경로를 반환한다. 이 경로는 고정된 파일 핸들이 아니므로 후속 열기와 사용의 안전성은 패키지 제공자의 계약에 따른다. 업데이트 후에는 `registerPackage`로 재등록해야 한다. 디렉터리나 manifest의 심볼릭 링크와 Windows reparse point는 거부한다.

목록·검색, 분류 필드와 컬렉션은 일반 파일과 동일한 Catalog 기능을 사용한다. 등록된 패키지 내부의 파일을 별도 Catalog 항목으로 등록하거나 생성할 수 없다. 이미 내부 파일이 각각 등록된 경로에 대해서는 분류를 삭제하지 않고 등록을 거부한다. 패키지와 외부 분류 보조 파일 `<name>.iiproject.iisacc.json`을 함께 이동하면 같은 Catalog는 디렉터리의 네이티브 식별자로 기존 키를 복구한다. 같은 패키지를 복사한 경우 복사본은 독립된 Catalog 키를 받는다. 단, 이것이 프로젝트 내부 `projectId`까지 새로 발급한다는 뜻은 아니다.

패키지 `contentVersion`은 디렉터리 식별자와 현재 manifest의 파일 식별자·크기·수정 시각으로 구성된다. manifest의 확장 속성이나 권한만 바뀌어 변경 시각(ctime)이 갱신되는 것은 프로젝트 내용 변경으로 취급하지 않는다. 이는 변경 감지와 재등록을 위한 표식이며 콘텐츠 암호학적 해시, 자산 전체의 스냅샷이나 악의적 변경에 대한 보안 증거가 아니다. 변경 중인 파일 시스템과의 모든 경쟁 조건까지 막는 고정 핸들 방식은 아니다. 하위 에셋만 직접 변경한 경우에는 manifest가 바뀌지 않아 이 표식이 달라지지 않을 수 있다.

기존 schema 1 Catalog에 별도의 `package_items` 테이블을 추가한다. 기존 데이터를 다시 쓰지 않으며 테이블이 없는 과거 Catalog도 읽기 전용으로 일반 파일을 조회할 수 있다. 이전 버전의 Catalog 소비자는 새 항목 유형을 이해하지 못할 수 있으므로 갱신된 API를 사용해야 한다. `registerPackage` 결과의 `metadataWritten`과 `metadataError`는 기존 companion 게시 계약과 같다.

```cpp
#include <FileCatalog.h>
iiFileProvider::FileCatalog catalog(absoluteLibraryRoot);
auto item = catalog.registerPackage("Projects/Character.iiproject");
if (catalog.itemKind(item.file.key) == iiFileProvider::CatalogEntryKind::Package) {
    auto packagePath = catalog.resolvePackage(item.file.key, item.file.contentVersion);
    // 앱이 프로젝트 패키지 제공자를 연결해 packagePath를 연다.
}
```
