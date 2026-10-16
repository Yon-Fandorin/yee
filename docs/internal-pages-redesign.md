# 내부 페이지 리디자인 후보

[전체 주소 목록](internal-pages-inventory.md)은 현재 Chromium 소스의 등록과
설정 경로를 기록한다. 이 문서는 그 목록에서 사용자에게 도움이 되는 이관 작업을
고르는 제안이다. 실제 지원 여부와 범위를 확인한 뒤 작업을 시작한다.
구현이 끝난 화면의 동작과 주소는 [내부 페이지](internal-pages.md)를 따른다.

## 분류

| 분류 | 의미 |
| --- | --- |
| 완료 | Yee 자체 화면으로 구현하고 빌드·실제 앱 검수를 마친 범위 |
| 우선 추천 | 사용 빈도나 화면 일관성 개선 효과가 큰 다음 작업 |
| 후속 추천 | 기본 설정을 Yee 화면에서 직접 다룰 수 있도록 이관 |
| 범위 검토 | 상세 기능과 native 연결을 먼저 확인할 사용자 화면 |
| 기능 결정 | Yee에서 제공할 기능과 연결 서비스를 먼저 정할 화면 |
| 기본 유지 | 진단·개발·법적 정보 또는 기존 확인 절차를 사용하는 화면 |

등록 주소·설정 하위 경로·삽입 화면을 아래 기능별 작업 단위로 묶는다.
하위 경로와 확인창을 하나씩 새로운 독립 페이지로 만들지는 않는다.

## 완료한 범위

| 사용자 화면 | 포함한 범위 | 남은 관련 화면 |
| --- | --- | --- |
| 설정 홈 | Yee 설정 탐색과 기본 설정 연결 | Chromium 설정의 세부 항목 |
| 콘텐츠 차단 | 기본 필터, 외부 구독, 도메인 규칙, 사이트 예외, 가져오기·내보내기 | [현재 지원 범위](settings.md) 밖의 기능은 별도 제품 결정 |
| 다운로드 목록 | 검색, 파일 종류 아이콘, 일반 파일 작업, 기록 삭제·실행 취소 | 저장 폴더 등 다운로드 설정, native 보안 확인 |
| 방문 기록 | 검색, 추가 조회, 날짜별 목록, 개별·선택 삭제 | 다른 기기의 탭, 묶음 기록·비교 목록 등 조건부 기능 |
| 북마크 관리 | 폴더 탐색, 검색, 편집·이동·삭제·실행 취소, 가져오기·내보내기 | 북마크 Side Panel은 다른 화면 |
| 확장 프로그램 관리 | 목록·검색·사용 여부·상세 정보, 개별 확장의 사이트 접근·시크릿·파일 접근, native 옵션·삭제 연결 | 단축키·개발자 진단·프로필 전체 사이트 권한은 원래 화면 |

설정 홈 완료는 설정 전체 이관을 뜻하지 않는다. 다운로드 목록과 다운로드
설정도 서로 다른 작업이다. 원래 `chrome://` 페이지는 기존 화면을 제공한다.

## 리디자인하면 좋은 사용자 화면

| 분류 | 작업 단위 | 현재 화면 | 권장 범위와 확인할 점 |
| --- | --- | --- | --- |
| 우선 추천 | 모양·글꼴 | `chrome://settings/appearance`, `/fonts` | 제공되는 테마·글꼴·확대 비율을 공통 설정 행으로 이관. Yee chrome에 영향을 주는 항목은 native 연결 확인 |
| 후속 추천 | 확장 단축키·전체 사이트 권한 | `chrome://extensions/shortcuts`, `/sitePermissions` | 현재 원래 화면으로 연결한다. 이관 시 프로필 전체 사이트 제한과 개별 확장의 권한을 함께 처리하는 native 흐름 확인 |
| 후속 추천 | 시작 동작 | `chrome://settings/onStartup` | 시작 페이지·이전 세션 복원. 기존 세션 저장과 정책 사용 |
| 후속 추천 | 검색엔진 | `chrome://settings/search`, `/searchEngines` | 기본 검색엔진·사이트 검색. 정책·확장 프로그램이 제어하는 값 표시 |
| 후속 추천 | 다운로드 설정 | `chrome://settings/downloads` | 저장 위치·위치 묻기. 기존 폴더 선택기 사용 |
| 후속 추천 | 기본 브라우저 | `chrome://settings/defaultBrowser` | 현재 상태와 OS 기본 브라우저 설정 연결 |
| 후속 추천 | 성능·시스템 | `chrome://settings/performance`, `/system` | 해당 빌드에서 제공되는 절전·메모리·가속 설정. 재시작 필요 여부 표시 |
| 후속 추천 | 언어·접근성 | `chrome://settings/languages`, `/spellCheck`, `/accessibility` | 언어·번역·맞춤법·접근성. OS가 제공하는 설정과 native 연결 구분 |
| 후속 추천 | 브라우저 정보 | `chrome://settings/help` | Yee 버전·라이선스 연결. 업데이트 상태는 Yee 배포·업데이트 지원 범위 확인 후 표시 |
| 범위 검토 | 로컬 프로필·가져오기 | `chrome://settings/manageProfile`, `/importData`, `chrome://profile-picker/` | 기존 프로필 모델·파일 선택·가져오기 handler 사용. 탭 사이드바의 프로필 UI와 역할 확인 |
| 범위 검토 | 사이트 권한·데이터 | `chrome://settings/content` 및 하위 경로 | 사이트별 권한, 권한 종류별 목록, 저장 데이터. native 정책·권한 변경·삭제 절차 유지 |
| 범위 검토 | 개인정보·보안 | `chrome://settings/privacy`, `/security`, `/cookies`, `/safetyCheck`, `/clearBrowserData` | 기능별 상태와 확인 절차를 정리. 보안 검사·삭제 범위·native 제한 유지 |
| 범위 검토 | 비밀번호·자동완성 | `chrome://password-manager/`, `chrome://settings/autofill` 및 하위 경로 | 목록·검색·편집과 관련 설정. OS 재인증·가져오기·내보내기·계정 기능의 지원 범위 확인 |
| 범위 검토 | 설정 초기화 | `chrome://settings/reset` 및 확인창 | 초기화 범위 설명과 기존 native 확인·실행 절차 사용 |
| 범위 검토 | 설치된 웹앱 | `chrome://apps/`, `chrome://app-settings/` | 앱 목록·상세·권한·삭제. 해당 앱 관리 기능을 Yee에 노출할지 결정 |
| 범위 검토 | 첫 실행·프로필 꾸미기 | `chrome://intro/`, `chrome://profile-customization/`, `chrome://default-browser-modal/` | Yee 첫 실행 흐름이 필요한지 결정하고 기존 설정 페이지와 중복 확인 |
| 범위 검토 | 보조 화면 | `chrome://print/`, `chrome://certificate-manager/`, `chrome://management/`, `chrome://feedback/`, `chrome://whats-new/` | 인쇄·인증서·관리 상태는 실제 사용 경로 확인. 피드백·새 기능 안내는 Yee의 제공 서비스와 콘텐츠가 필요 |

## 기능 결정이 필요한 화면

| 기능군 | 관련 화면 | 결정할 내용 |
| --- | --- | --- |
| 계정·동기화 | `settings/people`, `/account`, `/googleServices`, `/syncSetup`, `history/syncedTabs`와 로그인·동기화 확인창 | Yee가 지원할 계정·동기화 서비스와 세션 범위 |
| AI·서비스 연동 | `settings/ai`, `glic`, `contextual-tasks`, `context-hub`, `skills`, Lens·Compose·Drive·Microsoft 인증 프레임 | 실제 제공 기능과 외부 서비스 연결. 소스에 등록된 것만으로 Yee의 지원 기능이 되지는 않음 |
| 새 탭·시작 화면 | `chrome://newtab/`, `chrome://new-tab-page/` | 현재 새 탭은 유지한다. AI·대시보드 또는 사용자 지정 URL을 제공할 때 화면 선택과 적용 범위를 결정한다. 새 탭과 브라우저 시작 동작을 구분하고 검색엔진·확장 프로그램의 새 탭 대체·시크릿·게스트 동작을 확인 |
| 삽입 화면 | 북마크·방문 기록·읽기 목록·다른 기기의 탭·탭 검색·읽기 모드·페이지 꾸미기 등 `*.top-chrome` 화면 | Side Panel과 Yee의 Tab Sidebar는 다른 영역. 예약된 Sidebar 슬롯 활성화는 별도 제품 결정 |

## 기본 UI 유지 권장

- `flags`, `gpu`, `version`, `policy`, `components`, `crashes`, `system`,
  `net-export`, `net-internals`, `discards`, `tracing`, `traces`와 각종 `*-internals`.
- 확장 프로그램의 활동 로그·오류, `inspect`, `iwa-dev` 등 개발 도구 화면.
  일반 확장 관리의 이관 범위와 구분하고 기존 개발 UI를 사용한다.
- 라이선스·약관 원문인 `credits`, `terms`.
- 인증·보안 중간 화면과 native 재인증·위험 파일·초기화 등 확인 절차.
- DevTools와 내부 리소스 endpoint. 별도 진단·개발 UI 작업이 필요할 때 검토한다.

`chrome://system/`은 진단 정보이고 `chrome://settings/system`은 사용자 설정이다.
`chrome://accessibility/`과 `chrome://settings/accessibility`도 같은 방식으로 구분한다.

## 병렬 작업과 완료 기준

확장 프로그램·설정 세부 화면은 기능별 native 소유 디렉터리와 frontend
route에서 병렬로 구현할 수 있다. 공통 UI·번역 ID·주소 변환·Chromium 등록 glue와
`0001` 패치 갱신은 한 담당자가 모아 통합하고 최종 빌드를 수행한다.
같은 설정 bridge·상태 모델을 변경하는 세부 항목은 한 작업 단위에서 진행한다.

각 작업은 포함할 상세 화면·확인창·정책·시크릿/게스트 동작을 먼저 정한다.
기존 데이터·native 서비스·정책을 사용하고, 원래 Chromium 경로는 유지한다.
빌드·필요한 기능 검사·새로 실행한 실제 Yee 화면 검수를 마친 범위만 완료로 표시한다.
진단 주소의 개수로 사용자 페이지 이관 완료 여부를 판단하지 않는다.
