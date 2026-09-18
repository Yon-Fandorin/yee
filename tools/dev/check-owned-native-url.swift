// Match owned native URL metadata, including the displayed empty HTTP root path.
// Never descend into web content or read tab titles, text, or page DOM.
import AppKit
import ApplicationServices
import Foundation

func ax(_ e: AXUIElement, _ name: String) -> CFTypeRef? {
    var v: CFTypeRef?
    return AXUIElementCopyAttributeValue(e, name as CFString, &v) == .success ? v : nil
}
guard CommandLine.arguments.count == 3,
      let expected = URL(string:CommandLine.arguments[1]), expected.host == "127.0.0.1",
      expected.port == 8787, expected.scheme == "http" else { exit(2) }
let apps = NSRunningApplication.runningApplications(withBundleIdentifier:CommandLine.arguments[2])
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
    var nativeTabs = 0
    var omniboxValue: String?
    var omniboxFocused = false
    while let node = pending.popLast(), visited < 1000 {
        visited += 1
        let role = ax(node,kAXRoleAttribute) as? String ?? ""
        if ["AXTab","AXRadioButton"].contains(role) { nativeTabs += 1 }
        if CommandLine.arguments[2] == "org.chromium.Chromium" && role == "AXTextField" {
            let label = (ax(node,kAXDescriptionAttribute) as? String) ?? (ax(node,kAXTitleAttribute) as? String) ?? ""
            if ["주소창 및 검색창","Address and search bar"].contains(label) {
                webAreas += 1
                let value = ax(node,kAXValueAttribute) as? String
                omniboxFocused = ax(node,kAXFocusedAttribute) as? Bool ?? false
                if let value {
                    let candidate = URL(string:value.hasPrefix("http://") ? value : "http://"+value)
                    if value.isEmpty || (candidate?.host == "127.0.0.1" && candidate?.port == 8787) {
                        omniboxValue = value
                    }
                }
                let exact = value == expected.absoluteString || value == String(expected.absoluteString.dropFirst(7))
                // Chromium formats the empty root path without its slash even
                // in a focused omnibox. Do not omit any query, fragment or path.
                let bareRoot = expected.path == "/" && expected.query == nil && expected.fragment == nil
                    && (value == String(expected.absoluteString.dropLast())
                        || value == String(expected.absoluteString.dropFirst(7).dropLast()))
                if exact || bareRoot { matching += 1 }
            }
        }
        if role == "AXWebArea" {
            if CommandLine.arguments[2] == "org.chromium.Chromium" { continue }
            webAreas += 1
            let value = ax(node,kAXURLAttribute)
            let url = (value as? URL)?.absoluteString ?? (value as? String)
            if url == expected.absoluteString { matching += 1 }
            continue
        }
        pending += ax(node,kAXChildrenAttribute) as? [AXUIElement] ?? []
    }
    var observation:[String:Any] = ["attribute":attribute,"web_areas":webAreas,"matching_web_areas":matching,
                         "truncated":!pending.isEmpty,"expected_url_matches":webAreas == 1 && matching == 1 && pending.isEmpty,
                         "native_tab_count":nativeTabs,"omnibox_focused":omniboxFocused]
    if let omniboxValue { observation["fixture_omnibox_value"] = omniboxValue }
    observations.append(observation)
}
let ok = !observations.isEmpty && observations.allSatisfy { $0["expected_url_matches"] as? Bool == true }
let result:[String:Any] = ["schema":"yee.owned-native-active-url.v2","time_ns":Int64(Date().timeIntervalSince1970*1e9),
 "bundle_id":CommandLine.arguments[2],"pid":app.processIdentifier,"app_frontmost":app.isActive,
 "expected_url":expected.absoluteString,"observations":observations,"matches":ok,
 "method":CommandLine.arguments[2] == "org.chromium.Chromium" ? "macOS AXMainWindow/AXFocusedWindow exact native omnibox URL; no page content read" : "macOS AXMainWindow/AXFocusedWindow single AXWebArea URL; no page content read"]
print(String(decoding:try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys]),as:UTF8.self))
exit(ok ? 0 : 1)
