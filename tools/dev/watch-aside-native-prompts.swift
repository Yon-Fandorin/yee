// Observe native permission-button surfaces without reading web areas or titles.
// Counts are observations, not a complete audit of embedded/hidden agent UI.
import AppKit
import ApplicationServices
import Foundation

func attribute(_ element: AXUIElement, _ name: String) -> CFTypeRef? {
    var value: CFTypeRef?
    return AXUIElementCopyAttributeValue(element, name as CFString, &value) == .success ? value : nil
}
func candidates(_ pid: pid_t) -> (Set<UInt>, Int, Bool) {
    let app = AXUIElementCreateApplication(pid)
    guard let windows = attribute(app, kAXWindowsAttribute) as? [AXUIElement] else { return ([], 0, false) }
    var found = Set<UInt>(); var skipped = 0; var complete = true
    for window in windows {
        var queue = [window]; var visited = 0; var allow = false; var deny = false
        while let node = queue.popLast(), visited < 1500 {
            visited += 1
            let role = attribute(node, kAXRoleAttribute) as? String ?? ""
            if role == "AXWebArea" { skipped += 1; continue }
            if role == "AXButton" {
                let label = (attribute(node, kAXTitleAttribute) as? String ?? "").lowercased().trimmingCharacters(in: .whitespacesAndNewlines)
                allow = allow || ["allow", "allow once", "allow for task", "approve", "허용", "승인", "한 번 허용"].contains(label)
                deny = deny || ["deny", "reject", "거부", "거절"].contains(label)
            }
            queue += attribute(node, kAXChildrenAttribute) as? [AXUIElement] ?? []
        }
        if !queue.isEmpty { complete = false }
        if allow && deny { found.insert(CFHash(window)) }
    }
    return (found, skipped, complete)
}
func emit(_ value: [String: Any]) {
    print(String(decoding: try! JSONSerialization.data(withJSONObject: value, options: [.sortedKeys]), as: UTF8.self))
    fflush(stdout)
}
let args = CommandLine.arguments
if args.count == 2 && args[1] == "--self-test" {
    let app = NSApplication.shared
    app.setActivationPolicy(.accessory)
    app.finishLaunching()
    let previous = NSWorkspace.shared.frontmostApplication
    let alert = NSAlert(); alert.messageText = "Yee measurement observer calibration"
    alert.informativeText = "Synthetic buttons only. This window closes automatically."
    alert.addButton(withTitle: "Allow"); alert.addButton(withTitle: "Deny")
    var visible: (Set<UInt>, Int, Bool) = ([],0,false)
    DispatchQueue.main.asyncAfter(deadline:.now()+0.4) {
        visible = candidates(getpid())
        app.stopModal()
    }
    app.activate(ignoringOtherApps:true)
    alert.runModal()
    alert.window.orderOut(nil)
    RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.2))
    let hidden = candidates(getpid())
    previous?.activate(options:[])
    let passed = visible.0.count == 1 && hidden.0.isEmpty && visible.2 && hidden.2
    emit(["self_test_pass": passed, "visible_candidates": visible.0.count, "hidden_candidates": hidden.0.count])
    exit(passed ? 0 : 1)
}
guard args.count == 3, let seconds = Double(args[2]), seconds > 0, seconds <= 900,
      args[1].hasPrefix("/private/tmp/yee-") else { exit(2) }
let apps = NSRunningApplication.runningApplications(withBundleIdentifier: "at.studio.AsideBrowser")
guard apps.count == 1, AXIsProcessTrusted() else { emit(["ready":false,"reason":"Aside or AX unavailable"]); exit(1) }
let pid = apps[0].processIdentifier
let initial = candidates(pid)
guard initial.2 else { emit(["ready":false,"reason":"native window inventory unavailable"]); exit(1) }
let begin = ProcessInfo.processInfo.systemUptime
var previous = initial.0; var newCount = 0; var samples = 0; var partial = 0; var maxGap = 0.0
var lastSample = begin
emit(["ready":true,"pid":pid,"initial_candidates":initial.0.count,"time_ns":Int64(Date().timeIntervalSince1970*1e9)])
while ProcessInfo.processInfo.systemUptime - begin < seconds && !FileManager.default.fileExists(atPath:args[1]) {
    let now = ProcessInfo.processInfo.systemUptime
    maxGap = max(maxGap,now-lastSample);lastSample=now
    let state = candidates(pid); samples += 1
    if !state.2 { partial += 1 }
    let added = state.0.subtracting(previous).count
    let removed = previous.subtracting(state.0).count
    if added > 0 || removed > 0 {
        newCount += added
        emit(["event":"native_permission_candidate_change","added":added,"removed":removed,
              "time_ns":Int64(Date().timeIntervalSince1970*1e9)])
    }
    previous = state.0
    Thread.sleep(forTimeInterval:0.02)
}
emit(["finished":true,"new_native_permission_candidates":newCount,"samples":samples,
      "partial_samples":partial,"max_sample_gap_seconds":maxGap,
      "elapsed_seconds":ProcessInfo.processInfo.systemUptime-begin,
      "stopped_by_marker":FileManager.default.fileExists(atPath:args[1]),
      "complete_native_approval_audit":false,
      "coverage":"Native AX buttons only; all AXWebArea content skipped; short-lived or differently labelled prompts may be missed"])
