// Read only URLs of AXWebAreas exposed under the main/focused native window.
// Never descend into web content or read tab titles, text, or page DOM.
import AppKit
import ApplicationServices
import Foundation

func ax(_ e: AXUIElement, _ name: String) -> CFTypeRef? {
    var v: CFTypeRef?
    return AXUIElementCopyAttributeValue(e, name as CFString, &v) == .success ? v : nil
}
guard CommandLine.arguments.count == 2,
      let expected = URL(string:CommandLine.arguments[1]), expected.host == "127.0.0.1",
      expected.port == 8787, expected.scheme == "http" else { exit(2) }
let apps = NSRunningApplication.runningApplications(withBundleIdentifier:"at.studio.AsideBrowser")
guard apps.count == 1 else { fputs("one Aside app required\n",stderr);exit(1) }
let app = apps[0]
let element = AXUIElementCreateApplication(app.processIdentifier)
var observations: [[String:Any]] = []
for attribute in [kAXMainWindowAttribute,kAXFocusedWindowAttribute] {
    guard let raw = ax(element,attribute), CFGetTypeID(raw) == AXUIElementGetTypeID() else { continue }
    let window = raw as! AXUIElement
    var pending = [window]
    var visited = 0
    var webAreas = 0
    var matching = 0
    while let node = pending.popLast(), visited < 1000 {
        visited += 1
        let role = ax(node,kAXRoleAttribute) as? String ?? ""
        if role == "AXWebArea" {
            webAreas += 1
            let value = ax(node,kAXURLAttribute)
            let url = (value as? URL)?.absoluteString ?? (value as? String)
            if url == expected.absoluteString { matching += 1 }
            continue
        }
        pending += ax(node,kAXChildrenAttribute) as? [AXUIElement] ?? []
    }
    observations.append(["attribute":attribute,"web_areas":webAreas,"matching_web_areas":matching,
                         "truncated":!pending.isEmpty,"expected_url_matches":webAreas == 1 && matching == 1 && pending.isEmpty])
}
let ok = !observations.isEmpty && observations.allSatisfy { $0["expected_url_matches"] as? Bool == true }
let result:[String:Any] = ["schema":"yee.aside-native-active-url.v1","time_ns":Int64(Date().timeIntervalSince1970*1e9),
 "bundle_id":"at.studio.AsideBrowser","pid":app.processIdentifier,"app_frontmost":app.isActive,
 "expected_url":expected.absoluteString,"observations":observations,"matches":ok,
 "method":"macOS AXMainWindow/AXFocusedWindow single AXWebArea URL; no tab discovery or page content read"]
print(String(decoding:try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys]),as:UTF8.self))
exit(ok ? 0 : 1)
