# iiFileProvider 작업 지침

메모리 파일 검색 스냅샷은 `FileSearchIndex`가 소유하며 경로 탐색이나 Society 의존성을 추가하지 않는다. `docs/FILE_SEARCH_INDEX.md`의 검색 및 병합 계약을 유지한다.

C++23과 표준·iiSDK의 공개 인터페이스를 사용한다. SDK 소스와 헤더는 src/에서 함께 유지한다. 새 네이티브 경로에 Qt 의존성을 추가하지 않는다.

빌드·설치 스테이징·검사는 저장소의 build/ 안에서 수행한다. 소스 변경 시 관련 docs/와 tests/를 함께 갱신한다. README.md는 영어, docs/ 문서는 한국어로 작성한다. 기존 작업과 사용자 파일을 보존한다. 플랫폼별 소스 이식과 실제 실행 검증을 구분한다.

## 라이선스

iiFileProvider는 iiSDK 모듈이며 Apache License 2.0이다. 자사 코드, 문서, 도구, 테스트가 포함된다. 저작권 표시는 `Copyright (c) 2026 iisacc`이고 고지는 루트 `NOTICE`에 있다. 사람용 한국어 설명은 [docs/License.md](docs/License.md)이다.

`Product` 아래의 iisacc 제품은 GNU Affero General Public License 3.0 이후 버전(AGPL-3.0-or-later)이다.

제3자와 벤더 코드는 원래 라이선스를 유지한다. 문서 재편 중에도 그 라이선스와 외부 원본은 보존한다.

<!-- workspace-docs:agent:start -->
## 문서 구조와 에이전트 탐색

- 프로젝트 루트의 `README.md`는 사람용 영문 진입 문서이며 `AGENT.md`는 에이전트 진입점이다.
- 하위 인간용 Markdown과 구조화된 agent JSON을 함께 유지한다. Markdown이 원본이며 JSON은 원문 해시와 줄 범위를 가진 파생 문서이다.
- 먼저 [docs/agent-index.json](docs/agent-index.json)에서 관련 문서와 JSON 투영을 찾는다. 원문 SHA-256이 다르면 현재 Markdown을 읽고 투영을 갱신한다.
- JSON의 규칙 후보는 검색용 어휘 일치 결과이다. 과거 기록·예제·실패 상태를 현재 지시나 검증 완료로 자동 해석하지 않는다.
- 기존 프로젝트별 규칙, 동결된 증거, 라이선스, 외부 원본, 사용자 데이터와 다른 작업의 변경을 보존한다.
- 이 문서 재편 지침은 과거 작업의 기능 구현·배포를 자동 재개하라는 요청이 아니다. 현재 사용자의 작업 범위를 우선한다.
- 문서를 수정하면 JSON 쌍·색인을 함께 갱신하고 검증한다. 기존 소스 변경에 대한 빌드·테스트 규칙은 계속 따른다.

Workspace에서 이 프로젝트의 문서를 갱신하는 명령은 다음과 같다.

```sh
python3 ../../Engineering/WorkspaceDocumentation/App/workspace_docs.py --workspace ../.. sync --project SDK/iiFileProvider
```
<!-- workspace-docs:agent:end -->
