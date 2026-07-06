#pragma once

#include "search/FileIndex.hpp"

#include <string>
#include <vector>

namespace diskscan {

struct SearchOptions {
    size_t maxResults = 5000;
};

/// Everything-style query matching over a flat file index.
///
/// Supported filters (space-separated terms are ANDed):
///   text              - matches name or full path (case-insensitive)
///   *.ext / .ext      - extension glob on file name
///   ext:zip           - extension filter
///   name:foo          - file/folder name only
///   path:windows      - full path only
///   file: / dir:      - limit to files or directories
///   size:>100mb       - size comparisons (b, kb, mb, gb, tb)
///   audio: video: compressed: pictures: documents: executable: folders:
///   type:audio        - same category filters via type: prefix
class Search {
public:
    enum class FileCategory {
        Audio,
        Video,
        Compressed,
        Pictures,
        Documents,
        Executable,
        Folders,
    };

    static std::vector<const IndexEntry*> query(const FileIndex& index,
                                                const std::string& rawQuery,
                                                const SearchOptions& options = {});

private:
    struct Term {
        enum class Kind {
            Text,
            Name,
            Path,
            Extension,
            NameGlob,
            SizeCompare,
            FilesOnly,
            DirsOnly,
            Category,
        };

        Kind          kind = Kind::Text;
        std::string   value;
        FileCategory  category = FileCategory::Audio;
        enum class SizeOp { None, Gt, Lt, Eq, Ge, Le };
        SizeOp        sizeOp = SizeOp::None;
        uint64_t      sizeBytes = 0;
    };

    static std::vector<Term> parse(const std::string& rawQuery);
    static bool matches(const IndexEntry& entry, const Term& term);
    static bool parseSize(const std::string& text, Term::SizeOp& op, uint64_t& bytes);
    static bool globMatch(const std::string& patternLower, const std::string& nameLower);
    static bool categoryFromKey(const std::string& key, FileCategory& category);
    static bool extensionInCategory(const std::string& extension, FileCategory category);
};

} // namespace diskscan