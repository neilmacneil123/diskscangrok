#ifdef PLATFORM_WINDOWS

#include "MftCache.hpp"

#include <sqlite3.h>

#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace diskscan {

namespace {

std::string trimTrailingBackslash(std::string path) {
    while (path.size() > 1 && (path.back() == '\\' || path.back() == '/')) {
        path.pop_back();
    }
    return path;
}

bool execSql(sqlite3* db, const char* sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return false;
    }
    return true;
}

bool openDatabase(sqlite3** db, std::string* errorMessage) {
    const auto path = MftCache::databasePath();
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);

    const int rc = sqlite3_open_v2(
        path.c_str(),
        db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);

    if (rc != SQLITE_OK) {
        if (errorMessage) {
            std::ostringstream out;
            out << "Could not open MFT cache database at " << path;
            if (*db) {
                out << ": " << sqlite3_errmsg(*db);
                sqlite3_close(*db);
                *db = nullptr;
            }
            *errorMessage = out.str();
        } else if (*db) {
            sqlite3_close(*db);
            *db = nullptr;
        }
        return false;
    }

    static constexpr const char* kSchema =
        "PRAGMA journal_mode=WAL;"
        "CREATE TABLE IF NOT EXISTS volumes ("
        "  volume_path TEXT PRIMARY KEY,"
        "  volume_serial INTEGER NOT NULL,"
        "  scanned_at INTEGER NOT NULL,"
        "  entry_count INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS mft_entries ("
        "  volume_path TEXT NOT NULL,"
        "  mft_id INTEGER NOT NULL,"
        "  parent_mft_id INTEGER NOT NULL,"
        "  name TEXT NOT NULL,"
        "  alloc_size INTEGER NOT NULL,"
        "  is_dir INTEGER NOT NULL,"
        "  PRIMARY KEY (volume_path, mft_id)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_mft_parent"
        "  ON mft_entries(volume_path, parent_mft_id);";

    if (!execSql(*db, kSchema)) {
        if (errorMessage) {
            *errorMessage = std::string("Could not initialize MFT cache schema: ") +
                            sqlite3_errmsg(*db);
        }
        sqlite3_close(*db);
        *db = nullptr;
        return false;
    }

    return true;
}

} // namespace

std::string MftCache::databasePath() {
    const char* localAppData = std::getenv("LOCALAPPDATA");
    std::filesystem::path base =
        localAppData ? std::filesystem::path(localAppData)
                     : std::filesystem::temp_directory_path();
    return (base / "diskscangrok" / "mft_cache.db").string();
}

uint64_t MftCache::volumeSerial(const std::string& volumePath) {
    std::string root = trimTrailingBackslash(volumePath);
    if (root.size() == 2 && root[1] == ':') {
        root += "\\";
    }

    DWORD serial = 0;
    if (!GetVolumeInformationA(root.c_str(), nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)) {
        return 0;
    }
    return static_cast<uint64_t>(serial);
}

bool MftCache::enabled() {
    char value[8] = {};
    const DWORD length = GetEnvironmentVariableA("DISKSCAN_USE_MFT_CACHE", value, sizeof(value));
    if (length >= 1 && value[0] == '0') {
        return false;
    }
    return true;
}

bool MftCache::load(const std::string& volumePath,
                    uint64_t volumeSerial,
                    std::vector<MftFlatEntry>& out,
                    Metadata* metadata,
                    std::string* errorMessage) {
    out.clear();

    sqlite3* db = nullptr;
    if (!openDatabase(&db, errorMessage)) {
        return false;
    }

    sqlite3_stmt* metaStmt = nullptr;
    const char* metaSql =
        "SELECT volume_serial, entry_count, scanned_at"
        " FROM volumes WHERE volume_path = ?;";
    if (sqlite3_prepare_v2(db, metaSql, -1, &metaStmt, nullptr) != SQLITE_OK) {
        if (errorMessage) {
            *errorMessage = std::string("MFT cache metadata query failed: ") +
                            sqlite3_errmsg(db);
        }
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_text(metaStmt, 1, volumePath.c_str(), -1, SQLITE_TRANSIENT);
    const int metaRc = sqlite3_step(metaStmt);
    if (metaRc != SQLITE_ROW) {
        sqlite3_finalize(metaStmt);
        sqlite3_close(db);
        return false;
    }

    const uint64_t storedSerial =
        static_cast<uint64_t>(sqlite3_column_int64(metaStmt, 0));
    const uint64_t entryCount =
        static_cast<uint64_t>(sqlite3_column_int64(metaStmt, 1));
    const int64_t scannedAt = sqlite3_column_int64(metaStmt, 2);
    sqlite3_finalize(metaStmt);

    if (storedSerial != volumeSerial) {
        if (errorMessage) {
            std::ostringstream outMsg;
            outMsg << "MFT cache serial mismatch for " << volumePath;
            *errorMessage = outMsg.str();
        }
        sqlite3_close(db);
        return false;
    }

    sqlite3_stmt* entryStmt = nullptr;
    const char* entrySql =
        "SELECT mft_id, parent_mft_id, name, alloc_size, is_dir"
        " FROM mft_entries WHERE volume_path = ?;";
    if (sqlite3_prepare_v2(db, entrySql, -1, &entryStmt, nullptr) != SQLITE_OK) {
        if (errorMessage) {
            *errorMessage = std::string("MFT cache entry query failed: ") +
                            sqlite3_errmsg(db);
        }
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_text(entryStmt, 1, volumePath.c_str(), -1, SQLITE_TRANSIENT);
    out.reserve(static_cast<size_t>(entryCount));

    while (sqlite3_step(entryStmt) == SQLITE_ROW) {
        MftFlatEntry entry;
        entry.mftId = static_cast<uint64_t>(sqlite3_column_int64(entryStmt, 0));
        entry.parentMftId = static_cast<uint64_t>(sqlite3_column_int64(entryStmt, 1));
        const unsigned char* name = sqlite3_column_text(entryStmt, 2);
        entry.name = name ? reinterpret_cast<const char*>(name) : "";
        entry.allocSize = static_cast<uint64_t>(sqlite3_column_int64(entryStmt, 3));
        entry.isDir = sqlite3_column_int(entryStmt, 4) != 0;
        out.push_back(std::move(entry));
    }

    sqlite3_finalize(entryStmt);
    sqlite3_close(db);

    if (out.empty()) {
        if (errorMessage) {
            *errorMessage = "MFT cache was empty for " + volumePath;
        }
        return false;
    }

    if (metadata) {
        metadata->volumeSerial = storedSerial;
        metadata->entryCount = out.size();
        metadata->scannedAt = scannedAt;
    }

    return true;
}

bool MftCache::save(const std::string& volumePath,
                    uint64_t volumeSerial,
                    const std::vector<MftFlatEntry>& entries,
                    std::string* errorMessage) {
    if (entries.empty()) {
        if (errorMessage) {
            *errorMessage = "Refusing to save an empty MFT cache";
        }
        return false;
    }

    sqlite3* db = nullptr;
    if (!openDatabase(&db, errorMessage)) {
        return false;
    }

    if (!execSql(db, "BEGIN IMMEDIATE;")) {
        if (errorMessage) {
            *errorMessage = std::string("MFT cache transaction failed: ") +
                            sqlite3_errmsg(db);
        }
        sqlite3_close(db);
        return false;
    }

    sqlite3_stmt* deleteEntries = nullptr;
    const char* deleteEntriesSql =
        "DELETE FROM mft_entries WHERE volume_path = ?;";
    if (sqlite3_prepare_v2(db, deleteEntriesSql, -1, &deleteEntries, nullptr) != SQLITE_OK) {
        execSql(db, "ROLLBACK;");
        if (errorMessage) {
            *errorMessage = std::string("MFT cache delete failed: ") + sqlite3_errmsg(db);
        }
        sqlite3_close(db);
        return false;
    }
    sqlite3_bind_text(deleteEntries, 1, volumePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(deleteEntries);
    sqlite3_finalize(deleteEntries);

    sqlite3_stmt* insertEntry = nullptr;
    const char* insertEntrySql =
        "INSERT INTO mft_entries"
        " (volume_path, mft_id, parent_mft_id, name, alloc_size, is_dir)"
        " VALUES (?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, insertEntrySql, -1, &insertEntry, nullptr) != SQLITE_OK) {
        execSql(db, "ROLLBACK;");
        if (errorMessage) {
            *errorMessage = std::string("MFT cache insert prepare failed: ") +
                            sqlite3_errmsg(db);
        }
        sqlite3_close(db);
        return false;
    }

    for (const auto& entry : entries) {
        sqlite3_reset(insertEntry);
        sqlite3_clear_bindings(insertEntry);
        sqlite3_bind_text(insertEntry, 1, volumePath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(insertEntry, 2, static_cast<sqlite3_int64>(entry.mftId));
        sqlite3_bind_int64(insertEntry, 3, static_cast<sqlite3_int64>(entry.parentMftId));
        sqlite3_bind_text(insertEntry, 4, entry.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(insertEntry, 5, static_cast<sqlite3_int64>(entry.allocSize));
        sqlite3_bind_int(insertEntry, 6, entry.isDir ? 1 : 0);
        if (sqlite3_step(insertEntry) != SQLITE_DONE) {
            sqlite3_finalize(insertEntry);
            execSql(db, "ROLLBACK;");
            if (errorMessage) {
                *errorMessage = std::string("MFT cache insert failed: ") + sqlite3_errmsg(db);
            }
            sqlite3_close(db);
            return false;
        }
    }
    sqlite3_finalize(insertEntry);

    const auto scannedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_stmt* upsertVolume = nullptr;
    const char* upsertVolumeSql =
        "INSERT INTO volumes (volume_path, volume_serial, scanned_at, entry_count)"
        " VALUES (?, ?, ?, ?)"
        " ON CONFLICT(volume_path) DO UPDATE SET"
        " volume_serial = excluded.volume_serial,"
        " scanned_at = excluded.scanned_at,"
        " entry_count = excluded.entry_count;";
    if (sqlite3_prepare_v2(db, upsertVolumeSql, -1, &upsertVolume, nullptr) != SQLITE_OK) {
        execSql(db, "ROLLBACK;");
        if (errorMessage) {
            *errorMessage = std::string("MFT cache volume upsert failed: ") +
                            sqlite3_errmsg(db);
        }
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_text(upsertVolume, 1, volumePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(upsertVolume, 2, static_cast<sqlite3_int64>(volumeSerial));
    sqlite3_bind_int64(upsertVolume, 3, static_cast<sqlite3_int64>(scannedAt));
    sqlite3_bind_int64(upsertVolume, 4, static_cast<sqlite3_int64>(entries.size()));
    const int upsertRc = sqlite3_step(upsertVolume);
    sqlite3_finalize(upsertVolume);

    if (upsertRc != SQLITE_DONE || !execSql(db, "COMMIT;")) {
        execSql(db, "ROLLBACK;");
        if (errorMessage) {
            *errorMessage = std::string("MFT cache commit failed: ") + sqlite3_errmsg(db);
        }
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return true;
}

bool MftCache::invalidate(const std::string& volumePath, std::string* errorMessage) {
    sqlite3* db = nullptr;
    if (!openDatabase(&db, errorMessage)) {
        return false;
    }

    sqlite3_stmt* deleteEntries = nullptr;
    const char* deleteEntriesSql =
        "DELETE FROM mft_entries WHERE volume_path = ?;";
    if (sqlite3_prepare_v2(db, deleteEntriesSql, -1, &deleteEntries, nullptr) != SQLITE_OK) {
        if (errorMessage) {
            *errorMessage = std::string("MFT cache invalidate failed: ") + sqlite3_errmsg(db);
        }
        sqlite3_close(db);
        return false;
    }
    sqlite3_bind_text(deleteEntries, 1, volumePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(deleteEntries);
    sqlite3_finalize(deleteEntries);

    sqlite3_stmt* deleteVolume = nullptr;
    const char* deleteVolumeSql =
        "DELETE FROM volumes WHERE volume_path = ?;";
    if (sqlite3_prepare_v2(db, deleteVolumeSql, -1, &deleteVolume, nullptr) != SQLITE_OK) {
        if (errorMessage) {
            *errorMessage = std::string("MFT cache invalidate failed: ") + sqlite3_errmsg(db);
        }
        sqlite3_close(db);
        return false;
    }
    sqlite3_bind_text(deleteVolume, 1, volumePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(deleteVolume);
    sqlite3_finalize(deleteVolume);

    sqlite3_close(db);
    return true;
}

} // namespace diskscan

#endif // PLATFORM_WINDOWS