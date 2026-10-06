# Yee 설정

## 주소와 소유 경계

| 사용자 주소 | 실제 WebUI | 역할 |
| --- | --- | --- |
| `브랜드://settings` | `chrome://yee-settings/` | Yee 설정 홈 |
| `브랜드://settings/content-blocking` | `chrome://yee-settings/content-blocking` | 기본 필터와 사이트 예외 |
| `브랜드://chromium-settings` | `chrome://settings/` | 기존 Chromium 설정 |

접두어는 `branding/brand.json`에서 생성한다. `chrome://settings`와 하위 경로는
기존 화면으로 연결하며, 실제 origin과 저장 URL은 브랜드 변경에도 유지된다.
일반 설정 메뉴는 Yee 설정 홈을 연다. 사이트 권한·다운로드·개인정보 등 기존
세부 설정 링크와 Chromium 설정 구현은 유지한다. Sidebar footer에는 새 설정
계층을 복제하지 않는다.

UI 소스와 리소스는 `browser/ui/settings/`에 둔다. Chromium 연결은 WebUI config
등록·일반 설정 메뉴의 목적지·번역 pak 합치기다. SvelteKit의 정적 빌드 결과를
C++ 리소스로 묶어 `WebUIDataSource`로 제공한다. 외부 폰트·이미지·CDN을 설정
화면에서 요청하지 않는다.

## 프런트엔드와 번역

- SvelteKit 3·Svelte 5·TypeScript를 사용한다. 버전은 `frontend/package.json`과
  잠금 파일에 고정한다. Node 22.17 이상과 npm은 빌드 도구다.
- 설정은 `vite.config.ts`, 타입 설정은 `$app/tsconfig`를 따른다. `package.json`의
  subpath import로 `#lib/*`와 `#styles`를 선언하며 모듈 확장자를 명시한다.
- 기본 컨트롤은 shadcn-svelte의 `Button`, `Input`, `Label`, `Badge`, `Separator`,
  `Card`를 사용한다. 소스는 `src/lib/components/ui/`에 소유하며 설정 행·설정 묶음은
  이 기본 컴포넌트를 조합한다. 아이콘은 Lucide에서 필요한 항목만 가져온다.
  타입이 있는 브리지가 기존
  WebUI 메시지를 호출하고, 상태 모델이 갱신 알림과 요청 중 상태를 관리한다.
- GRIT 원문은 `strings/settings_strings.grd`, 한국어 번역은
  `strings/settings_strings_ko.xtb`에 둔다. 브라우저 표시 언어를 따르며
  번역이 없는 언어는 영어로 표시한다. 문서 방향과 날짜 형식도 표시 언어를 따른다.
- 번역은 기존 locale pak에 합친 뒤 `AddLocalizedStrings()`와 `loadTimeData`로
  전달한다. 독립 resource ID 범위는 65000–65099이며 pak 합치기가 중복을 검사한다.
- 정적 bootstrap은 외부 JS 파일로 제공하고 Svelte의 `fragments: 'tree'`를 사용한다.
  Chromium의 기본 CSP·Trusted Types·외부 frame 제한을 유지한다.
- 설정 코드는 브라우저 실행 파일에 묶여 있다. SvelteKit의 포커스 복귀 시 버전
  확인은 공개 `hooks.client.ts`의 `init`에서 같은 WebUI의 `/_app/version.json` GET에
  현재 번들 버전을 응답한다. `chrome://`를 지원하지 않는 Fetch API로 인한 오류를
  방지하며, 다른 요청은 원래 Fetch API로 전달한다. 개발 서버에서는 이 처리를
  사용하지 않는다.

빌드 action은 npm 의존성과 Svelte 생성물을 GN 출력 디렉터리에 둔다.
제품 소스 디렉터리에는 `node_modules`나 생성된 JS를 보관하지 않는다.
첫 빌드 또는 잠금 파일 변경 때 `npm ci`로 설치하고 타입 검사·정적 빌드를 수행한다.

프런트엔드 포맷은 고정된 Prettier와 Svelte·Tailwind 플러그인으로 통일한다.
`frontend/.prettierrc.json`에 규칙을 두고 `format`은 정돈, `format:check`는 검사를
수행한다. 빌드의 `check`에도 포맷 검사를 포함한다. 생성 디렉터리와 npm 잠금 파일은
포맷 대상에서 제외한다. Svelte CLI의 기본 규칙에 맞춰 탭 들여쓰기, 작은따옴표,
마지막 쉼표 생략과 100자 줄 폭을 사용하며 Tailwind 클래스도 자동 정렬한다.
의존성이 설치된 GN 프런트엔드 workspace에서 제품 소스를
정돈하려면 저장소 루트에서 다음을 실행한다.

```sh
settings_source="$PWD/browser/ui/settings/frontend"
settings_workspace="$PWD/.local-build/chromium/src/out/YeePilot/gen/chrome/browser/ui/views/yee/settings/frontend_build"
(cd "$settings_workspace" && npm exec --offline -- prettier --write \
  --ignore-path "$settings_source/.prettierignore" "$settings_source")
```

### 기본 컴포넌트와 테마

shadcn-svelte 공식 레지스트리의 Mira 컴포넌트 소스를 가져와 `#lib/utils.ts` 경로에
연결했다. 원본은 `huntabyte/shadcn-svelte`의
`493481fab94f68b8982949bc8a37eede786f2462` 커밋,
`docs/static/registry/styles/mira/` 아래의 여섯 컴포넌트다. MIT 고지는
`frontend/src/lib/components/ui/LICENSE`에 보관한다. 컴포넌트 소스 갱신은 기존
제품 조합과 별도로 검토하며 바인딩·키보드 포커스·WebUI 보안 정책을 확인한다.
구분선 방향 클래스는 설치된 Bits UI의 `data-orientation` 속성에 맞춘다.
기본 강조 버튼·배지의 hover 색상도 제품 토큰을 사용해 흰 글자의 대비를 유지한다.

Tailwind CSS 4는 Vite 플러그인으로 빌드하며 탐색 대상은 프런트엔드 `src/`로
제한한다. 공통 색상·밝기·모서리·포커스 토큰은 `src/lib/styles/theme.css`, 설정
화면의 배치와 컴포넌트 스타일은 각 `.svelte` 파일의 `<style>`에 둔다.
Tailwind 기반 설정과 문서 전체의 기본 스타일은 `src/styles.css`에 둔다.
루트 layout은 이 스타일을 컴포넌트보다 먼저 가져와 CSS layer 순서를 유지한다.
`cn()`으로 컴포넌트 기본 클래스와 제품별 클래스를 합친다.
기본 버튼·입력창을 전역 태그 선택자로 덮어쓰지
않으며, 입력창의 값과 DOM ref는 Svelte 바인딩으로 전달한다.

### 컴포넌트 배치

페이지 전용 UI는 해당 `routes` 경로 아래의 `components/`에 둔다. 전체 설정의
탐색은 `routes/components/SettingsNavigation.svelte`, 필터 목록과 사이트 예외는
`routes/content-blocking/components/`가 소유한다. 여러 화면에서 사용하는 헤더·행·
설정 묶음·안내 UI와 기본 컨트롤은 `src/lib/components/`에 둔다.

`+layout.svelte`는 모델 수명과 화면 구성을 연결한다. 본문 스크롤과 이동 후 포커스,
뒤로·앞으로·새로고침의 위치 복원은 인접한 `routes/settings-scroll.svelte.ts`가
관리한다. 각 페이지와 전용 컴포넌트는 기존 공통 상태 모델을 사용하며,
전용 컴포넌트에서 새로운 WebUI 메시지나 상태 모델을 만들지 않는다.

## 화면 설계

평평한 본문과 묶음별 설정 행, 얇은 구분선과 작은 아이콘을 사용한다. 항목명과
설명은 왼쪽, 현재 상태와 조작은 오른쪽에 둔다. 시스템 글꼴과 작은 보라색 조작
강조를 사용한다. 탐색 너비 218px, 본문 너비 상한 680px로 설정이 화면 전체에
퍼지지 않게 한다. 설정 화면은 탭의 뷰포트 높이에 맞추고 왼쪽 탐색은 전체 높이를
채운다. 본문은 독립적으로 스크롤하며, 새 설정 항목으로 이동하면 맨 위에서 시작하고
뒤로·앞으로 이동하거나 새로고침하면 이전 스크롤 위치를 복원한다. 높이가 작은 창에서는
탐색도 스크롤할 수 있다. 좁은 탭에서는 여백을 줄이고, 540px 이하에서는 탐색을 상단으로
이동해 본문 폭을 확보한다. OS 밝기 설정과 키보드 focus를 지원하며 자동 등장
애니메이션은 없다.

## 현재 기능

- EasyList·EasyPrivacy의 기본/다운로드 상태, 마지막 확인 시각과 재시작 적용 안내.
- ‘업데이트 확인’: 기존 공식 목록 coordinator를 사용하며 진행 중인 갱신에는
  합류한다. 쿠키·인증 정보 없이 다운로드하고 기존 검증·복구·다음 실행 적용
  정책을 따른다. 실패하면 현재 필터를 유지한다.
- 정확한 hostname 단위 사이트 예외 추가와 차단 다시 켜기. 방패 버튼과 같은
  프로필 service를 사용하고 변경 이벤트를 받아 표시를 갱신한다. 이미 열린
  사이트는 새로고침 후 적용된다.
- 시크릿·게스트에서는 필터 갱신을 허용하지 않으며 예외는 해당 세션에만 적용한다.
- 사이트 권한·개인정보·다운로드·모양과 전체 고급 설정으로 이동.

사용자 구독 목록, 개별 기본 필터 선택과 실행 중 전체 필터 generation 전환은
아직 지원하지 않는다. 지원하지 않는 조작을 설정 화면에 표시하지 않는다.

## 검증

`InternalURLsTest`는 양방향 주소 변환과 기존 설정 하위 주소 보존을 검사한다.
`InternalURLsBrowserTest`는 실제 WebUI, 주소창 표시·복사·저장, 일반 설정 메뉴와
기존 사이트 정책 service 연결을 검사한다. `BaselineListUpdaterTest`는 수동
갱신의 예약 우회·중복 합류·실패와 owner 종료를 검사한다. 제품 완료 판정에는
새로 실행한 Yee 앱의 실제 설정 탭과 밝기·폭별 화면 확인도 포함한다.
