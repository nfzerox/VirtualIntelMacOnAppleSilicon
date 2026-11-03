// Drive the real AppKit VM window, including its normal absolute input path.
// Usage: swift opengl-menu-workload.swift PID WINDOW MENU_Y FILE_X EDIT_X VIEW_X
// Coordinates are AppKit window-relative points measured from a host capture.
import AppKit
import CoreGraphics
import Foundation

let args = CommandLine.arguments
guard args.count == 7, let pid = Int32(args[1]), let windowID = Int(args[2]),
      let y = Double(args[3]), let file = Double(args[4]), let edit = Double(args[5]),
      let view = Double(args[6]), let app = NSRunningApplication(processIdentifier: pid),
      CGPreflightPostEventAccess() else { exit(64) }

func windows() -> [[String: Any]] {
    CGWindowListCopyWindowInfo(.optionOnScreenOnly, kCGNullWindowID) as? [[String: Any]] ?? []
}
func bounds(_ window: [String: Any]) -> CGRect? {
    (window["kCGWindowBounds"] as? NSDictionary).flatMap {
        CGRect(dictionaryRepresentation: $0 as CFDictionary)
    }
}
func owns(_ window: [String: Any]) -> Bool {
    window["kCGWindowOwnerPID"] as? Int == Int(pid) &&
    window["kCGWindowNumber"] as? Int == windowID
}
guard let frame = windows().first(where: owns).flatMap(bounds),
      [y, file, edit, view].allSatisfy(\.isFinite),
      y >= 0, y < frame.height,
      [file, edit, view].allSatisfy({ $0 >= 0 && $0 < frame.width }) else { exit(65) }
let points = [file, edit, view].map { CGPoint(x: frame.minX + $0, y: frame.minY + y) }
app.activate(options: [])
usleep(300_000)
guard points.allSatisfy({ point in
    windows().first(where: {
        $0["kCGWindowLayer"] as? Int == 0 && bounds($0)?.contains(point) == true
    }).map(owns) == true
}) else { exit(66) }

func post(_ type: CGEventType, at point: CGPoint, wait: useconds_t = 55_000) {
    CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: point,
            mouseButton: .left)?.post(tap: .cghidEventTap)
    usleep(wait)
}
func mark(_ phase: String) {
    print("\(ProcessInfo.processInfo.systemUptime) \(phase)")
    fflush(stdout)
}

let start = points[0]
mark("File held")
post(.mouseMoved, at: start)
post(.leftMouseDown, at: start)
post(.leftMouseUp, at: start, wait: 900_000)
mark("File/Edit/View hover")
for _ in 0..<6 {
    for point in [points[1], points[2], points[1], start] { post(.mouseMoved, at: point) }
}
post(.mouseMoved, at: start, wait: 900_000)
post(.leftMouseDown, at: start)
post(.leftMouseUp, at: start)
mark("File/Edit/View click-drag")
post(.leftMouseDown, at: start, wait: 220_000)
for _ in 0..<6 {
    for point in [points[1], points[2], points[1], start] { post(.leftMouseDragged, at: point) }
}
post(.leftMouseUp, at: start, wait: 650_000)
mark("finished")
