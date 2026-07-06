#include "Search.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace diskscan {
namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() &&
           text.compare(0, prefix.size(), prefix) == 0;
}

bool containsInsensitive(const std::string& haystackLower, const std::string& needle) {
    if (needle.empty()) return true;
    return haystackLower.find(toLower(needle)) != std::string::npos;
}

const std::unordered_set<std::string_view>& extensionsFor(Search::FileCategory category) {
    static const std::unordered_set<std::string_view> kAudio = {
        "mp3", "flac", "wav", "m4a", "aac", "ogg", "oga", "wma", "opus",
        "aiff", "aif", "ape", "alac", "mid", "midi", "wv", "tta", "mka",
    };
    static const std::unordered_set<std::string_view> kVideo = {
        "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v", "mpg",
        "mpeg", "ts", "m2ts", "3gp", "ogv", "vob", "divx", "asf", "rm", "rmvb",
    };
    static const std::unordered_set<std::string_view> kCompressed = {
        "zip", "rar", "7z", "tar", "gz", "bz2", "xz", "tgz", "tbz2", "txz",
        "cab", "iso", "dmg", "arj", "lz", "zst", "lzma", "cpio", "jar", "war",
    };
    static const std::unordered_set<std::string_view> kPictures = {
        "jpg", "jpeg", "png", "gif", "bmp", "webp", "tiff", "tif", "svg",
        "ico", "heic", "heif", "raw", "cr2", "nef", "orf", "dng", "psd",
        "jfif", "avif",
    };
    static const std::unordered_set<std::string_view> kDocuments = {
        "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "txt", "rtf",
        "odt", "ods", "odp", "csv", "md", "epub", "pages", "numbers", "key",
        "one", "json", "xml", "html", "htm", "log",
    };
    static const std::unordered_set<std::string_view> kExecutable = {
        "exe", "msi", "dll", "sys", "bat", "cmd", "ps1", "com", "scr",
        "appx", "msix", "lnk", "cpl", "drv", "ocx",
    };
    static const std::unordered_set<std::string_view> kEmpty;

    switch (category) {
        case Search::FileCategory::Audio:       return kAudio;
        case Search::FileCategory::Video:       return kVideo;
        case Search::FileCategory::Compressed:  return kCompressed;
        case Search::FileCategory::Pictures:    return kPictures;
        case Search::FileCategory::Documents:   return kDocuments;
        case Search::FileCategory::Executable:  return kExecutable;
        case Search::FileCategory::Folders:     return kEmpty;
    }
    return kEmpty;
}

} // namespace

bool Search::categoryFromKey(const std::string& key, FileCategory& category) {
    if (key == "audio" || key == "music") {
        category = FileCategory::Audio;
        return true;
    }
    if (key == "video" || key == "movie" || key == "movies") {
        category = FileCategory::Video;
        return true;
    }
    if (key == "compressed" || key == "archive" || key == "archives" || key == "zip") {
        category = FileCategory::Compressed;
        return true;
    }
    if (key == "pictures" || key == "picture" || key == "image" || key == "images" ||
        key == "photo" || key == "photos") {
        category = FileCategory::Pictures;
        return true;
    }
    if (key == "documents" || key == "document" || key == "docs") {
        category = FileCategory::Documents;
        return true;
    }
    if (key == "executable" || key == "executables" || key == "exe") {
        category = FileCategory::Executable;
        return true;
    }
    if (key == "folders" || key == "folder") {
        category = FileCategory::Folders;
        return true;
    }
    return false;
}

bool Search::extensionInCategory(const std::string& extension, FileCategory category) {
    if (category == FileCategory::Folders) {
        return false;
    }
    if (extension.empty()) {
        return false;
    }
    const auto& allowed = extensionsFor(category);
    return allowed.count(extension) != 0;
}

bool Search::parseSize(const std::string& text, Term::SizeOp& op, uint64_t& bytes) {
    if (text.empty()) return false;

    size_t pos = 0;
    op = Term::SizeOp::None;
    if (text[pos] == '>' || text[pos] == '<' || text[pos] == '=') {
        const char first = text[pos++];
        if (pos < text.size() && text[pos] == '=') {
            ++pos;
            op = (first == '>') ? Term::SizeOp::Ge :
                 (first == '<') ? Term::SizeOp::Le : Term::SizeOp::Eq;
        } else {
            op = (first == '>') ? Term::SizeOp::Gt :
                 (first == '<') ? Term::SizeOp::Lt : Term::SizeOp::Eq;
        }
    }

    std::string number;
    while (pos < text.size() &&
           (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '.')) {
        number.push_back(text[pos++]);
    }
    if (number.empty()) return false;

    double value = 0.0;
    try {
        value = std::stod(number);
    } catch (...) {
        return false;
    }

    std::string unit = toLower(text.substr(pos));
    uint64_t multiplier = 1;
    if (unit.empty() || unit == "b") {
        multiplier = 1;
    } else if (unit == "kb" || unit == "kib" || unit == "k") {
        multiplier = 1024ULL;
    } else if (unit == "mb" || unit == "mib" || unit == "m") {
        multiplier = 1024ULL * 1024ULL;
    } else if (unit == "gb" || unit == "gib" || unit == "g") {
        multiplier = 1024ULL * 1024ULL * 1024ULL;
    } else if (unit == "tb" || unit == "tib" || unit == "t") {
        multiplier = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    } else {
        return false;
    }

    bytes = static_cast<uint64_t>(value * static_cast<double>(multiplier));
    return true;
}

bool Search::globMatch(const std::string& patternLower, const std::string& nameLower) {
    if (patternLower == "*") return true;

    const auto star = patternLower.find('*');
    if (star == std::string::npos) {
        if (nameLower.size() >= patternLower.size() + 1 &&
            nameLower[nameLower.size() - patternLower.size() - 1] == '.' &&
            nameLower.compare(nameLower.size() - patternLower.size(), patternLower.size(),
                              patternLower) == 0) {
            return true;
        }
        return nameLower == patternLower;
    }

    const std::string prefix = patternLower.substr(0, star);
    const std::string suffix = patternLower.substr(star + 1);

    if (!prefix.empty() && !startsWith(nameLower, prefix)) return false;
    if (!suffix.empty() && nameLower.size() < suffix.size()) return false;
    if (!suffix.empty()) {
        return nameLower.compare(nameLower.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    return true;
}

std::vector<Search::Term> Search::parse(const std::string& rawQuery) {
    std::vector<Term> terms;
    std::istringstream stream(rawQuery);
    std::string token;
    while (stream >> token) {
        token = trim(token);
        if (token.empty()) continue;

        Term term;
        const auto colon = token.find(':');
        if (colon != std::string::npos) {
            const std::string key = toLower(token.substr(0, colon));
            const std::string value = token.substr(colon + 1);

            if (key == "ext") {
                term.kind = Term::Kind::Extension;
                term.value = toLower(value);
                if (term.value.empty() && value.size() > 1 && value[0] == '.') {
                    term.value = toLower(value.substr(1));
                }
            } else if (key == "name") {
                term.kind = Term::Kind::Name;
                term.value = value;
            } else if (key == "path") {
                term.kind = Term::Kind::Path;
                term.value = value;
            } else if (key == "size") {
                term.kind = Term::Kind::SizeCompare;
                term.value = value;
                if (!parseSize(toLower(value), term.sizeOp, term.sizeBytes)) {
                    continue;
                }
            } else if (key == "file") {
                term.kind = Term::Kind::FilesOnly;
            } else if (key == "dir") {
                term.kind = Term::Kind::DirsOnly;
            } else if (key == "type" || key == "cat" || key == "kind") {
                if (!categoryFromKey(toLower(value), term.category)) {
                    continue;
                }
                term.kind = Term::Kind::Category;
            } else if (categoryFromKey(key, term.category)) {
                term.kind = Term::Kind::Category;
            } else {
                term.kind = Term::Kind::Text;
                term.value = token;
            }
        } else if (startsWith(token, "*.")) {
            term.kind = Term::Kind::NameGlob;
            term.value = toLower(token.substr(2));
        } else if (startsWith(token, ".")) {
            term.kind = Term::Kind::Extension;
            term.value = toLower(token.substr(1));
        } else {
            term.kind = Term::Kind::Text;
            term.value = token;
        }

        terms.push_back(std::move(term));
    }
    return terms;
}

bool Search::matches(const IndexEntry& entry, const Term& term) {
    switch (term.kind) {
        case Term::Kind::Text:
            return containsInsensitive(entry.nameLower, term.value) ||
                   containsInsensitive(entry.pathLower, term.value);
        case Term::Kind::Name:
            return containsInsensitive(entry.nameLower, term.value);
        case Term::Kind::Path:
            return containsInsensitive(entry.pathLower, term.value);
        case Term::Kind::Extension:
            return entry.extensionLower == toLower(term.value);
        case Term::Kind::NameGlob:
            return globMatch(term.value, entry.nameLower);
        case Term::Kind::FilesOnly:
            return entry.type == NodeType::File;
        case Term::Kind::DirsOnly:
            return entry.type == NodeType::Directory;
        case Term::Kind::Category:
            if (term.category == FileCategory::Folders) {
                return entry.type == NodeType::Directory;
            }
            return entry.type == NodeType::File &&
                   extensionInCategory(entry.extensionLower, term.category);
        case Term::Kind::SizeCompare: {
            switch (term.sizeOp) {
                case Term::SizeOp::Gt: return entry.sizeBytes > term.sizeBytes;
                case Term::SizeOp::Lt: return entry.sizeBytes < term.sizeBytes;
                case Term::SizeOp::Eq: return entry.sizeBytes == term.sizeBytes;
                case Term::SizeOp::Ge: return entry.sizeBytes >= term.sizeBytes;
                case Term::SizeOp::Le: return entry.sizeBytes <= term.sizeBytes;
                default:               return entry.sizeBytes >= term.sizeBytes;
            }
        }
    }
    return true;
}

std::vector<const IndexEntry*> Search::query(const FileIndex& index,
                                             const std::string& rawQuery,
                                             const SearchOptions& options) {
    const auto trimmed = trim(rawQuery);
    if (trimmed.empty()) return {};

    const auto terms = parse(trimmed);
    if (terms.empty()) return {};

    std::vector<const IndexEntry*> hits;
    hits.reserve(std::min(options.maxResults, index.entries().size()));

    for (const auto& entry : index.entries()) {
        bool ok = true;
        for (const auto& term : terms) {
            if (!Search::matches(entry, term)) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;

        hits.push_back(&entry);
        if (hits.size() >= options.maxResults) break;
    }

    std::sort(hits.begin(), hits.end(), [](const IndexEntry* a, const IndexEntry* b) {
        if (a->sizeBytes != b->sizeBytes) return a->sizeBytes > b->sizeBytes;
        return a->pathLower < b->pathLower;
    });

    return hits;
}

} // namespace diskscan