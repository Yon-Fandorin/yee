# 브라우저 화면 프로토타입

HTML·CSS·JavaScript로 화면 배치와 기본 동작을 비교한다.
실제 Chromium의 탭이나 주소창, 웹페이지 실행 기능을 구현한 화면은 아니다.
제품의 기준은 [브라우저 셸 명세](../../docs/browser-shell-spec.md)다.

## 열기

저장소 루트에서 실행한다.

```sh
python3 -m http.server 4173 --bind 127.0.0.1
```

`http://127.0.0.1:4173/experiments/shell-prototype/?titlebar=regular&tenant=offset&sidebar=open`을 연다.

| 주소의 옵션 | 비교할 내용 |
| --- | --- |
| `titlebar=regular|thin` | 일반·압축 제목 표시줄 |
| `tenant=squircle|offset|inset` | Tenant 이미지 형태 |
| `sidebar=open|closed` | 사이드바 열림·닫힘 |
| `os=windows|mac|linux` | 플랫폼별 창 테두리와 버튼 |

`app.js`가 `js/`의 검색·탭 관리·하단 영역·DOM 도우미를 연결한다.
프로토타입과 현재 제품 구현의 모양이 다를 수 있다.
화면 실험이 끝나도 실제 앱의 동작은 별도로 확인해야 한다.
