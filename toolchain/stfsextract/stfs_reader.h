#pragma once
// Xbox 360 STFS packages (CON / LIVE / PIRS) read from a file descriptor, for the XDroid360
// launcher's on-device build (recipe format v2, the "extract" step with format "stfs").
// A port of SoDRecomp/content/stfs.cpp (the PC launcher's reader, Windows API) to pread on
// an fd the launcher opened through SAF: the same layout, the same checks. The packages are
// the player's files and treated as untrusted: every block, name and size is checked first.
// No dependency on the runtime: this builds alone as libstfsextract.so (stfs_jni.cpp) and as
// a host tool for tests (stfs_cli.cpp).

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace stfs
{
    // StfsExtractor.nativeExtract's return codes (recipe format v2, "STFS extraction").
    enum class Result : int
    {
        Ok = 0,
        SourceUnreadable = 1,
        SourceNotSeekable = 2,
        ExtractFailed = 3,
        NotEnoughSpace = 4,
        Cancelled = 5,
    };

    // Content types (XCONTENTTYPE_*).
    constexpr uint32_t kContentSave = 0x00000001;
    constexpr uint32_t kContentMarketplace = 0x00000002; // downloadable content
    constexpr uint32_t kContentTitleUpdate = 0x000B0000;
    constexpr uint32_t kContentArcadeTitle = 0x000D0000; // an XBLA game

    struct Header
    {
        std::string magic;          // "CON ", "LIVE" or "PIRS"
        uint32_t titleId = 0;
        uint32_t contentType = 0;
        uint32_t mediaId = 0;
        std::string contentId;      // the header's SHA-1 (hex), which the console names packages by
        std::u16string displayName;
        bool readOnly = false;      // LIVE/PIRS as delivered; CON (saves) are writable
        bool stfs = false;          // an STFS volume (not SVOD, as games on demand are)
    };

    struct File
    {
        std::string path;           // '/'-separated, names checked
        uint64_t size = 0;
        uint32_t startBlock = 0;
        bool contiguous = false;
    };

    class Package
    {
    public:
        Package(const Package&) = delete;
        Package& operator=(const Package&) = delete;

        // Reads the header of the package open as `fd` (not owned, never closed, read with
        // pread only, so its offset is left alone). nullptr if it isn't a package, with
        // why in `error` and the code in `result`.
        static std::unique_ptr<Package> Open(int fd, Result& result, std::string& error);

        Header header;
        std::vector<File> files;

        // Reads the file table: read-only STFS packages only (what extraction needs).
        Result ReadTable(std::string& error);
        uint64_t TotalSize() const;

        // Extracts every file into `into` (created; existing files are overwritten, so a
        // package cut off is redone). progress(done, total) is called as bytes are written
        // and returning false cancels. What is written before an error or cancel stays.
        Result Extract(const std::string& into, const std::function<bool(uint64_t, uint64_t)>& progress,
            std::string& error);

    private:
        Package() = default;
        bool ReadAt(uint64_t offset, void* buffer, size_t size);
        uint64_t BlockOffset(uint32_t block) const;
        bool ReadBlock(uint32_t block, void* buffer, size_t size, std::string& error);
        bool ReadRun(uint32_t block, uint32_t count, uint8_t* buffer, size_t size, std::string& error);
        bool NextBlock(uint32_t block, uint32_t& next, std::string& error);

        int m_fd = -1;
        uint64_t m_size = 0;
        uint64_t m_base = 0;
        uint32_t m_tableBlocks = 0;
        uint32_t m_tableBlock = 0;
        uint32_t m_totalBlocks = 0;
        // The hash table NextBlock read last (a chain stays in one for 170 blocks).
        std::vector<uint8_t> m_hashTable;
        uint64_t m_hashTableAt = UINT64_MAX;
    };

    // Where a package's files go under the instance's shared folder (recipe ${GAME}), as
    // SoD's runtime looks for them:
    //   an XBLA game (0x000D0000)  -> <instance>/game/
    //   DLC (0x00000002)           -> <instance>/userdata/content/<title ID>/00000002/<content ID>/
    //                                 (plus <content ID>.name beside it, the display name)
    //   a title update (0x000B0000) -> <instance>/title_update/  (kernel/title_update.cpp)
    // Empty for any other content type (saves, ...): such packages are passed over.
    std::string TargetFolder(const Header& header, const std::string& instance);

    // Writes <folder>/<content ID>.name for DLC: the display name as 256 bytes of big-endian
    // UTF-16, as the game's XamContentCreateEx keeps it (SoDRecomp/content/content.h).
    bool WriteDisplayName(const Header& header, const std::string& folder);

    // The header as JSON for StfsExtractor.nativeProbe: title_id, content_type, content_id,
    // media_id, magic, display_name, size (the bytes extraction writes; 0 if not read-only STFS).
    std::string ProbeJson(Package& package);
}
