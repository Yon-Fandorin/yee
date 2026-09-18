// Reversible viewport setup, guarded by the exact synthetic main-window URL.
import AppKit
import ApplicationServices
import Foundation
func ax(_ e:AXUIElement,_ n:String)->CFTypeRef?{var v:CFTypeRef?;return AXUIElementCopyAttributeValue(e,n as CFString,&v) == .success ? v:nil}
guard (CommandLine.arguments.count==4 || (CommandLine.arguments.count==5 && CommandLine.arguments[4]=="absolute")),let expected=URL(string:CommandLine.arguments[1]),
 expected.scheme=="http",expected.host=="127.0.0.1",expected.port==8787,
 let dx=Double(CommandLine.arguments[2]),let dy=Double(CommandLine.arguments[3]),abs(dx)<=4000,abs(dy)<=3000,
 let app=NSRunningApplication.runningApplications(withBundleIdentifier:"at.studio.AsideBrowser").first,
 let raw=ax(AXUIElementCreateApplication(app.processIdentifier),kAXMainWindowAttribute),CFGetTypeID(raw)==AXUIElementGetTypeID() else{exit(2)}
let window=raw as! AXUIElement
var pending=[window];var visited=0;var areas=0;var matches=0
while let n=pending.popLast(),visited<1000{visited+=1
 if ax(n,kAXRoleAttribute) as? String=="AXWebArea"{
  areas+=1;let value=ax(n,kAXURLAttribute)
  if (value as? URL)?.absoluteString==expected.absoluteString{matches+=1};continue
 }
 pending += ax(n,kAXChildrenAttribute) as? [AXUIElement] ?? []
}
guard areas==1,matches==1,pending.isEmpty,let rawSize=ax(window,kAXSizeAttribute),CFGetTypeID(rawSize)==AXValueGetTypeID() else{exit(1)}
var before=CGSize.zero
guard AXValueGetValue(rawSize as! AXValue,.cgSize,&before) else{exit(1)}
var after=CommandLine.arguments.count==5 ? CGSize(width:dx,height:dy) : CGSize(width:before.width+dx,height:before.height+dy)
guard after.width>=400,after.height>=300,after.width<=4000,after.height<=3000,
 let value=AXValueCreate(.cgSize,&after),AXUIElementSetAttributeValue(window,kAXSizeAttribute as CFString,value) == .success else{exit(1)}
let result:[String:Any]=["expected_url":expected.absoluteString,"before_width":before.width,"before_height":before.height,"requested_width":after.width,"requested_height":after.height]
print(String(decoding:try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys]),as:UTF8.self))
