// Synthetic test operator only. Open once in the exact Aside process and
// verify native ownership before entering a localhost fixture URL.
import AppKit
import ApplicationServices
import Foundation

func ax(_ element: AXUIElement, _ name: String) -> CFTypeRef? {
    var value: CFTypeRef?
    return AXUIElementCopyAttributeValue(element, name as CFString, &value) == .success ? value : nil
}
func element(_ value: CFTypeRef?) -> AXUIElement? {
    guard let value, CFGetTypeID(value) == AXUIElementGetTypeID() else { return nil }
    return (value as! AXUIElement)
}
struct Chrome {
    let window: AXUIElement
    let tabs: [AXUIElement]
    let fields: [AXUIElement]
    let urls: [String]
}
enum Failure: Error { case stopped(String) }
let args = CommandLine.arguments
guard args.count == 3, args[1] == "open",
      let url = URL(string: args[2]), url.scheme == "http",
      url.host == "127.0.0.1", url.port == 8787,
      AXIsProcessTrusted() else { exit(2) }
let apps = NSRunningApplication.runningApplications(withBundleIdentifier: "at.studio.AsideBrowser")
guard apps.count == 1, !apps[0].isTerminated else { exit(2) }
let app = apps[0]
let pid = app.processIdentifier
let root = AXUIElementCreateApplication(pid)
var stage = "activation"
var baseline: Chrome?
var createdTab: AXUIElement?
var probes = 0
var newRequests = 0
let started = ProcessInfo.processInfo.systemUptime

func chrome(requireActive: Bool = true) -> Chrome? {
    guard !app.isTerminated, let window = element(ax(root, kAXMainWindowAttribute)),
          ax(window, kAXModalAttribute) as? Bool != true else { return nil }
    if requireActive {
        guard app.isActive, NSWorkspace.shared.frontmostApplication?.processIdentifier == pid,
              let focused = element(ax(root, kAXFocusedWindowAttribute)), CFEqual(window, focused) else { return nil }
    }
    var pending = [window], tabs = [AXUIElement](), fields = [AXUIElement](), urls = [String]()
    var visited = 0
    while let node = pending.popLast(), visited < 1000 {
        visited += 1
        let role = ax(node, kAXRoleAttribute) as? String ?? ""
        if ["AXTab", "AXRadioButton"].contains(role) { tabs.append(node) }
        if role == "AXTextField" { fields.append(node) }
        if role == "AXWebArea" {
            let raw = ax(node, kAXURLAttribute)
            if let value = (raw as? URL)?.absoluteString ?? (raw as? String) { urls.append(value) }
            continue // Native URL metadata only; never read page content.
        }
        pending += ax(node, kAXChildrenAttribute) as? [AXUIElement] ?? []
    }
    guard pending.isEmpty, !tabs.isEmpty, fields.count == 1 else { return nil }
    return Chrome(window: window, tabs: tabs, fields: fields, urls: urls)
}
func wait(_ condition: (Chrome) -> Bool) throws -> Chrome {
    let deadline = min(started + 10, ProcessInfo.processInfo.systemUptime + 4)
    repeat {
        probes += 1
        if let current = chrome(), condition(current) { return current }
        RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.05))
    } while ProcessInfo.processInfo.systemUptime < deadline
    throw Failure.stopped("native state did not settle: " + stage)
}
func addedTab(_ current: Chrome) -> AXUIElement? {
    guard let before = baseline, CFEqual(current.window, before.window),
          current.tabs.count == before.tabs.count + 1,
          before.tabs.allSatisfy({ old in current.tabs.contains(where: { CFEqual(old, $0) }) }) else { return nil }
    let added = current.tabs.filter { node in !before.tabs.contains(where: { CFEqual(node, $0) }) }
    return added.count == 1 ? added[0] : nil
}
func owned(_ current: Chrome) -> Bool {
    guard let tab = addedTab(current) else { return false }
    createdTab = tab
    return ax(tab, kAXValueAttribute) as? Bool == true || ax(tab, kAXSelectedAttribute) as? Bool == true
}
func focusedField(_ current: Chrome, value: String) -> Bool {
    owned(current) && ax(current.fields[0], kAXFocusedAttribute) as? Bool == true
        && ax(current.fields[0], kAXValueAttribute) as? String == value
}
func menu(_ command: String) throws {
    guard chrome() != nil, let bar = element(ax(root, kAXMenuBarAttribute)) else {
        throw Failure.stopped("owned process focus not established")
    }
    var pending = [bar], matches = [AXUIElement](), visited = 0
    while let node = pending.popLast(), visited < 1000 {
        visited += 1
        if ax(node, kAXRoleAttribute) as? String == "AXMenuItem",
           (ax(node, "AXMenuItemCmdChar") as? String)?.lowercased() == command,
           ax(node, "AXMenuItemCmdModifiers") as? Int == 0,
           ax(node, kAXEnabledAttribute) as? Bool == true { matches.append(node) }
        pending += ax(node, kAXChildrenAttribute) as? [AXUIElement] ?? []
    }
    guard pending.isEmpty, matches.count == 1 else { throw Failure.stopped("unique enabled native menu required") }
    // Each native menu action is dispatched once, directly to this PID's AX tree.
    if command == "t" { newRequests += 1 }
    guard AXUIElementPerformAction(matches[0], kAXPressAction as CFString) == .success else {
        throw Failure.stopped("native menu action failed")
    }
}
func receipt(_ verified: Bool, error: String? = nil) {
    let observed = chrome(requireActive: false)
    if let current = observed, let tab = addedTab(current) { createdTab = tab }
    var result: [String: Any] = ["schema": "yee.owned-native-tab-open.v1", "pid": pid,
        "expected_url": args[2], "native_tab_input_verified": verified, "stage": stage,
        "new_tab_requests": newRequests, "native_tab_created": createdTab != nil,
        "app_frontmost": app.isActive, "cleanup_pending": !verified && createdTab != nil,
        "settlement_probes": probes, "elapsed_seconds": ProcessInfo.processInfo.systemUptime - started,
        "input_scope": "exact-owned-process-native-menu-and-PID", "clipboard_mutations": 0]
    if let before = baseline { result["before_native_tab_count"] = before.tabs.count }
    if let current = observed { result["observed_native_tab_count"] = current.tabs.count }
    if let error { result["error"] = error }
    print(String(decoding: try! JSONSerialization.data(withJSONObject: result, options: [.sortedKeys]), as: UTF8.self))
}
do {
    // Activation is a request. Wait for actual foreground PID and focused window
    // before sampling the baseline; a fixed delay does not prove either state.
    _ = app.activate(options: [])
    baseline = try wait { _ in true }
    stage = "new-tab"
    try menu("t")
    let blank = try wait { current in
        owned(current) && ax(current.fields[0], kAXValueAttribute) as? String == "" && current.urls.count == 1
            && (current.urls[0] == "about:blank"
                || URL(string: current.urls[0])?.scheme == "chrome-extension")
    }
    guard owned(blank) else { throw Failure.stopped("added native tab not selected") }
    stage = "address-focus"
    try menu("l")
    _ = try wait { focusedField($0, value: "") }
    stage = "url-input"
    // Recheck the added tab and focused empty field immediately before input.
    guard let current = chrome(), focusedField(current, value: "") else {
        throw Failure.stopped("owned empty address field lost")
    }
    let units = Array(args[2].utf16)
    guard let down = CGEvent(keyboardEventSource: nil, virtualKey: 0, keyDown: true),
          let up = CGEvent(keyboardEventSource: nil, virtualKey: 0, keyDown: false) else {
        throw Failure.stopped("keyboard event unavailable")
    }
    units.withUnsafeBufferPointer { buffer in
        down.keyboardSetUnicodeString(stringLength: buffer.count, unicodeString: buffer.baseAddress)
        up.keyboardSetUnicodeString(stringLength: buffer.count, unicodeString: buffer.baseAddress)
    }
    down.postToPid(pid); up.postToPid(pid)
    _ = try wait { focusedField($0, value: args[2]) }
    stage = "navigation"
    guard let current = chrome(), focusedField(current, value: args[2]),
          let enter = CGEvent(keyboardEventSource: nil, virtualKey: 36, keyDown: true),
          let release = CGEvent(keyboardEventSource: nil, virtualKey: 36, keyDown: false) else {
        throw Failure.stopped("owned exact address lost before navigation")
    }
    enter.postToPid(pid); release.postToPid(pid)
    _ = try wait { owned($0) && $0.urls == [args[2]] }
    stage = "verified"
    receipt(true)
} catch {
    receipt(false, error: String(describing: error))
    exit(1)
}
