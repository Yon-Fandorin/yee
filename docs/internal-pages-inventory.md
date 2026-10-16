# 내부 페이지 전체 목록

[리디자인 후보](internal-pages-redesign.md)는 사용자 화면의 작업 단위와 우선순위를
정리한다. 이 문서는 현재 체크아웃에서 확인한 주소와 조건을 기록한다.

## 기준과 범위

- 기준 Chromium revision: `25189dfe0a49b3f8324586374b8251b8d12ad1c2`에 현재 Yee
  `patches/0001-integrate-yee-shell.patch`와 `build/overlay.json`의 source overlay를
  적용한 체크아웃이다. Chrome 등록 154개에는 upstream 149개와 Yee 소유 5개가 포함된다.
- Chrome·Content 공통 WebUI 등록 파일 세 곳의 179개 등록, Settings router의
  103개 경로 정의와 주요 사용자 페이지의 상세 경로를 포함한다.
- 등록 파일에 함께 있는 다른 플랫폼·빌드 조건도 표시한다. 등록은 실제 실행 중
  노출과 다르며 feature flag, `IsWebUIEnabled`, 계정·프로필·정책에 따라 접근이 달라진다.
- Android·ChromeOS 전용 전체 제품 화면은 Yee 데스크톱 이관 범위에 포함하지 않는다.
  ChromeOS 별도 등록 함수의 전체 OS 화면은 이 목록의 조사 범위 밖이다.
- 메뉴·주소창·Tab Sidebar 같은 native Views UI, 일반 웹사이트와 확장 프로그램의
  자체 웹페이지는 WebUI 페이지 목록에 포함하지 않는다.

### 소스

아래 경로는 Chromium checkout root 기준이다.

| 기준 | 소스 |
| --- | --- |
| Chrome WebUI 등록 | `chrome/browser/ui/webui/chrome_web_ui_configs.cc` |
| 삽입 untrusted WebUI 등록 | `chrome/browser/ui/webui/chrome_untrusted_web_ui_configs.cc` |
| Content WebUI 등록 | `content/browser/webui/content_web_ui_configs.cc` |
| 카탈로그·명령 주소 | `chrome/common/webui_url_constants.cc`, `ChromeURLHosts()`·`ChromeDebugURLs()` |
| 설정 경로 | `chrome/browser/resources/settings/route.ts` |
| 확장 프로그램 상세 경로 | `chrome/browser/resources/extensions/navigation_helper.ts` |
| 방문 기록 상세 경로 | `chrome/browser/resources/history/router.ts` |
| 비밀번호 상세 경로 | `chrome/browser/resources/password_manager/router.ts` |

`chrome://chrome-urls/`는 실행 중 빌드의 공개 주소 카탈로그를 확인하는 진입점이다.
모든 등록·설정 하위 경로·삽입 프레임을 열거하지는 않는다.

해당 주소의 처리 제안은 `우선 추천`, `후속 추천`, `범위 검토`, `기능 결정`,
`기본 유지`로 표시한다. 원래 Chromium 주소와 Yee 소유 주소의 상태를 각각 기록한다.
Yee 소유 화면은 `완료`로 표시하며 설정 홈의 완료 범위는
[후보 문서](internal-pages-redesign.md#완료한-범위)를 따른다.
조건의 `공통`은 등록 위치에 빌드 분기가 없다는 뜻이다. 런타임 활성화를 뜻하지 않는다.
빌드 조건의 이름은 `BUILDFLAG()` 값을 가리킨다. 설정 생성 조건의
`visibility.항목`은 원문의 `visibility.항목 !== false`를 줄여 쓴 것이다.
주소는 구현 origin과 원래 Chromium 경로를 기준으로 적었다. 브랜드 주소와 Yee
소유 경로의 연결은 [내부 페이지](internal-pages.md)를 따른다.

## 전체 등록 주소

### Chrome WebUI: 154개 등록

| 주소 | 등록 type | 빌드 조건 | 해당 주소의 처리 제안 |
| --- | --- | --- | --- |
| `chrome://access-code-cast/` | `media_router::AccessCodeCastUIConfig` | (!(IS_ANDROID)) | 기능 결정 |
| `chrome://accessibility/` | `AccessibilityUIConfig` | 공통 | 기본 유지 |
| `chrome://actor-internals/` | `ActorInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://actor-overlay/` | `actor::ui::ActorOverlayUIConfig` | (!(IS_ANDROID)) | 기능 결정 |
| `chrome://app-service-internals/` | `AppServiceInternalsUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://app-settings/` | `WebAppSettingsUIConfig` | (IS_WIN \|\| IS_MAC \|\| IS_LINUX) | 범위 검토 |
| `chrome://apps/` | `AppHomeUIConfig` | (!(IS_ANDROID)) && (!IS_CHROMEOS) | 범위 검토 |
| `chrome://autofill-internals/` | `AutofillInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://autofill-ml-internals/` | `AutofillMlInternalsUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://batch-upload/` | `BatchUploadUIConfig` | (ENABLE_DICE_SUPPORT \|\| IS_CHROMEOS) | 기능 결정 |
| `chrome://bluetooth-internals/` | `BluetoothInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://bookmarks-side-panel.top-chrome/` | `BookmarksSidePanelUIConfig` | (!(IS_ANDROID)) | 기능 결정: 삽입 UI |
| `chrome://bookmarks/` | `BookmarksUIConfig` | (!(IS_ANDROID)) | 기본 유지: 원래 경로 |
| `chrome://browser-switch/` | `BrowserSwitchUIConfig` | (IS_WIN \|\| IS_MAC \|\| IS_LINUX) | 기본 유지 |
| `chrome://cast-feedback/` | `media_router::CastFeedbackUIConfig` | (!(IS_ANDROID)) && (GOOGLE_CHROME_BRANDING) | 기능 결정 |
| `chrome://certificate-manager/` | `CertificateManagerUIConfig` | (CHROME_ROOT_STORE_CERT_MANAGEMENT_UI) | 범위 검토 |
| `chrome://chrome-finds-internals/` | `chrome_finds_internals::ChromeFindsInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://chrome-signin/` | `InlineLoginUIConfig` | (IS_WIN \|\| IS_CHROMEOS) | 기능 결정 |
| `chrome://chrome-urls/` | `chrome_urls::ChromeUrlsUIConfig` | 공통 | 기본 유지 |
| `chrome://color-pipeline-internals/` | `ColorPipelineInternalsUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://comments-side-panel.top-chrome/` | `CommentsSidePanelUIConfig` | (!(IS_ANDROID)) | 기능 결정: 삽입 UI |
| `chrome://commerce-internals/` | `commerce::CommerceInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://components/` | `ComponentsUIConfig` | 공통 | 기본 유지 |
| `chrome://conflicts/` | `ConflictsUIConfig` | (IS_WIN) | 기본 유지 |
| `chrome://connection-help/` | `security_interstitials::ConnectionHelpUIConfig` | 공통 | 기본 유지 |
| `chrome://connection-monitoring-detected/` | `security_interstitials::KnownInterceptionDisclosureUIConfig` | 공통 | 기본 유지 |
| `chrome://connectors-internals/` | `enterprise_connectors::ConnectorsInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://constrained-test/` | `ConstrainedWebDialogUIConfig` | 공통 | 기본 유지 |
| `chrome://content-annotator-internals/` | `content_annotator_internals::ContentAnnotatorInternalsUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://context-hub/` | `ContextHubUIConfig` | 공통 | 기능 결정 |
| `chrome://contextual-cueing-internals/` | `contextual_cueing_internals::ContextualCueingInternalsUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://contextual-tasks/` | `ContextualTasksUIConfig` | 공통 | 기능 결정 |
| `chrome://crashes/` | `CrashesUIConfig` | 공통 | 기본 유지 |
| `chrome://credits/` | `CreditsUIConfig` | 공통 | 기본 유지 |
| `chrome://cross-device-signin-qr-bubble/` | `CrossDeviceSigninQrBubbleUIConfig` | (ENABLE_DICE_SUPPORT) | 기능 결정 |
| `chrome://customize-chrome-side-panel.top-chrome/` | `CustomizeChromeUIConfig` | (!(IS_ANDROID)) | 기능 결정: 삽입 UI |
| `chrome://data-sharing-internals/` | `DataSharingInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://debug-webuis-disabled/` | `InternalDebugPagesDisabledUIConfig` | 공통 | 기본 유지 |
| `chrome://default-browser-modal/` | `DefaultBrowserModalUIConfig` | (!IS_CHROMEOS && !IS_ANDROID) | 범위 검토 |
| `chrome://default-browser/` | `VisualGuidedSetterUIConfig` | (IS_WIN) | 범위 검토 |
| `chrome://device-log/` | `chromeos::DeviceLogUIConfig` | 공통 | 기본 유지 |
| `chrome://discards/` | `DiscardsUIConfig` | (IS_WIN \|\| IS_MAC \|\| IS_LINUX \|\| IS_CHROMEOS \|\| IS_DESKTOP_ANDROID) | 기본 유지 |
| `chrome://dlp-internals/` | `policy::DlpInternalsUIConfig` | (IS_CHROMEOS) | 기본 유지 |
| `chrome://download-internals/` | `DownloadInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://downloads/` | `DownloadsUIConfig` | (!(IS_ANDROID)) | 기본 유지: 원래 경로 |
| `chrome://drive-picker-host/` | `DrivePickerHostUIConfig` | (!(IS_ANDROID)) | 기능 결정 |
| `chrome://extensions-zero-state/` | `extensions::ZeroStatePromoControllerConfig` | (ENABLE_EXTENSIONS) | 기본 유지 |
| `chrome://extensions/` | `extensions::ExtensionsUIConfig` | (ENABLE_EXTENSIONS_CORE) | 기본 유지: 일반 관리 Yee 화면 제공 |
| `chrome://family-link-user-internals/` | `FamilyLinkUserInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://feature-showcase/` | `FeatureShowcaseUIConfig` | (ENABLE_DICE_SUPPORT) | 기능 결정 |
| `chrome://feedback/` | `FeedbackUIConfig` | (!(IS_ANDROID)) | 범위 검토 |
| `chrome://flags/` | `FlagsUIConfig` | 공통 | 기본 유지 |
| `chrome://gcm-internals/` | `GCMInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://glic-experimental-opt-in/` | `glic::GlicExperimentalOptInUIConfig` | (!(IS_ANDROID)) | 기능 결정 |
| `chrome://glic/` | `glic::GlicUIConfig` | 공통 | 기능 결정 |
| `chrome://history-clusters-internals/` | `history_clusters_internals::HistoryClustersInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://history-clusters-side-panel.top-chrome/` | `HistoryClustersSidePanelUIConfig` | (!(IS_ANDROID)) | 기능 결정: 삽입 UI |
| `chrome://history-side-panel.top-chrome/` | `HistorySidePanelUIConfig` | (!(IS_ANDROID)) | 기능 결정: 삽입 UI |
| `chrome://history-sync-optin/` | `HistorySyncOptinUIConfig` | (IS_WIN \|\| IS_MAC \|\| IS_LINUX) | 기능 결정 |
| `chrome://history/` | `HistoryUIConfig` | (!(IS_ANDROID)) | 기본 유지: 원래 경로 |
| `chrome://indigo-internals/` | `IndigoInternalsUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://infobar-internals/` | `InfobarInternalsUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://inspect/` | `InspectUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://internals/` | `InternalsUIConfig` | (!(IS_ANDROID)) && (ENABLE_SESSION_SERVICE) | 기본 유지 |
| `chrome://interstitials/` | `InterstitialUIConfig` | 공통 | 기본 유지 |
| `chrome://intro/` | `IntroUIConfig` | (!IS_CHROMEOS && !IS_ANDROID) | 범위 검토 |
| `chrome://iwa-dev/` | `IwaDevUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://linux-proxy-config/` | `LinuxProxyConfigUI` | (IS_LINUX \|\| IS_CHROMEOS \|\| IS_OPENBSD) | 기본 유지 |
| `chrome://local-state/` | `LocalStateUIConfig` | 공통 | 기본 유지 |
| `chrome://location-internals/` | `LocationInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://managed-user-profile-notice/` | `ManagedUserProfileNoticeUIConfig` | (!IS_CHROMEOS && !IS_ANDROID) | 기능 결정 |
| `chrome://management/` | `ManagementUIConfig` | 공통 | 범위 검토 |
| `chrome://media-engagement/` | `MediaEngagementUIConfig` | 공통 | 기본 유지 |
| `chrome://media-router-internals/` | `media_router::MediaRouterInternalsUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://memory-internals/` | `MemoryInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://metrics-internals/` | `MetricsInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://multistep-filter-internals/` | `multistep_filter_internals::MultistepFilterInternalsUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://net-export/` | `NetExportUIConfig` | 공통 | 기본 유지 |
| `chrome://net-internals/` | `NetInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://new-tab-page-third-party/` | `NewTabPageThirdPartyUIConfig` | (!(IS_ANDROID)) && (ENABLE_WEBUI_NTP) | 기능 결정 |
| `chrome://new-tab-page/` | `NewTabPageUIConfig` | (ENABLE_WEBUI_NTP) | 기능 결정 |
| `chrome://newtab-footer/` | `NewTabFooterUIConfig` | (!(IS_ANDROID)) && (ENABLE_WEBUI_NTP) | 기능 결정 |
| `chrome://newtab/` | `NewTabUIConfig` | (!(IS_ANDROID)) | 기능 결정 |
| `chrome://notebooks-internals/` | `notebooks::NotebooksInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://notifications-internals/` | `NotificationsInternalsUIConfig` | (IS_ANDROID) | 기본 유지 |
| `chrome://ntp-tiles-internals/` | `NTPTilesInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://omnibox-everywhere.top-chrome/` | `OmniboxEverywhereUIConfig` | (!(IS_ANDROID)) | 기능 결정: 삽입 UI |
| `chrome://omnibox-popup.top-chrome/` | `OmniboxPopupUIConfig` | (!(IS_ANDROID)) | 기능 결정: 삽입 UI |
| `chrome://omnibox/` | `OmniboxUIConfig` | 공통 | 기본 유지 |
| `chrome://on-device-internals/` | `on_device_internals::OnDeviceInternalsUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://on-device-translation-internals/` | `OnDeviceTranslationInternalsUIConfig` | (IS_WIN \|\| IS_MAC \|\| IS_LINUX) | 기본 유지 |
| `chrome://optimization-guide-internals/` | `OptimizationGuideInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://organizer-panel.top-chrome/` | `OrganizerPanelUIConfig` | (!(IS_ANDROID)) | 기능 결정: 삽입 UI |
| `chrome://password-manager-internals/` | `PasswordManagerInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://password-manager/` | `PasswordManagerUIConfig` | (!(IS_ANDROID)) | 범위 검토 |
| `chrome://policy/` | `PolicyUIConfig` | 공통 | 기본 유지 |
| `chrome://predictors/` | `PredictorsUIConfig` | 공통 | 기본 유지 |
| `chrome://print/` | `printing::PrintPreviewUIConfig` | (ENABLE_PRINT_PREVIEW) | 범위 검토 |
| `chrome://private-ai-internals/` | `private_ai::PrivateAiInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://profile-customization/` | `ProfileCustomizationUIConfig` | (!IS_CHROMEOS && !IS_ANDROID) | 범위 검토 |
| `chrome://profile-internals/` | `ProfileInternalsUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://profile-picker/` | `ProfilePickerUIConfig` | (!IS_CHROMEOS && !IS_ANDROID) | 범위 검토 |
| `chrome://read-later.top-chrome/` | `ReadingListUIConfig` | (!(IS_ANDROID)) | 기능 결정: 삽입 UI |
| `chrome://regional-capabilities-internals/` | `RegionalCapabilitiesInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://reset-password/` | `ResetPasswordUIConfig` | (FULL_SAFE_BROWSING) | 기본 유지 |
| `chrome://safe-browsing/` | `safe_browsing::ChromeSafeBrowsingUIConfig` | (SAFE_BROWSING_AVAILABLE) | 기본 유지 |
| `chrome://sandbox/` | `SandboxInternalsUIConfig` | (IS_WIN \|\| IS_LINUX \|\| IS_CHROMEOS \|\| IS_ANDROID) | 기본 유지 |
| `chrome://saved-tab-groups-unsupported/` | `SavedTabGroupsUnsupportedUIConfig` | 공통 | 기본 유지 |
| `chrome://search-engine-choice/` | `SearchEngineChoiceUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://segmentation-internals/` | `SegmentationInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://settings/` | `settings::SettingsUIConfig` | (!(IS_ANDROID)) | 기본 유지: 원래 경로 |
| `chrome://shopping-insights-side-panel.top-chrome/` | `ShoppingInsightsSidePanelUIConfig` | (!(IS_ANDROID)) | 기능 결정: 삽입 UI |
| `chrome://signin-dice-web-intercept.top-chrome/` | `DiceWebSigninInterceptUIConfig` | (ENABLE_DICE_SUPPORT) | 기능 결정: 삽입 UI |
| `chrome://signin-email-confirmation/` | `SigninEmailConfirmationUIConfig` | (!IS_CHROMEOS && !IS_ANDROID) | 기능 결정 |
| `chrome://signin-error/` | `SigninErrorUIConfig` | (!IS_CHROMEOS && !IS_ANDROID) | 기능 결정 |
| `chrome://signin-internals/` | `SignInInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://signout-confirmation/` | `SignoutConfirmationUIConfig` | (ENABLE_DICE_SUPPORT) | 기능 결정 |
| `chrome://site-engagement/` | `SiteEngagementUIConfig` | 공통 | 기본 유지 |
| `chrome://skills/` | `skills::SkillsUIConfig` | (IS_WIN \|\| IS_MAC \|\| IS_LINUX \|\| IS_CHROMEOS) | 기능 결정 |
| `chrome://snippets-internals/` | `FeedInternalsUIConfig` | (IS_ANDROID) | 기본 유지 |
| `chrome://subresource-filter-internals/` | `subresource_filter::SubresourceFilterInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://suggest-internals/` | `SuggestInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://support-tool/` | `SupportToolUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://sync-confirmation/` | `SyncConfirmationUIConfig` | (!(IS_ANDROID)) | 기능 결정 |
| `chrome://sync-internals/` | `SyncInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://system/` | `SystemInfoUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://tab-group-home/` | `tabs::TabGroupHomeUIConfig` | (!(IS_ANDROID)) | 기능 결정 |
| `chrome://tab-search.top-chrome/` | `TabSearchUIConfig` | (!(IS_ANDROID)) | 기능 결정: 삽입 UI |
| `chrome://tab-strip-internals/` | `TabStripInternalsUIConfig` | (!(IS_ANDROID)) && (ENABLE_SESSION_SERVICE) | 기본 유지 |
| `chrome://tabs-from-other-devices.top-chrome/` | `TabsFromOtherDevicesUIConfig` | (!(IS_ANDROID)) | 기능 결정: 삽입 UI |
| `chrome://terms/` | `TermsUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://translate-internals/` | `TranslateInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://unexportable-keys-internals/` | `UnexportableKeysInternalsUIConfig` | (IS_MAC) | 기본 유지 |
| `chrome://updater/` | `UpdaterUIConfig` | (IS_MAC \|\| IS_WIN \|\| IS_LINUX) | 기본 유지 |
| `chrome://usb-internals/` | `UsbInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://user-actions/` | `user_actions_ui::UserActionsUIConfig` | 공통 | 기본 유지 |
| `chrome://user-education-internals/` | `UserEducationInternalsUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://version/` | `VersionUIConfig` | 공통 | 기본 유지 |
| `chrome://view-cert/` | `CertificateViewerUIConfig` | (ENABLE_WEBUI_CERTIFICATE_VIEWER) | 기본 유지 |
| `chrome://watermark/` | `WatermarkUIConfig` | (ENTERPRISE_WATERMARK) | 기본 유지 |
| `chrome://web-app-internals/` | `WebAppInternalsUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://webapks/` | `WebApksUIConfig` | (IS_ANDROID) | 기본 유지 |
| `chrome://webnn-internals/` | `WebNNInternalsUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://webrtc-logs/` | `WebRtcLogsUIConfig` | 공통 | 기본 유지 |
| `chrome://webui-browser/` | `WebUIBrowserUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://webui-gallery/` | `WebuiGalleryUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://webui-toolbar.top-chrome/` | `WebUIToolbarConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://webuijserror/` | `WebUIJsErrorUIConfig` | (!(IS_ANDROID)) | 기본 유지 |
| `chrome://whats-new/` | `WhatsNewUIConfig` | (IS_WIN \|\| IS_MAC \|\| IS_LINUX) | 범위 검토 |
| `chrome://yee-bookmarks/` | `yee::BookmarksUIConfig` | (!(IS_ANDROID)) | 완료 |
| `chrome://yee-downloads/` | `yee::DownloadsUIConfig` | (!(IS_ANDROID)) | 완료 |
| `chrome://yee-extensions/` | `yee::ExtensionsUIConfig` | (ENABLE_EXTENSIONS_CORE) && (!(IS_ANDROID)) | 완료 |
| `chrome://yee-history/` | `yee::HistoryUIConfig` | (!(IS_ANDROID)) | 완료 |
| `chrome://yee-settings/` | `yee::SettingsUIConfig` | (!(IS_ANDROID)) | 완료 |

### Content WebUI: 14개 등록

| 주소 | 등록 type | 빌드 조건 | 해당 주소의 처리 제안 |
| --- | --- | --- | --- |
| `chrome://gpu/` | `GpuInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://histograms/` | `HistogramsInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://indexeddb-internals/` | `indexed_db::IndexedDBInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://media-internals/` | `MediaInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://network-errors/` | `NetworkErrorsListingUIConfig` | 공통 | 기본 유지 |
| `chrome://process-internals/` | `ProcessInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://quota-internals/` | `QuotaInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://serviceworker-internals/` | `ServiceWorkerInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://traces-internals/` | `TracesInternalsLegacyUIConfig` | (!IS_ANDROID && !IS_IOS && !IS_FUCHSIA) | 기본 유지 |
| `chrome://traces/` | `TracesInternalsUIConfig` | (!IS_ANDROID && !IS_IOS && !IS_FUCHSIA) | 기본 유지 |
| `chrome://tracing/` | `TracingUIConfig` | (!IS_ANDROID && !IS_IOS && !IS_FUCHSIA) | 기본 유지 |
| `chrome://ukm/` | `UkmInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://webrtc-internals/` | `WebRTCInternalsUIConfig` | 공통 | 기본 유지 |
| `chrome://webxr-internals/` | `WebXrInternalsUIConfig` | (ENABLE_VR) | 기본 유지 |

### Untrusted WebUI: 11개 등록

| 주소 | 등록 type | 빌드 조건 | 해당 주소의 처리 제안 |
| --- | --- | --- | --- |
| `chrome-untrusted://ai-overlay-dialog/` | `ttc::AiOverlayDialogUntrustedUIConfig` | (defined(TOOLKIT_VIEWS)) | 기능 결정 |
| `chrome-untrusted://compose/` | `ComposeUIUntrustedConfig` | (defined(TOOLKIT_VIEWS)) && (ENABLE_COMPOSE) | 기능 결정 |
| `chrome-untrusted://data-sharing/` | `DataSharingUIConfig` | (defined(TOOLKIT_VIEWS)) | 기능 결정 |
| `chrome-untrusted://drive-picker-host/` | `DrivePickerUntrustedHostUIConfig` | (!(IS_ANDROID)) | 기능 결정 |
| `chrome-untrusted://glic/` | `glic::SelectionOverlayUntrustedUIConfig` | (defined(TOOLKIT_VIEWS)) | 기능 결정 |
| `chrome-untrusted://lens-overlay/` | `lens::LensOverlayUntrustedUIAndroidConfig` | (IS_ANDROID) | 기능 결정 |
| `chrome-untrusted://lens-overlay/` | `lens::LensOverlayUntrustedUIConfig` | (defined(TOOLKIT_VIEWS)) | 기능 결정 |
| `chrome-untrusted://lens/` | `lens::LensSidePanelUntrustedUIConfig` | (defined(TOOLKIT_VIEWS)) | 기능 결정 |
| `chrome-untrusted://ntp-microsoft-auth/` | `NtpMicrosoftAuthUntrustedUIConfig` | (!(IS_ANDROID)) | 기능 결정 |
| `chrome-untrusted://print/` | `printing::PrintPreviewUIUntrustedConfig` | (ENABLE_PRINT_PREVIEW) | 기본 유지: 삽입 프레임 |
| `chrome-untrusted://read-anything-side-panel.top-chrome/` | `ReadAnythingUIUntrustedConfig` | (defined(TOOLKIT_VIEWS)) | 기능 결정: 삽입 UI |

## 설정 세부 경로

모두 `chrome://settings` 아래의 원래 경로다. 상대 하위 경로는 부모 경로에 결합해
표시했다. `BASIC`·`ADVANCED`는 라우터 구성용 정의를 포함한다. 프로필·feature flag·
OS 조건에 따라 일부 정의는 실행 중 만들어지지 않는다.

| 원래 주소 | 라우터 이름 | 생성 조건 | 해당 주소의 처리 제안 |
| --- | --- | --- | --- |
| `chrome://settings/` | `BASIC` | 공통 | 라우터 구성 |
| `chrome://settings/accessibility` | `ACCESSIBILITY` | visibility.a11y | 후속 추천 |
| `chrome://settings/account` | `ACCOUNT` | visibility.people; replaceSyncPromosWithSignInPromos | 기능 결정 |
| `chrome://settings/advanced` | `ADVANCED` | 공통 | 라우터 구성 |
| `chrome://settings/ai` | `AI` | visibility.ai && showAiPage | 기능 결정 |
| `chrome://settings/ai/aiModeSearch` | `AI_MODE_SEARCH` | visibility.ai && showAiPage; enableAiModeSearchSetting | 기능 결정 |
| `chrome://settings/ai/dictation` | `DICTATION` | visibility.ai && showAiPage; showDictationControl | 기능 결정 |
| `chrome://settings/ai/gemini` | `GEMINI` | visibility.ai && showAiPage; showGlicSettings | 기능 결정 |
| `chrome://settings/ai/gemini/login` | `GEMINI_LOGIN` | visibility.ai && showAiPage; showGlicSettings; actorLoginFederatedLoginSupportEnabled | 기능 결정 |
| `chrome://settings/ai/helpMeWrite` | `OFFER_WRITING_HELP` | visibility.ai && showAiPage; showComposeControl | 기능 결정 |
| `chrome://settings/ai/historySearch` | `HISTORY_SEARCH` | visibility.ai && showAiPage; showHistorySearchControl | 기능 결정 |
| `chrome://settings/ai/inlineCueMenu` | `INLINE_CUE_MENU` | visibility.ai && showAiPage; showInlineCueMenuControl | 기능 결정 |
| `chrome://settings/ai/skills` | `SKILLS` | visibility.ai && showAiPage; showSkillsSettingPage | 기능 결정 |
| `chrome://settings/ai/suggestions` | `AI_SUGGESTIONS` | visibility.ai && showAiPage; showAiSuggestionsControl | 기능 결정 |
| `chrome://settings/appearance` | `APPEARANCE` | visibility.appearance | 우선 추천 |
| `chrome://settings/autofill` | `AUTOFILL` | visibility.yourSavedInfo | 범위 검토 |
| `chrome://settings/captions` | `CAPTIONS` | visibility.a11y; is_linux | 후속 추천 |
| `chrome://settings/clearBrowserData` | `CLEAR_BROWSER_DATA` | visibility.privacy | 범위 검토 |
| `chrome://settings/contactInfo` | `CONTACT_INFO` | visibility.yourSavedInfo | 범위 검토 |
| `chrome://settings/content` | `SITE_SETTINGS` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/ads` | `SITE_SETTINGS_ADS` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/all` | `SITE_SETTINGS_ALL` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/ar` | `SITE_SETTINGS_AR` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/autoPictureInPicture` | `SITE_SETTINGS_AUTO_PICTURE_IN_PICTURE` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/autoVerify` | `SITE_SETTINGS_AUTO_VERIFY` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/automaticDownloads` | `SITE_SETTINGS_AUTOMATIC_DOWNLOADS` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/automaticFullScreen` | `SITE_SETTINGS_AUTOMATIC_FULLSCREEN` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/backgroundSync` | `SITE_SETTINGS_BACKGROUND_SYNC` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/bluetoothDevices` | `SITE_SETTINGS_BLUETOOTH_DEVICES` | visibility.privacy; enableWebBluetoothNewPermissionsBackend | 범위 검토 |
| `chrome://settings/content/bluetoothScanning` | `SITE_SETTINGS_BLUETOOTH_SCANNING` | visibility.privacy; enableExperimentalWebPlatformFeatures | 범위 검토 |
| `chrome://settings/content/camera` | `SITE_SETTINGS_CAMERA` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/capturedSurfaceControl` | `SITE_SETTINGS_CAPTURED_SURFACE_CONTROL` | visibility.privacy; enableCapturedSurfaceControl | 범위 검토 |
| `chrome://settings/content/clipboard` | `SITE_SETTINGS_CLIPBOARD` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/federatedIdentityApi` | `SITE_SETTINGS_FEDERATED_IDENTITY_API` | visibility.privacy; enableFederatedIdentityApiContentSetting | 범위 검토 |
| `chrome://settings/content/filesystem` | `SITE_SETTINGS_FILE_SYSTEM_WRITE` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/filesystem/siteDetails` | `SITE_SETTINGS_FILE_SYSTEM_WRITE_DETAILS` | visibility.privacy; enablePersistentPermissions | 범위 검토 |
| `chrome://settings/content/handTracking` | `SITE_SETTINGS_HAND_TRACKING` | visibility.privacy; enableHandTrackingContentSetting | 범위 검토 |
| `chrome://settings/content/hidDevices` | `SITE_SETTINGS_HID_DEVICES` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/idleDetection` | `SITE_SETTINGS_IDLE_DETECTION` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/images` | `SITE_SETTINGS_IMAGES` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/insecureContent` | `SITE_SETTINGS_MIXEDSCRIPT` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/javascript` | `SITE_SETTINGS_JAVASCRIPT` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/keyboardLock` | `SITE_SETTINGS_KEYBOARD_LOCK` | visibility.privacy; enableKeyboardLockPrompt | 범위 검토 |
| `chrome://settings/content/localFonts` | `SITE_SETTINGS_LOCAL_FONTS` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/localNetwork` | `SITE_SETTINGS_LOCAL_NETWORK` | visibility.privacy; enableLocalNetworkAccessSetting | 범위 검토 |
| `chrome://settings/content/location` | `SITE_SETTINGS_LOCATION` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/loopbackNetwork` | `SITE_SETTINGS_LOOPBACK_NETWORK` | visibility.privacy; enableLocalNetworkAccessSetting | 범위 검토 |
| `chrome://settings/content/microphone` | `SITE_SETTINGS_MICROPHONE` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/midiDevices` | `SITE_SETTINGS_MIDI_DEVICES` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/notifications` | `SITE_SETTINGS_NOTIFICATIONS` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/paymentHandler` | `SITE_SETTINGS_PAYMENT_HANDLER` | visibility.privacy; enablePaymentHandlerContentSetting | 범위 검토 |
| `chrome://settings/content/pdfDocuments` | `SITE_SETTINGS_PDF_DOCUMENTS` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/popups` | `SITE_SETTINGS_POPUPS` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/protectedContent` | `SITE_SETTINGS_PROTECTED_CONTENT` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/sensors` | `SITE_SETTINGS_SENSORS` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/serialPorts` | `SITE_SETTINGS_SERIAL_PORTS` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/siteData` | `SITE_SETTINGS_SITE_DATA` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/siteDetails` | `SITE_SETTINGS_SITE_DETAILS` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/smartCardReaders` | `SITE_SETTINGS_SMART_CARD_READERS` | visibility.privacy; is_chromeos; enableSmartCardReadersContentSetting | 범위 검토 |
| `chrome://settings/content/sound` | `SITE_SETTINGS_SOUND` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/storageAccess` | `SITE_SETTINGS_STORAGE_ACCESS` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/usbDevices` | `SITE_SETTINGS_USB_DEVICES` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/v8` | `SITE_SETTINGS_JAVASCRIPT_OPTIMIZER` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/vr` | `SITE_SETTINGS_VR` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/webApplications` | `SITE_SETTINGS_WEB_APP_INSTALLATION` | visibility.privacy; enableWebAppInstallation | 범위 검토 |
| `chrome://settings/content/webPrinting` | `SITE_SETTINGS_WEB_PRINTING` | visibility.privacy; is_chromeos; enableWebPrintingContentSetting | 범위 검토 |
| `chrome://settings/content/windowManagement` | `SITE_SETTINGS_WINDOW_MANAGEMENT` | visibility.privacy | 범위 검토 |
| `chrome://settings/content/zoomLevels` | `SITE_SETTINGS_ZOOM_LEVELS` | visibility.privacy | 범위 검토 |
| `chrome://settings/cookies` | `COOKIES` | visibility.privacy | 범위 검토 |
| `chrome://settings/defaultBrowser` | `DEFAULT_BROWSER` | not is_chromeos; visibility.defaultBrowser | 후속 추천 |
| `chrome://settings/downloads` | `DOWNLOADS` | visibility.downloads | 후속 추천 |
| `chrome://settings/editDictionary` | `EDIT_DICTIONARY` | visibility.languages; not is_chromeos and not is_macosx | 후속 추천 |
| `chrome://settings/enhancedAutofill` | `SUGGESTIONS_FROM_GEMINI` | visibility.yourSavedInfo; showSuggestionsFromGeminiSettings | 기능 결정 |
| `chrome://settings/fonts` | `FONTS` | visibility.appearance | 우선 추천 |
| `chrome://settings/googleServices` | `GOOGLE_SERVICES` | visibility.people; replaceSyncPromosWithSignInPromos | 기능 결정 |
| `chrome://settings/handlers` | `SITE_SETTINGS_HANDLERS` | visibility.privacy | 범위 검토 |
| `chrome://settings/help` | `ABOUT` | 공통 | 후속 추천 |
| `chrome://settings/identityDocs` | `IDENTITY_DOCS` | visibility.yourSavedInfo | 범위 검토 |
| `chrome://settings/importData` | `IMPORT_DATA` | visibility.people; not is_chromeos | 범위 검토 |
| `chrome://settings/languages` | `LANGUAGES` | visibility.languages | 후속 추천 |
| `chrome://settings/manageProfile` | `MANAGE_PROFILE` | visibility.people; not is_chromeos | 범위 검토 |
| `chrome://settings/onStartup` | `ON_STARTUP` | visibility.onStartup | 후속 추천 |
| `chrome://settings/passkeys` | `PASSKEYS` | visibility.yourSavedInfo; is_win or is_macosx | 범위 검토 |
| `chrome://settings/payments` | `PAYMENTS` | visibility.yourSavedInfo | 범위 검토 |
| `chrome://settings/people` | `PEOPLE` | visibility.people | 기능 결정 |
| `chrome://settings/performance` | `PERFORMANCE` | visibility.performance | 후속 추천 |
| `chrome://settings/privacy` | `PRIVACY` | visibility.privacy | 범위 검토 |
| `chrome://settings/privacy/guide` | `PRIVACY_GUIDE` | visibility.privacy; showPrivacyGuide | 범위 검토 |
| `chrome://settings/reset` | `RESET` | visibility.reset | 범위 검토 |
| `chrome://settings/resetProfileSettings` | `RESET_DIALOG` | visibility.reset | 범위 검토 |
| `chrome://settings/safetyCheck` | `SAFETY_HUB` | visibility.privacy; visibility.safetyHub | 범위 검토 |
| `chrome://settings/search` | `SEARCH` | 공통 | 후속 추천 |
| `chrome://settings/searchEngines` | `SEARCH_ENGINES` | !searchSettingsUpdate | 후속 추천 |
| `chrome://settings/security` | `SECURITY` | visibility.privacy | 범위 검토 |
| `chrome://settings/securityKeys` | `SECURITY_KEYS` | visibility.privacy; enableSecurityKeysSubpage | 범위 검토 |
| `chrome://settings/shopping` | `SHOPPING` | visibility.yourSavedInfo; shoppingIntegrationEnabled | 기능 결정 |
| `chrome://settings/signOut` | `SIGN_OUT` | visibility.people; not is_chromeos | 기능 결정 |
| `chrome://settings/spellCheck` | `SPELL_CHECK` | visibility.languages | 후속 추천 |
| `chrome://settings/syncSetup` | `SYNC` | visibility.people | 기능 결정 |
| `chrome://settings/syncSetup/advanced` | `SYNC_ADVANCED` | visibility.people | 기능 결정 |
| `chrome://settings/system` | `SYSTEM` | not is_chromeos; visibility.system | 후속 추천 |
| `chrome://settings/travel` | `TRAVEL` | visibility.yourSavedInfo | 범위 검토 |
| `chrome://settings/triggeredResetProfileSettings` | `TRIGGERED_RESET_DIALOG` | visibility.reset | 범위 검토 |

## 사용자 페이지의 상세 화면

| 화면 | 원래 주소 또는 URL 형식 | 분류 |
| --- | --- | --- |
| 확장 목록 | `chrome://extensions/` | Yee 일반 관리 완료, 원래 주소 유지 |
| 확장 상세 | `chrome://extensions/?id={extensionId}` | Yee 일반 상세·개별 사이트 접근 완료, 원래 주소 유지 |
| 확장 옵션창 | `chrome://extensions/?options={extensionId}` | Yee에서 native 옵션 연결 완료, 콘텐츠는 확장 소유 |
| 확장 단축키 | `chrome://extensions/shortcuts` | 후속 추천, 현재 원래 화면 연결 |
| 단축키 이전 경로 | `chrome://extensions/configureCommands` | `/shortcuts`로 정규화 |
| 확장의 사이트 권한 | `chrome://extensions/sitePermissions`, `/sitePermissions/allSites` | 후속 추천, 프로필 전체 사이트 권한은 원래 화면 유지 |
| 확장 활동 로그·오류 | `chrome://extensions/?activity={extensionId}`, `?errors={extensionId}` | 기본 유지 |
| 방문 기록·검색 | `chrome://history/`, `?q={query}` | Yee 일반 목록 완료, 원래 주소 유지 |
| 묶음 방문 기록 | `chrome://history/grouped` | 기능 결정 |
| 다른 기기의 탭 | `chrome://history/syncedTabs` | 기능 결정 |
| 제품 비교 목록 | `chrome://history/comparisonTables` | 기능 결정 |
| 북마크 폴더·검색 | `chrome://bookmarks/?id={folderId}`, `?q={query}` | Yee 일반 관리 완료, 원래 주소 유지 |
| 다운로드 목록 | `chrome://downloads/` | Yee 일반 목록·검색 완료, 원래 주소 유지. Yee 검색의 `?q=`는 [구현 문서](internal-pages.md#다운로드)를 따름 |
| 비밀번호 목록 | `chrome://password-manager/passwords` | 범위 검토 |
| 비밀번호 상세 | `chrome://password-manager/passwords/{origin}` | 범위 검토 |
| 비밀번호 검사 | `chrome://password-manager/checkup` | 범위 검토 |
| 검사 상세 | `chrome://password-manager/checkup/compromised`, `/reused`, `/weak` | 범위 검토 |
| 비밀번호 설정 | `chrome://password-manager/settings` | 범위 검토 |
| 비밀번호 변경 | `chrome://password-manager/settings/password-change` | 기능 결정, 기존 서비스·확인 절차 사용 |

## 등록 목록 밖의 카탈로그 주소

`ChromeURLHosts()`의 112개 항목 중 위 등록 주소와 겹치지 않는 항목이다.
별도 제공 방식·별칭·다른 OS 기능을 포함하므로 사용 가능한 화면으로 일괄 간주하지 않는다.

| 주소 | 카탈로그 조건 | 처리 제안 |
| --- | --- | --- |
| `chrome://about/` | 공통 | 기본 유지·별도 제공 방식 확인 |
| `chrome://blob-internals/` | 공통 | 기본 유지·별도 제공 방식 확인 |
| `chrome://borealis-credits/` | (IS_CHROMEOS) | 기본 유지·별도 제공 방식 확인 |
| `chrome://crostini-credits/` | (IS_CHROMEOS) | 기본 유지·별도 제공 방식 확인 |
| `chrome://cryptohome/` | (IS_CHROMEOS) | 기본 유지·별도 제공 방식 확인 |
| `chrome://dino/` | 공통 | 기본 유지·별도 제공 방식 확인 |
| `chrome://drive-internals/` | (IS_CHROMEOS) | 기본 유지·별도 제공 방식 확인 |
| `chrome://extensions-internals/` | (ENABLE_EXTENSIONS_CORE) | 기본 유지·별도 제공 방식 확인 |
| `chrome://help/` | (!IS_ANDROID) | 기본 유지·별도 제공 방식 확인 |
| `chrome://internet-config-dialog/` | (IS_CHROMEOS) | 기본 유지·별도 제공 방식 확인 |
| `chrome://internet-detail-dialog/` | (IS_CHROMEOS) | 기본 유지·별도 제공 방식 확인 |
| `chrome://lock-network/` | (IS_CHROMEOS) | 기본 유지·별도 제공 방식 확인 |
| `chrome://network/` | (IS_CHROMEOS) | 기본 유지·별도 제공 방식 확인 |
| `chrome://oobe/` | (IS_CHROMEOS) | 기본 유지·별도 제공 방식 확인 |
| `chrome://os-credits/` | (IS_CHROMEOS) | 기본 유지·별도 제공 방식 확인 |
| `chrome://os-settings/` | (IS_CHROMEOS) | 기본 유지·별도 제공 방식 확인 |
| `chrome://power/` | (IS_CHROMEOS) | 기본 유지·별도 제공 방식 확인 |
| `chrome://prefs-internals/` | 공통 | 기본 유지·별도 제공 방식 확인 |
| `chrome://sys-internals/` | (IS_CHROMEOS) | 기본 유지·별도 제공 방식 확인 |

## 테스트·실행 명령 주소

`ChromeDebugURLs()`의 37개 항목이다. 크래시·정지·프로세스 종료·재시작 같은 동작을
요청하는 주소를 포함하므로 리디자인할 사용자 페이지로 세지 않는다.

| 주소 | 카탈로그 조건 |
| --- | --- |
| `chrome://badcastcrash/` | 공통 |
| `chrome://crash/` | 공통 |
| `chrome://crash/browser/heap-overflow` | 공통 |
| `chrome://crash/browser/heap-underflow` | 공통 |
| `chrome://crash/browser/member-dereference-after-free` | 공통 |
| `chrome://crash/browser/use-after-free` | 공통 |
| `chrome://crash/cfg` | (IS_WIN) |
| `chrome://crash/gpu/heap-overflow` | 공통 |
| `chrome://crash/gpu/heap-underflow` | 공통 |
| `chrome://crash/gpu/member-dereference-after-free` | 공통 |
| `chrome://crash/gpu/use-after-free` | 공통 |
| `chrome://crash/renderer/heap-overflow` | 공통 |
| `chrome://crash/renderer/heap-underflow` | 공통 |
| `chrome://crash/renderer/member-dereference-after-free` | 공통 |
| `chrome://crash/renderer/use-after-free` | 공통 |
| `chrome://crash/rust` | 공통 |
| `chrome://crash/rust-overflow` | (defined(ADDRESS_SANITIZER)) |
| `chrome://crashdump/` | 공통 |
| `chrome://gpu-java-crash/` | (IS_ANDROID) |
| `chrome://gpuclean/` | 공통 |
| `chrome://gpucrash/` | 공통 |
| `chrome://gpuhang/` | 공통 |
| `chrome://hang/` | 공통 |
| `chrome://heapcorruptioncrash/` | (IS_WIN) |
| `chrome://inducebrowsercrashforrealz/` | 공통 |
| `chrome://inducebrowserdcheckforrealz/` | 공통 |
| `chrome://inducebrowserheapcorruption/` | (IS_WIN) |
| `chrome://java-crash/` | (IS_ANDROID) |
| `chrome://kill/` | 공통 |
| `chrome://memory-exhaust/` | 공통 |
| `chrome://memory-pressure-critical/` | 공통 |
| `chrome://memory-pressure-moderate/` | 공통 |
| `chrome://quit/` | 공통 |
| `chrome://restart/` | 공통 |
| `chrome://shorthang/` | 공통 |
| `chrome://uithreadhang/` | 공통 |
| `chrome://webuijserror/` | (!(IS_ANDROID)) |

## 주소 등록 외의 화면과 리소스

| 종류 | 범위 | 처리 제안 |
| --- | --- | --- |
| 오류·보안 중간 화면 | 탐색 실패, 인증서·위험 사이트 경고, 다운로드 보안 확인 | native 서비스·판정·확인 절차 유지 |
| DevTools | `devtools://`로 제공하는 개발 도구 | 기존 개발 도구 UI 유지 |
| 내부 리소스 | `chrome://resources`, `chrome://theme`, `chrome://favicon`·`favicon2` 등 | 화면이 사용하는 데이터 source이며 독립 사용자 페이지로 세지 않음 |
| native 대화상자 | 파일·폴더 선택, OS 재인증, 권한 요청 등 | 페이지 이관에서 해당 기능의 native 연결로 관리 |

## 갱신

Chromium 버전을 올리거나 WebUI 등록·설정 router·Yee 자체 페이지를 추가할 때
이 목록과 후보 문서를 함께 갱신한다. 등록 수·URL 상수·플랫폼 조건·상세 경로를
대조하고 새 화면은 목적에 따라 분류한다. `완료` 표시는 실제 구현·검수 범위에만 쓴다.
조회 시점의 이 소스 목록으로 향후 Chromium 버전이나 모든 플랫폼의 전체 UI를
완료했다고 판단하지 않는다.
