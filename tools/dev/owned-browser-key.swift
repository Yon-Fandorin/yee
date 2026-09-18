// Test-operator input goes to the exact owned browser PID, never the global
// foreground application. This helper is not exposed as a model/MCP tool.
import AppKit
import ApplicationServices
import Foundation

guard CommandLine.arguments.count >= 4,
      let pid = Int32(CommandLine.arguments[1]), pid > 0,
      ["org.chromium.Chromium", "at.studio.AsideBrowser"].contains(CommandLine.arguments[2]),
      let app = NSRunningApplication(processIdentifier: pid),
      app.bundleIdentifier == CommandLine.arguments[2], !app.isTerminated else { exit(2) }
let action = CommandLine.arguments[3]
// Native menu dispatch avoids dependence on synthetic command-key routing.
// Select exactly one enabled, unmodified Command menu item in this PID.
let menuCommands = ["new":"t", "close":"w", "reload":"r", "location":"l"]
if let command = menuCommands[action] {
    guard CommandLine.arguments.count == 4, app.isActive else { exit(2) }
    func ax(_ e:AXUIElement,_ name:String)->CFTypeRef? {
        var v:CFTypeRef?
        return AXUIElementCopyAttributeValue(e,name as CFString,&v) == .success ? v:nil
    }
    let root = AXUIElementCreateApplication(pid)
    guard let raw = ax(root,kAXMenuBarAttribute), CFGetTypeID(raw) == AXUIElementGetTypeID() else { exit(1) }
    var pending = [raw as! AXUIElement]
    var matches = [AXUIElement]()
    var visited = 0
    while let node = pending.popLast(), visited < 1000 {
        visited += 1
        if ax(node,kAXRoleAttribute) as? String == "AXMenuItem",
           (ax(node,"AXMenuItemCmdChar") as? String)?.lowercased() == command,
           ax(node,"AXMenuItemCmdModifiers") as? Int == 0,
           ax(node,kAXEnabledAttribute) as? Bool == true { matches.append(node) }
        pending += ax(node,kAXChildrenAttribute) as? [AXUIElement] ?? []
    }
    guard pending.isEmpty, matches.count == 1 else { exit(1) }
    let result = AXUIElementPerformAction(matches[0],kAXPressAction as CFString)
    print(String(decoding:try JSONSerialization.data(withJSONObject:["pid":pid,"bundle_id":CommandLine.arguments[2],
        "action":action,"input_scope":"exact-owned-process-native-menu", "ax_returncode":result.rawValue],options:[.sortedKeys]),as:UTF8.self))
    exit(result == .success ? 0:1)
}
if let index = Int(action), (1...5).contains(index) {
    guard CommandLine.arguments.count == 4, app.isActive,
          CommandLine.arguments[2] == "org.chromium.Chromium" else { exit(2) }
    func ax(_ e:AXUIElement,_ name:String)->CFTypeRef? {
        var v:CFTypeRef?
        return AXUIElementCopyAttributeValue(e,name as CFString,&v) == .success ? v:nil
    }
    let root = AXUIElementCreateApplication(pid)
    guard let raw = ax(root,kAXMainWindowAttribute), CFGetTypeID(raw) == AXUIElementGetTypeID() else { exit(1) }
    var pending = [raw as! AXUIElement]
    var tabs = [(AXUIElement, CGPoint)]()
    var visited = 0
    while let node = pending.popLast(), visited < 1000 {
        visited += 1
        let role = ax(node,kAXRoleAttribute) as? String ?? ""
        if role == "AXWebArea" { continue }
        if ["AXTab","AXRadioButton"].contains(role) {
            guard let rawPoint = ax(node,kAXPositionAttribute), CFGetTypeID(rawPoint) == AXValueGetTypeID() else { exit(1) }
            var point = CGPoint.zero
            guard AXValueGetValue(rawPoint as! AXValue,.cgPoint,&point), point.x.isFinite, point.y.isFinite else { exit(1) }
            tabs.append((node,point))
        }
        pending += ax(node,kAXChildrenAttribute) as? [AXUIElement] ?? []
    }
    tabs.sort { $0.1.y == $1.1.y ? $0.1.x < $1.1.x : $0.1.y < $1.1.y }
    guard pending.isEmpty, tabs.count >= index, tabs.count <= 5,
          Set(tabs.map { "\($0.1.x),\($0.1.y)" }).count == tabs.count else { exit(1) }
    let result = AXUIElementPerformAction(tabs[index-1].0,kAXPressAction as CFString)
    print(String(decoding:try JSONSerialization.data(withJSONObject:["pid":pid,"bundle_id":CommandLine.arguments[2],
        "action":action,"native_tab_count":tabs.count,"input_scope":"exact-owned-process-native-tab", "ax_returncode":result.rawValue],options:[.sortedKeys]),as:UTF8.self))
    exit(result == .success ? 0:1)
}
let keycodes: [String: CGKeyCode] = ["close":13,"reload":15,"new":17,"location":37,"escape":53,"enter":36,
                                  "1":18,"2":19,"3":20,"4":21,"5":23]
if action == "text" {
    guard CommandLine.arguments.count == 5, let url = URL(string:CommandLine.arguments[4]),
          url.scheme == "http", url.host == "127.0.0.1", url.port == 8787 else { exit(2) }
    if app.bundleIdentifier == "org.chromium.Chromium" {
        guard app.isActive else { exit(2) }
        func ax(_ e:AXUIElement,_ name:String)->CFTypeRef? {
            var v:CFTypeRef?;return AXUIElementCopyAttributeValue(e,name as CFString,&v) == .success ? v:nil
        }
        let root=AXUIElementCreateApplication(pid)
        guard let raw=ax(root,kAXMainWindowAttribute),CFGetTypeID(raw)==AXUIElementGetTypeID() else { exit(1) }
        var pending=[raw as! AXUIElement];var matches=[AXUIElement]();var visited=0
        while let node=pending.popLast(),visited<1000 {
            visited += 1
            let role=ax(node,kAXRoleAttribute) as? String ?? ""
            if role=="AXWebArea" { continue }
            let label=(ax(node,kAXDescriptionAttribute) as? String) ?? (ax(node,kAXTitleAttribute) as? String) ?? ""
            if role=="AXTextField",["주소창 및 검색창","Address and search bar"].contains(label),ax(node,kAXFocusedAttribute) as? Bool==true { matches.append(node) }
            pending += ax(node,kAXChildrenAttribute) as? [AXUIElement] ?? []
        }
        guard pending.isEmpty,matches.count==1 else { exit(1) }
        var settable=DarwinBoolean(false)
        guard AXUIElementIsAttributeSettable(matches[0],kAXValueAttribute as CFString,&settable) == .success,settable.boolValue else { exit(1) }
        let result=AXUIElementSetAttributeValue(matches[0],kAXValueAttribute as CFString,CommandLine.arguments[4] as CFString)
        guard result == .success,ax(matches[0],kAXValueAttribute) as? String==CommandLine.arguments[4] else { exit(1) }
    } else {
        let units = Array(CommandLine.arguments[4].utf16)
        guard let down = CGEvent(keyboardEventSource:nil,virtualKey:0,keyDown:true),
              let up = CGEvent(keyboardEventSource:nil,virtualKey:0,keyDown:false) else { exit(1) }
        units.withUnsafeBufferPointer { buffer in
            down.keyboardSetUnicodeString(stringLength:buffer.count,unicodeString:buffer.baseAddress)
            up.keyboardSetUnicodeString(stringLength:buffer.count,unicodeString:buffer.baseAddress)
        }
        down.postToPid(pid);up.postToPid(pid)
    }
} else {
    guard CommandLine.arguments.count == 4, let code = keycodes[action],
          let down = CGEvent(keyboardEventSource:nil,virtualKey:code,keyDown:true),
          let up = CGEvent(keyboardEventSource:nil,virtualKey:code,keyDown:false) else { exit(2) }
    if !["escape","enter"].contains(action) { down.flags = .maskCommand; up.flags = .maskCommand }
    down.postToPid(pid);up.postToPid(pid)
}
print(String(decoding:try JSONSerialization.data(withJSONObject:["pid":pid,"bundle_id":CommandLine.arguments[2],
      "action":action,"input_scope":"direct-to-owned-process"],options:[.sortedKeys]),as:UTF8.self))
