#ifdef PLATFORM_WINDOWS

#include "MftReader.hpp"

#include <windows.h>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cwchar>
#include <codecvt>
#include <locale>
#include <string>
#include <memory>
#include <algorithm>
#include <cstddef>
#include <sstream>

namespace diskscan {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

std::string wideToUtf8(const wchar_t* wstr, int len) {
    if (len <= 0) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, wstr, len, nullptr, 0, nullptr, nullptr);
    std::string out(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, len, out.data(), needed, nullptr, nullptr);
    return out;
}

// Apply NTFS fixup array to a sector-aligned buffer (in-place).
void applyFixup(uint8_t* buf, uint32_t recordSize, uint16_t sectorSize) {
    struct RecordHdr { uint8_t sig[4]; uint16_t usaOff; uint16_t usaCnt; };
    auto* hdr = reinterpret_cast<RecordHdr*>(buf);
    if (hdr->usaOff == 0) return;
    uint16_t* usa = reinterpret_cast<uint16_t*>(buf + hdr->usaOff);
    uint16_t  seq = usa[0];
    uint32_t  numSectors = recordSize / sectorSize;
    for (uint32_t i = 0; i < numSectors; ++i) {
        uint16_t* endOfSector = reinterpret_cast<uint16_t*>(
            buf + (i + 1) * sectorSize - 2);
        if (*endOfSector != seq) {
            // Sequence mismatch – record is corrupt; zero it out.
            memset(buf, 0, recordSize);
            return;
        }
        *endOfSector = usa[i + 1];
    }
}

bool attrInRecord(const uint8_t* recordStart,
                  uint32_t recordSize,
                  const uint8_t* attrPtr,
                  uint32_t attrLength) {
    auto attrOffset = static_cast<uint64_t>(attrPtr - recordStart);
    return attrOffset < recordSize &&
           attrLength <= recordSize - attrOffset;
}

std::string windowsErrorMessage(const std::string& operation, DWORD error) {
    LPSTR message = nullptr;
    DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&message),
        0,
        nullptr);

    std::ostringstream out;
    out << operation << " failed";
    if (error != ERROR_SUCCESS) {
        out << " (" << error << ")";
    }
    if (length > 0 && message) {
        std::string text(message, length);
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == '.')) {
            text.pop_back();
        }
        out << ": " << text;
        LocalFree(message);
    }
    return out.str();
}

void setError(const MftReader::Config& cfg, std::string message) {
    if (cfg.errorMessage) {
        *cfg.errorMessage = std::move(message);
    }
}

bool enableVolumeReadPrivilege(const char* privilegeName) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &token)) {
        return false;
    }

    LUID luid{};
    if (!LookupPrivilegeValueA(nullptr, privilegeName, &luid)) {
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);

    const bool enabled = GetLastError() != ERROR_NOT_ALL_ASSIGNED;
    CloseHandle(token);
    return enabled;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// MftReader::read
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<FileNode> MftReader::read(const Config& cfg) {
    // NTFS volume reads require backup/restore privilege even for Administrators.
    enableVolumeReadPrivilege(SE_BACKUP_NAME);
    enableVolumeReadPrivilege(SE_RESTORE_NAME);

    // ── 1. Open volume ────────────────────────────────────────────────────────
    std::string volPath = "\\\\.\\" + cfg.volumePath;
    // Convert to wide
    int wlen = MultiByteToWideChar(CP_UTF8, 0, volPath.c_str(), -1, nullptr, 0);
    std::wstring wVolPath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, volPath.c_str(), -1, wVolPath.data(), wlen);

    HANDLE hVol = CreateFileW(
        wVolPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);

    if (hVol == INVALID_HANDLE_VALUE) {
        setError(cfg, windowsErrorMessage("Open " + volPath, GetLastError()));
        return nullptr;
    }

    auto closeGuard = [hVol](void*) { CloseHandle(hVol); };
    std::unique_ptr<void, decltype(closeGuard)> guard(hVol, closeGuard);

    // ── 2. Ask NTFS for volume geometry ───────────────────────────────────────
    NTFS_VOLUME_DATA_BUFFER volumeData{};
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(
            hVol,
            FSCTL_GET_NTFS_VOLUME_DATA,
            nullptr,
            0,
            &volumeData,
            sizeof(volumeData),
            &bytesReturned,
            nullptr)) {
        setError(cfg, windowsErrorMessage("FSCTL_GET_NTFS_VOLUME_DATA", GetLastError()));
        return nullptr;
    }

    const uint32_t bytesPerSector = volumeData.BytesPerSector;
    const uint32_t bytesPerFrs = volumeData.BytesPerFileRecordSegment;
    if (bytesPerSector == 0 || bytesPerFrs == 0) {
        setError(cfg, "FSCTL_GET_NTFS_VOLUME_DATA returned invalid NTFS geometry");
        return nullptr;
    }

    const uint64_t maxRecordNumber =
        static_cast<uint64_t>(volumeData.MftValidDataLength.QuadPart) / bytesPerFrs;

    // ── 4. Iterate all MFT records ────────────────────────────────────────────
    // We store a flat map: MFT record# (48-bit) → {name, parentRef, allocSize, isDir}
    struct FlatEntry {
        std::string name;
        uint64_t    parentRef  = 0;
        uint64_t    allocSize  = 0;
        bool        isDir      = false;
        bool        inUse      = false;
    };

    std::unordered_map<uint64_t, FlatEntry> entries;
    entries.reserve(512 * 1024); // pre-alloc for typical drives

    std::vector<uint8_t> record(bytesPerFrs);
    std::vector<uint8_t> recordOutBuf(
        offsetof(NTFS_FILE_RECORD_OUTPUT_BUFFER, FileRecordBuffer) + bytesPerFrs);
    uint64_t             filesProcessed = 0;

    // FSCTL_GET_NTFS_FILE_RECORD returns the highest in-use record <= the request.
    // Walk upward by request id, skipping gaps and already-seen records.
    std::unordered_set<uint64_t> seenRecords;
    seenRecords.reserve(512 * 1024);

    uint64_t nextRequest = 0;
    const uint64_t lastRequest = maxRecordNumber + 1024;

    while (nextRequest <= lastRequest) {
        if (cfg.abortFlag && cfg.abortFlag->load()) break;

        NTFS_FILE_RECORD_INPUT_BUFFER recordIn{};
        recordIn.FileReferenceNumber.QuadPart = static_cast<LONGLONG>(nextRequest);
        std::fill(recordOutBuf.begin(), recordOutBuf.end(), 0);

        if (!DeviceIoControl(
                hVol,
                FSCTL_GET_NTFS_FILE_RECORD,
                &recordIn,
                sizeof(recordIn),
                recordOutBuf.data(),
                static_cast<DWORD>(recordOutBuf.size()),
                &bytesReturned,
                nullptr)) {
            ++nextRequest;
            continue;
        }

        auto* recordOut =
            reinterpret_cast<NTFS_FILE_RECORD_OUTPUT_BUFFER*>(recordOutBuf.data());
        const uint64_t actualRecord =
            static_cast<uint64_t>(recordOut->FileReferenceNumber.QuadPart) &
            0x0000FFFFFFFFFFFFULL;

        uint64_t followingRequest = nextRequest + 1;
        if (actualRecord + 1 > followingRequest) {
            followingRequest = actualRecord + 1;
        }

        if (seenRecords.count(actualRecord)) {
            nextRequest = followingRequest;
            continue;
        }
        seenRecords.insert(actualRecord);

        if (recordOut->FileRecordLength < sizeof(MftRecordHeader) ||
            recordOut->FileRecordLength > bytesPerFrs) {
            nextRequest = followingRequest;
            continue;
        }

        std::fill(record.begin(), record.end(), 0);
        std::memcpy(record.data(), recordOut->FileRecordBuffer, recordOut->FileRecordLength);
        // FSCTL_GET_NTFS_FILE_RECORD returns records with fixups already applied.
        auto* rh = reinterpret_cast<MftRecordHeader*>(record.data());
        if (std::string(rh->signature, 4) != "FILE") {
            nextRequest = followingRequest;
            continue;
        }
        if (!(rh->flags & FILE_FLAG_IN_USE)) {
            nextRequest = followingRequest;
            continue;
        }
        if (rh->attributeOffset >= recordOut->FileRecordLength) {
            nextRequest = followingRequest;
            continue;
        }

        FlatEntry fe;
        fe.inUse = true;
        fe.isDir = (rh->flags & FILE_FLAG_DIRECTORY) != 0;

        const uint8_t* ap = record.data() + rh->attributeOffset;
        while (true) {
            auto* ah = reinterpret_cast<const AttributeHeader*>(ap);
            if (ah->typeCode == AT_END || ah->typeCode == 0) break;
            if (ah->length == 0) break;
            if (!attrInRecord(record.data(), recordOut->FileRecordLength, ap, ah->length)) break;

            if (ah->typeCode == AT_FILE_NAME) {
                const uint8_t* val = ap;
                if (ah->nonResident == 0) {
                    auto* rattr = reinterpret_cast<const ResidentAttributeHeader*>(ap);
                    if (rattr->valueOffset >= ah->length ||
                        rattr->valueLength > ah->length - rattr->valueOffset) {
                        ap += ah->length;
                        continue;
                    }
                    val = ap + rattr->valueOffset;
                }
                auto* fn = reinterpret_cast<const FileNameAttribute*>(val);
                // namespace: 0=POSIX, 1=Win32, 2=DOS, 3=Win32&DOS
                // Prefer Win32 or Win32+DOS. Fall back to any if we have nothing.
                bool better = false;
                if (fe.name.empty()) {
                    better = true;
                } else {
                    // current fe namespace (we don't store it, so use simple heuristic:
                    // if new one is Win32 or Win32+DOS (1 or 3) prefer it
                    if (fn->filenameNamespace == 1 || fn->filenameNamespace == 3) {
                        better = true;
                    }
                }
                if (better) {
                    const wchar_t* nameW =
                        reinterpret_cast<const wchar_t*>(val + sizeof(FileNameAttribute));
                    fe.name      = wideToUtf8(nameW, fn->filenameLength);
                    fe.parentRef = fn->parentMftRef & 0x0000FFFFFFFFFFFF;
                    fe.allocSize = fn->allocatedSize;
                }
            } else if (ah->typeCode == AT_DATA && ah->nonResident) {
                auto* nrh = reinterpret_cast<const NonResidentAttributeHeader*>(ap);
                if (ah->nameLength == 0) {
                    fe.allocSize = nrh->allocatedSize;
                }
            } else if (ah->typeCode == AT_DATA && !ah->nonResident) {
                auto* rattr = reinterpret_cast<const ResidentAttributeHeader*>(ap);
                if (ah->nameLength == 0 && fe.allocSize == 0) {
                    fe.allocSize = rattr->valueLength;
                }
            }

            ap += ah->length;
        }

        // Always record in-use entries (root dir record 5 may have a "." or POSIX name)
        if (actualRecord == 5 || !fe.name.empty())
            entries[actualRecord] = std::move(fe);

        ++filesProcessed;
        if (cfg.progressCb && (filesProcessed % 10000) == 0)
            cfg.progressCb(filesProcessed);

        nextRequest = followingRequest;
    }

    if (cfg.progressCb) cfg.progressCb(filesProcessed);

    // ── 5. Build tree ─────────────────────────────────────────────────────────
    // Ensure we have a root (record 5) even if the FSCTL walk didn't surface it
    // with a name (some volumes / privilege levels behave oddly for the root dir record).
    if (entries.find(5) == entries.end()) {
        FlatEntry rootFe;
        rootFe.name = cfg.volumePath + "\\";
        rootFe.isDir = true;
        rootFe.inUse = true;
        entries[5] = rootFe;
    }

    std::unordered_map<uint64_t, std::shared_ptr<FileNode>> nodes;
    nodes.reserve(entries.size());

    for (auto& [id, fe] : entries) {
        auto node       = std::make_shared<FileNode>();
        node->name      = fe.name;
        node->sizeBytes = fe.allocSize;
        node->type      = fe.isDir ? NodeType::Directory : NodeType::File;
        nodes[id]       = node;
    }

    // Root of the requested path (record 5 is always the NTFS root dir)
    // Link children to parents
    std::shared_ptr<FileNode> root;
    for (auto& [id, fe] : entries) {
        if (!nodes.count(id)) continue;
        auto& child = nodes[id];
        uint64_t parentId = (fe.parentRef & 0x0000FFFFFFFFFFFFULL);
        if (parentId == id || !nodes.count(parentId)) {
            // orphan or root
            if (id == 5) { // NTFS root directory record (always force a nice name)
                root = child;
                root->name = cfg.volumePath + "\\";
            }
            continue;
        }
        auto& parent = nodes[parentId];
        child->parent = parent.get();
        child->depth  = parent->depth + 1;
        parent->children.push_back(child);
    }

    if (!root) {
        setError(cfg, "MFT read completed but no NTFS root record was found");
        // Fallback: pick a node with no valid parent
        for (auto& [id, nd] : nodes) {
            if (!nd->parent) { root = nd; break; }
        }
    }

    if (root) {
        root->computeSizes();
        root->sortChildren();
        root->path = cfg.volumePath + "\\";
        // propagate paths (best-effort)
        std::function<void(FileNode*, const std::string&)> setPaths =
            [&](FileNode* n, const std::string& base) {
            for (auto& c : n->children) {
                c->path = base + c->name + (c->isDir() ? "\\" : "");
                setPaths(c.get(), c->path);
            }
        };
        setPaths(root.get(), root->path);
    }

    return root;
}

} // namespace diskscan

#endif // PLATFORM_WINDOWS
