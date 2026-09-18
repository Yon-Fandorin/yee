// Graceful process-scoped Yee shutdown. No application-name resolution or focus.
import AppKit
import Foundation

let argv = CommandLine.arguments
guard argv.count >= 2 && argv.count <= 4 else { exit(2) }
var dryRun = false
var expectedPid: pid_t?
for option in argv.dropFirst(2) {
    if option == "--dry-run" && !dryRun {
        dryRun = true
    } else if option.hasPrefix("--pid=") && expectedPid == nil,
              let pid = Int32(option.dropFirst(6)), pid > 0 {
        expectedPid = pid
    } else { exit(2) }
}
let executable = URL(fileURLWithPath: argv[1]).standardizedFileURL.resolvingSymlinksInPath()
let expectedBundle = executable.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
guard executable.deletingLastPathComponent().lastPathComponent == "MacOS",
      executable.deletingLastPathComponent().deletingLastPathComponent().lastPathComponent == "Contents",
      expectedBundle.pathExtension == "app",
      let bundle = Bundle(url: expectedBundle),
      bundle.bundleIdentifier == "org.chromium.Chromium",
      bundle.executableURL?.standardizedFileURL.resolvingSymlinksInPath() == executable else {
    fputs("Expected the explicit executable of the branded Chromium app bundle\n", stderr); exit(2)
}
func matches(_ app: NSRunningApplication) -> Bool {
    (expectedPid == nil || app.processIdentifier == expectedPid) &&
    app.executableURL?.standardizedFileURL.resolvingSymlinksInPath() == executable &&
    app.bundleURL?.standardizedFileURL.resolvingSymlinksInPath() == expectedBundle &&
    app.bundleIdentifier == "org.chromium.Chromium"
}
let apps = NSWorkspace.shared.runningApplications.filter(matches)
var receipts: [[String: Any]] = []
var failed = expectedPid != nil && apps.isEmpty
for app in apps {
    let pid = app.processIdentifier
    guard let current = NSRunningApplication(processIdentifier: pid), matches(current) else {
        failed = true; continue
    }
    let requested = dryRun ? false : current.terminate()
    if !dryRun {
        let deadline = Date().addingTimeInterval(15)
        while !current.isTerminated && Date() < deadline {
            RunLoop.current.run(until: Date().addingTimeInterval(0.1))
        }
        if !current.isTerminated { failed = true }
    }
    receipts.append(["pid": pid, "executable": executable.path,
                     "quit_requested": requested, "terminated": current.isTerminated])
}
print(String(decoding: try JSONSerialization.data(withJSONObject: [
    "schema": "yee.exact-process-shutdown.v1", "dry_run": dryRun,
    "matched_processes": receipts, "passed": !failed
], options: [.sortedKeys]), as: UTF8.self))
exit(failed ? 1 : 0)
