#pragma once

#ifdef PLATFORM_WINDOWS

#include "model/FileNode.hpp"
#include <functional>
#include <memory>
#include <string>
#include <atomic>

namespace diskscan {

/// Low-level MFT reader for Windows NTFS volumes.
///
/// Uses FSCTL_GET_NTFS_FILE_RECORD + FSCTL_GET_NTFS_VOLUME_DATA to walk the $MFT
/// directly (same basic technique as WizTree). This is the fast path.
///
/// It is enabled by default for drive-root scans when the process has the
/// required privilege (run as Administrator). Set DISKSCAN_USE_MFT=0 to force
/// the Win32 fallback walker.
///
/// The Win32 path is always available as a reliable (slower) fallback.
class MftReader {
public:
    struct Config {
        std::string              volumePath; // e.g. "C:"
        std::function<void(uint64_t filesRead)> progressCb;
        std::string*             errorMessage = nullptr;
        std::atomic<bool>*       abortFlag = nullptr;
    };

    /// Build a complete FileNode tree for the volume.
    /// Returns nullptr on failure (no admin, non-NTFS, broken, etc.).
    static std::shared_ptr<FileNode> read(const Config& cfg);

private:
    // -----------------------------------------------------------------
    // NTFS on-disk structures (little-endian)
    // -----------------------------------------------------------------
#pragma pack(push, 1)

    struct NtfsBootSector {
        uint8_t  jump[3];
        char     oemId[8];          // "NTFS    "
        uint16_t bytesPerSector;
        uint8_t  sectorsPerCluster;
        uint8_t  reserved[7];
        uint8_t  mediaDescriptor;
        uint16_t unused1;
        uint16_t sectorsPerTrack;
        uint16_t headsPerCylinder;
        uint32_t hiddenSectors;
        uint32_t unused2;
        uint32_t unused3;
        uint64_t totalSectors;
        uint64_t mftCluster;        // Logical Cluster Number of $MFT
        uint64_t mftMirrorCluster;
        int8_t   clustersPerFrsRaw; // positive = clusters, negative = 2^(-n) bytes
        uint8_t  reserved2[3];
        int8_t   clustersPerIxb;
        uint8_t  reserved3[3];
        uint64_t volumeSerialNumber;
        uint32_t checksum;
    };

    // Standard MFT record header (offset 0 in every 1024-byte record)
    struct MftRecordHeader {
        char     signature[4]; // "FILE"
        uint16_t updateSeqOffset;
        uint16_t updateSeqCount;
        uint64_t logFileSequenceNumber;
        uint16_t sequenceNumber;
        uint16_t hardLinkCount;
        uint16_t attributeOffset;
        uint16_t flags;        // bit 0 = in-use, bit 1 = directory
        uint32_t usedSize;
        uint32_t allocSize;
        uint64_t baseFileRecord;
        uint16_t nextAttributeId;
        uint16_t unused;
        uint32_t mftRecordNumber;
    };

    struct AttributeHeader {
        uint32_t typeCode;
        uint32_t length;
        uint8_t  nonResident; // 0 = resident
        uint8_t  nameLength;
        uint16_t nameOffset;
        uint16_t flags;
        uint16_t attributeId;
    };

    struct ResidentAttributeHeader {
        AttributeHeader hdr;
        uint32_t        valueLength;
        uint16_t        valueOffset;
        uint16_t        unused;
    };

    struct NonResidentAttributeHeader {
        AttributeHeader hdr;
        uint64_t startVcn;
        uint64_t endVcn;
        uint16_t dataRunsOffset;
        uint16_t compressionUnit;
        uint32_t unused;
        uint64_t allocatedSize;
        uint64_t dataSize;
        uint64_t initializedSize;
    };

    // $FILE_NAME attribute (type 0x30)
    struct FileNameAttribute {
        uint64_t parentMftRef; // parent dir MFT record (lower 48 bits)
        uint64_t creationTime;
        uint64_t changeTime;
        uint64_t mftChangeTime;
        uint64_t accessTime;
        uint64_t allocatedSize;
        uint64_t realSize;
        uint32_t flags;
        uint32_t reparse;
        uint8_t  filenameLength; // in UTF-16 code units
        uint8_t  filenameNamespace;
        // char16_t filename[filenameLength]  follows immediately
    };

#pragma pack(pop)

    static constexpr uint32_t AT_STANDARD_INFORMATION = 0x10;
    static constexpr uint32_t AT_FILE_NAME            = 0x30;
    static constexpr uint32_t AT_DATA                 = 0x80;
    static constexpr uint32_t AT_END                  = 0xFFFFFFFF;

    static constexpr uint16_t FILE_FLAG_IN_USE    = 0x01;
    static constexpr uint16_t FILE_FLAG_DIRECTORY = 0x02;
};

} // namespace diskscan

#endif // PLATFORM_WINDOWS
