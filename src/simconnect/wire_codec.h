#pragma once

// =============================================================================
// WireCodec V2 — zero-allocation delta protocol builder/parser
// =============================================================================
//
// Builds and parses AN-24 V2 binary wire protocol packets.
// V2 replaces full-read/write with delta-based operations:
//   - DeltaRead: 8-byte header with tier mask (no records)
//   - DeltaUpdate: header + only changed records
//   - FullSync: header + all records (periodic safety net)
//   - DeltaWrite: header + only changed output records
//   - WriteAck: 8-byte header only
//
// All methods operate on caller-provided buffers — no heap allocation.

#include "wire_protocol.h"

#include <cstddef>
#include <cstring>
#include <span>

class WireCodec {
public:
    // ========================================================================
    // Building — Host side (SimConnectBridge)
    // ========================================================================

    /// Build a DeltaRead request: 8-byte header, count = tier mask.
    /// The WASM bridge reads variables in the requested tiers and responds
    /// with either DeltaUpdate (changed records) or FullSync (all records).
    /// Returns bytes written (always 8), or 0 if buffer too small.
    size_t build_delta_read(uint8_t* buf, size_t buf_size,
                             uint16_t tier_mask, uint16_t epoch) const {
        if (sizeof(PacketHeader) > buf_size) return 0;

        auto* hdr = reinterpret_cast<PacketHeader*>(buf);
        hdr->magic    = PACKET_MAGIC;
        hdr->version  = PROTOCOL_VERSION;
        hdr->cmd      = static_cast<uint8_t>(Cmd::DeltaRead);
        hdr->count    = tier_mask;  // Tier bitmask, not record count
        hdr->seq_id   = epoch;

        return sizeof(PacketHeader);
    }

    /// Build a DeltaWrite request: header + only changed output records.
    /// Host side: extracts output signal values that differ from last-sent.
    /// Returns bytes written, or 0 on overflow.
    size_t build_delta_write(uint8_t* buf, size_t buf_size,
                              std::span<const VarRecord> changed_records,
                              uint16_t epoch) const {
        return build_with_records(buf, buf_size, Cmd::DeltaWrite, changed_records, epoch);
    }

    // ========================================================================
    // Building — WASM side (bridge responds to host)
    // ========================================================================

    /// Build a DeltaUpdate response: header + only changed records.
    /// WASM side: shadow buffer detected these values changed since last response.
    /// Returns bytes written, or 0 on overflow.
    size_t build_delta_update(uint8_t* buf, size_t buf_size,
                               std::span<const VarRecord> changed_records,
                               uint16_t epoch) const {
        return build_with_records(buf, buf_size, Cmd::DeltaUpdate, changed_records, epoch);
    }

    /// Build a FullSync response: header + all records.
    /// WASM side: periodic safety net (every FULL_SYNC_INTERVAL frames)
    /// or forced by host (tier_mask = TIER_MASK_FORCE_FULL_SYNC).
    /// Returns bytes written, or 0 on overflow.
    size_t build_full_sync(uint8_t* buf, size_t buf_size,
                            std::span<const VarRecord> all_records,
                            uint16_t epoch) const {
        return build_with_records(buf, buf_size, Cmd::FullSync, all_records, epoch);
    }

    /// Build a WriteAck response: 8-byte header only, no records.
    /// WASM side: confirms DeltaWrite was processed.
    size_t build_write_ack(uint8_t* buf, size_t buf_size,
                            uint16_t epoch) const {
        if (sizeof(PacketHeader) > buf_size) return 0;

        auto* hdr = reinterpret_cast<PacketHeader*>(buf);
        hdr->magic    = PACKET_MAGIC;
        hdr->version  = PROTOCOL_VERSION;
        hdr->cmd      = static_cast<uint8_t>(Cmd::WriteAck);
        hdr->count    = 0;
        hdr->seq_id   = epoch;

        return sizeof(PacketHeader);
    }

    // ========================================================================
    // Parsing — both sides (header + records from CommBus)
    // ========================================================================

    /// Validate and return pointer to the header in the data.
    /// Returns nullptr if data is too short, bad magic, or wrong version.
    const PacketHeader* parse_header(const uint8_t* data, size_t len) const {
        if (len < sizeof(PacketHeader)) return nullptr;

        PacketHeader hdr;
        std::memcpy(&hdr, data, sizeof(PacketHeader));

        if (hdr.magic != PACKET_MAGIC || hdr.version != PROTOCOL_VERSION) {
            return nullptr;
        }

        // DeltaRead is header-only (count = tier mask, not record count)
        if (static_cast<Cmd>(hdr.cmd) == Cmd::DeltaRead ||
            static_cast<Cmd>(hdr.cmd) == Cmd::WriteAck) {
            return reinterpret_cast<const PacketHeader*>(data);
        }

        // All other packets: validate total packet size fits in the data
        if (len < packet_size(hdr.count)) {
            return nullptr;
        }

        return reinterpret_cast<const PacketHeader*>(data);
    }

    /// Get a span of VarRecords from parsed data. Call after parse_header succeeds.
    /// Returns empty span for header-only packets (DeltaRead, WriteAck)
    /// or if data is invalid.
    std::span<const VarRecord> parse_records(const uint8_t* data, size_t len,
                                               const PacketHeader& hdr) const {
        // Header-only packets have no records
        auto cmd = static_cast<Cmd>(hdr.cmd);
        if (cmd == Cmd::DeltaRead || cmd == Cmd::WriteAck) {
            return {};
        }

        size_t expected = packet_size(hdr.count);
        if (len < expected) return {};

        return {reinterpret_cast<const VarRecord*>(data + sizeof(PacketHeader)),
                hdr.count};
    }

    /// Convenience: parse header + records in one call.
    struct ParseResult {
        const PacketHeader* header = nullptr;
        std::span<const VarRecord> records;
    };

    ParseResult parse(const uint8_t* data, size_t len) const {
        ParseResult result;
        result.header = parse_header(data, len);
        if (!result.header) return {};

        result.records = parse_records(data, len, *result.header);
        return result;
    }

private:
    /// Common builder: header + memcpy records. All record-bearing packets
    /// (DeltaUpdate, FullSync, DeltaWrite) share this structure.
    size_t build_with_records(uint8_t* buf, size_t buf_size, Cmd cmd,
                               std::span<const VarRecord> records,
                               uint16_t epoch) const {
        size_t needed = packet_size(static_cast<uint16_t>(records.size()));
        if (needed > buf_size || needed > MAX_PACKET_SIZE) return 0;

        auto* hdr = reinterpret_cast<PacketHeader*>(buf);
        hdr->magic    = PACKET_MAGIC;
        hdr->version  = PROTOCOL_VERSION;
        hdr->cmd      = static_cast<uint8_t>(cmd);
        hdr->count    = static_cast<uint16_t>(records.size());
        hdr->seq_id   = epoch;

        auto* dst = reinterpret_cast<VarRecord*>(buf + sizeof(PacketHeader));
        std::memcpy(dst, records.data(), records.size() * sizeof(VarRecord));

        return needed;
    }
};
