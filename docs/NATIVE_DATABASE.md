# Qt 독립 SQLite 실행 API

`iiFileProviderNative` 0.9.0은 `iiFileProviderDatabase.h`와 `iiFileProvider::native::sql`을 제공한다. 파일과 SQLite 실행의 기존 소유권을 유지하며 별도 상위 SQL 모듈이나 ORM을 만들지 않는다. 스키마, 데이터 의미, 마이그레이션 정책은 소비자가 소유한다.

```cmake
find_package(iiFileProviderNative 0.9 CONFIG REQUIRED)
target_link_libraries(backend PRIVATE iiFileProvider::Native)
```

`Database::open(path, options)`의 기본 모드는 기존 파일 읽기/쓰기이다. 생성은 `OpenMode::create`, 읽기 전용은 `read_only`, 메모리 DB는 `Database::memory()`로 명시한다. Qt 패키지, `QString`, `QVariant`, 전역 연결 이름이나 Qt SQL 드라이버가 필요하지 않다. 원시 SQLite 핸들은 공개하지 않는다. 기존 `Database.h`의 Qt 연결 구현은 이 추가 API에 의해 자동 전환되지 않는다.

`prepare`는 NUL이 없는 단일 문장만 허용하며 바인딩은 1부터 시작한다. 값은 NULL, int64, 유한 double, 길이가 보존되는 UTF-8 문자열, BLOB이다. 빈 BLOB과 NULL을 구분한다. `value()`는 현재 행에서만 유효한 소유 값의 복사본을 반환한다. `step()`의 완료는 `reset()` 전까지 완료로 유지되므로 SQLite의 암묵적 재실행을 노출하지 않는다. 실패한 문장도 reset 후 재사용할 수 있다. 바인딩된 문자열/BLOB을 호출 후 유지할 필요는 없다.

문장과 트랜잭션은 연결 상태를 공유 소유하므로 원래 `Database` 객체를 제거해도 조기에 연결을 해제하지 않는다. 트랜잭션은 commit 또는 명시적 rollback 전 제거되면 rollback한다. 실패한 commit은 성공으로 취급하지 않는다. 외부 SQL의 COMMIT으로 종료한 이전 트랜잭션이 새 트랜잭션을 rollback하지 않도록 소유 번호를 확인한다. 소멸자의 rollback 실패는 반환할 수 없으므로 오류 처리가 필요한 경로에서는 `rollback()` 결과를 직접 확인한다.

연결과 문장의 논리적 작업은 소비자의 한 실행 영역, 예컨대 Core SerialQueue에서 직렬화한다. SQLite FULLMUTEX가 여러 문장의 트랜잭션 순서를 보장하는 것은 아니다. `interrupt()`만 다른 스레드에서 호출할 수 있으며 그동안 Database 객체의 수명을 유지해야 한다. 이동·파괴와 동시에 호출하지 않는다. 바쁜 잠금 대기 시간과 값 크기를 옵션으로 제한하고 원시 SQLite 오류 코드를 보존한다. 조회 결과 행 수의 누적 제한은 소비자 책임이다.

독립 검증은 Native 프로젝트의 `build/`에서 수행한다. 전체 provider의 Qt 어댑터 및 다른 사용자의 작업을 재구성할 필요가 없다.

```sh
cmake -S Native -B Native/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON
cmake --build Native/build --parallel 4
ctest --test-dir Native/build --output-on-failure
cmake --install Native/build --prefix "$PWD/Native/build/install"
```

테스트는 Unicode 경로, 읽기 전용, 형식별 바인딩, NUL 문자열, 빈 BLOB, 중복/복수 문장 거절, rollback/commit, 한도, 연결 제거 후 문장 수명을 검증한다. 이 API의 제공은 SocietyHelper의 기존 QSqlDatabase/QSqlQuery 사용처가 모두 이행했다는 뜻이 아니다.
# 내구성 있는 편집과 스냅샷

`Database::configure_durable()`은 defensive 모드, 신뢰하지 않는 스키마,
DELETE journal과 EXTRA 동기화를 선택한다. `has_moved()`는 SQLite 핸들을 노출하지 않고
파일 경로 교체를 감지한다. `patch_blob()`은 명시적 트랜잭션 안에서 크기가 같은 BLOB의
변경한 바이트 구간만 기록한다. 롤백하면 원래 바이트가 복원된다.
`backup_to()`는 커밋된 WAL을 포함하는 일관된 스냅샷을 바이트·단조 시계 시간 한도 안에서
복사한다. 대상은 트랜잭션 밖에 있는 다른 연결이어야 한다. SQLite 실행과 핸들은
FileProvider가 소유하며 문서 스키마의 의미는 소비하는 문서 SDK에 남긴다.


## 기존 쓰기 잠금 호환 패키지

`Compatibility/`는 `iiFileProviderCompatibility` 설치 패키지와 `iiFileProvider::Compatibility` 타깃을 제공한다. 기존 `.iisacc-lock` 프로토콜과 원자적 교체는 네이티브 파일 제공자에 유지한다. 설치 타깃에는 소스 경로를 포함하지 않는다. `iiFileProvider.Compatibility.Contracts`는 연속 교체와 잠금 정리를 검사한다.
