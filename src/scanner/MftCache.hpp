#pragma once

#ifdef PLATFORM_WINDOWS

#include "scanner/MftFlatEntry.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace diskscan {

/// Persists raw MFT rows in a local SQLite database so drive scans can
/// start instantly from cache until the user requests a rescan.
class MftCache {
public:
    struct Metadata {
        uint64_t    volumeSerial = 0;
        uint64_t    entryCount   = 0;
        int64_t     scannedAt    = 0; // unix epoch seconds
    };

    /// e.g. %LOCALAPPDATA%\diskscangrok\mft_cache.db
    static std::string databasePath();

    /// Read the NTFS volume serial for cache validation (0 on failure).
    static uint64_t volumeSerial(const std::string& volumePath);

    /// True when DISKSCAN_USE_MFT_CACHE is not set to 0.
    static bool enabled();

    /// Load cached rows when the stored volume serial matches.
    static bool load(const std::string& volumePath,
                     uint64_t volumeSerial,
                     std::vector<MftFlatEntry>& out,
                     Metadata* metadata = nullptr,
                     std::string* errorMessage = nullptr);

    /// Replace cached rows for a volume after a live MFT read.
    static bool save(const std::string& volumePath,
                     uint64_t volumeSerial,
                     const std::vector<MftFlatEntry>& entries,
                     std::string* errorMessage = nullptr);

    /// Remove cached rows for a volume (used before forced rescans).
    static bool invalidate(const std::string& volumePath,
                           std::string* errorMessage = nullptr);
};

} // namespace diskscan

#endif // PLATFORM_WINDOWS