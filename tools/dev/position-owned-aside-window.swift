// Root-only reversible positioning of a verified active localhost fixture.
import AppKit
import ApplicationServices
import Foundation
func ax(_ element: AXUIElement, _ name: String) -> CFTypeRef? {
    var value: CFTypeRef?
    return AXUIElementCopyAttributeValue(element, name as CFString, &value) == .success ? value : nil
}
let args = CommandLine.arguments
let apps = NSRunningApplication.runningApplications(withBundleIdentifier: "at.studio.AsideBrowser")
guard args.count == 4, let url = URL(string: args[1]), url.scheme == "http",
      url.host == "127.0.0.1", url.port == 8787,
      let x = Double(args[2]), let y = Double(args[3]), x.isFinite, y.isFinite,
      apps.count == 1, let app = apps.first, app.isActive,
      NSWorkspace.shared.frontmostApplication?.processIdentifier == app.processIdentifier else { exit(2) }
var point = CGPoint(x: x, y: y)
guard CGDisplayBounds(CGMainDisplayID()).contains(point) else { exit(2) }
let root = AXUIElementCreateApplication(app.processIdentifier)
guard let raw = ax(root, kAXMainWindowAttribute), CFGetTypeID(raw) == AXUIElementGetTypeID(),
      let focused = ax(root, kAXFocusedWindowAttribute), CFEqual(raw, focused) else { exit(1) }
let window = raw as! AXUIElement
var pending = [window], visited = 0, urls = [String]()
while let node = pending.popLast(), visited < 1000 {
    visited += 1
    if ax(node, kAXRoleAttribute) as? String == "AXWebArea" {
        let rawURL = ax(node, kAXURLAttribute)
        if let value = (rawURL as? URL)?.absoluteString ?? (rawURL as? String) { urls.append(value) }
        continue
    }
    pending += ax(node, kAXChildrenAttribute) as? [AXUIElement] ?? []
}
guard pending.isEmpty, urls == [args[1]], let value = AXValueCreate(.cgPoint, &point) else { exit(1) }
let acknowledgement = AXUIElementSetAttributeValue(window, kAXPositionAttribute as CFString, value)
// Verify the requested result even if an acknowledgement is delayed. No repeat.
let deadline = ProcessInfo.processInfo.systemUptime + 3
var settled = false
repeat {
    if let rawPoint = ax(window, kAXPositionAttribute), CFGetTypeID(rawPoint) == AXValueGetTypeID() {
        var actual = CGPoint.zero
        if AXValueGetValue(rawPoint as! AXValue, .cgPoint, &actual),
           abs(actual.x - x) <= 1, abs(actual.y - y) <= 1 { settled = true; break }
    }
    RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.05))
} while ProcessInfo.processInfo.systemUptime < deadline
print(String(decoding: try! JSONSerialization.data(withJSONObject: ["expected_url":args[1],
    "pid":app.processIdentifier, "requested_x":x, "requested_y":y,
    "position_requested":true, "settled":settled, "native_ack_code":acknowledgement.rawValue],
    options:[.sortedKeys]), as:UTF8.self))
exit(settled ? 0 : 1)
