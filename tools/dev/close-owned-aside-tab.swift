// Press only the Close item in the context menu of a verified synthetic tab.
import AppKit
import ApplicationServices
import Foundation
func ax(_ e:AXUIElement,_ n:String)->CFTypeRef?{var v:CFTypeRef?;return AXUIElementCopyAttributeValue(e,n as CFString,&v) == .success ? v:nil}
func stop(_ stage:String,_ detail:[String:Any]=[:])->Never{
    var value=detail;value["stage"]=stage;value["close_requested"]=false
    print(String(decoding:try! JSONSerialization.data(withJSONObject:value,options:[.sortedKeys]),as:UTF8.self));exit(1)
}
guard CommandLine.arguments.count==3,
      CommandLine.arguments[1].hasPrefix("Cedar delivery policy revision "),
      let expected=URL(string:CommandLine.arguments[2]),expected.scheme=="http",
      expected.host=="127.0.0.1",expected.port==8787 else{exit(2)}
let apps=NSRunningApplication.runningApplications(withBundleIdentifier:"at.studio.AsideBrowser")
guard apps.count==1,let app=apps.first,app.isActive else{stop("active_application",["application_count":apps.count])}
let application=AXUIElementCreateApplication(app.processIdentifier)
func matches(_ attribute:String)->Bool{
    guard let raw=ax(application,attribute),CFGetTypeID(raw)==AXUIElementGetTypeID() else{return false}
    var pending=[raw as! AXUIElement];var count=0;var areas=0;var matching=0
    while let e=pending.popLast(),count<1000{count+=1
        let role=ax(e,kAXRoleAttribute) as? String ?? ""
        if role=="AXWebArea"{areas+=1;let rawURL=ax(e,kAXURLAttribute)
            let url=(rawURL as? URL)?.absoluteString ?? (rawURL as? String)
            if url==expected.absoluteString{matching+=1};continue}
        pending += ax(e,kAXChildrenAttribute) as? [AXUIElement] ?? []
    }
    return pending.isEmpty && areas==1 && matching==1
}
let mainMatches=matches(kAXMainWindowAttribute),focusedMatches=matches(kAXFocusedWindowAttribute)
guard mainMatches,focusedMatches,let raw=ax(application,kAXMainWindowAttribute)
    else{stop("owned_window",["main_matches":mainMatches,"focused_matches":focusedMatches])}
var pending=[raw as! AXUIElement];var found:[AXUIElement]=[];var count=0
while let e=pending.popLast(),count<1000{count+=1
    let role=ax(e,kAXRoleAttribute) as? String ?? ""
    if role=="AXWebArea"{continue}
    let labels=[ax(e,kAXTitleAttribute) as? String,ax(e,kAXDescriptionAttribute) as? String].compactMap{$0}
    if ["AXTab","AXRadioButton"].contains(role),labels.contains(where:{$0.contains(CommandLine.arguments[1])}){found.append(e)}
    pending += ax(e,kAXChildrenAttribute) as? [AXUIElement] ?? []
}
guard pending.isEmpty,found.count==1 else{stop("owned_tab",["matches":found.count,"traversal_complete":pending.isEmpty])}
let menuAcknowledgement=AXUIElementPerformAction(found[0],"AXShowMenu" as CFString)
// An accessibility request can time out after the native menu has appeared.
// Verify the resulting menu instead of repeating the input request.
Thread.sleep(forTimeInterval:0.2)
pending=[application];count=0;var closes:[AXUIElement]=[]
while let e=pending.popLast(),count<1500{count+=1
    let role=ax(e,kAXRoleAttribute) as? String ?? ""
    if ["AXWebArea","AXMenuBar"].contains(role){continue}
    let children=ax(e,kAXChildrenAttribute) as? [AXUIElement] ?? []
    if role=="AXMenu"{
        let titles=children.compactMap{ax($0,kAXTitleAttribute) as? String}
        if Set(["닫기","다른 탭 닫기","아래 탭 닫기"]).isSubset(of:Set(titles)){
            closes += children.filter{ax($0,kAXRoleAttribute) as? String=="AXMenuItem" && ax($0,kAXTitleAttribute) as? String=="닫기"}
        }
    }
    pending += children
}
var owner:pid_t=0
guard pending.isEmpty,closes.count==1,app.isActive,matches(kAXMainWindowAttribute),
      AXUIElementGetPid(closes[0],&owner) == .success,owner==app.processIdentifier
    else{stop("owned_context_menu",["close_items":closes.count,"traversal_complete":pending.isEmpty,"app_active":app.isActive,"main_matches":matches(kAXMainWindowAttribute),"menu_ack_code":menuAcknowledgement.rawValue])}
let acknowledgement=AXUIElementPerformAction(closes[0],kAXPressAction as CFString)
print(String(decoding:try JSONSerialization.data(withJSONObject:["pid":owner,"expected_url":expected.absoluteString,
    "owned_tab":CommandLine.arguments[1],"action":"AXPress exact owned tab context Close","keyboard_input":false,
    "close_requested":true,"native_acknowledged":acknowledgement == .success,"menu_ack_code":menuAcknowledgement.rawValue,
    "native_ack_code":acknowledgement.rawValue],options:[.sortedKeys]),as:UTF8.self))
exit(acknowledgement == .success ? 0:1)
