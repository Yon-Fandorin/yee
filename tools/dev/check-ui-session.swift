// Read-only preflight. Never unlock the desktop or change security settings.
import CoreGraphics
import Foundation

guard let session = CGSessionCopyCurrentDictionary() as? [String: Any] else {
  fputs("UI_SESSION_UNAVAILABLE: Cannot inspect the macOS GUI session. A sandbox may restrict this query; retry through your approved local terminal before assuming the desktop is unavailable.\n", stderr)
  exit(21)
}

if (session["CGSSessionScreenIsLocked"] as? NSNumber)?.boolValue == true {
  fputs("UI_SESSION_LOCKED: Unlock this Mac, then retry. A running Yee process is not proof of an automatable window (cgWindowNotFound).\n", stderr)
  exit(20)
}

// Success only establishes an unlocked session, not capture/accessibility
// authorization for a separate automation process or a visible Yee window.
print("UI_SESSION_UNLOCKED: Verify the Yee window through your UI automation client next.")
