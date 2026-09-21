// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Capture the separate native Agent dialog without changing the foreground app.
// Usage: capture-yee-native-prompt PID OUTPUT.png [--ax]

import AppKit
import ApplicationServices
import ScreenCaptureKit

@main
struct CaptureYeeNativePrompt {
  static func main() async {
    do {
      try await capture()
    } catch {
      fputs("capture-yee-native-prompt: \(error)\n", stderr)
      exit(1)
    }
  }

  @MainActor private static func capture() async throws {
    _ = NSApplication.shared
    guard
      CommandLine.arguments.count == 3
        || (CommandLine.arguments.count == 4 && CommandLine.arguments[3] == "--ax"),
      let pid = Int32(CommandLine.arguments[1]),
      pid > 0
    else {
      throw CaptureError.invalidArguments
    }

    let output = URL(fileURLWithPath: CommandLine.arguments[2])
    guard NSRunningApplication(processIdentifier: pid)?.executableURL?.lastPathComponent == "Yee"
    else {
      throw CaptureError.notYee
    }
    guard !FileManager.default.fileExists(atPath: output.path) else {
      throw CaptureError.outputExists
    }
    let content = try await SCShareableContent.excludingDesktopWindows(
      false, onScreenWindowsOnly: false)
    let matches = content.windows.filter {
      $0.owningApplication?.processID == pid && $0.title == "Yee Agent"
    }
    guard matches.count == 1, let window = matches.first else {
      throw CaptureError.windowCount(matches.count)
    }
    let accessibility =
      CommandLine.arguments.count == 4
      ? try accessibilityJSON(pid: pid) : nil

    let filter = SCContentFilter(desktopIndependentWindow: window)
    let configuration = SCStreamConfiguration()
    configuration.width = Int(filter.contentRect.width * CGFloat(filter.pointPixelScale))
    configuration.height = Int(filter.contentRect.height * CGFloat(filter.pointPixelScale))
    guard configuration.width > 0, configuration.height > 0 else {
      throw CaptureError.invalidWindowSize
    }
    configuration.showsCursor = false
    let image = try await SCScreenshotManager.captureImage(
      contentFilter: filter, configuration: configuration)
    guard
      let png = NSBitmapImageRep(cgImage: image).representation(
        using: .png, properties: [:])
    else {
      throw CaptureError.encodingFailed
    }
    try png.write(to: output, options: .atomic)
    print(
      "captured Yee Agent window \(window.windowID) (\(configuration.width)x\(configuration.height)) to \(output.path)"
    )
    if let accessibility {
      print(accessibility)
    }
  }

  private static func attribute(_ element: AXUIElement, _ name: String) -> CFTypeRef? {
    var value: CFTypeRef?
    return AXUIElementCopyAttributeValue(element, name as CFString, &value) == .success
      ? value : nil
  }

  private static func string(_ element: AXUIElement, _ name: String) -> String {
    attribute(element, name) as? String ?? ""
  }

  private static func accessibilityJSON(pid: Int32) throws -> String {
    guard AXIsProcessTrusted() else { throw CaptureError.accessibilityUnavailable }
    let app = AXUIElementCreateApplication(pid)
    AXUIElementSetMessagingTimeout(app, 5)
    var windowsValue: CFTypeRef?
    let windowsError = AXUIElementCopyAttributeValue(
      app, kAXWindowsAttribute as CFString, &windowsValue)
    guard windowsError == .success else {
      throw CaptureError.accessibilityQueryFailed(windowsError.rawValue)
    }
    let windows = windowsValue as? [AXUIElement] ?? []
    let matches = windows.filter { string($0, kAXTitleAttribute) == "Yee Agent" }
    guard matches.count == 1, let dialog = matches.first else {
      throw CaptureError.accessibilityWindowCount(matches.count)
    }

    var pending = [dialog]
    var elements: [[String: Any]] = []
    while let element = pending.popLast() {
      guard elements.count < 200 else { throw CaptureError.accessibilityTreeTooLarge }
      let role = string(element, kAXRoleAttribute)
      if ["AXStaticText", "AXButton", "AXTextField"].contains(role) {
        elements.append([
          "role": role,
          "title": string(element, kAXTitleAttribute),
          "description": string(element, kAXDescriptionAttribute),
          "value": role == "AXTextField" ? "" : string(element, kAXValueAttribute),
          "enabled": attribute(element, kAXEnabledAttribute) as? Bool ?? false,
          "focused": attribute(element, kAXFocusedAttribute) as? Bool ?? false,
        ])
      }
      pending += attribute(element, kAXChildrenAttribute) as? [AXUIElement] ?? []
    }
    let json = try JSONSerialization.data(
      withJSONObject: ["window": "Yee Agent", "elements": elements],
      options: [.sortedKeys])
    return String(decoding: json, as: UTF8.self)
  }
}

enum CaptureError: Error, CustomStringConvertible {
  case invalidArguments
  case notYee
  case outputExists
  case windowCount(Int)
  case invalidWindowSize
  case encodingFailed
  case accessibilityUnavailable
  case accessibilityQueryFailed(Int32)
  case accessibilityWindowCount(Int)
  case accessibilityTreeTooLarge

  var description: String {
    switch self {
    case .invalidArguments:
      return "usage: capture-yee-native-prompt PID OUTPUT.png [--ax]"
    case .notYee:
      return "PID does not belong to a running Yee app"
    case .outputExists:
      return "output file already exists"
    case .windowCount(let count):
      return "expected exactly one Yee Agent window for PID; found \(count)"
    case .invalidWindowSize:
      return "Yee Agent window has invalid capture dimensions"
    case .encodingFailed:
      return "failed to encode captured window as PNG"
    case .accessibilityUnavailable:
      return "macOS accessibility permission is unavailable"
    case .accessibilityQueryFailed(let code):
      return "failed to read Yee accessibility windows (AX error \(code))"
    case .accessibilityWindowCount(let count):
      return "expected exactly one accessible Yee Agent window; found \(count)"
    case .accessibilityTreeTooLarge:
      return "Yee Agent accessibility tree exceeded 200 nodes"
    }
  }
}
