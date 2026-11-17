// Called under Tally's existing lock. Stores already assembled notes only; performs no guest reads.
struct RecentTerminalSignals {
    private static let marker = "\n  [terminal note truncated]\n"
    private let capacity: Int
    private let byteLimit: Int
    private(set) var notes: [String] = []
    private(set) var observed: UInt64 = 0
    private(set) var dropped: UInt64 = 0
    private(set) var truncated: UInt64 = 0

    init(capacity: Int = 64, byteLimit: Int = 8192) {
        precondition(capacity > 0 && byteLimit >= Self.marker.utf8.count)
        self.capacity = capacity
        self.byteLimit = byteLimit
    }

    static func isTerminalWait(result: Int64, status: UInt32?, failed: Bool) -> Bool {
        guard !failed, result > 0, let status else { return false }
        let signal = status & 0x7F
        return signal != 0 && signal != 0x7F // Exclude exited, stopped and continued wait statuses.
    }

    mutating func append(_ note: String) {
        observed &+= 1
        var retained = note
        if note.utf8.count > byteLimit {
            let available = byteLimit - Self.marker.utf8.count
            retained = String(decoding: note.utf8.prefix(available), as: UTF8.self)
            // Decoding a split UTF8 scalar may add a replacement character; keep the byte budget strict.
            while retained.utf8.count > available { retained.removeLast() }
            retained += Self.marker
            truncated &+= 1
        }
        if notes.count == capacity {
            notes.removeFirst()
            dropped &+= 1
        }
        notes.append(retained)
    }
}
