// 검사 프로세스 트리와 사용자 세션을 읽기 전용으로 관찰한다.
import AppKit
import ApplicationServices
import Darwin
import Dispatch
import Foundation

struct ProcessIdentity: Hashable {
    let parent: pid_t
    let started: UInt64

    static func == (lhs: Self, rhs: Self) -> Bool { lhs.started == rhs.started }
    func hash(into hasher: inout Hasher) { hasher.combine(started) }
}

struct RecordedSession: Decodable {
    let id: pid_t
    let root_pid: pid_t
    let process_group: pid_t
    let started_us: UInt64
    let ended_us: UInt64?

    func contains(_ identity: ProcessIdentity) -> Bool {
        identity.started >= started_us
            && (ended_us == nil || identity.started <= ended_us!)
    }
}

struct SessionRegistry: Decodable {
    let schema_version: Int
    let sessions: [RecordedSession]

    static func read(_ path: String) throws -> Self {
        let registry = try JSONDecoder().decode(Self.self, from: Data(contentsOf: URL(fileURLWithPath: path)))
        guard registry.schema_version == 1,
              Set(registry.sessions.map { $0.id }).count == registry.sessions.count,
              registry.sessions.allSatisfy({ $0.id > 0 && $0.root_pid == $0.id
                  && $0.process_group == $0.id && $0.started_us > 0
                  && ($0.ended_us == nil || $0.ended_us! >= $0.started_us) }) else {
            throw CocoaError(.fileReadCorruptFile)
        }
        return registry
    }
}

struct ProcessOwnership {
    var known: [pid_t: ProcessIdentity] = [:]
    var current: [pid_t: ProcessIdentity] = [:]
    var sessions: [RecordedSession] = []
    var enumeratedGroups: Set<pid_t> = []
    var confirmedRoots: Set<pid_t> = []
    var readGaps = 0
    var processListReadGaps = 0
    var processGroupReadGaps = 0

    var complete: Bool {
        readGaps == 0 && !sessions.isEmpty && sessions.allSatisfy {
            enumeratedGroups.contains($0.process_group) && confirmedRoots.contains($0.root_pid)
        }
    }

    func owns(_ pid: pid_t, _ identity: ProcessIdentity?) -> Bool {
        guard let identity = identity else { return known[pid] != nil }
        return known[pid] == identity
    }

    mutating func update(_ listing: [pid_t: ProcessIdentity]?,
                         groups: [pid_t: [pid_t: ProcessIdentity]], excluding: pid_t) -> Set<pid_t> {
        var unavailable = listing == nil
        if listing == nil { processListReadGaps += 1 }
        current = listing ?? [:]
        for session in sessions {
            if let members = groups[session.process_group] {
                current.merge(members) { _, member in member }
                enumeratedGroups.insert(session.process_group)
                for (pid, identity) in members where pid != excluding && session.contains(identity) {
                    known[pid] = identity
                    if pid == session.root_pid { confirmedRoots.insert(pid) }
                }
            } else {
                unavailable = true
                processGroupReadGaps += 1
            }
            // 그룹을 바꾼 자식도 root의 자손으로 추적한다.
            if let root = current[session.root_pid], session.contains(root) {
                known[session.root_pid] = root
            }
        }
        if unavailable { readGaps += 1 }
        var changed = true
        while changed {
            changed = false
            for (pid, identity) in current where pid != excluding && known[pid] != identity {
                if let parent = known[identity.parent], current[identity.parent] == parent {
                    known[pid] = identity
                    changed = true
                }
            }
        }
        return Set(known.keys.filter { current[$0] == known[$0] })
    }
}

func processes(group: pid_t? = nil,
               query: ([Int32]) -> [kinfo_proc]? = processTable) -> [pid_t: ProcessIdentity]? {
    let mib: [Int32] = [CTL_KERN, KERN_PROC, group == nil ? KERN_PROC_ALL : KERN_PROC_PGRP, group ?? 0]
    guard let entries = query(mib) else { return nil }
    var result: [pid_t: ProcessIdentity] = [:]
    for entry in entries where entry.kp_proc.p_pid > 0 {
        let start = entry.kp_proc.p_un.__p_starttime
        result[entry.kp_proc.p_pid] = ProcessIdentity(parent: entry.kp_eproc.e_ppid,
            started: UInt64(start.tv_sec) * 1_000_000 + UInt64(start.tv_usec))
    }
    return result
}

func processTable(_ selection: [Int32]) -> [kinfo_proc]? {
    var mib = selection
    let stride = MemoryLayout<kinfo_proc>.stride
    for _ in 0..<3 {
        var bytes = 0
        guard sysctl(&mib, u_int(mib.count), nil, &bytes, nil, 0) == 0 else { return nil }
        var entries = [kinfo_proc](repeating: kinfo_proc(), count: bytes / stride + 64)
        bytes = entries.count * stride
        let status = entries.withUnsafeMutableBytes {
            sysctl(&mib, u_int(mib.count), $0.baseAddress, &bytes, nil, 0)
        }
        if status != 0 {
            if errno == ENOMEM { continue }
            return nil
        }
        guard bytes % stride == 0 else { return nil }
        return Array(entries.prefix(bytes / stride))
    }
    return nil
}

struct PIDChanges {
    var first: pid_t?
    var last: pid_t?
    var changes = 0
    var readGaps = 0
    var validSamples = 0

    mutating func sample(_ pid: pid_t?) {
        guard let pid = pid, pid > 0 else { readGaps += 1; return }
        if let last = last, last != pid { changes += 1 }
        if first == nil { first = pid }
        last = pid
        validSamples += 1
    }
}

func launchServicesQuery(_ arguments: [String]) -> String? {
    let process = Process()
    let output = Pipe()
    let finished = DispatchSemaphore(value: 0)
    process.executableURL = URL(fileURLWithPath: "/usr/bin/lsappinfo")
    process.arguments = arguments
    process.standardInput = FileHandle.nullDevice
    process.standardOutput = output
    process.standardError = FileHandle.nullDevice
    process.terminationHandler = { _ in finished.signal() }
    do { try process.run() } catch { return nil }
    if finished.wait(timeout: .now() + .milliseconds(500)) == .timedOut {
        // 직접 만든 조회 프로세스만 회수한다. 읽기 실패는 관측 미완료로 남긴다.
        if process.isRunning { kill(process.processIdentifier, SIGKILL) }
        process.waitUntilExit()
        return nil
    }
    guard process.terminationStatus == 0 else { return nil }
    return String(data: output.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8)
}

func frontmostApplicationPID(query: ([String]) -> String? = launchServicesQuery) -> pid_t? {
    // NSWorkspace의 캐시와 무관하게 각 표본의 ASN과 PID를 LaunchServices에 질의한다.
    guard let asn = query(["front"])?.trimmingCharacters(in: .whitespacesAndNewlines),
          asn.range(of: #"^ASN:0x[0-9a-fA-F]+[-:]0x[0-9a-fA-F]+:$"#,
                    options: .regularExpression) != nil,
          let info = query(["info", "-only", "pid", asn]) else { return nil }
    let parts = info.trimmingCharacters(in: .whitespacesAndNewlines)
        .split(separator: "=", maxSplits: 1)
    guard parts.count == 2,
          [#""pid""#, #""LSApplicationPID""#].contains(
            parts[0].trimmingCharacters(in: .whitespaces)),
          let pid = pid_t(parts[1].trimmingCharacters(in: .whitespaces)), pid > 0 else { return nil }
    return pid
}

struct DesktopItem: Hashable {
    let owner: pid_t
    let identity: ProcessIdentity?
    let application: String?
    let window: Int?
    let layer: Int?
    var bounds: CGRect? = nil
    var alpha: Double? = nil
    var onScreen: Bool? = nil

    // 이동·최소화로 표시 상태가 달라져도 같은 창으로 추적한다.
    static func == (lhs: Self, rhs: Self) -> Bool {
        lhs.owner == rhs.owner && lhs.identity == rhs.identity && lhs.application == rhs.application
            && lhs.window == rhs.window && lhs.layer == rhs.layer
    }

    func hash(into hasher: inout Hasher) {
        hasher.combine(owner)
        hasher.combine(identity)
        hasher.combine(application)
        hasher.combine(window)
        hasher.combine(layer)
    }

    var isStandardWindow: Bool {
        guard window != nil, layer == 0, let bounds = bounds else { return false }
        // 작은 hover card·tooltip·상태 팝업을 제외하는 화면 좌표 기준 최소 크기다.
        return bounds.width >= 300 && bounds.height >= 200
    }

    var isVisibleStandardWindow: Bool {
        isStandardWindow && onScreen == true && (alpha ?? 0) > 0
    }

    var windowJSON: [String: Any] {
        var result: [String: Any] = ["pid": owner, "window_id": window ?? 0]
        result["layer"] = layer
        if let bounds = bounds {
            result["bounds"] = ["x": bounds.origin.x, "y": bounds.origin.y,
                                "width": bounds.width, "height": bounds.height]
        }
        result["alpha"] = alpha
        result["on_screen"] = onScreen
        return result
    }
}

func applicationIdentity(bundleIdentifier: String?, executablePath: String?, name: String?) -> String? {
    for (kind, value) in [("bundle", bundleIdentifier), ("executable", executablePath), ("name", name)] {
        if let value = value, !value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            return "\(kind):\(value)"
        }
    }
    return nil
}

struct ApplicationSample {
    let owner: pid_t
    let terminated: Bool
    let activationPolicy: NSApplication.ActivationPolicy
    let readIdentity: () -> String?
}

func desktopWindows(_ windows: [[String: Any]], processes: [pid_t: ProcessIdentity],
                    applications: [pid_t: String] = [:]) -> Set<DesktopItem>? {
    var result: Set<DesktopItem> = []
    for window in windows {
        guard let pid = window[kCGWindowOwnerPID as String] as? Int32,
              let number = window[kCGWindowNumber as String] as? Int,
              let layer = window[kCGWindowLayer as String] as? Int else { return nil }
        let application = applications[pid] ?? applicationIdentity(bundleIdentifier: nil,
            executablePath: nil, name: window[kCGWindowOwnerName as String] as? String)
        // 일반 창의 identity는 필수이며, 제외 layer는 없어도 된다.
        guard layer != 0 || application != nil else { return nil }
        let bounds = (window[kCGWindowBounds as String] as? [String: Any]).flatMap {
            CGRect(dictionaryRepresentation: $0 as CFDictionary)
        }
        result.insert(DesktopItem(owner: pid, identity: processes[pid], application: application,
                                  window: number, layer: layer, bounds: bounds,
                                  alpha: window[kCGWindowAlpha as String] as? Double,
                                  onScreen: window[kCGWindowIsOnscreen as String] as? Bool))
    }
    return result
}

func visibleTestWindows(_ windows: Set<DesktopItem>, owned: Set<pid_t>) -> Set<DesktopItem> {
    windows.filter { owned.contains($0.owner) }
}

struct DesktopLoss {
    let application: String
    let before: Int
    let after: Int
    let windowsBefore: [DesktopItem]
    let windowsAfter: [DesktopItem]

    var count: Int { before - after }
    var json: [String: Any] {
        ["identity": application, "before": before, "after": after, "lost": count]
    }

    var windowJSON: [String: Any] {
        var result = json
        for (key, windows) in [("windows_before", windowsBefore), ("windows_after", windowsAfter)] {
            result[key] = windows.sorted {
                ($0.owner, $0.window ?? 0) < ($1.owner, $1.window ?? 0)
            }.map { $0.windowJSON }
        }
        return result
    }
}

func unrelatedLosses(_ initial: Set<DesktopItem>, _ latest: Set<DesktopItem>,
                     ownership: ProcessOwnership, observer: pid_t,
                     includingOtherLayers: Bool = false) -> [DesktopLoss] {
    func comparable(_ items: Set<DesktopItem>) -> Set<DesktopItem> {
        // 나중에 소유권이 확인된 helper도 양쪽 표본에서 제외한다.
        items.filter { $0.owner != observer && !ownership.owns($0.owner, $0.identity)
            && $0.application != nil && ($0.window == nil || $0.layer == 0 || includingOtherLayers) }
    }
    func counts(_ items: Set<DesktopItem>) -> [String: Int] {
        var result: [String: Int] = [:]
        for item in items {
            let application = item.application!
            if item.window == nil {
                result[application] = 1
            } else {
                result[application, default: 0] += 1
            }
        }
        return result
    }
    let initialItems = comparable(initial), latestItems = comparable(latest)
    let before = counts(initialItems), after = counts(latestItems)
    return before.keys.sorted().compactMap { application in
        let remaining = after[application, default: 0]
        guard before[application]! > remaining else { return nil }
        return DesktopLoss(application: application, before: before[application]!, after: remaining,
            windowsBefore: initialItems.filter { $0.application == application && $0.window != nil }.map { $0 },
            windowsAfter: latestItems.filter { $0.application == application && $0.window != nil }.map { $0 })
    }
}

func focusedWindowOwner(frontmost: pid_t?) -> pid_t? {
    guard AXIsProcessTrusted(), let frontmost = frontmost else { return nil }
    let app = AXUIElementCreateApplication(frontmost)
    AXUIElementSetMessagingTimeout(app, 0.02)
    var value: CFTypeRef?
    guard AXUIElementCopyAttributeValue(app, kAXFocusedWindowAttribute as CFString, &value) == .success,
          let window = value, CFGetTypeID(window) == AXUIElementGetTypeID() else { return nil }
    var owner: pid_t = 0
    guard AXUIElementGetPid(window as! AXUIElement, &owner) == .success, owner > 0 else { return nil }
    return owner
}

final class Observation {
    let ownPID = getpid()
    let sessionsFile: String
    var ownership = ProcessOwnership()
    var currentProcesses: [pid_t: ProcessIdentity] { ownership.current }
    var eventSources: [(pid_t, ProcessIdentity?, Bool)] = []
    var frontmostSources: [(pid_t, ProcessIdentity?)] = []
    var currentFrontmost: (pid_t, ProcessIdentity?)?
    var userActiveApplications: Set<String> = []
    var incompleteReasons: Set<String> = []
    var notes: Set<String> = []
    var counters: [String: Int] = [
        "max_visible_test_windows": 0, "cursor_changes": 0, "modifier_changes": 0
    ]
    var samples = 0
    var frontmost = PIDChanges()
    var keyOwner = PIDChanges()
    var firstCursor: CGPoint?
    var lastCursor: CGPoint?
    var firstFlags: UInt64?
    var lastFlags: UInt64?
    var initialApps: Set<DesktopItem> = []
    var initialHelperApps: Set<DesktopItem> = []
    var initialAppsCaptured = false
    var unreadableApplicationPIDs: Set<pid_t> = []
    var initialWindows: Set<DesktopItem> = []
    var stableInitialWindows: Set<DesktopItem> = []
    var initialWindowSampleTime: TimeInterval?
    var initialWindowsConfirmed = false
    var latestApps: Set<DesktopItem> = []
    var latestWindows: Set<DesktopItem> = []
    var lastSample: TimeInterval?
    var maxGapMS = 0.0
    var visibleOwners: Set<pid_t> = []
    var tap: CFMachPort?

    init(sessionsFile: String) {
        self.sessionsFile = sessionsFile
    }

    func updateTree(query: (pid_t?) -> [pid_t: ProcessIdentity]? = { processes(group: $0) }) -> Set<pid_t>? {
        do {
            ownership.sessions = try SessionRegistry.read(sessionsFile).sessions
        } catch {
            ownership.readGaps += 1
            incompleteReasons.insert("test process group unavailable: registry unreadable")
            return nil
        }
        let current = query(nil)
        var groups: [pid_t: [pid_t: ProcessIdentity]] = [:]
        for session in ownership.sessions {
            if let members = query(session.process_group) {
                groups[session.process_group] = members
            }
        }
        return ownership.update(current, groups: groups, excluding: ownPID)
    }

    func acknowledgeSessions() {
        let directory = URL(fileURLWithPath: sessionsFile).deletingLastPathComponent()
        for root in ownership.confirmedRoots {
            let ready = directory.appendingPathComponent("observer-session-\(root).ready")
            if !FileManager.default.fileExists(atPath: ready.path) {
                do { try Data().write(to: ready, options: .atomic) }
                catch { notes.insert("test process group acknowledgement unavailable") }
            }
        }
    }

    func installEventTap() {
        let types: [CGEventType] = [.flagsChanged, .keyDown, .keyUp, .mouseMoved,
            .leftMouseDown, .leftMouseUp, .rightMouseDown, .rightMouseUp,
            .otherMouseDown, .otherMouseUp, .leftMouseDragged, .rightMouseDragged, .scrollWheel]
        let mask = types.reduce(CGEventMask(0)) { $0 | (CGEventMask(1) << $1.rawValue) }
        tap = CGEvent.tapCreate(tap: .cgSessionEventTap, place: .headInsertEventTap,
            options: .listenOnly, eventsOfInterest: mask, callback: { _, type, event, context in
                let observation = Unmanaged<Observation>.fromOpaque(context!).takeUnretainedValue()
                if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
                    observation.notes.insert("input event tap disabled; injection detail incomplete")
                } else {
                    let source = pid_t(event.getIntegerValueField(.eventSourceUnixProcessID))
                    if source > 0 {
                        observation.eventSources.append((source, observation.currentProcesses[source],
                                                         type == .flagsChanged))
                    }
                }
                return Unmanaged.passUnretained(event)
            }, userInfo: Unmanaged.passUnretained(self).toOpaque())
        if let tap = tap, let source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0) {
            CFRunLoopAddSource(CFRunLoopGetCurrent(), source, .commonModes)
        } else {
            notes.insert("input event tap unavailable; injection detail unavailable")
        }
    }

    func sampleApplications(_ running: [ApplicationSample]) -> [pid_t: String] {
        var applications: [pid_t: String] = [:]
        var apps: Set<DesktopItem> = []
        for app in running {
            let pid = app.owner, process = currentProcesses[pid]
            if pid == ownPID || ownership.owns(pid, process) {
                if app.terminated { continue }
                if let application = app.readIdentity() {
                    applications[pid] = application
                } else {
                    unreadableApplicationPIDs.insert(pid)
                }
                continue
            }
            var initial = initialApps.union(initialHelperApps).first { $0.owner == pid
                && ($0.identity == nil || process == nil || $0.identity == process) }
            // 시작 시 있던 앱의 종료는 저장한 identity로 손실을 판정한다.
            if app.terminated && initial != nil { continue }
            let readable = app.readIdentity()
            if readable == nil { unreadableApplicationPIDs.insert(pid) }
            // 시작 시각을 대조하지 못하면 같은 종류의 다른 identity를 PID 재사용으로 본다.
            if let captured = initial?.application, let readable = readable,
               initial?.identity == nil || process == nil, captured != readable,
               captured.prefix(while: { $0 != ":" }) == readable.prefix(while: { $0 != ":" }) {
                initial = nil
            }
            if app.terminated || (initialAppsCaptured && initial == nil) {
                notes.insert("unrelated application outside initial snapshot: pid=\(pid) "
                    + "identity=\(readable ?? "unavailable") terminated=\(app.terminated)")
            }
            if app.terminated { continue }
            let application = initial?.application ?? readable
            if let application = application { applications[pid] = application }
            let item = DesktopItem(owner: pid, identity: process, application: application,
                                   window: nil, layer: nil)
            apps.insert(item)
            rememberUserActiveApplication(item)
            if !initialAppsCaptured {
                if app.activationPolicy == .regular {
                    initialApps.insert(item)
                    if application == nil {
                        incompleteReasons.insert("initial application identity unavailable")
                    }
                } else {
                    initialHelperApps.insert(item)
                    if application == nil {
                        notes.insert("initial non-regular application identity unavailable: pid=\(pid)")
                    }
                }
            }
        }
        initialAppsCaptured = true
        latestApps = apps
        return applications
    }

    func sampleFrontmost(_ pid: pid_t?) {
        frontmost.sample(pid)
        currentFrontmost = nil
        guard let pid = pid, pid > 0 else { return }
        let source = (pid, currentProcesses[pid])
        currentFrontmost = source
        frontmostSources.append(source)
        for app in latestApps { rememberUserActiveApplication(app) }
    }

    func rememberUserActiveApplication(_ app: DesktopItem) {
        guard let (pid, process) = currentFrontmost, app.owner == pid,
              app.identity == process, let application = app.application,
              pid != ownPID, !ownership.owns(pid, process) else { return }
        // PID 재사용은 시작 시각으로 구분하고, 확인한 앱 identity를 끝까지 보존한다.
        userActiveApplications.insert(application)
    }

    func sampleWindows(_ snapshot: Set<DesktopItem>, at time: TimeInterval) {
        latestWindows = snapshot
        guard !initialWindowsConfirmed else { return }
        guard let first = initialWindowSampleTime else {
            initialWindows = snapshot
            initialWindowSampleTime = time
            return
        }
        guard time - first >= 1 else { return }
        let standard = snapshot.filter { $0.isVisibleStandardWindow }
        stableInitialWindows = initialWindows.filter { $0.isVisibleStandardWindow && standard.contains($0) }
        initialWindowsConfirmed = true
    }

    func sample() {
        let now = ProcessInfo.processInfo.systemUptime
        if let previous = lastSample { maxGapMS = max(maxGapMS, (now - previous) * 1000) }
        lastSample = now
        guard let owned = updateTree() else { return }
        let front = frontmostApplicationPID()
        sampleFrontmost(front)
        keyOwner.sample(focusedWindowOwner(frontmost: front))
        if let event = CGEvent(source: nil) {
            let cursor = event.location
            let flags = CGEventSource.flagsState(.combinedSessionState).rawValue
            if let previous = lastCursor, cursor != previous { counters["cursor_changes", default: 0] += 1 }
            if let previous = lastFlags, flags != previous { counters["modifier_changes", default: 0] += 1 }
            if firstCursor == nil { firstCursor = cursor; firstFlags = flags }
            lastCursor = cursor; lastFlags = flags
        } else {
            notes.insert("cursor and modifier state unreadable")
        }
        guard let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements],
            kCGNullWindowID) as? [[String: Any]],
              let allWindows = CGWindowListCopyWindowInfo([.optionAll, .excludeDesktopElements],
            kCGNullWindowID) as? [[String: Any]] else {
            incompleteReasons.insert("window enumeration unavailable")
            return
        }
        let windowSampleTime = ProcessInfo.processInfo.systemUptime
        let applications = sampleApplications(NSWorkspace.shared.runningApplications.map { app in
            ApplicationSample(owner: app.processIdentifier, terminated: app.isTerminated,
                activationPolicy: app.activationPolicy, readIdentity: {
                    applicationIdentity(bundleIdentifier: app.bundleIdentifier,
                        executablePath: app.executableURL?.path, name: app.localizedName)
                })
        })
        guard let snapshot = desktopWindows(allWindows, processes: currentProcesses, applications: applications),
              let onScreen = desktopWindows(windows, processes: currentProcesses, applications: applications) else {
            incompleteReasons.insert("window enumeration or application identity unavailable")
            return
        }
        sampleWindows(snapshot, at: windowSampleTime)
        let visible = visibleTestWindows(onScreen, owned: owned)
        visibleOwners.formUnion(visible.map { $0.owner })
        counters["max_visible_test_windows"] = max(counters["max_visible_test_windows"]!, visible.count)
        samples += 1
        acknowledgeSessions()
    }

    func summary() -> [String: Any] {
        counters["frontmost_changes"] = frontmost.changes
        counters["test_frontmost_samples"] = frontmostSources.filter { ownership.owns($0.0, $0.1) }.count
        counters["key_window_changes"] = keyOwner.changes
        counters["input_injections"] = eventSources.filter { ownership.owns($0.0, $0.1) }.count
        counters["modifier_injections"] = eventSources.filter { ownership.owns($0.0, $0.1) && $0.2 }.count
        // 시작 시 regular였던 앱 identity만 창 손실의 실패 대상으로 고정한다.
        let regularApplications = Set(initialApps.compactMap { $0.application })
        let activeApplications = regularApplications.intersection(userActiveApplications)
        let activeApps = initialApps.filter {
            $0.application.map { activeApplications.contains($0) } ?? false
        }
        let appsLost = unrelatedLosses(initialApps.subtracting(activeApps), latestApps,
                                       ownership: ownership, observer: ownPID)
        let activeAppsLost = unrelatedLosses(activeApps, latestApps, ownership: ownership, observer: ownPID)
        let regularWindows = initialWindows.filter {
            $0.application.map { regularApplications.contains($0) } ?? false
        }
        let activeWindows = regularWindows.filter {
            $0.application.map { activeApplications.contains($0) } ?? false
        }
        let inactiveWindows = regularWindows.subtracting(activeWindows)
        let durableWindows = inactiveWindows.intersection(stableInitialWindows)
        // 시작 창은 이후 숨겨져도 생존한다. 재실행·교체된 표준 창도 앱별 개수에 포함한다.
        let latestDurableWindows = latestWindows.filter {
            stableInitialWindows.contains($0) || (!initialWindows.contains($0) && $0.isStandardWindow)
        }
        let windowsLost = unrelatedLosses(durableWindows, latestDurableWindows,
                                          ownership: ownership, observer: ownPID)
        let transientWindowsLost = unrelatedLosses(inactiveWindows.subtracting(durableWindows),
            latestWindows.subtracting(latestDurableWindows), ownership: ownership, observer: ownPID,
            includingOtherLayers: true)
        let activeWindowsLost = unrelatedLosses(activeWindows, latestWindows,
                                                ownership: ownership, observer: ownPID)
        let helperAppsLost = unrelatedLosses(initialHelperApps, latestApps, ownership: ownership, observer: ownPID)
        let helperWindowsLost = unrelatedLosses(initialWindows.subtracting(regularWindows), latestWindows,
                                                ownership: ownership, observer: ownPID)
        counters["unrelated_apps_lost"] = appsLost.reduce(0) { $0 + $1.count }
        counters["unrelated_windows_lost"] = windowsLost.reduce(0) { $0 + $1.count }
        counters["transient_windows_lost"] = transientWindowsLost.reduce(0) { $0 + $1.count }
        counters["user_active_apps_lost"] = activeAppsLost.reduce(0) { $0 + $1.count }
        counters["user_active_windows_lost"] = activeWindowsLost.reduce(0) { $0 + $1.count }
        counters["non_regular_apps_lost"] = helperAppsLost.reduce(0) { $0 + $1.count }
        counters["non_regular_windows_lost"] = helperWindowsLost.reduce(0) { $0 + $1.count }
        if !ownership.complete {
            incompleteReasons.insert("test process group unavailable")
        }
        if ownership.processListReadGaps > 0 {
            incompleteReasons.insert("process list enumeration unavailable")
        }
        if ownership.processGroupReadGaps > 0 {
            incompleteReasons.insert("test process group enumeration unavailable")
        }
        if samples < 2 {
            incompleteReasons.insert("sampling coverage incomplete")
        }
        if keyOwner.readGaps > 0 { notes.insert("AX focused window owner unavailable in some samples; optional detail") }
        if keyOwner.changes > 0 { notes.insert("AX focused window owner changed; optional detail") }
        if frontmost.readGaps > 0 || frontmost.validSamples < 2 {
            incompleteReasons.insert("LaunchServices frontmost application sampling incomplete")
        }
        var result: [String: Any] = counters
        result["unrelated_apps_lost_details"] = appsLost.map { $0.json }
        result["unrelated_windows_lost_details"] = windowsLost.map { $0.windowJSON }
        result["transient_windows_lost_details"] = transientWindowsLost.map { $0.windowJSON }
        result["user_active_apps_lost_details"] = activeAppsLost.map { $0.json }
        result["user_active_windows_lost_details"] = activeWindowsLost.map { $0.json }
        result["non_regular_apps_lost_details"] = helperAppsLost.map { $0.json }
        result["non_regular_windows_lost_details"] = helperWindowsLost.map { $0.json }
        result["schema_version"] = 1
        result["sample_count"] = samples
        result["interval_ms"] = 100
        result["max_sample_gap_ms"] = maxGapMS
        result["observation_complete"] = incompleteReasons.isEmpty
        result["ownership_complete"] = ownership.complete
        result["ownership_read_gaps"] = ownership.readGaps
        result["process_list_read_gaps"] = ownership.processListReadGaps
        result["process_group_read_gaps"] = ownership.processGroupReadGaps
        result["application_identity_unreadable_pids"] = unreadableApplicationPIDs.sorted()
        result["incomplete_reasons"] = incompleteReasons.sorted()
        result["notes"] = notes.sorted()
        result["accessibility_available"] = AXIsProcessTrusted()
        result["event_tap_available"] = tap != nil
        result["frontmost_pid"] = frontmost.first ?? 0
        result["frontmost_source"] = "launchservices"
        result["frontmost_read_gaps"] = frontmost.readGaps
        result["frontmost_sample_count"] = frontmost.validSamples
        result["key_window_owner_pid"] = keyOwner.first ?? 0
        result["key_window_read_gaps"] = keyOwner.readGaps
        result["cursor_start"] = ["x": firstCursor?.x ?? 0, "y": firstCursor?.y ?? 0]
        result["modifier_flags_start"] = firstFlags ?? 0
        result["visible_test_window_owners"] = visibleOwners.sorted()
        result["observed_test_pids"] = ownership.known.keys.sorted()
        result["observed_test_root_pids"] = ownership.confirmedRoots.sorted()
        result["observed_test_sessions"] = ownership.sessions.map { $0.id }.sorted()
        result["observed_test_process_groups"] = ownership.enumeratedGroups.sorted()
        return result
    }
}

// GUI 없는 tooling fixture는 같은 소유권·창 목록 판정 함수를 직접 컴파일한다.
#if !YEE_BACKGROUND_OBSERVER_LIBRARY
@main
struct BackgroundObserverMain {
    static func main() {
        var options: [String: String] = [:]
        for argument in CommandLine.arguments.dropFirst() {
            let parts = argument.split(separator: "=", maxSplits: 1, omittingEmptySubsequences: false)
            guard parts.count == 2, options[String(parts[0])] == nil else { exit(2) }
            options[String(parts[0])] = String(parts[1])
        }
        guard options.count == 5, let runner = pid_t(options["--runner-pid"] ?? ""), runner > 0,
              let sessions = options["--sessions-file"], !sessions.isEmpty,
              let output = options["--output"], !output.isEmpty,
              let ready = options["--ready-file"], !ready.isEmpty,
              let stop = options["--stop-file"], !stop.isEmpty else { exit(2) }
        let observation = Observation(sessionsFile: sessions)
        observation.installEventTap()
        observation.sample()
        do {
            // 첫 표본과 1초 이상 떨어진 시작 표본을 확보한 뒤 실행기를 준비시킨다.
            while !observation.initialWindowsConfirmed && observation.incompleteReasons.isEmpty
                && !FileManager.default.fileExists(atPath: stop) && kill(runner, 0) == 0 {
                RunLoop.current.run(until: Date().addingTimeInterval(0.1))
                observation.sample()
            }
            try Data().write(to: URL(fileURLWithPath: ready), options: .atomic)
            while !FileManager.default.fileExists(atPath: stop) && kill(runner, 0) == 0 {
                RunLoop.current.run(until: Date().addingTimeInterval(0.1))
                observation.sample()
            }
            observation.sample()
            let data = try JSONSerialization.data(withJSONObject: observation.summary(), options: [.sortedKeys, .prettyPrinted])
            try data.write(to: URL(fileURLWithPath: output), options: .atomic)
        } catch {
            fputs("background observation 저장 실패: \(error)\n", stderr)
            exit(1)
        }
    }
}
#endif
