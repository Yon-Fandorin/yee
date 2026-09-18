// Read only chrome geometry for an independently owned selected fixture tab.
import AppKit
import ApplicationServices
import Foundation
func ax(_ e:AXUIElement,_ n:String)->CFTypeRef? { var v:CFTypeRef?;return AXUIElementCopyAttributeValue(e,n as CFString,&v) == .success ? v:nil }
guard CommandLine.arguments.count == 2,let expected = URL(string:CommandLine.arguments[1]),
      expected.scheme == "http",expected.host == "127.0.0.1",expected.port == 8787 else { exit(2) }
let apps = NSRunningApplication.runningApplications(withBundleIdentifier:"at.studio.AsideBrowser")
guard apps.count == 1,let app = apps.first,app.isActive,
      let raw = ax(AXUIElementCreateApplication(app.processIdentifier),kAXMainWindowAttribute),
      CFGetTypeID(raw) == AXUIElementGetTypeID() else { exit(1) }
let window = raw as! AXUIElement
var pending=[window];var sliders:[AXUIElement]=[];var areas=0;var matching=0;var count=0
while let node=pending.popLast(),count<1000 { count += 1
    let role=ax(node,kAXRoleAttribute) as? String ?? ""
    if role == "AXWebArea" { areas += 1;if (ax(node,kAXURLAttribute) as? URL)?.absoluteString == expected.absoluteString { matching += 1 };continue }
    if role == "AXSlider",(ax(node,kAXDescriptionAttribute) as? String ?? "").contains("세로 탭 표시줄 핸들") { sliders.append(node) }
    pending += ax(node,kAXChildrenAttribute) as? [AXUIElement] ?? []
}
guard pending.isEmpty,areas == 1,matching == 1,sliders.count == 1,
      let wp=ax(window,kAXPositionAttribute),let ws=ax(window,kAXSizeAttribute),let sp=ax(sliders[0],kAXPositionAttribute),
      CFGetTypeID(wp) == AXValueGetTypeID(),CFGetTypeID(ws) == AXValueGetTypeID(),CFGetTypeID(sp) == AXValueGetTypeID() else { exit(1) }
var position=CGPoint.zero;var size=CGSize.zero;var handle=CGPoint.zero
guard AXValueGetValue(wp as! AXValue,.cgPoint,&position),AXValueGetValue(ws as! AXValue,.cgSize,&size),AXValueGetValue(sp as! AXValue,.cgPoint,&handle) else { exit(1) }
print(String(decoding:try JSONSerialization.data(withJSONObject:["pid":app.processIdentifier,"expected_url":expected.absoluteString,
    "app_frontmost":app.isActive,"window_x":position.x,"window_y":position.y,"window_width":size.width,"window_height":size.height,
    "sidebar_handle_relative_x":handle.x-position.x,"scope":"selected fixture chrome geometry; no page content"],options:[.sortedKeys]),as:UTF8.self))
