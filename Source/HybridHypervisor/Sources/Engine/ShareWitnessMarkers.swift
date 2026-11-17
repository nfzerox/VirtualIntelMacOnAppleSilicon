// Pure bounded diagnostic storage. inspect performs exactly the existing <=240-byte
// read when disabled. Call append under Tally's existing lock; no guest reads there.
struct ShareWitnessMarkers {
    static let prefix = Array("LC_FCP_SHARE_WITNESS v=1 event=".utf8)
    struct Inspection {
        let ordinaryBytes: [UInt8]?
        let matched: Bool
        let payload: String?
    }
    struct Record {
        let hostTicks: UInt64
        let elapsedTicks: UInt64
        let cr3: UInt64
        let payload: String
    }
    private(set) var records: [Record] = []
    private(set) var observed: UInt64 = 0
    private(set) var dropped: UInt64 = 0
    private(set) var rejected: UInt64 = 0

    static func inspect(enabled: Bool, fd: UInt64, count: UInt64,
                        read: (Int) -> [UInt8]?) -> Inspection {
        let ordinary = read(Int(min(count, 240)))
        guard enabled, fd == 2, ordinary?.starts(with: prefix) == true else {
            return Inspection(ordinaryBytes: ordinary, matched: false, payload: nil)
        }
        let refused = Inspection(ordinaryBytes: ordinary, matched: true, payload: nil)
        guard count <= 512, count > UInt64(prefix.count) else { return refused }
        let complete = count <= 240 ? ordinary : read(Int(count))
        guard let bytes = complete, bytes.count == Int(count), bytes.starts(with: prefix),
              bytes.last == 10,
              bytes.dropLast().allSatisfy({ $0 >= 32 && $0 <= 126 }) else { return refused }
        return Inspection(ordinaryBytes: ordinary, matched: true,
                          payload: String(decoding: bytes.dropLast(), as: UTF8.self))
    }

    mutating func append(_ inspection: Inspection, hostTicks: UInt64,
                         elapsedTicks: UInt64, cr3: UInt64) {
        guard inspection.matched else { return }
        observed &+= 1
        guard let payload = inspection.payload else { rejected &+= 1; return }
        if records.count == 64 { records.removeFirst(); dropped &+= 1 }
        records.append(Record(hostTicks: hostTicks, elapsedTicks: elapsedTicks,
                              cr3: cr3, payload: payload))
    }
}
