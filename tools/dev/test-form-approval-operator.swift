// Test operator, NOT an agent tool. Only the fixed localhost Cedar form pair.
// No default approvals, global policy changes, navigation or attach consent.
import Cocoa
import ApplicationServices
import Darwin

let fillQuestion = "Allow one fill on this page?\n\nhttp://127.0.0.1:8766\nTarget: Name\nValue: Cedar\n\nPage actions may send data or navigate."
let clickQuestion = "Allow one click on this page?\n\nhttp://127.0.0.1:8766\nTarget: Save locally\n\nPage actions may send data or navigate."
let batchQuestion = "Allow these ordered actions once?\n\nhttp://127.0.0.1:8766\n1. fill \"Name\" = \"Cedar\"\n2. click \"Save locally\"\n\nOnly this list is approved. Stops on error; completed actions cannot be undone. Page actions may send data or navigate."
let batchMode = CommandLine.arguments.last == "--batch"
let expectedQuestions = batchMode ? [batchQuestion] : [fillQuestion, clickQuestion]

func permitted(_ question: String, step: Int) -> Bool {
  return step >= 0 && step < expectedQuestions.count && question == expectedQuestions[step]
}

func attribute(_ element: AXUIElement, _ name: String) -> CFTypeRef? {
  var result: CFTypeRef?
  guard AXUIElementCopyAttributeValue(element, name as CFString, &result) == .success else { return nil }
  return result
}

func string(_ element: AXUIElement, _ name: String) -> String {
  return attribute(element, name) as? String ?? ""
}

func descendants(_ root: AXUIElement) -> [AXUIElement] {
  var pending = [root], result: [AXUIElement] = []
  while let next = pending.popLast() {
    if result.count >= 64 { return [] } // Native dialog is small; fail closed.
    result.append(next)
    pending += attribute(next, kAXChildrenAttribute) as? [AXUIElement] ?? []
  }
  return result
}

if CommandLine.arguments == [CommandLine.arguments[0], "--self-test"] {
  precondition(permitted(fillQuestion, step: 0))
  precondition(permitted(clickQuestion, step: 1))
  precondition(!permitted(clickQuestion, step: 0))
  precondition(!permitted(fillQuestion, step: 1))
  precondition(!permitted(clickQuestion, step: 2))
  precondition(!permitted(fillQuestion.replacingOccurrences(of: "Cedar", with: "Other"), step: 0))
  precondition(!permitted(fillQuestion.replacingOccurrences(of: "127.0.0.1", with: "example.com"), step: 0))
  precondition(!permitted(fillQuestion + "\nExtra instruction", step: 0))
  precondition(!permitted(batchQuestion, step: 0))
  precondition(batchQuestion.contains("1. fill \"Name\" = \"Cedar\"\n2. click \"Save locally\""))
  print("10 exact-approval policy checks passed")
  exit(0)
}

guard CommandLine.arguments.count == 2 || CommandLine.arguments.count == 3 ||
      (CommandLine.arguments.count == 4 && batchMode) else {
  fputs("Usage: test-form-approval-operator PRIVATE_TEST_BRIDGE [RUN_NAME [--batch]] | --self-test\n", stderr)
  exit(2)
}
let bridge = CommandLine.arguments[1]
var info = stat()
guard bridge.range(of: #"^/private/tmp/yee-agent\.[A-Za-z0-9]+$"#, options: .regularExpression) != nil,
      lstat(bridge, &info) == 0, info.st_mode & S_IFMT == S_IFDIR,
      info.st_uid == getuid(), info.st_mode & 0o077 == 0 else {
  fputs("Refusing non-private test bridge\n", stderr)
  exit(2)
}
let runName = CommandLine.arguments.count >= 3 ? CommandLine.arguments[2] : "test"
guard runName.range(of: #"^[a-z0-9-]{1,40}$"#, options: .regularExpression) != nil else { exit(2) }
let fd = open(bridge + "/operator-" + runName + ".jsonl", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0o600)
guard fd >= 0 else { fputs("Cannot create new private operator record\n", stderr); exit(2) }
let record = FileHandle(fileDescriptor: fd, closeOnDealloc: true)
func log(_ event: String, _ values: [String: Any] = [:]) {
  let payload = values.merging(["event": event, "unix_seconds": Date().timeIntervalSince1970]) { _, new in new }
  do {
    var data = try JSONSerialization.data(withJSONObject: payload, options: [.sortedKeys])
    data.append(10)
    try record.write(contentsOf: data)
    try record.synchronize()
    FileHandle.standardOutput.write(data)
  } catch {
    fputs("Operator recording failed; stopping\n", stderr)
    exit(3)
  }
}

let repoRoot = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
guard let configData = try? Data(contentsOf: repoRoot.appendingPathComponent("branding/brand.json")),
      let config = (try? JSONSerialization.jsonObject(with: configData)) as? [String: Any],
      let productName = config["name"] as? String, !productName.isEmpty,
      !productName.contains("/"), !productName.contains("\\") else {
  log("refused", ["reason": "missing valid product branding configuration"])
  exit(2)
}
let localBuildRoot = ProcessInfo.processInfo.environment["YEE_LOCAL_BUILD_ROOT"].map {
  URL(fileURLWithPath: $0)
} ?? repoRoot.appendingPathComponent(".local-build")
let appURL = localBuildRoot.appendingPathComponent("chromium/src/out/YeePilot/" + productName + ".app")
  .standardizedFileURL.resolvingSymlinksInPath()
let expectedExecutable = appURL.appendingPathComponent("Contents/MacOS/" + productName)
let candidates = NSRunningApplication.runningApplications(withBundleIdentifier: "org.chromium.Chromium")
  .filter {
    $0.bundleURL?.standardizedFileURL.resolvingSymlinksInPath() == appURL &&
    $0.executableURL?.standardizedFileURL.resolvingSymlinksInPath() == expectedExecutable
  }
guard candidates.count == 1, AXIsProcessTrusted() else {
  log("refused", ["reason": "expected exactly one built Yee and existing AX permission"])
  exit(2)
}
let app = candidates[0]
// Bind the operator to the chosen test profile, not merely another Yee window
// whose page happens to have the same origin and labels. Never print argv.
let check = Process()
let output = Pipe()
check.executableURL = URL(fileURLWithPath: "/bin/ps")
check.arguments = ["-p", String(app.processIdentifier), "-o", "command="]
check.standardOutput = output
check.standardError = FileHandle.nullDevice
do { try check.run() } catch { log("refused", ["reason": "cannot verify process scope"]); exit(2) }
let commandData = output.fileHandleForReading.readDataToEndOfFile()
check.waitUntilExit()
guard check.terminationStatus == 0,
      let command = String(data: commandData, encoding: .utf8),
      command.split(whereSeparator: { $0.isWhitespace }).contains(Substring("--yee-agent-bridge=" + bridge)) else {
  log("refused", ["reason": "Yee process does not own selected bridge"])
  exit(2)
}
let root = AXUIElementCreateApplication(app.processIdentifier)
AXUIElementSetMessagingTimeout(root, 1)
func nativeResponse() -> (String, Bool)? {
  let input = open(bridge + "/response.json", O_RDONLY | O_NOFOLLOW | O_NONBLOCK)
  guard input >= 0 else { return nil }
  let handle = FileHandle(fileDescriptor: input, closeOnDealloc: true)
  var metadata = stat()
  guard fstat(input, &metadata) == 0, metadata.st_mode & S_IFMT == S_IFREG,
        metadata.st_uid == getuid(), metadata.st_mode & 0o077 == 0,
        metadata.st_size <= 65536,
        let data = try? handle.read(upToCount: 65537), data.count <= 65536,
        let object = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
        let id = object["id"] as? String, let ok = object["ok"] as? Bool else { return nil }
  return (id, ok)
}
log("started", ["pid": app.processIdentifier, "bridge": bridge, "max_approvals": expectedQuestions.count, "version": 6, "batch": batchMode])
let deadline = ProcessInfo.processInfo.systemUptime + 180
var step = 0
while ProcessInfo.processInfo.systemUptime < deadline && !app.isTerminated {
  let windows = attribute(root, kAXWindowsAttribute) as? [AXUIElement] ?? []
  let dialogs = windows.filter { string($0, kAXTitleAttribute) == "Yee Agent — needs your input" }
  if dialogs.count > 1 { log("refused", ["reason": "ambiguous dialog"]); exit(2) }
  if let dialog = dialogs.first {
    let elements = descendants(dialog)
    let questions = elements.filter { string($0, kAXRoleAttribute) == "AXStaticText" }
      .map { string($0, kAXValueAttribute) }
      .filter { $0.contains("Page actions may send data or navigate.") }
    if questions.count == 1 {
      let question = questions[0]
      // A just-approved dialog may remain in AX briefly while it closes.
      if step > 0 && question == expectedQuestions[step - 1] {
        Thread.sleep(forTimeInterval: 0.05)
        continue
      }
      guard permitted(question, step: step) else {
        log("refused", ["reason": "unexpected complete prompt", "step": step])
        exit(2)
      }
      let buttons = elements.filter { string($0, kAXRoleAttribute) == "AXButton"
        && string($0, kAXTitleAttribute) == "Allow once" }
      if buttons.count == 1 && (attribute(buttons[0], kAXEnabledAttribute) as? Bool) == true {
        log("observed", ["step": step, "question": question])
        guard let previous = nativeResponse() else { log("refused", ["reason": "missing baseline response"]); exit(2) }
        app.activate(options: [])
        // Respect Chromium's click-activation protection; never disable it.
        // Newly shown dialogs reject rapid clicks even when AX returns success.
        let settleSeconds = max(1.0, NSEvent.doubleClickInterval + 0.25)
        log("settling", ["step": step, "seconds": settleSeconds])
        Thread.sleep(forTimeInterval: settleSeconds)
        guard descendants(dialog).contains(where: {
          string($0, kAXRoleAttribute) == "AXStaticText" && string($0, kAXValueAttribute) == question
        }) else { log("refused", ["reason": "prompt changed before click"]); exit(2) }
        // System Events click is the real-app-validated action path. A direct
        // AXPress returned success without activating the native button.
        let click = Process()
        click.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
        click.arguments = ["-e", "on run argv", "-e", "set expectedQuestion to item 1 of argv",
          "-e", "tell application \"System Events\" to tell process \"Yee\"",
          "-e", "set frontmost to true",
          "-e", "delay \(settleSeconds)",
          "-e", "set didClick to false",
          "-e", "set allItems to entire contents of window \"Yee Agent — needs your input\"",
          "-e", "set sawQuestion to false",
          "-e", "repeat with uiItem in allItems",
          "-e", "if value of attribute \"AXRole\" of uiItem is \"AXStaticText\" then",
          "-e", "if value of attribute \"AXValue\" of uiItem is expectedQuestion then set sawQuestion to true",
          "-e", "end if", "-e", "end repeat",
          "-e", "if not sawQuestion then error \"prompt changed\"",
          "-e", "repeat with uiItem in allItems",
          "-e", "if class of uiItem is button and name of uiItem is \"Allow once\" then",
          "-e", "click uiItem", "-e", "set didClick to true", "-e", "exit repeat", "-e", "end if", "-e", "end repeat",
          "-e", "if not didClick then error \"expected button not found\"", "-e", "end tell",
          "-e", "end run", question]
        click.standardOutput = FileHandle.nullDevice
        click.standardError = FileHandle.nullDevice
        do { try click.run() } catch { log("refused", ["reason": "click process failed"]); exit(2) }
        let clickDeadline = ProcessInfo.processInfo.systemUptime + 5
        while click.isRunning && ProcessInfo.processInfo.systemUptime < clickDeadline {
          Thread.sleep(forTimeInterval: 0.02)
        }
        if click.isRunning { click.terminate(); log("incomplete", ["reason": "click timeout"]); exit(1) }
        guard click.terminationStatus == 0 else { log("refused", ["reason": "click failed"]); exit(2) }
        let responseDeadline = ProcessInfo.processInfo.systemUptime + 3
        var confirmed = false
        while ProcessInfo.processInfo.systemUptime < responseDeadline {
          if let response = nativeResponse(), response.0 != previous.0 {
            log("native_result", ["step": step, "request_id": response.0, "ok": response.1])
            guard response.1 else { exit(1) }
            confirmed = true
            break
          }
          Thread.sleep(forTimeInterval: 0.02)
        }
        guard confirmed else { log("incomplete", ["reason": "native action not confirmed"]); exit(1) }
        step += 1
        if step == expectedQuestions.count { log("complete"); exit(0) }
      }
    }
  }
  Thread.sleep(forTimeInterval: 0.05)
}
log("incomplete", ["approved": step, "reason": app.isTerminated ? "app exited" : "timeout"])
exit(1)
