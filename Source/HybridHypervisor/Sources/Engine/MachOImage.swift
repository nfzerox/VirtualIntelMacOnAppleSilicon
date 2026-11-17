import Foundation

/// A statically linked 64-bit Mach-O as produced by `ld -preload`: segments at fixed addresses, one entry point.
public struct MachOImage {
    public struct Segment {
        public let name: String
        public let address: UInt64
        public let size: UInt64
        public let fileOffset: Int
        public let fileSize: Int
    }

    public let bytes: [UInt8]
    public let segments: [Segment]
    public let entry: UInt64?

    public init(bytes: [UInt8]) throws {
        self.bytes = bytes
        func u32(_ at: Int) -> UInt32 {
            bytes.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: at, as: UInt32.self) }
        }
        func u64(_ at: Int) -> UInt64 {
            bytes.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: at, as: UInt64.self) }
        }
        guard bytes.count > 32, u32(0) == 0xFEED_FACF else {
            throw EngineError.badImage("not a 64-bit Mach-O")
        }
        var cursor = 32
        var segments: [Segment] = []
        var entry: UInt64?
        for _ in 0..<u32(16) {
            let command = u32(cursor)
            let size = Int(u32(cursor + 4))
            if command == 0x19 {  // LC_SEGMENT_64
                let nameBytes = bytes[(cursor + 8)..<(cursor + 24)].prefix { $0 != 0 }
                segments.append(
                    Segment(
                        name: String(decoding: nameBytes, as: UTF8.self),
                        address: u64(cursor + 24), size: u64(cursor + 32),
                        fileOffset: Int(u64(cursor + 40)), fileSize: Int(u64(cursor + 48))))
            } else if command == 0x5 {  // LC_UNIXTHREAD: pc is register 16 for x86_64, 32 for arm64
                let flavor = u32(cursor + 8)
                let state = cursor + 16
                entry = flavor == 4 ? u64(state + 16 * 8) : u64(state + 32 * 8)
            }
            cursor += size
        }
        self.segments = segments
        self.entry = entry
    }

    /// Copies each segment's file contents to `base + (address - origin)`.
    public func copy(to base: UnsafeMutableRawPointer, origin: UInt64) {
        bytes.withUnsafeBytes { raw in
            for segment in segments where segment.fileSize > 0 && segment.name != "__LINKEDIT" {
                let target = base + Int(segment.address - origin)
                target.copyMemory(from: raw.baseAddress! + segment.fileOffset, byteCount: segment.fileSize)
            }
        }
    }
}

public enum EngineError: Error, CustomStringConvertible {
    case badImage(String)
    case hypervisor(String, Int32)
    case stop(String)

    public var description: String {
        switch self {
        case .badImage(let why): "bad image: \(why)"
        case .hypervisor(let call, let code): "\(call) failed: \(String(UInt32(bitPattern: code), radix: 16))"
        case .stop(let why): why
        }
    }
}
