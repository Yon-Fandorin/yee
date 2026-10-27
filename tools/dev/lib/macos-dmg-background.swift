import AppKit

// Finder의 앱·응용 프로그램 아이콘 사이에 드래그 방향을 표시한다.
let output = CommandLine.arguments[1]
let width = 640
let height = 400
let bitmap = NSBitmapImageRep(
    bitmapDataPlanes: nil, pixelsWide: width * 2, pixelsHigh: height * 2,
    bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
    colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
bitmap.size = NSSize(width: width, height: height)
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
NSColor.white.setFill()
NSRect(x: 0, y: 0, width: width, height: height).fill()
for index in 0..<5 {
    NSColor(white: 0.85 - CGFloat(index) * 0.16, alpha: 1).setStroke()
    let x = CGFloat(286 + index * 15)
    let path = NSBezierPath()
    path.move(to: NSPoint(x: x, y: 201))
    path.line(to: NSPoint(x: x + 7, y: 209))
    path.line(to: NSPoint(x: x, y: 217))
    path.lineWidth = 2.5
    path.lineCapStyle = .round
    path.lineJoinStyle = .round
    path.stroke()
}
NSGraphicsContext.restoreGraphicsState()
try bitmap.representation(using: .tiff, properties: [:])!
    .write(to: URL(fileURLWithPath: output), options: .atomic)
