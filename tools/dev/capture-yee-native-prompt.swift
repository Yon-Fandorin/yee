// Copyright 2026 The Yee Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Capture the separate native Agent dialog without changing the foreground app.
// Usage: capture-yee-native-prompt PID OUTPUT.png

import AppKit
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
    guard CommandLine.arguments.count == 3,
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
  }
}

enum CaptureError: Error, CustomStringConvertible {
  case invalidArguments
  case notYee
  case outputExists
  case windowCount(Int)
  case invalidWindowSize
  case encodingFailed

  var description: String {
    switch self {
    case .invalidArguments:
      return "usage: capture-yee-native-prompt PID OUTPUT.png"
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
    }
  }
}
