import CEngine
import Foundation

/// Every reason execution left the engine for the host CPU emulator, including uncommon fallback paths. Written whole each time, to a
/// file that survives a guest wedging or the process being killed. The vCPU thread records, one other thread writes.
public final class Tally: @unchecked Sendable {
    private struct Row {
        var count: UInt64 = 0
        var hostTicks: UInt64 = 0
    }
    private struct Site {
        var count: UInt64 = 0
        var firstRIP: UInt64
        var detail: UInt64
    }

    private let engine: Engine
    private let path: String
    private let lock = NSLock()
    private let startTicks = mach_absolute_time()
    private var reasons: [String: Row] = [:]
    private var sites: [String: Site] = [:]
    private var mmioPages: [UInt64: UInt64] = [:]
    private var mmioSites: [String: UInt64] = [:]
    private let exclusiveStops = UnsafeMutablePointer<Int>.allocate(capacity: 1)
    private struct SharedFrame {
        var blocks: UInt64 = 0, spaces = Set<UInt64>(), low = UInt64.max, high: UInt64 = 0
    }
    private var coldByDirectory: [UInt64: SharedFrame] = [:], coldByTable: [UInt64: SharedFrame] = [:]
    private var ports: [UInt16: UInt64] = [:]
    private var boundaries: UInt64 = 0
    private var samples: [String] = []
    private var timeline: [String] = []
    private var firstRIP: UInt64?
    private var hardStop: String?
    private var lastReason: String?
    private var lastLeftEngine: UInt64 = 0
    private var fallbacks: UInt64 = 0
    private var notes: [String] = []
    private var portWrites: [String] = []
    private var processLog: [String] = []
    private var recentTerminalSignals = RecentTerminalSignals()
    private let shareMarkersEnabled: Bool
    private let shareMarkerTimebase: mach_timebase_info_data_t?
    private var shareMarkers = ShareWitnessMarkers()

    init(engine: Engine, path: String) {
        self.engine = engine
        self.path = path
        let enabled = ProcessInfo.processInfo.environment["LC_HVDBT_SHARE_MARKERS"] == "1"
        shareMarkersEnabled = enabled
        var timebase = mach_timebase_info_data_t()
        shareMarkerTimebase = enabled && mach_timebase_info(&timebase) == KERN_SUCCESS
            && timebase.numer != 0 && timebase.denom != 0 ? timebase : nil
    }

    func startWriting() {
        write()
        Thread.detachNewThread { [self] in
            var tick = 0
            while true {
                Thread.sleep(forTimeInterval: 1)
                engine.requestBoundary()  // one stop a second is a rip sample, and shows where a wedged guest sits
                tick += 1
                if tick % 2 == 0 {
                    addTimelineRow()
                    write()
                }
            }
        }
    }

    func entered(rip: UInt64) {
        lock.lock()
        if firstRIP == nil { firstRIP = rip }
        closeAway()
        lock.unlock()
    }

    private func closeAway() {
        if let lastReason {
            reasons[lastReason, default: Row()].hostTicks += mach_absolute_time() &- lastLeftEngine
            self.lastReason = nil
        }
    }

    /// Off the engine by choice: the counters below stop, though the guest may be running well on TCG.
    func refused(_ reason: String) {
        lock.lock()
        closeAway()
        reasons[reason, default: Row()].count += 1
        lastReason = reason
        lastLeftEngine = mach_absolute_time()
        lock.unlock()
    }

    func note(_ text: String) {
        lock.lock()
        notes.append(text)
        lock.unlock()
    }

    /// Runs on the vCPU thread during the port write, so the monitor's copy of the registers is the guest's.
    func portWrite(port: UInt16, size: UInt32, value: UInt32, slide: UInt64) {
        let c = engine.ctx.pointee
        let rip = c.req_a.4
        let regs = withUnsafeBytes(of: c.x) { Array($0.bindMemory(to: UInt64.self)) }
        func unslid(_ at: UInt64) -> String { hex(at) + " unslid " + hex(at &- slide) }
        var o = String(format: "%.3f s: ", seconds(mach_absolute_time() &- startTicks))
        o +=
            "port \(hex(UInt64(port))) size \(size) value \(hex(UInt64(value))) rip \(unslid(rip)) cpl \(c.cpl) cr3 \(hex(c.cr3))\n"
        o += "  bytes at rip: " + engine.bytes(at: rip, count: 16) + "\n"
        o +=
            "  rax \(hex(regs[0])) rdx \(hex(regs[2])) rsp \(hex(regs[4])) rbp \(hex(regs[5]))\n  frame pointers, return addresses:\n"
        var frame = regs[5]
        for _ in 0..<32 {
            guard let words = engine.readGuest(frame, count: 16) else { break }
            let (next, back) = words.withUnsafeBytes {
                (
                    $0.loadUnaligned(fromByteOffset: 0, as: UInt64.self),
                    $0.loadUnaligned(fromByteOffset: 8, as: UInt64.self)
                )
            }
            o += "    " + unslid(back) + "\n"
            if next <= frame { break }
            frame = next
        }
        o += "  stack from rsp:\n"
        if let stack = engine.readGuest(regs[4], count: 256) {
            stack.withUnsafeBytes { raw in
                for i in 0..<32 {
                    o += hex(raw.loadUnaligned(fromByteOffset: i * 8, as: UInt64.self)) + (i % 4 == 3 ? "\n" : " ")
                }
            }
        }
        lock.lock()
        portWrites.append(o)
        lock.unlock()
        write()
    }

    private func guestString(_ va: UInt64) -> String {
        var out: [UInt8] = []
        for i in 0..<200 {
            guard let b = engine.readGuest(va + UInt64(i), count: 1), b[0] != 0 else { break }
            out.append(b[0])
        }
        return String(decoding: out, as: UTF8.self)
    }

    private func guestArguments(_ va: UInt64) -> String {
        var out: [String] = []
        for i in 0..<12 {
            guard let raw = engine.readGuest(va + UInt64(i * 8), count: 8) else { break }
            let at = raw.withUnsafeBytes { $0.loadUnaligned(as: UInt64.self) }
            if at == 0 { break }
            out.append(guestString(at))
        }
        return out.joined(separator: " ")
    }

    /// One line per process event, each starting `proc `, so a batch of runs can be compared with a grep.
    func processNote() {
        let c = engine.ctx.pointee
        let r = withUnsafeBytes(of: c.x) { Array($0.bindMemory(to: UInt64.self)) }
        let noteTicks = mach_absolute_time()
        var o = String(format: "proc %8.3f ", seconds(noteTicks &- startTicks)) + "cr3 \(hex(c.cr3)) "
        var shareInspection: ShareWitnessMarkers.Inspection?
        var retainTerminalSignal = false
        switch c.req_a.0 {
        case 1:
            switch r[0] & 0xFFFFFF {
            case 4, 0x18D:
                let inspection = ShareWitnessMarkers.inspect(enabled: shareMarkersEnabled, fd: r[7], count: r[2]) {
                    engine.readGuest(r[6], count: $0)
                }
                shareInspection = inspection
                let text = inspection.ordinaryBytes.map { String(decoding: $0, as: UTF8.self) } ?? "unreadable"
                o += "write fd \(r[7]): " + text.replacingOccurrences(of: "\n", with: " ")
            case 1: o += "exit \(r[7] & 0xFF)"
            case 0x37: o += "reboot how \(hex(r[7]))"
            case 0x3B: o += "execve " + guestString(r[7]) + " [" + guestArguments(r[6]) + "]"
            case 0xF4: o += "posix_spawn " + guestString(r[6]) + " [" + guestArguments(r[10]) + "]"
            default: o += "wait4 pid \(Int32(truncatingIfNeeded: r[7]))"
            }
        case 2:
            let status = engine.readGuest(c.req_a.2, count: 4)?.withUnsafeBytes { $0.loadUnaligned(as: UInt32.self) }
            let result = Int64(bitPattern: r[0])
            if c.req_a.3 != 0 {  // rax is an errno, and the status word is whatever the caller had there
                o += "wait4 failed, error \(result)"
                break
            }
            retainTerminalSignal = RecentTerminalSignals.isTerminalWait(result: result, status: status, failed: false)
            o += "wait4 returned \(result) status " + (status.map { hex(UInt64($0)) } ?? "unreadable")
            // With no child ready (WNOHANG returns 0) or an error, the kernel left the status word as it found it.
            if result > 0, let status, status & 0x7F != 0 {
                o += " SIGNAL \(status & 0x7F)\n  last user page faults, rip, address, error, cr3:\n"
                let faults = withUnsafeBytes(of: c.user_pf) { Array($0.bindMemory(to: EngineEvent.self)) }
                for i in 0..<UInt64(faults.count) {
                    let e = faults[Int((c.user_pf_next &+ i) % UInt64(faults.count))]
                    if e.cr3 != 0 { o += "    \(hex(e.rip)) \(hex(e.a)) \(hex(UInt64(e.vector))) \(hex(e.cr3))\n" }
                }
                o.removeLast()
            } else if let status {
                o += " exit \((status >> 8) & 0xFF)"
            }
        default:
            o +=
                "FAULT vector \(c.req_a.2) error \(hex(c.req_a.3)) rip \(hex(c.req_a.1))  "
                + engine.bytes(at: c.req_a.1, count: 16) + "\n  "
            for i in 0..<16 { o += hex(r[i]) + (i == 15 ? "" : " ") }
            // dyld's fatal error is an int3 with its message in rdi: that is the process's own account of why it died.
            if c.req_a.2 == 3 { o += "\n  rdi as a string: " + guestString(r[7]) }
        }
        lock.lock()
        if processLog.count < 2000 { processLog.append(o) }
        if let shareInspection {
            shareMarkers.append(shareInspection, hostTicks: noteTicks,
                                elapsedTicks: noteTicks &- startTicks, cr3: c.cr3)
        }
        if retainTerminalSignal { recentTerminalSignals.append(o) }
        lock.unlock()
    }

    func tcgSample(rip: UInt64, cpl: UInt32, cr3: UInt64) { addSample("tcg", rip, cpl, cr3) }

    private func addSample(_ place: String, _ rip: UInt64, _ cpl: UInt32, _ cr3: UInt64) {
        lock.lock()
        samples.append(
            String(format: "%8.1f ", seconds(mach_absolute_time() &- startTicks))
                + "\(place) \(hex(rip)) cpl \(cpl) cr3 \(hex(cr3))")
        if samples.count > 64 { samples.removeFirst() }
        lock.unlock()
    }

    func fallback(reason: String, rip: UInt64, bytes: String, detail: UInt64) {
        lock.lock()
        reasons[reason, default: Row()].count += 1
        sites["\(reason) | \(bytes)", default: Site(firstRIP: rip, detail: detail)].count += 1
        fallbacks += 1
        lastReason = reason
        lastLeftEngine = mach_absolute_time()
        lock.unlock()
    }

    func boundary(rip: UInt64, cpl: UInt32, cr3: UInt64) {
        lock.lock()
        boundaries += 1
        lock.unlock()
        addSample("engine", rip, cpl, cr3)
    }

    // Whether address spaces share page tables under the code they run, which decides whether they could share its translations.
    func coldUserBlock(rip: UInt64, cr3: UInt64) {
        func entry(_ table: UInt64, _ index: UInt64) -> UInt64? {
            engine.host((table & 0x000F_FFFF_FFFF_F000) + index * 8)?.loadUnaligned(as: UInt64.self)
        }
        guard let top = entry(cr3, (rip >> 39) & 0x1FF), top & 1 != 0, let upper = entry(top, (rip >> 30) & 0x1FF),
            upper & 1 != 0, upper & 0x80 == 0
        else { return }
        let directory = upper & 0x000F_FFFF_FFFF_F000
        let space = cr3 & 0x000F_FFFF_FFFF_F000
        lock.lock()
        defer { lock.unlock() }
        func count(_ row: inout SharedFrame) {
            row.blocks += 1
            row.spaces.insert(space)
            row.low = min(row.low, rip)
            row.high = max(row.high, rip)
        }
        count(&coldByDirectory[directory, default: SharedFrame()])
        if let middle = entry(directory, (rip >> 21) & 0x1FF), middle & 1 != 0, middle & 0x80 == 0 {
            count(&coldByTable[middle & 0x000F_FFFF_FFFF_F000, default: SharedFrame()])
        }
    }

    func mmioSample(rip: UInt64, gpa: UInt64, size: UInt64, write: Bool) {
        let bytes =
            engine.readGuest(rip, count: 8).map { $0.map { String(format: "%02x", $0) }.joined(separator: " ") }
            ?? "unreadable"
        // The device by its megabyte, so a loop over a frame buffer is one row.
        mmioSites["\(hex(rip)) \(write ? "w" : "r") \(size) \(hex(gpa & ~0xF_FFFF))  \(bytes)", default: 0] += 1
    }

    // Any thread, so approximate, which is all it is for.
    func exclusiveStop() { exclusiveStops.pointee += 1 }

    var apicRegisters: [UInt64: Int] = [:]
    func mmio(_ gpa: UInt64) {
        lock.lock()
        mmioPages[gpa & ~0xFFF, default: 0] += 1
        if gpa & ~0xFFF == 0xFEE0_0000 { apicRegisters[gpa & 0xFF0, default: 0] += 1 } // which local APIC register: a timer arm or an EOI
        lock.unlock()
    }

    func port(_ port: UInt16) {
        lock.lock()
        ports[port, default: 0] += 1
        lock.unlock()
    }

    func stopped(_ text: String) {
        lock.lock()
        hardStop = text
        lock.unlock()
        write()
    }

    private func hex(_ v: UInt64) -> String { "0x" + String(v, radix: 16) }
    private func seconds(_ ticks: UInt64) -> Double { Double(ticks) / 24_000_000 }

    private func addTimelineRow() {
        let snapshotStartTicks = shareMarkersEnabled ? mach_absolute_time() : 0
        let c = engine.ctx.pointee
        let t = tr_stats()
        lock.lock()
        var row = [
            String(format: "%8.1f", seconds(mach_absolute_time() &- startTicks)), String(c.stat_pt_writes),
            String(c.stat_pt_writes_zap),
            String(c.stat_cr3), String(t.blocks), String(t.insns),
            String(format: "%.1f", seconds(t.translate_ns) * 1000),
            String(c.stat_mmio), String(fallbacks), String(c.stat_irq), String(format: "%.1f", seconds(c.stat_mon_wait) * 1000),
            String(format: "%.1f", seconds(engine.translatorWaitTicks) * 1000), samples.last ?? "-"
        ]
        if shareMarkersEnabled {
            // Bracket all counter reads and formatting; not an atomic cross-vCPU snapshot.
            let snapshotEndTicks = mach_absolute_time()
            row.append(String(snapshotStartTicks))
            row.append(String(snapshotEndTicks))
        }
        timeline.append(row.joined(separator: "\t"))
        lock.unlock()
    }

    public func write() {
        let c = engine.ctx.pointee
        let t = tr_stats()
        lock.lock()
        let elapsed = max(seconds(mach_absolute_time() &- startTicks), 0.001)
        func perSecond(_ v: UInt64) -> String { String(format: "%12d  %12.0f/s", v, Double(v) / elapsed) }
        var o =
            "# hvdbt tally. One vCPU; TCG up to XNU's 64-bit entry, then the engine. All counts are measured on this run.\n"
        o += "state: " + (hardStop.map { "HARD STOP: \($0)" } ?? "running or exited normally") + "\n"
        let away = lastReason == nil ? 0 : mach_absolute_time() &- lastLeftEngine
        if let lastReason, seconds(away) > 1 {
            o += String(format: "off the engine, on TCG, for %.1f s now, after: ", seconds(away)) + lastReason + "\n"
            o += "the engine's counters below don't move meanwhile, and say nothing about the guest\n"
        }
        o += String(format: "seconds since hand-off: %.1f\n", elapsed)
        o += "hand-off rip: " + (firstRIP.map(hex) ?? "none") + "\n"
        o += "last rip sample: " + (samples.last ?? "none") + "\n"
        for note in notes { o += note + "\n" }
        let halted = portWrites.first { $0.contains("port 0x604 size 2 value 0x2") || $0.contains("port 0x605") }
        o +=
            "outcome: " + (hardStop != nil ? "hard stop" : halted != nil ? "guest powered off" : "running")
            + String(
                format: " at %.1f s, syscalls %d, page faults %d, bad hash see dump\n", elapsed, c.stat_syscall,
                c.stat_pf_delivered)
        o += "\n"
        o += "== process log: spawns, exits, wait4 results, user faults other than #PF and #NM, oldest first ==\n"
        for line in processLog { o += line + "\n" }
        o += "\n"
        if shareMarkersEnabled {
            o += "== share witness writes (this vCPU; newest64; payload <=512 ASCII bytes; pre-syscall, delivery unqualified) ==\n"
            o += "host_start_ticks \(startTicks)\n"
            if let timebase = shareMarkerTimebase {
                o += "host_timebase_numer \(timebase.numer) host_timebase_denom \(timebase.denom)\n"
            } else { o += "host_timebase_unavailable 1\n" }
            o += "share_marker_observed \(shareMarkers.observed) dropped \(shareMarkers.dropped) rejected \(shareMarkers.rejected)\n"
            for marker in shareMarkers.records {
                o += "host_ticks \(marker.hostTicks) elapsed_ticks \(marker.elapsedTicks) cr3 \(hex(marker.cr3)) " + marker.payload + "\n"
            }
            o += "\n"
        }
        o += "== recent terminal-signal waits (this vCPU; newest64 retained, oldest first; each <=8192 UTF8 bytes) ==\n"
        o += "terminal_waits_observed \(recentTerminalSignals.observed) dropped \(recentTerminalSignals.dropped) truncated \(recentTerminalSignals.truncated)\n"
        for line in recentTerminalSignals.notes { o += line + "\n" }
        o += "\n"
        if !portWrites.isEmpty {
            o +=
                "== writes to the power and reset ports, in order. Bit 13 of PM1a_CNT is SLP_EN ==\n"
                + portWrites.joined() + "\n"
        }

        o +=
            "== stops and fallbacks, ranked by count. tcg_ms: time away from the engine after each, with TCG running ==\n"
        for (reason, row) in reasons.sorted(by: { $0.value.count > $1.value.count }) {
            let open = reason == lastReason ? away : 0
            o += String(format: "%12d %10.1f  ", row.count, seconds(row.hostTicks &+ open) * 1000) + reason + "\n"
        }
        o += "\n== by site: reason | first bytes at rip, ranked; first rip, detail (msr, address) ==\n"
        for (key, site) in sites.sorted(by: { $0.value.count > $1.value.count }).prefix(200) {
            o += String(format: "%12d  ", site.count) + key + "  @\(hex(site.firstRIP)) \(hex(site.detail))\n"
        }

        o += "\n== rates the design lists as unknown, totals and per wall second since hand-off ==\n"
        o += "page-table stores    " + perSecond(c.stat_pt_writes) + "\n"
        o += "  frames found stale " + perSecond(c.stat_pt_stale) + "\n"
        o += "  dropping a table   " + perSecond(c.stat_pt_writes_zap) + "\n"
        o += "  by one-word step   " + perSecond(c.stat_pt_step) + "\n"
        o += "  tables unshadowed  " + perSecond(c.stat_pt_unshadow) + "\n"
        o += "\n== keeper inactive flood guard (this vCPU; enabled is configuration state) ==\n"
        o += "keeper_inactive_guard_enabled \(c.keeper_inactive_guard)\n"
        o += "keeper_inactive_guard_suppressed \(c.stat_keeper_inactive_suppressed)\n"
        o += "keeper_inactive_guard_threshold_fallbacks \(c.stat_keeper_inactive_threshold_fallbacks)\n"
        o += "\n== identical 64-bit PTE stores (this vCPU; experimental, state is not a delta) ==\n"
        o += "pte_same64_enabled \(c.pte_same64)\n"
        o += "pte_same64_eligible_exchanges \(c.stat_pte_same64_exchanges)\n"
        o += "pte_same64_exact_equal \(c.stat_pte_same64_equal)\n"
        o += "pte_same64_changed \(c.stat_pte_same64_changed)\n"
        o += "pte_same64_nonempty_leaf_skips \(c.stat_pte_same64_leaf_skips)\n"
        o += "pte_same64_xmark_skips \(c.stat_pte_same64_xmark_skips)\n"
        o += "Leaf/xmark skips count shadow mirrors, not saved body kills or elapsed time. Flood/root/higher-level actions remain enabled.\n"
        let repInvocations = withUnsafeBytes(of: c.stat_svc) { $0.bindMemory(to: UInt64.self)[Int(SVC_REP_PAGE)] }
        o += "\n== experimental page REP service (this vCPU; enabled is repeated global configuration) ==\n"
        o += "page_rep_enabled \(t.page_rep_enabled)\n"
        o += "page_rep_invocations \(repInvocations)\n"
        o += "page_rep_committed_prefixes \(c.stat_rep_page_prefixes)\n"
        o += "page_rep_committed_bytes \(c.stat_rep_page_bytes)\n"
        o += "Positive prefixes/bytes follow actual copy and architectural progress commit. Invocations include declines; async snapshots are not coherent.\n"
        if c.profile_mmu_observations != 0 {
            o += "\n== MMU observations (this vCPU; count/timing only, no skipped operations) ==\n"
            o += "mmu_observations_enabled \(c.profile_mmu_observations)\n"
            o += "mmu_counter_hz \(c.stat_mmu_counter_hz)\n"
            o += "mmu_walk_sample_period 256\n"
            o += "mmu_first_walk_calls \(c.stat_mmu_first_walk_calls)\n"
            o += "mmu_first_walk_samples \(c.stat_mmu_first_walk_samples)\n"
            o += "mmu_first_walk_sampled_ticks \(c.stat_mmu_first_walk_ticks)\n"
            o += "mmu_locked_rewalk_calls \(c.stat_mmu_locked_rewalk_calls)\n"
            o += "mmu_locked_rewalk_samples \(c.stat_mmu_locked_rewalk_samples)\n"
            o += "mmu_locked_rewalk_sampled_ticks \(c.stat_mmu_locked_rewalk_ticks)\n"
            let observed = withUnsafeBytes(of: c.stat_mmu_store_observed) { Array($0.bindMemory(to: UInt64.self)) }
            let unchanged = withUnsafeBytes(of: c.stat_mmu_store_unchanged) { Array($0.bindMemory(to: UInt64.self)) }
            for (index, kind) in ["plain32", "plain64", "lse32", "lse64"].enumerated() {
                o += "mmu_\(kind)_observed \(observed[index])\n"
                o += "mmu_\(kind)_observed_unchanged \(unchanged[index])\n"
            }
            o += "Sampled ticks are unscaled/nonserialized counter reads; divide by counter_hz for seconds.\n"
            o += "Equality is an observation, not proof a store/zap can be skipped: concurrent walkers update A/D outside this lock.\n"
            o += "Only aligned plain4/8 and LSE32/64 stores are observed; CAS already skips unchanged updates.\n\n"
            let flood = withUnsafeBytes(of: c.stat_mmu_flood_decisions) { Array($0.bindMemory(to: UInt64.self)) }
            let xmarks = withUnsafeBytes(of: c.stat_mmu_xmark_zaps) { Array($0.bindMemory(to: UInt64.self)) }
            o += "== shadow invalidation census (this vCPU; decisions and marked leaves, not killed bodies) ==\n"
            for (cause, name) in ["threshold_only", "inactive_only", "both"].enumerated() {
                for keeper in 0...1 {
                    o += "mmu_flood_\(name)_keeper\(keeper) \(flood[cause * 2 + keeper])\n"
                }
            }
            for reason in 0..<Int(KILL_KINDS) {
                for keeper in 0...1 {
                    for valid in 0...1 {
                        o += "mmu_xmark_reason\(reason)_keeper\(keeper)_valid\(valid) \(xmarks[reason * 4 + keeper * 2 + valid])\n"
                    }
                }
            }
            o += "valid0 code-only marks have unknown prior PFN in killed_unmoved; zero does not mean moved.\n\n"
        }
        o += "cold blocks          " + perSecond(t.blocks) + "\n"
        o += "cold x86 insns       " + perSecond(t.insns) + "\n"
        o += "cold ARM words       " + perSecond(t.words) + "\n"
        let holds = withUnsafeBytes(of: c.stat_mon_hold) { Array($0.bindMemory(to: UInt64.self)) }
        o += String(format: "monitor lock waited %.2f s; held, s: other %.2f, view switch %.2f, before translating %.2f, after %.2f, fault %.2f, table store %.2f\n", seconds(c.stat_mon_wait), seconds(holds[0]), seconds(holds[1]), seconds(holds[2]), seconds(holds[3]), seconds(holds[4]), seconds(holds[5]))
        let opInsns = withUnsafeBytes(of: t.op_insns) { Array($0.bindMemory(to: UInt64.self)) }
        let opWords = withUnsafeBytes(of: t.op_words) { Array($0.bindMemory(to: UInt64.self)) }
        o += "  words after the last instruction of blocks: \(t.exit_words)\n  by OP_ number in tr_internal.h, ranked by words: op, insns, words, words per insn\n"
        for op in opWords.indices.sorted(by: { opWords[$0] > opWords[$1] }).prefix(24) where opInsns[op] > 0 {
            o += String(format: "  %3d %12llu %12llu %6.2f\n", op, opInsns[op], opWords[op], Double(opWords[op]) / Double(opInsns[op]))
        }
        o += String(format: "translation time     %12.1f ms\n", seconds(t.translate_ns) * 1000)
        o += "blocks invalidated   " + perSecond(t.invalidated) + "\n"
        o += "inline_shr8_enabled \(t.inline_shr8_enabled) (global translator configuration; not a counter or delta)\n"
        o += "\n== shift helper census (this vCPU; entries, not retired instructions or time) ==\n"
        o += "shift_helpers_enabled \(c.profile_shift_helpers)\n"
        if c.profile_shift_helpers != 0 {
            let shiftCounts = withUnsafeBytes(of: c.stat_shift_helpers) { Array($0.bindMemory(to: UInt64.self)) }
            let shiftSVC = withUnsafeBytes(of: c.stat_svc) { Array($0.bindMemory(to: UInt64.self)) }
            o += "shift_helpers_cpu_index \(engine.cpuIndex)\n"
            o += "shift_helpers_svc64_entries \(shiftSVC[Int(SVC_H_SHIFT)])\n"
            o += "shift_helpers_unclassified \(c.stat_shift_helpers_unclassified)\n"
            o += "masked_zero uses the x86 31/63 count mask, before rotate modulo; immediate-zero and inline paths are not counted.\n"
            o += "A memory-store fault/retry can count again; snapshots are asynchronous. Never sum duplicated global translator counters.\n"
            for bucket in 0..<Int(SHIFT_HELPER_PROFILE_BUCKETS) {
                let kind = bucket >> 5, bytes = 1 << ((bucket >> 3) & 3)
                let memory = (bucket >> 2) & 1, cl = (bucket >> 1) & 1, zero = bucket & 1
                o += "shift_helpers_bucket kind=\(kind) bytes=\(bytes) memory=\(memory) cl=\(cl) masked_zero=\(zero) count=\(shiftCounts[bucket])\n"
            }
        }
        o += "\n== invalidation membership prototype (global translator; flags/state are not interval deltas) ==\n"
        o += "invalidation_membership_enabled \(t.invalidation_membership_enabled)\n"
        o += "invalidation_membership_reliable \(t.invalidation_membership_reliable)\n"
        o += "invalidation_membership_bytes \(t.invalidation_membership_bytes)\n"
        o += "invalidation_membership_inserts \(t.invalidation_membership_inserts)\n"
        o += "invalidation_membership_view_visits \(t.invalidation_membership_view_visits)\n"
        o += "invalidation_membership_stale_visits \(t.invalidation_membership_stale_visits)\n"
        o += "invalidation_membership_removals \(t.invalidation_membership_removals)\n"
        o += "invalidation_membership_tracking_failures \(t.invalidation_membership_tracking_failures)\n"
        o += "invalidation_membership_full_scans \(t.invalidation_membership_full_scans)\n"
        o += "Conservative bits may be stale; removals require the current ARM entry. Do not sum vCPU files.\n\n"
        if t.invalidation_profile_enabled != 0 {
            o += "\n== sampled invalidation cost (global translator; do not sum vCPU files) ==\n"
            o += "sample_period_kills 256; costs are unscaled sampled nanoseconds, including counting overhead\n"
            o += "invalidation_profile_enabled \(t.invalidation_profile_enabled)\n"
            o += "invalidation_profiled_kills \(t.invalidation_profiled_kills)\n"
            o += "invalidation_sampled_kills \(t.invalidation_sampled_kills)\n"
            o += "invalidation_sampled_registered \(t.invalidation_sampled_registered)\n"
            o += "invalidation_sampled_shared \(t.invalidation_sampled_shared)\n"
            o += "invalidation_hash_view_visits \(t.invalidation_hash_view_visits)\n"
            o += "invalidation_hash_probes \(t.invalidation_hash_probes)\n"
            o += "invalidation_hash_removals \(t.invalidation_hash_removals)\n"
            o += "invalidation_map_unlink_ns \(t.invalidation_map_unlink_ns)\n"
            o += "invalidation_hash_fanout_ns \(t.invalidation_hash_fanout_ns)\n"
            o += "invalidation_icache_ns \(t.invalidation_icache_ns)\n\n"
            let rejected = withUnsafeBytes(of: t.reuse_reject) { Array($0.bindMemory(to: UInt64.self)) }
            o += "== dead body reuse census (global translator; do not sum vCPU files) ==\n"
            o += "reuse_attempts \(t.reuse_attempts)\n"
            o += "reuse_successes \(t.reused)\n"
            for (index, name) in ["disabled", "no_slot", "not_dead", "rip", "key", "no_read_copy", "cpl", "compat", "fetch", "frame", "smp_entry", "bytes", "entry_cas"].enumerated() {
                o += "reuse_reject_\(name) \(rejected[index])\n"
            }
            o += "First failed condition only, original short-circuit order. no_read_copy includes unsupported/cross-page/read-copy-budget cases.\n\n"
        }
        o += "== shared dead candidate hints (global translator; repeated in every vCPU file, do not sum) ==\n"
        o += "retain_shared_dead_enabled \(t.retain_shared_dead_enabled)\n"
        o += "retain_shared_dead_retained_hints \(t.shared_dead_retained_hints)\n"
        o += "retain_shared_dead_cleared_hints \(t.shared_dead_cleared_hints)\n"
        o += "Page REP is experimental: latched fixed ordinary-RAM GPAs per page prefix, not a freeze of page-table RAM. SVC 71 counts entries, not copied bytes or successful service.\n"
        o += "Enabled-only nonzero hint-slot actions since tr_init. The same hint can count at multiple flushes; not unique bodies, successful reuse, or saved time.\n\n"
        o += "== analyzed input recurrence (inert global translator census; repeated, do not sum vCPU files) ==\n"
        o += "compile_inputs_enabled \(t.compile_inputs_enabled)\n"
        o += "compile_inputs_available \(t.compile_inputs_available)\n"
        o += "compile_inputs_bytes \(t.compile_inputs_bytes)\n"
        o += "compile_inputs_allocation_failures \(t.compile_inputs_allocation_failures)\n"
        o += "compile_inputs_sampling_shift \(t.compile_inputs_sampling_shift)\n"
        o += "compile_inputs_config_base \(t.compile_inputs_config_base)\n"
        o += "compile_inputs_attempts \(t.compile_inputs_attempts)\n"
        o += "compile_inputs_admitted \(t.compile_inputs_admitted)\n"
        let compileRejected = withUnsafeBytes(of: t.compile_inputs_reject) { Array($0.bindMemory(to: UInt64.self)) }
        for (index, name) in ["single", "unavailable", "cross_page", "partial", "inconsistent", "mixed_frame", "unprotected", "fetch_fault", "unsupported", "empty"].enumerated() {
            o += "compile_inputs_reject_\(name) \(compileRejected[index])\n"
        }
        o += "compile_inputs_empty \(t.compile_inputs_empty)\n"
        o += "compile_inputs_evictions \(t.compile_inputs_evictions)\n"
        o += "compile_inputs_prior \(t.compile_inputs_prior)\n"
        o += "compile_inputs_exact \(t.compile_inputs_exact)\n"
        o += "compile_inputs_coverage_mismatch \(t.compile_inputs_coverage_mismatch)\n"
        o += "compile_inputs_bytes_mismatch \(t.compile_inputs_bytes_mismatch)\n"
        o += "compile_inputs_exact_same_key \(t.compile_inputs_exact_same_key)\n"
        o += "compile_inputs_exact_different_key \(t.compile_inputs_exact_different_key)\n"
        o += "compile_inputs_exact_same_pfn \(t.compile_inputs_exact_same_pfn)\n"
        o += "compile_inputs_exact_different_pfn \(t.compile_inputs_exact_different_pfn)\n"
        let compileJoint = withUnsafeBytes(of: t.compile_inputs_exact_joint) { Array($0.bindMemory(to: UInt64.self)) }
        for (index, name) in ["same_key_same_pfn", "same_key_different_pfn", "different_key_same_pfn", "different_key_different_pfn"].enumerated() {
            o += "compile_inputs_exact_\(name) \(compileJoint[index])\n"
        }
        o += "Full cold-attempt population after existing reuse miss; first encountered rejection only. Exact returned bytes and coverage, including same-page liveness; zero holes.\n"
        o += "8192 direct-mapped entries, last observation only, reset at tr_init, survive executable-cache resets. Empty/eviction is missing retained history, not first-ever code.\n"
        o += "Config schema 1 in high32; base low bits: precise/BMI/hybrid/KPTI/bulk/return/allflags/shift-profile/SHR8/page-REP. Per-input bit16 is current SMP entry form.\n"
        o += "Exact key/PFN splits compare preceding retained observation; no executable reuse, hit-rate projection, saved-time or CPU-fraction claim. Async snapshots may not conserve; qualify coherent endpoints only.\n\n"
        o +=
            "cold blocks a cache keyed by frame had: \(t.again_live) live in another view, \(t.again_dead) dead with the frame unchanged. Lower bounds\n\n"
        o +=
            "cold 32-bit user blocks: \(t.i386_blocks), of which a cache keyed by frame had \(t.i386_again)\n"
        o +=
            "shared user blocks: \(t.shared_blocks) translated, \(t.shared_found) found by another view, under \(c.shared_dir_count) directories\n"
        o +=
            "view lookup tables emptied: \(t.hash_refills) as full, \(t.hash_forgets) forgetting a shared directory. Code cache resets: \(t.cache_resets), and of its private part \(t.cache_private_resets)\n"
        o += "stopped for another vCPU's exclusive section: \(exclusiveStops.pointee)\n"
        o += String(format: "dead blocks reused: %llu. Direct calls between keys given a lookup: %llu\n", t.reused, t.call_tails)
        o += String(format: "this vCPU waited for the translator lock %llu times, %.2f s\n\n", engine.translatorWaits, seconds(engine.translatorWaitTicks))
        o += String(format: "page invalidations: %llu, %.2f s waiting for the translator lock, %.2f s in all\n\n", engine.invalidations, seconds(engine.invalidationWaitTicks), seconds(engine.invalidationTicks))
        o += "== why blocks died: blocks, of those with the page still on the same frame, calls ==\n"
        let killed = withUnsafeBytes(of: t.killed) { Array($0.bindMemory(to: UInt64.self)) }
        let unmoved = withUnsafeBytes(of: t.killed_unmoved) { Array($0.bindMemory(to: UInt64.self)) }
        let calls = withUnsafeBytes(of: t.kill_calls) { Array($0.bindMemory(to: UInt64.self)) }
        let kinds = [
            "a store onto code", "DMA or TCG wrote code", "a leaf page-table store", "a store higher in the tables",
            "a table written too often to shadow",
            "the shadow pool ran out", "its view was reused", "device memory left the VM"
        ]
        for (i, kind) in kinds.enumerated() {
            o += String(format: "%12llu %12llu %12llu  ", killed[i], unmoved[i], calls[i]) + kind + "\n"
        }
        o += "\n"

        o += "== where the time goes: 24 MHz clock, averages fine over many ==\n"
        o += String(
            format: "%10.1f s inside hv_vcpu_run, translated code, the monitor and the exit round trips\n",
            seconds(engine.ticksInVCPU))
        o += "   count        total s    us each  host call (HC_ number in engine_abi.h)\n"
        for code in engine.hostCallCount.indices where engine.hostCallCount[code] != 0 {
            let total = seconds(engine.hostCallTicks[code])
            o += String(
                format: "%12llu %10.2f %10.2f  %d\n", engine.hostCallCount[code], total,
                total * 1e6 / Double(engine.hostCallCount[code]), code)
        }
        o += "monitor calls by SVC_ number in engine_abi.h, ranked:\n"
        let svc = withUnsafeBytes(of: c.stat_svc) { Array($0.bindMemory(to: UInt64.self)) }
        for (n, count) in svc.enumerated().sorted(by: { $0.element > $1.element }).prefix(20) where count != 0 {
            o += String(
                format: "%12llu, %8.0f/s  %d\n", count,
                Double(count) / max(seconds(mach_absolute_time() &- startTicks), 1), n)
        }
        o += "\n"

        o += "== other counters ==\n"
        let counters: [(String, UInt64)] = [
            ("vm exits", engine.exitCount), ("fallbacks", fallbacks), ("boundary stops", boundaries),
            ("links made in the monitor", c.stat_links_here),
            ("deepest monitor stack, bytes", c.stat_stack_deepest),
            ("shadow fills", c.stat_fills),
            ("dirty upgrades", c.stat_dirty), ("smc", c.stat_smc), ("page faults delivered", c.stat_pf_delivered),
            ("cr3 writes", c.stat_cr3),
            ("iret", c.stat_iret), ("syscall", c.stat_syscall), ("interrupts delivered", c.stat_irq),
            ("arm steps to a boundary", c.stat_steps),
            ("hlts ended by an interrupt while polling", c.stat_hlt_polled),
            ("mmio through the host", c.stat_mmio), ("#NM from CR0.TS", c.stat_nm),
            ("code frames noted by DMA or TCG", c.stat_note_code),
            ("table frames noted by DMA or TCG", c.stat_note_pt), ("view evictions", c.stat_view_evictions),
            ("pool reclaims", c.stat_pool_reclaim)
        ]
        for (name, value) in counters {
            o += name.padding(toLength: 34, withPad: " ", startingAt: 0) + perSecond(value) + "\n"
        }

        o += "\n== events delivered by vector, ranked; last return rip, error code, bytes there ==\n"
        let vectors = withUnsafeBytes(of: c.stat_vector) { Array($0.bindMemory(to: UInt64.self)) }
        let vectorRIP = withUnsafeBytes(of: c.vector_last_rip) { Array($0.bindMemory(to: UInt64.self)) }
        let vectorErr = withUnsafeBytes(of: c.vector_last_err) { Array($0.bindMemory(to: UInt64.self)) }
        let vectorCR3 = withUnsafeBytes(of: c.vector_last_cr3) { Array($0.bindMemory(to: UInt64.self)) }
        for v in vectors.indices.filter({ vectors[$0] > 0 }).sorted(by: { vectors[$0] > vectors[$1] }) {
            o +=
                "vector " + hex(UInt64(v)) + perSecond(vectors[v]) + "  " + hex(vectorRIP[v]) + " err "
                + hex(vectorErr[v]) + "  " + engine.bytes(at: vectorRIP[v], cr3: vectorCR3[v]) + "\n"
        }
        o += "\n== the monitor's last events, oldest first: E delivered, I iret, C cr3 write, S syscall, R sysret ==\n"
        o +=
            "kind vector rip a(error, target, cr3 value) cr3, bytes at rip. Read while running, so a line can be torn.\n"
        let events = withUnsafeBytes(of: c.events) { Array($0.bindMemory(to: EngineEvent.self)) }
        for i in 0..<UInt64(events.count) {
            let e = events[Int((c.event_next &+ i) % UInt64(events.count))]
            if e.kind != 0 {
                o +=
                    String(UnicodeScalar(UInt8(truncatingIfNeeded: e.kind))) + " " + hex(UInt64(e.vector)) + " "
                    + hex(e.rip) + " " + hex(e.a) + " " + hex(e.cr3) + "  "
                    + engine.bytes(at: e.rip, count: 8, cr3: e.cr3) + "\n"
            }
        }

        for (title, rows) in [("page directory", coldByDirectory), ("page table", coldByTable)] {
            o +=
                "\n== cold user blocks by the \(title) frame above them: blocks, address spaces seen sharing it, lowest and highest rip ==\n"
            for (frame, row) in rows.sorted(by: { $0.value.blocks > $1.value.blocks }).prefix(16) {
                o +=
                    String(format: "%12llu %6d  ", row.blocks, row.spaces.count) + hex(frame) + " " + hex(row.low) + " "
                    + hex(row.high) + "\n"
            }
            let shared = rows.values.filter { $0.spaces.count > 1 }.reduce(0) { $0 + $1.blocks }
            o +=
                "of all \(rows.values.reduce(0) { $0 + $1.blocks }) blocks, \(shared) under a frame more than one address space used\n"
        }
        o += "\n== mmio by page ==\n"
        for (page, count) in mmioPages.sorted(by: { $0.value > $1.value }).prefix(40) {
            o += String(format: "%12d  ", count) + hex(page) + "\n"
        }
        o += "\n== local APIC accesses by register ==\n"
        for (register, count) in apicRegisters.sorted(by: { $0.value > $1.value }) {
            o += String(format: "%12d  ", count) + hex(register) + "\n"
        }
        o +=
            "\n== mmio by site: 1 in 256 sampled, so counts are approximate. rip, read or write, size, device's megabyte, bytes at rip ==\n"
        for (site, count) in mmioSites.sorted(by: { $0.value > $1.value }).prefix(30) {
            o += String(format: "%12d  ", count &* 256) + site + "\n"
        }
        o += "\n== ports ==\n"
        for (port, count) in ports.sorted(by: { $0.value > $1.value }).prefix(40) {
            o += String(format: "%12d  ", count) + hex(UInt64(port)) + "\n"
        }
        o +=
            "\n== rip samples: seconds, where, rip. One a second, oldest first; a tcg rip is only as fresh as the last TB exit ==\n"
            + samples.joined(separator: "\n") + "\n"
        o +=
            "\n== timeline: seconds, pt stores, pt table drops, cr3, blocks, insns, translate ms, mmio, fallbacks, irqs, all vCPUs' ms waiting for the monitor lock, this vCPU's for the translator's, rip" + (shareMarkersEnabled ? ", snapshot start host ticks, snapshot end host ticks ==\n" : " ==\n")
        o += timeline.joined(separator: "\n") + "\n"
        lock.unlock()
        try? Data(o.utf8).write(to: URL(fileURLWithPath: path), options: .atomic)
    }
}
