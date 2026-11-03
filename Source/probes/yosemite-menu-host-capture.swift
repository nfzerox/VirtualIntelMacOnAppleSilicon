// Diagnostic only: count black menu-bar strips on the actual AppKit window.
// Read no other window. Save only a small menu crop when explicitly requested.
import AppKit
import CoreGraphics
import CoreImage
import CoreMedia
import CoreVideo
import Foundation
import ScreenCaptureKit

private final class MenuFrames: NSObject, SCStreamOutput, @unchecked Sendable {
    private let lock = NSLock()
    private var frames = 0
    private var blackFrames = 0
    private var transitionFrames = 0
    private var peakRows = 0
    private var selectionPositions = Set<Int>()
    private let spaces: Bool
    private let cropDirectory: URL?
    private lazy var cropContext = CIContext()
    private var nextCrop = 0.0
    private var crops = 0

    init(spaces: Bool, cropDirectory: URL?) {
        self.spaces = spaces
        self.cropDirectory = cropDirectory
    }

    func stream(_ stream: SCStream, didOutputSampleBuffer sample: CMSampleBuffer, of type: SCStreamOutputType) {
        guard type == .screen, let buffer = sample.imageBuffer else { return }
        if let cropDirectory {
            // Opt-in evidence from just the upper-left of the selected VM.
            // Bound image writes and keep them out of the counting-only path.
            let time = ProcessInfo.processInfo.systemUptime
            if crops < 120 && time >= nextCrop {
                nextCrop = time + 1.0/15
                let frame = CIImage(cvPixelBuffer: buffer)
                let area = CGRect(x: 0, y: max(0,frame.extent.height-300),
                                  width: min(480,frame.extent.width), height: min(300,frame.extent.height))
                let crop = frame.cropped(to: area).transformed(by: CGAffineTransform(translationX: 0, y: -area.minY))
                let output = cropDirectory.appendingPathComponent(String(format:"%04d.png",crops))
                try? cropContext.writePNGRepresentation(of: crop, to: output, format: .RGBA8,
                    colorSpace: CGColorSpace(name: CGColorSpace.sRGB)!)
                crops += 1
            }
            return
        }
        guard
              CVPixelBufferGetPixelFormatType(buffer) == kCVPixelFormatType_32BGRA,
              CVPixelBufferGetWidth(buffer) >= 640, CVPixelBufferGetHeight(buffer) >= 152,
              CVPixelBufferLockBaseAddress(buffer, .readOnly) == kCVReturnSuccess else { return }
        defer { CVPixelBufferUnlockBaseAddress(buffer, .readOnly) }
        guard let base = CVPixelBufferGetBaseAddress(buffer)?.assumingMemoryBound(to: UInt8.self) else { return }
        let stride = CVPixelBufferGetBytesPerRow(buffer)
        var run = 0, longest = 0
        var thinMenuStrip = false
        var selected = Set<Int>()
        func opaqueBlack(_ x: Int, _ y: Int) -> Bool {
            let p = base + y * stride + 4 * x
            return p[0] < 18 && p[1] < 18 && p[2] < 18 && p[3] > 235
        }
        let width = CVPixelBufferGetWidth(buffer)
        let height = CVPixelBufferGetHeight(buffer)
        let right = 450..<min(850, width - 12)
        // A black vertical seam between Spaces is expected, but proves
        // that the keyboard workload actually triggered a transition.
        let bodyRows = [height / 5, height / 3, height / 2, height * 2 / 3, height * 4 / 5]
        // A fully black startup/reconfiguration frame is not a Spaces gap.
        let hasDesktop = [width / 6, width / 2, width * 5 / 6].contains { x in
            bodyRows.filter { !opaqueBlack(x, $0) }.count >= 3
        }
        let transition = spaces && hasDesktop && Swift.stride(from: 20, to: width - 20, by: 8).contains { x in
            bodyRows.allSatisfy { opaqueBlack(x, $0) && opaqueBlack(x + 7, $0) }
        }
        for y in 0..<120 {
            let row = base + y * stride
            func black(_ range: Range<Int>) -> Int {
                range.reduce(0) { n, x in
                    let p = row + 4 * x
                    return n + (p[0] < 18 && p[1] < 18 && p[2] < 18 && p[3] > 235 ? 1 : 0)
                }
            }
            let leftDark = spaces
                ? black(8..<52) > 27 && (8..<52).filter { !opaqueBlack($0, y + 22) }.count > 32
                : black(12..<360) > 255
            if leftDark && black(right) < 50 {
                run += 1
                longest = max(longest, run)
            } else {
                if (8...26).contains(run) { thinMenuStrip = true }
                run = 0
            }
            var blueRun = 0
            for x in 8..<390 {
                let p = row + 4 * x
                if Int(p[0]) - Int(p[2]) > 70 && p[0] > 170 && p[3] > 235 {
                    blueRun += 1
                    if blueRun == 20 { selected.insert((x - 19) / 10) }
                } else { blueRun = 0 }
            }
        }
        lock.lock()
        frames += 1
        if transition { transitionFrames += 1 }
        selectionPositions.formUnion(selected)
        peakRows = max(peakRows, longest)
        // Spaces has an intentional full-height black gap between desktops.
        // A thin run matching the menu-bar height is the artifact of interest.
        if spaces ? thinMenuStrip || (8...26).contains(run) : longest >= 8 {
            blackFrames += 1
            print("black-frame=\(frames) black_rows=\(longest)")
        }
        lock.unlock()
    }

    func report() {
        lock.lock()
        print("host-menu-flash frames=\(frames) transition_frames=\(transitionFrames) black_frames=\(blackFrames) peak_black_rows=\(peakRows) blue_left_bins=\(selectionPositions.sorted())")
        lock.unlock()
    }
}

@main struct Main {
    // Drive the actual app's keyboard path; there must already be two
    // desktops and the shortcuts enabled in the guest.
    private static func switchSpaces(in directory: URL) async throws {
        let request = directory.appendingPathComponent("Request.json")
        let running = directory.appendingPathComponent("Running.json")
        for index in 0..<6 {
            let deadline = ProcessInfo.processInfo.systemUptime + 5
            while [request, running].contains(where: { FileManager.default.fileExists(atPath: $0.path) }) {
                guard ProcessInfo.processInfo.systemUptime < deadline else {
                    throw NSError(domain: "The guest diagnostics channel is busy", code: 3)
                }
                try await Task.sleep(nanoseconds: 100_000_000)
            }
            let arrow = index.isMultiple(of: 2) ? "right" : "left"
            let keys = [("ctrl", true), (arrow, true), (arrow, false), ("ctrl", false)]
            let events = keys.map { key, down in
                ["type": "key", "data": ["down": down, "key": ["type": "qcode", "data": key]]] as [String: Any]
            }
            try JSONSerialization.data(withJSONObject: ["Action": "input", "Events": events])
                .write(to: request, options: .atomic)
            try await Task.sleep(nanoseconds: 1_200_000_000)
        }
    }

    @MainActor static func main() async {
        do { try await run() }
        catch {
            FileHandle.standardError.write(Data("\(error)\n".utf8))
            exit(1)
        }
    }

    @MainActor private static func run() async throws {
        _ = NSApplication.shared
        let args = CommandLine.arguments
        let spaces = (3...4).contains(args.count) && args[2] == "--spaces"
        let crops = args.count == 5 && args[2] == "--menu-crops"
        guard (args.count == 2 || spaces || crops),
              let pid = Int32(args[1]),
              CGPreflightScreenCaptureAccess() else {
            throw NSError(domain: "Provide the app PID and existing Screen Recording access", code: 1)
        }
        let title = crops ? args[3] : "Yosemite"
        let cropDirectory = crops ? URL(fileURLWithPath: args[4],isDirectory:true) : nil
        if let cropDirectory { try FileManager.default.createDirectory(at: cropDirectory,withIntermediateDirectories:false) }
        let content = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: true)
        guard let window = content.windows.first(where: {
            $0.owningApplication?.processID == pid && $0.title == title && $0.frame.width >= 640
        }) else { throw NSError(domain: "The requested visible VM window was not found", code: 2) }
        let config = SCStreamConfiguration()
        config.width = Int(window.frame.width)
        config.height = Int(window.frame.height)
        config.showsCursor = false
        config.ignoreShadowsSingleWindow = true
        config.pixelFormat = kCVPixelFormatType_32BGRA
        config.minimumFrameInterval = CMTime(value: 1, timescale: 60)
        let output = MenuFrames(spaces: spaces,cropDirectory: cropDirectory)
        let stream = SCStream(filter: SCContentFilter(desktopIndependentWindow: window), configuration: config, delegate: nil)
        try stream.addStreamOutput(output, type: .screen, sampleHandlerQueue: DispatchQueue(label: "org.vim.menu-capture"))
        try await stream.startCapture()
        FileHandle.standardOutput.write(Data("READY window=\(window.windowID)\n".utf8))
        do {
            if spaces && args.count == 4 {
                try await switchSpaces(in: URL(fileURLWithPath: args[3], isDirectory: true))
                try await Task.sleep(nanoseconds: 1_000_000_000)
            } else {
                try await Task.sleep(nanoseconds: 12_000_000_000)
            }
        } catch {
            try? await stream.stopCapture()
            output.report()
            throw error
        }
        try await stream.stopCapture()
        if !crops { output.report() }
    }
}
