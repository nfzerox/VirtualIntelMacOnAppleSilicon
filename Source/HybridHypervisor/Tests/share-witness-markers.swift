// Headless RAM test; compile with Sources/Engine/ShareWitnessMarkers.swift. No VM.
import Foundation

@main struct CheckShareWitnessMarkers {
    static func main() {
        var checks = 0
        func check(_ value: @autoclosure () -> Bool, _ label: String) {
            precondition(value(), label); checks += 1
        }
        func line(_ size: Int) -> [UInt8] {
            let head = ShareWitnessMarkers.prefix + Array("begin ".utf8)
            precondition(size > head.count)
            return head + [UInt8](repeating: 65, count: size - head.count - 1) + [10]
        }
        func inspect(_ bytes: [UInt8], enabled: Bool = true, fd: UInt64 = 2,
                     count: UInt64? = nil, second: [UInt8]?? = nil) -> (ShareWitnessMarkers.Inspection, [Int]) {
            var requests: [Int] = []
            let value = ShareWitnessMarkers.inspect(enabled: enabled, fd: fd, count: count ?? UInt64(bytes.count)) { n in
                requests.append(n)
                if requests.count == 2, let override = second { return override }
                return Array(bytes.prefix(n))
            }
            return (value, requests)
        }
        for n in [240, 241, 512] {
            let (value, reads) = inspect(line(n))
            check(value.payload?.utf8.count == n - 1, "complete size \(n)")
            check(reads == (n <= 240 ? [n] : [240, n]), "bounded reads \(n)")
        }
        for enabled in [false, true] {
            let (value, reads) = inspect(line(513), enabled: enabled)
            check(reads == [240], "oversize no extra reads")
            check(value.matched == enabled && value.payload == nil, "oversize classification")
        }
        for n in [0, 1, 240, 241, 512, 513] {
            let bytes = n > 50 ? line(n) : [UInt8](repeating: 65, count: n)
            let (value, reads) = inspect(bytes, enabled: false)
            check(reads == [min(n, 240)] && !value.matched && value.payload == nil, "disabled unchanged")
        }
        let (huge, hugeReads) = inspect(line(512), count: UInt64.max)
        check(huge.matched && huge.payload == nil && hugeReads == [240], "UInt64 bound")
        for fd: UInt64 in [0, 1, 3, UInt64.max] {
            let (value, reads) = inspect(line(512), fd: fd)
            check(!value.matched && reads == [240], "fd exact")
        }
        for bytes in [Array("x LC_FCP_SHARE_WITNESS v=1 event=begin\n".utf8),
                      Array("LC_FCP_SHARE_WITNESS v=2 event=begin\n".utf8)] {
            check(!inspect(bytes).0.matched, "exact prefix")
        }
        var bads = [[UInt8]]()
        var bad = line(512); bad[40] = 0; bads.append(bad)
        bad = line(512); bad[40] = 10; bads.append(bad)
        bad = line(512); bad[40] = 255; bads.append(bad)
        bad = line(512); bad[511] = 65; bads.append(bad)
        bad = line(512); bad[0] = 120; bads.append(bad)
        for bytes in bads {
            let (value, reads) = inspect(line(512), second: .some(bytes))
            check(value.matched && value.payload == nil && reads == [240, 512], "bad second read rejected")
        }
        let (failedRead, failedReads) = inspect(line(512), second: .some(nil))
        check(failedRead.matched && failedRead.payload == nil && failedReads == [240,512], "unreadable second")
        check(inspect(line(512), second: .some(line(241))).0.payload == nil, "short second")
        let noFirst = ShareWitnessMarkers.inspect(enabled: true, fd: 2, count: 512) { _ in nil }
        check(!noFirst.matched, "unreadable first cannot classify")
        for index in [40, 239] {
            var bytes = line(240); bytes[index] = 0
            check(inspect(bytes).0.payload == nil, "invalid short single-read record")
        }
        var ring = ShareWitnessMarkers()
        // The ordinary first-2000 list is already full. Independent storage must still retain.
        var ordinary = Array(repeating: "old", count: 2000)
        for i in 0..<65 {
            let value = inspect(line(240)).0
            if ordinary.count < 2000 { ordinary.append("unexpected") }
            ring.append(value, hostTicks: UInt64(1000+i), elapsedTicks: UInt64(i), cr3: 0x123000)
        }
        check(ordinary.count == 2000 && ring.records.count == 64, "late cap independent")
        check(ring.observed == 65 && ring.dropped == 1 && ring.rejected == 0, "overflow counters")
        check(ring.records.first?.hostTicks == 1001 && ring.records.last?.hostTicks == 1064, "newest64 ordered")
        ring.append(failedRead, hostTicks: 2000, elapsedTicks: 1000, cr3: 0)
        check(ring.observed == 66 && ring.rejected == 1 && ring.dropped == 1, "rejection accounting")
        ring.append(noFirst, hostTicks: 2000, elapsedTicks: 1000, cr3: 0)
        check(ring.observed == 66, "unmatched not counted")
        check(ring.records.allSatisfy { $0.payload.utf8.count <= 511 }, "payload bound")
        print("PASS ShareWitnessMarkers \(checks) actual-helper RAM checks")
    }
}
