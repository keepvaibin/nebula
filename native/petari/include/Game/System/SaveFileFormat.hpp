#pragma once
#include <stddef.h>
#include <stdint.h>

// Disc/save import formats only. The game receives checked host-order data.
namespace NebulaSaveFormat {
    enum class Format { Unknown, Native3, Switch2, Wii2 };
    enum class Error { None, Short, EncryptedExport, Version, Length, Table, Chunk, Checksum };
    struct Header {
        Format format = Format::Unknown;
        uint32_t version = 0, entries = 0, size = 0;
    };
    inline uint16_t read16(const uint8_t* data, bool big) {
        return big ? uint16_t(uint16_t(data[0]) << 8 | data[1]) : uint16_t(uint16_t(data[1]) << 8 | data[0]);
    }
    inline uint32_t read32(const uint8_t* data, bool big) {
        return big ? uint32_t(read16(data,true)) << 16 | read16(data+2,true)
                   : uint32_t(read16(data+2,false)) << 16 | read16(data,false);
    }
    inline uint32_t checksum(const uint8_t* data, size_t bytes, bool big) {
        uint32_t sum = 0, inverse = 0;
        for (size_t i = 0; i+1 < bytes; i += 2) {
            const uint16_t word = read16(data+i,big);
            sum += word;
            inverse += uint16_t(~word);
        }
        return (sum & 65535u) << 16 | (inverse & 65535u);
    }
    inline Error validate(const uint8_t* data, size_t bytes, Header& output) {
        // Known encrypted SD export container: do not inspect its contents.
        if (bytes == 111360) return Error::EncryptedExport;
        if (data == nullptr || bytes < 16) return Error::Short;
        Header header;
        const uint32_t little = read32(data+4,false);
        const bool big = read32(data+4,true) == 2;
        header.format = big ? Format::Wii2 : little == 2 ? Format::Switch2 : little == 3 ? Format::Native3 : Format::Unknown;
        if (header.format == Format::Unknown) return Error::Version;
        header.version = big ? 2 : little;
        header.entries = read32(data+8,big);
        header.size = read32(data+12,big);
        if (header.size < 16 || header.size > UINT32_MAX-31u ||
            ((size_t(header.size)+31u) & ~size_t(31u)) != bytes) return Error::Length;
        if (header.entries == 0 || header.entries >= 24 || header.entries > (header.size-16u)/16u) return Error::Table;
        const uint32_t table_end = 16u+header.entries*16u;
        for (uint32_t i = 0; i < header.entries; ++i) {
            const uint8_t* entry = data+16u+i*16u;
            size_t name_bytes = 0;
            while (name_bytes < 12 && entry[name_bytes] != 0) {
                const uint8_t c = entry[name_bytes++];
                if (c < 32 || c > 126 || c == '/' || c == '\\' || c == ':') return Error::Table;
            }
            if (name_bytes == 0 || name_bytes == 12) return Error::Table;
            const uint32_t start = read32(entry+12,big);
            const uint32_t end = i+1 < header.entries ? read32(entry+28,big) : header.size;
            if (start < table_end || start > end || end > header.size || end-start < 4) return Error::Table;
            const uint8_t* file = data+start;
            if (file[0] != 1) return Error::Chunk;
            size_t position = 4;
            for (uint32_t chunk = 0; chunk < file[1]; ++chunk) {
                const size_t remaining = end-start-position;
                if (remaining < 12) return Error::Chunk;
                const uint32_t size = read32(file+position+8,big);
                if (size < 12 || size > remaining) return Error::Chunk;
                position += size;
            }
        }
        if (read32(data,big) != checksum(data+4,header.size-4,big)) return Error::Checksum;
        output = header;
        return Error::None;
    }
    inline const char* message(Error error) {
        switch (error) {
        case Error::None: return "Save validated.";
        case Error::EncryptedExport: return "Encrypted Wii SD export. Export a plain GameData.bin with a save tool; the original file is unchanged.";
        case Error::Short: return "Save header is incomplete; the original file is unchanged.";
        case Error::Version: return "Unsupported save version; the original file is unchanged.";
        case Error::Length: return "Save length is invalid; the original file is unchanged.";
        case Error::Table: return "Save file table is invalid; the original file is unchanged.";
        case Error::Chunk: return "Save chunk is incomplete; the original file is unchanged.";
        case Error::Checksum: return "Save checksum is invalid; the original file is unchanged.";
        }
        return "Save could not be read; the original file is unchanged.";
    }
}
