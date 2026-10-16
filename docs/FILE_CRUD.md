# 파일 CRUD 소유권

0.6부터 이 정책은 iisacc의 공통 파일 생성·제공 경계이다. 네이티브 메타데이터·키워드·별점·색상·컬렉션·저장 필터·검색은 Qt가 없는 `iiFileProvider::Catalog`가 제공한다. [분류 API](FILE_CATALOG.md)와 [공통 소유권](FILE_OWNERSHIP.md)에 파일별 동반 메타데이터 및 적용 범위를 명시한다. 기존 문서 코덱은 아래 파일 API를 계속 사용한다.

`iiFileProvider` 0.5부터 iisacc 문서 파일의 실제 생성·읽기·갱신·삭제 주체는 `iiFileProvider::File`이다. 확장자와 바이트 형식을 해석하지 않는다. `Authorship`과 `FileLink` 바이트도 그대로 보존한다. 포맷 검증, 객체 편집, 직렬화는 해당 도메인 SDK의 책임이다.

의존성은 `제품 → 문서 SDK → iiFileProvider → Qt Core / SQLite`이다. `iiFileProvider`는 다른 iisacc SDK의 헤더·CMake 패키지·모델·렌더러를 참조하지 않는다. 새 포맷을 추가해도 provider에 해당 SDK를 연결하지 않고, 도메인 코덱의 바이트 또는 `QIODevice`를 전달한다.

## API

- `File::create(path, bytes)`는 완성된 임시 파일을 새 경로로 공개한다. 이미 존재하는 경로는 덮어쓰지 않는다.
- `File::read(path, maximumBytes)`는 바이트 수를 제한해 읽는다. `openRead`는 provider가 연 읽기 전용 `QIODevice`를 반환하여 Qt 이미지·ZIP 코덱의 스트리밍을 지원한다. `readPrefix`는 형식 탐지용이다.
- `File::write` / `writeWith`는 원자적으로 생성 또는 교체한다. 콜백이 실패하면 임시 출력을 폐기한다. 직접 덮어쓰기 fallback은 사용하지 않는다.
- `File::update(path, expected, replacement)`는 현재 바이트를 비교한 뒤 교체한다. provider를 사용하는 프로세스끼리는 경로 잠금으로 직렬화한다. provider를 거치지 않는 외부 프로그램과의 동시 저장은 애플리케이션 차원의 조정이 필요하다.
- `File::remove`는 일반 파일만 삭제한다. 없는 파일은 false이며 디렉터리나 심볼릭 링크를 재귀 삭제하지 않는다.
- `File::copy`는 스트리밍 복사, `publish`는 검증된 임시 파일의 최종 교체, `publishDirectory`는 완성된 패키지 디렉터리의 배타적 공개를 담당한다. `createDirectories`는 출력 부모 디렉터리를 준비한다.
- `File::copyToUrl(device, url)`은 읽기 장치를 로컬 파일 URL에 원자적으로 공개한다. 입력 오류는 기존 목적지를 보존한다. Android의 사용자가 선택한 `content:` 문서는 OS 제공 스트림으로 기록하므로 동일한 sibling rename 보장을 제공하지 않는다. 그 외 URL은 거절한다.
- `StagedFile`은 libzip·FFmpeg처럼 경로를 요구하는 외부 코덱에 provider가 소유하는 임시 파일을 빌려준다. 코덱은 이 임시 경로만 쓰며, 최종 파일의 공개·실패 정리는 provider가 처리한다.
- `Database`, `Statement`, `Transaction`은 SQLite 핸들을 외부에 노출하지 않는다. 스키마·레코드 의미는 상위 SDK가 정의하고, 연결·쿼리 실행·즉시 트랜잭션·BLOB 부분 갱신·일관된 백업은 provider가 수행한다. `Statement::bytes`의 span은 다음 step 또는 statement 파괴 전까지만 유효하다. 외부 BLOB/파일 변경 충돌 정책은 도메인이 revision과 data_version으로 결정한다.

파일 경로는 유효한 로컬 경로여야 한다. 빈 경로, NUL, URL, SQLite 메모리 경로는 거절한다. 읽기는 일반 파일 대상 심볼릭 링크를 허용하고, 쓰기는 링크 및 디렉터리 목적지를 거절한다. 파일 API는 `FileError`와 `FileCode`로 실패를 구분한다. `publish`는 기존 문서 코덱의 진단 코드를 유지하기 위해 결과 값을 반환한다. `Database` 인스턴스 및 연결된 statement/transaction은 동일 스레드에서 순차 사용한다.

## 적용된 도메인

| SDK | provider가 소유하는 파일 작업 | 도메인에 유지되는 책임 |
| --- | --- | --- |
| iiSharedCanvas | SQLite 작업 파일, 네이티브 스냅샷 읽기·백업, 미디어 입출력, 타임라인 패키지 공개 | `.iisc` 형식, 레코드/체크섬, 편집·렌더링·충돌 판정 |
| iiGeneralDocument | PDF 읽기·검증 후 원자 출력, DOCX/ODT 읽기 장치, DOCX/ODT/FODT 공개, DOC 변환 결과 복사 | PDF/ZIP/XML 코덱, 문서 검증, Thinking Space/HTML/XML 바이트 직렬화 |
| iiCSMIDI | MIDI 생성·읽기·바이트 비교 후 갱신 | SMF 이벤트와 작성자 메타데이터 |
| iiPaintEngine | 이미지 읽기 장치, 이미지 writer의 원자 출력 | 래스터 편집·색 공간·이미지 코덱 |
| iiXml | 두 파일 파서의 파일 읽기 | XML 토큰화·검증 |
| iiSocietyHelper | 미디어 원본 읽기, 선택된 URL로 스트리밍 내보내기 | 이미지/MP4 유효성 검사, 내보내기 위치·사용자 오류 표시 |
| iiHtmlBlock | 선택된 iiXml 패키지를 유지하여 provider 의존성이 이전 설치로 바뀌지 않도록 함 | HTML 블록 모델·문자열 변환 |
| Congregation | `.iisc` 및 `.vrc` 읽기·원자 저장·최근 문서 삭제 | iiSharedCanvas 및 최근 작업 코덱, 세션 복구 검증, owner-only 권한 정책 |

Thinking Space처럼 이미 바이트 코덱인 파일은 다음과 같이 조합한다. provider에 상위 타입 등록이나 include가 필요하지 않다.

```cpp
using iiFileProvider::File;
File::create(path, initialDocument.toFileBytes());
auto bytes = File::read(path, 64 * 1024 * 1024);
auto document = ThinkingSpaceDocument::fromFileBytes(bytes);
// 도메인 API로 document를 편집하고 검증한다.
File::update(path, bytes, document.toFileBytes());
File::remove(path);
```

서비스의 전송 프로토콜, OS 가상 드라이브 구현, 모델 런타임 캐시, 인증 저장소는 문서 포맷 코덱과 다른 책임이다. 이 변경은 해당 모듈의 기기 간 동기화나 플랫폼 저장소를 대체하지 않는다. 제품이 독자적으로 저장하는 문서 파일은 동일 API를 사용해야 한다.

## 의존성 검토 및 검증

Qt 6.8.3 Core를 기존과 동일하게 사용한다. SQLite는 iiSharedCanvas가 이미 사용하던 공개 도메인 라이브러리를 provider의 private 링크로 이전했다. 새 원격 다운로드나 새로운 코덱 의존성을 추가하지 않는다. qpdf/libzip/FFmpeg는 각 문서·미디어 SDK에 남는다. SQLite는 유지보수되는 범용 저장 엔진이며 직접 저널/잠금/트랜잭션 구현보다 변경 비용이 작다.

참고: [Qt QSaveFile](https://doc.qt.io/qt-6.8/qsavefile.html), [SQLite transaction guarantees](https://www.sqlite.org/transactional.html), [SQLite license](https://www.sqlite.org/copyright.html).

CTest는 범용 CRUD, 중복 생성, 크기 제한, 원본 보존, stale update, 삭제, staged publication, 트랜잭션 rollback, BLOB 부분 기록, 백업을 검증한다. `dependency_direction.py`는 SDK CMake 선언의 DAG와 provider의 상위 SDK 참조 금지, 주요 문서 경로의 직접 파일 I/O 회귀를 검사한다. macOS 패키지는 직접·private 소비자 모두에 설치 경로의 RPATH를 전달한다. 각 도메인의 형식 round-trip/손상/충돌 테스트와 별도 설치 소비자도 함께 실행한다. 소스 빌드, staged 설치 및 실행 검증은 사용자의 전역 설치 갱신이나 제품 실기기 검증과 구분한다.
