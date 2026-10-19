// 회전된 실제 모니터의 작업 영역을 Chromium 창 좌표로 변환한다.
import AppKit
import CoreGraphics

let screens = NSScreen.screens
guard let primary = screens.first else {
    fputs("UI_TEST_DISPLAY_UNAVAILABLE: macOS 디스플레이를 읽을 수 없습니다. GUI 세션에 접근 가능한 터미널에서 실행하세요.\n", stderr)
    exit(21)
}

let rotated = screens.filter { screen in
    guard let id = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber else {
        return false
    }
    return CGDisplayRotation(id.uint32Value).truncatingRemainder(dividingBy: 360) != 0
}
if let screen = rotated.first {
    let area = screen.visibleFrame
    guard area.width >= 400, area.height >= 300 else {
        fputs("UI_TEST_DISPLAY_TOO_SMALL: 회전된 모니터의 작업 영역이 너무 작습니다.\n", stderr)
        exit(22)
    }
    // AppKit은 아래에서 위로, Chromium은 주 모니터의 위에서 아래로 센다.
    let x = Int(area.minX) + 48
    let y = Int(primary.frame.maxY - area.maxY) + 48
    fputs("UI 테스트 모니터: \(screen.localizedName) (회전됨), 시작 위치 \(x),\(y)\n", stderr)
    print("--window-position=\(x),\(y)")
} else {
    fputs("회전된 모니터 없음: UI 테스트의 기본 창 배치를 사용합니다.\n", stderr)
}
