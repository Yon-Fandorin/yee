// Root-only native sidebar drag on an exact active fixture. Integer hit-point
// avoids the old half-pixel pointer rounding; geometry is verified separately.
import AppKit
import ApplicationServices
import Foundation
func ax(_ e:AXUIElement,_ n:String)->CFTypeRef?{var v:CFTypeRef?;return AXUIElementCopyAttributeValue(e,n as CFString,&v) == .success ? v:nil}
guard CommandLine.arguments.count==3,let u=URL(string:CommandLine.arguments[1]),u.scheme=="http",u.host=="127.0.0.1",u.port==8787,let delta=Double(CommandLine.arguments[2]),delta.isFinite,abs(delta)<=100,let app=NSRunningApplication.runningApplications(withBundleIdentifier:"at.studio.AsideBrowser").first else{exit(2)}
guard app.isActive,NSWorkspace.shared.frontmostApplication?.processIdentifier==app.processIdentifier else{exit(2)}
let e=AXUIElementCreateApplication(app.processIdentifier)
guard let raw=ax(e,kAXMainWindowAttribute),CFGetTypeID(raw)==AXUIElementGetTypeID() else{exit(1)}
var pending=[raw as! AXUIElement];var sliders:[AXUIElement]=[];var areas=0;var matching=0;var count=0
while let n=pending.popLast(),count<1000{count+=1
 let role=ax(n,kAXRoleAttribute) as? String ?? ""
 if role=="AXWebArea"{areas+=1;if (ax(n,kAXURLAttribute) as? URL)?.absoluteString==u.absoluteString{matching+=1};continue}
 if role=="AXSlider",(ax(n,kAXDescriptionAttribute) as? String ?? "").contains("세로 탭 표시줄 핸들"){sliders.append(n)}
 pending += ax(n,kAXChildrenAttribute) as? [AXUIElement] ?? []
}
guard sliders.count==1,areas==1,matching==1,pending.isEmpty,let rp=ax(sliders[0],kAXPositionAttribute),let rs=ax(sliders[0],kAXSizeAttribute),CFGetTypeID(rp)==AXValueGetTypeID(),CFGetTypeID(rs)==AXValueGetTypeID() else{exit(1)}
var p=CGPoint.zero;var size=CGSize.zero
_ = AXValueGetValue(rp as! AXValue,.cgPoint,&p);_ = AXValueGetValue(rs as! AXValue,.cgSize,&size)
let from=CGPoint(x:p.x+min(1,size.width/2),y:p.y+size.height/2);let old=CGEvent(source:nil)?.location
CGEvent(mouseEventSource:nil,mouseType:.leftMouseDown,mouseCursorPosition:from,mouseButton:.left)?.post(tap:.cghidEventTap)
for i in 1...8{Thread.sleep(forTimeInterval:0.03);CGEvent(mouseEventSource:nil,mouseType:.leftMouseDragged,mouseCursorPosition:CGPoint(x:from.x+delta*Double(i)/8,y:from.y),mouseButton:.left)?.post(tap:.cghidEventTap)}
CGEvent(mouseEventSource:nil,mouseType:.leftMouseUp,mouseCursorPosition:CGPoint(x:from.x+delta,y:from.y),mouseButton:.left)?.post(tap:.cghidEventTap)
Thread.sleep(forTimeInterval:0.5)
if let old=old{CGEvent(mouseEventSource:nil,mouseType:.mouseMoved,mouseCursorPosition:old,mouseButton:.left)?.post(tap:.cghidEventTap)}
var after=CGPoint.zero
if let v=ax(sliders[0],kAXPositionAttribute),CFGetTypeID(v)==AXValueGetTypeID(){_ = AXValueGetValue(v as! AXValue,.cgPoint,&after)}
print(String(decoding:try JSONSerialization.data(withJSONObject:["expected_url":u.absoluteString,"before_handle_x":p.x,"after_handle_x":after.x,"requested_delta":delta],options:[.sortedKeys]),as:UTF8.self))
guard abs((after.x-p.x)-delta)<=1 else{exit(1)}
