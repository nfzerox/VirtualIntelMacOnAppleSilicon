// Pure headless Swift test; compile with Sources/Engine/RecentTerminalSignals.swift. No Engine/HVF/guest.
@main struct CheckRecentTerminalSignals {
    static func main() {
        let accepted: [(Int64, UInt32?, Bool)] = [(42, 11, false), (42, 0x8B, false), (42, 6, false)]
        let rejected: [(Int64, UInt32?, Bool)] = [(0, 11, false), (-1, 11, false), (42, nil, false),
            (42, 11, true), (42, 0, false), (42, 0x0B00, false), (42, 0x137F, false), (42, 0xFFFF, false)]
        for (result, status, failed) in accepted {
            precondition(RecentTerminalSignals.isTerminalWait(result: result, status: status, failed: failed))
        }
        for (result, status, failed) in rejected {
            precondition(!RecentTerminalSignals.isTerminalWait(result: result, status: status, failed: failed))
        }
        // Ordinary prefix saturation must not consume the independent fatal-note capacity.
        var prefix = (0..<2000).map { "ordinary process event \($0)" }
        let before = prefix
        var signals = RecentTerminalSignals(capacity: 2, byteLimit: 64)
        let late = "proc 300 cr3 0x123 wait4 returned42 status0xb SIGNAL11"
        if prefix.count < 2000 { prefix.append(late) }
        signals.append(late)
        precondition(prefix == before && signals.notes == [late])
        signals.append("signal6")
        signals.append("late signal11")
        precondition(signals.notes == ["signal6", "late signal11"])
        precondition(signals.observed == 3 && signals.dropped == 1 && signals.truncated == 0)
        signals.append(String(repeating: "🌊", count: 100))
        precondition(signals.notes.last!.utf8.count <= 64)
        precondition(signals.notes.last!.contains("[terminal note truncated]"))
        precondition(signals.observed == 4 && signals.dropped == 2 && signals.truncated == 1)
        var full = RecentTerminalSignals()
        for n in 0..<70 { full.append("signal11 #\(n)") }
        precondition(full.notes.count == 64 && full.notes.first == "signal11 #6" && full.notes.last == "signal11 #69")
        precondition(full.observed == 70 && full.dropped == 6)
        print("PASS terminal wait classification, late retention after2000 ordinary notes, bounded newest ring and UTF8 truncation")
    }
}
