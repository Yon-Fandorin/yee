// Read-only geometry diagnostic for the explicitly supplied Yee executable.
// No window titles, page contents, screenshots or other apps are printed.
import AppKit
import CoreGraphics
import ApplicationServices
import Foundation

guard CommandLine.arguments.count == 2 else { exit(2) }
let executable = URL(fileURLWithPath: CommandLine.arguments[1]).standardizedFileURL
let pids = Set(NSWorkspace.shared.runningApplications.compactMap { app -> pid_t? in
    app.executableURL?.standardizedFileURL == executable ? app.processIdentifier : nil
})
guard !pids.isEmpty,
      let windows = CGWindowListCopyWindowInfo(.optionOnScreenOnly, kCGNullWindowID) as? [[String: Any]] else {
    fputs("YEE_WINDOW_BOUNDS_UNAVAILABLE\n", stderr)
    exit(1)
}
let bounds = windows.compactMap { window -> [String: Any]? in
    guard let pid = window[kCGWindowOwnerPID as String] as? Int32,
          pids.contains(pid),
          (window[kCGWindowLayer as String] as? Int) == 0,
          let rect = window[kCGWindowBounds as String] as? [String: Any] else { return nil }
    return ["width": rect["Width"] ?? 0, "height": rect["Height"] ?? 0,
            "x": rect["X"] ?? 0, "y": rect["Y"] ?? 0]
}
guard !bounds.isEmpty else { exit(1) }
func ax(_ element: AXUIElement, _ name: String) -> CFTypeRef? {
    var value: CFTypeRef?
    return AXUIElementCopyAttributeValue(element, name as CFString, &value) == .success ? value : nil
}
var sliders: [[String: CGFloat]] = []
for pid in pids {
    var pending = [AXUIElementCreateApplication(pid)]
    var visited = 0
    while let element = pending.popLast(), visited < 500 {
        visited += 1
        let role = ax(element, kAXRoleAttribute) as? String ?? ""
        // Only inspect browser chrome, never descend into page content.
        if role == "AXWebArea" { continue }
        if role == "AXSlider",
           let position = ax(element, kAXPositionAttribute),
           let size = ax(element, kAXSizeAttribute),
           CFGetTypeID(position) == AXValueGetTypeID(), CFGetTypeID(size) == AXValueGetTypeID() {
            var point = CGPoint.zero
            var dimensions = CGSize.zero
            if AXValueGetValue(position as! AXValue, .cgPoint, &point),
               AXValueGetValue(size as! AXValue, .cgSize, &dimensions) {
                sliders.append(["x": point.x, "y": point.y,
                                "width": dimensions.width, "height": dimensions.height])
            }
        }
        pending += ax(element, kAXChildrenAttribute) as? [AXUIElement] ?? []
    }
}
let data = try JSONSerialization.data(withJSONObject: ["windows": bounds, "chrome_sliders": sliders], options: [.sortedKeys])
print(String(decoding: data, as: UTF8.self))
