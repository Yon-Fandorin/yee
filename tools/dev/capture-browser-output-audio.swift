// Process-scoped browser output capture. No microphone or global system mix.
import CoreAudio
import Foundation

func checked(_ status: OSStatus, _ operation: String) throws {
    if status != noErr { throw NSError(domain: operation, code: Int(status)) }
}
func address(_ selector: AudioObjectPropertySelector) -> AudioObjectPropertyAddress {
    AudioObjectPropertyAddress(mSelector: selector, mScope: kAudioObjectPropertyScopeGlobal,
                               mElement: kAudioObjectPropertyElementMain)
}
func descendantPIDs(_ root: Int32) throws -> Set<Int32> {
    let inventory = Process(); inventory.executableURL = URL(fileURLWithPath: "/bin/ps")
    inventory.arguments = ["-axo", "pid=,ppid="]
    let pipe = Pipe(); inventory.standardOutput = pipe
    try inventory.run()
    let data = pipe.fileHandleForReading.readDataToEndOfFile(); inventory.waitUntilExit()
    guard inventory.terminationStatus == 0 else { throw NSError(domain: "ProcessInventory", code: 1) }
    var parents: [Int32: Int32] = [:]
    for line in String(decoding: data, as: UTF8.self).split(separator: "\n") {
        let fields = line.split(whereSeparator: { $0.isWhitespace })
        if fields.count == 2, let child = Int32(fields[0]), let parent = Int32(fields[1]) {
            parents[child] = parent
        }
    }
    return Set(parents.keys.filter { candidate in
        var current = candidate
        for _ in 0..<50 {
            if current == root { return true }
            guard let parent = parents[current], parent > 0, parent != current else { break }
            current = parent
        }
        return false
    }).union([root])
}
func audioProcesses(_ root: Int32) throws -> [(AudioObjectID, Int32)] {
    let descendants = try descendantPIDs(root)
    var listAddress = address(kAudioHardwarePropertyProcessObjectList), size: UInt32 = 0
    try checked(AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject),
                &listAddress, 0, nil, &size), "AudioProcessListSize")
    var objects = [AudioObjectID](repeating: 0, count: Int(size) / MemoryLayout<AudioObjectID>.size)
    try checked(AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject),
                &listAddress, 0, nil, &size, &objects), "AudioProcessList")
    return objects.compactMap { object in
        var pid: Int32 = 0, pidSize = UInt32(MemoryLayout<Int32>.size)
        var pidAddress = address(kAudioProcessPropertyPID)
        guard AudioObjectGetPropertyData(object, &pidAddress, 0, nil, &pidSize, &pid) == noErr,
              descendants.contains(pid) else { return nil }
        return (object, pid)
    }
}

final class OutputRecorder {
    let energy: FileHandle, excerpt: FileHandle
    let stateURL: URL
    let rate: Double
    var samples = 0, sumSquares = 0.0, peak = 0.0, second = -1, excerptFrames = 0
    init(_ directory: URL, rate: Double) throws {
        self.rate = rate
        stateURL = directory.appendingPathComponent("audio-state.json")
        let energyURL = directory.appendingPathComponent("output-audio.jsonl")
        let excerptURL = directory.appendingPathComponent("advertisement-output.s16le")
        FileManager.default.createFile(atPath: energyURL.path, contents: nil)
        FileManager.default.createFile(atPath: excerptURL.path, contents: nil)
        energy = try FileHandle(forWritingTo: energyURL); excerpt = try FileHandle(forWritingTo: excerptURL)
    }
    func flush() {
        guard samples > 0 else { return }
        let row: [String: Any] = ["unixSecond": second, "samples": samples,
                                 "rms": sqrt(sumSquares / Double(samples)), "peak": peak]
        if let data = try? JSONSerialization.data(withJSONObject: row, options: [.sortedKeys]) {
            energy.write(data); energy.write(Data([10]))
        }
        samples = 0; sumSquares = 0; peak = 0
    }
    func consume(_ data: Data, timestamp: Int, channels: Int) {
        if timestamp != second { flush(); second = timestamp }
        data.withUnsafeBytes { bytes in
            let values = bytes.bindMemory(to: Float.self)
            for value in values where value.isFinite {
                let value = Double(value); sumSquares += value * value
                peak = max(peak, abs(value)); samples += 1
            }
            let limit = Int(90 * rate)
            guard excerptFrames < limit,
                  let stateData = try? Data(contentsOf: stateURL),
                  let state = try? JSONSerialization.jsonObject(with: stateData) as? [String: Any],
                  state["ad"] as? Bool == true else { return }
            let count = min(values.count / channels, limit - excerptFrames)
            var pcm = [Int16](); pcm.reserveCapacity(count)
            for i in 0..<count {
                let value = Double(values[i * channels])
                pcm.append(Int16(max(-32767, min(32767, (value.isFinite ? value : 0) * 32767))).littleEndian)
            }
            pcm.withUnsafeBytes { excerpt.write(Data($0)) }; excerptFrames += count
        }
    }
}

@main struct CaptureBrowserAudio {
    static func main() async {
        let args = CommandLine.arguments
        guard args.count == 4, let pid = Int32(args[1]), pid > 0,
              let seconds = Double(args[3]), seconds > 0, seconds <= 3600 else {
            fputs("Usage: capture-browser-output-audio PID OUTPUT_DIRECTORY SECONDS\n", stderr); exit(2)
        }
        do {
            let directory = URL(fileURLWithPath: args[2], isDirectory: true)
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            var processes: [(AudioObjectID, Int32)] = []
            for _ in 0..<20 {
                if FileManager.default.fileExists(atPath: directory.appendingPathComponent("audio-stop").path) {
                    throw NSError(domain: "BrowserAudioCaptureStoppedBeforeStartup", code: 1)
                }
                processes = try audioProcesses(pid)
                if !processes.isEmpty { break }
                try await Task.sleep(for: .seconds(1))
            }
            guard !processes.isEmpty else { throw NSError(domain: "BrowserAudioProcesses", code: 1) }
            let description = CATapDescription(monoMixdownOfProcesses: processes.map { $0.0 })
            description.name = "Yee browser output validation"
            description.isPrivate = true
            description.isProcessRestoreEnabled = false
            description.muteBehavior = .unmuted
            var tap: AudioObjectID = 0
            try checked(AudioHardwareCreateProcessTap(description, &tap), "CreateBrowserProcessTap")
            defer { AudioHardwareDestroyProcessTap(tap) }
            var format = AudioStreamBasicDescription(), formatAddress = address(kAudioTapPropertyFormat)
            var formatSize = UInt32(MemoryLayout<AudioStreamBasicDescription>.size)
            try checked(AudioObjectGetPropertyData(tap, &formatAddress, 0, nil, &formatSize, &format), "TapFormat")
            guard format.mFormatFlags & kAudioFormatFlagIsFloat != 0, format.mBitsPerChannel == 32 else {
                throw NSError(domain: "UnsupportedTapFormat", code: 1)
            }
            let configuration: [String: Any] = [
                kAudioAggregateDeviceNameKey: "Yee output validation",
                kAudioAggregateDeviceUIDKey: UUID().uuidString,
                kAudioAggregateDeviceIsPrivateKey: true,
                kAudioAggregateDeviceTapAutoStartKey: true,
                kAudioAggregateDeviceTapListKey: [[kAudioSubTapUIDKey: description.uuid.uuidString,
                                                  kAudioSubTapDriftCompensationKey: true]]]
            var device: AudioObjectID = 0
            try checked(AudioHardwareCreateAggregateDevice(configuration as CFDictionary, &device), "CreatePrivateTapDevice")
            defer { AudioHardwareDestroyAggregateDevice(device) }
            let recorder = try OutputRecorder(directory, rate: format.mSampleRate)
            let queue = DispatchQueue(label: "yee.output-audio-write")
            var io: AudioDeviceIOProcID?
            try checked(AudioDeviceCreateIOProcIDWithBlock(&io, device, nil, { _, input, _, _, _ in
                let buffers = UnsafeMutableAudioBufferListPointer(UnsafeMutablePointer(mutating: input))
                guard let first = buffers.first, let pointer = first.mData, first.mDataByteSize > 0 else { return }
                let data = Data(bytes: pointer, count: Int(first.mDataByteSize))
                let timestamp = Int(Date().timeIntervalSince1970)
                let channels = max(1, Int(first.mNumberChannels))
                queue.async { recorder.consume(data, timestamp: timestamp, channels: channels) }
            }), "CreateTapIOProc")
            guard let activeIO = io else { throw NSError(domain: "MissingTapIOProc", code: 1) }
            defer { AudioDeviceDestroyIOProcID(device, activeIO) }
            try checked(AudioDeviceStart(device, activeIO), "StartBrowserOutputCapture")
            print("Browser output tap ready for PID \(pid); audio PIDs \(processes.map { $0.1 })")
            let deadline = Date().addingTimeInterval(seconds)
            while Date() < deadline && !FileManager.default.fileExists(
                atPath: directory.appendingPathComponent("audio-stop").path) {
                try await Task.sleep(for: .seconds(1))
            }
            try checked(AudioDeviceStop(device, activeIO), "StopBrowserOutputCapture")
            queue.sync { recorder.flush() }
            try recorder.energy.close(); try recorder.excerpt.close()
            let report: [String: Any] = ["pid": pid, "selectedProcessIDs": processes.map { $0.1 },
                "method": "CoreAudio-process-tap", "scope": "selected-browser-process-tree-output",
                "microphone": false, "sampleRate": format.mSampleRate, "channels": 1,
                "excerptFrames": recorder.excerptFrames, "error": NSNull()]
            try JSONSerialization.data(withJSONObject: report, options: [.sortedKeys, .prettyPrinted])
                .write(to: directory.appendingPathComponent("output-audio-summary.json"))
        } catch {
            fputs("Browser output audio capture failed: \(error)\n", stderr); exit(1)
        }
    }
}
