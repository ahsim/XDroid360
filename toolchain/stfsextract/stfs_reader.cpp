#include "stfs_reader.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
// The host tool only (stfs_cli.cpp): no pread, so a seek and a read under a lock.
#include <io.h>
#include <mutex>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace stfs
{
    namespace
    {
        constexpr uint32_t kBlock = 0x1000;
        // Data blocks covered by one hash table at each level. The blocks under one
        // level-0 table are next to each other in the file.
        constexpr uint32_t kLevels[] = { 170, 170 * 170, 170 * 170 * 170 };
        constexpr size_t kHeaderRead = 0x1800;
        constexpr uint32_t kMaxTableBlocks = 1024; // 65,536 entries
        constexpr int kMaxDepth = 32;
        constexpr size_t kDisplayNameSize = 256;
        // Room kept free beyond the files themselves.
        constexpr uint64_t kSpaceMargin = 64ull << 20;
        // How often progress is reported, in bytes written.
        constexpr uint64_t kProgressStep = 4ull << 20;

        uint32_t Be32(const uint8_t* p)
        {
            return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
        }

        uint32_t Le24(const uint8_t* p)
        {
            return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16);
        }

        bool IsMagic(const uint8_t* p)
        {
            return memcmp(p, "CON ", 4) == 0 || memcmp(p, "LIVE", 4) == 0 || memcmp(p, "PIRS", 4) == 0;
        }

        std::string Lower(std::string s)
        {
            for (char& c : s)
                c = char(tolower(uint8_t(c)));
            return s;
        }

        // A name that stays in its folder and that Windows can hold too (the reader is
        // the same as the PC launcher's: an instance can be copied to a PC).
        bool ValidName(const std::string& name)
        {
            if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ')
                return false;
            for (const char ch : name)
            {
                const uint8_t c = uint8_t(ch);
                if (c < 0x20 || c == 0x7F || strchr("<>:\"/\\|?*", c))
                    return false;
            }
            std::string stem = name.substr(0, name.find('.'));
            for (char& c : stem)
                c = char(toupper(uint8_t(c)));
            if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL")
                return false;
            if ((stem.starts_with("COM") || stem.starts_with("LPT")) && stem.size() == 4 && isdigit(uint8_t(stem[3])))
                return false;
            return true;
        }

        void AppendUtf8(std::string& out, char32_t c)
        {
            if (c < 0x80)
                out += char(c);
            else if (c < 0x800)
            {
                out += char(0xC0 | (c >> 6));
                out += char(0x80 | (c & 0x3F));
            }
            else if (c < 0x10000)
            {
                out += char(0xE0 | (c >> 12));
                out += char(0x80 | ((c >> 6) & 0x3F));
                out += char(0x80 | (c & 0x3F));
            }
            else
            {
                out += char(0xF0 | (c >> 18));
                out += char(0x80 | ((c >> 12) & 0x3F));
                out += char(0x80 | ((c >> 6) & 0x3F));
                out += char(0x80 | (c & 0x3F));
            }
        }

        // A JSON string: UTF-16 to UTF-8, escaped; lone surrogates become U+FFFD.
        std::string JsonString(const std::u16string& s)
        {
            std::string out = "\"";
            for (size_t i = 0; i < s.size(); i++)
            {
                char32_t c = s[i];
                if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF)
                    c = 0x10000 + ((c - 0xD800) << 10) + (s[++i] - 0xDC00);
                else if (c >= 0xD800 && c <= 0xDFFF)
                    c = 0xFFFD;
                if (c == '"' || c == '\\')
                {
                    out += '\\';
                    out += char(c);
                }
                else if (c < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", unsigned(c));
                    out += buf;
                }
                else
                    AppendUtf8(out, c);
            }
            return out + "\"";
        }

        std::string Hex8(uint32_t v)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%08X", v);
            return buf;
        }

#ifdef _WIN32
        std::mutex g_readLock;
#endif
    }

    std::unique_ptr<Package> Package::Open(int fd, Result& result, std::string& error)
    {
        std::unique_ptr<Package> package(new Package());
        package->m_fd = fd;
#ifdef _WIN32
        const int64_t size = _lseeki64(fd, 0, SEEK_END);
#else
        const off64_t size = lseek64(fd, 0, SEEK_END);
#endif
        if (size < 0)
        {
            result = errno == ESPIPE ? Result::SourceNotSeekable : Result::SourceUnreadable;
            error = errno == ESPIPE ? "not seekable" : "can't be read";
            return nullptr;
        }
        package->m_size = uint64_t(size);

        std::vector<uint8_t> h(kHeaderRead);
        if (!package->ReadAt(0, h.data(), h.size()) || !IsMagic(h.data()))
        {
            result = Result::ExtractFailed;
            error = "not an Xbox 360 package";
            return nullptr;
        }
        // The header's fields are at the offsets Xenia's stfs_container_device.cc and
        // Free60's STFS page give.
        Header& hd = package->header;
        hd.magic.assign(reinterpret_cast<const char*>(h.data()), 4);
        const uint32_t headerSize = Be32(&h[0x340]);
        hd.contentType = Be32(&h[0x344]);
        hd.mediaId = Be32(&h[0x354]);
        hd.titleId = Be32(&h[0x360]);
        static constexpr char kHex[] = "0123456789ABCDEF";
        for (size_t i = 0; i < 20; i++)
        {
            hd.contentId += kHex[h[0x32C + i] >> 4];
            hd.contentId += kHex[h[0x32C + i] & 0xF];
        }
        const uint8_t* vd = &h[0x379];
        hd.readOnly = vd[2] & 1;
        hd.stfs = Be32(&h[0x3A9]) == 0;
        for (size_t i = 0; i < 0x40; i++)
        {
            const char16_t c = char16_t((h[0x411 + 2 * i] << 8) | h[0x412 + 2 * i]);
            if (!c)
                break;
            hd.displayName += c;
        }
        package->m_tableBlocks = vd[3] | (uint32_t(vd[4]) << 8);
        package->m_tableBlock = Le24(vd + 5);
        package->m_totalBlocks = Be32(vd + 0x1C);
        package->m_base = (uint64_t(headerSize) + kBlock - 1) / kBlock * kBlock;
        if (headerSize < 0x1000 || headerSize > 0x10000 || package->m_base > package->m_size)
        {
            result = Result::ExtractFailed;
            error = "damaged (bad header size)";
            return nullptr;
        }
        result = Result::Ok;
        return package;
    }

    bool Package::ReadAt(uint64_t offset, void* buffer, size_t size)
    {
        if (offset + size > m_size)
            return false;
        uint8_t* out = static_cast<uint8_t*>(buffer);
#ifdef _WIN32
        std::lock_guard lock(g_readLock);
        if (_lseeki64(m_fd, int64_t(offset), SEEK_SET) < 0)
            return false;
        while (size > 0)
        {
            const int n = _read(m_fd, out, unsigned(std::min<size_t>(size, 1u << 30)));
            if (n <= 0)
                return false;
            out += n;
            size -= size_t(n);
        }
#else
        while (size > 0)
        {
            const ssize_t n = pread64(m_fd, out, size, off64_t(offset));
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return false;
            out += n;
            offset += uint64_t(n);
            size -= size_t(n);
        }
#endif
        return true;
    }

    // Where data block `block` is: after the hash tables in front of it.
    uint64_t Package::BlockOffset(uint32_t block) const
    {
        uint64_t physical = block;
        for (const uint32_t level : kLevels)
        {
            physical += (uint64_t(block) + level) / level;
            if (block < level)
                break;
        }
        return m_base + (physical << 12);
    }

    bool Package::ReadBlock(uint32_t block, void* buffer, size_t size, std::string& error)
    {
        if (block >= m_totalBlocks)
        {
            error = "damaged (block out of range)";
            return false;
        }
        if (!ReadAt(BlockOffset(block), buffer, size))
        {
            error = "damaged (truncated)";
            return false;
        }
        return true;
    }

    // `count` blocks from `block`, all under one level-0 hash table (so next to each
    // other in the file): `size` bytes, the last block maybe only partly.
    bool Package::ReadRun(uint32_t block, uint32_t count, uint8_t* buffer, size_t size, std::string& error)
    {
        if (count == 0 || uint64_t(block) + count > m_totalBlocks ||
            block / kLevels[0] != (block + count - 1) / kLevels[0])
        {
            error = "damaged (block out of range)";
            return false;
        }
        if (!ReadAt(BlockOffset(block), buffer, size))
        {
            error = "damaged (truncated)";
            return false;
        }
        return true;
    }

    // The next block of a chain, from the block's level-0 hash table entry.
    bool Package::NextBlock(uint32_t block, uint32_t& next, std::string& error)
    {
        uint64_t table = 0;
        if (block >= kLevels[0])
        {
            table = uint64_t(block / kLevels[0]) * (kLevels[0] + 1) + block / kLevels[1] + 1;
            if (block >= kLevels[1])
                table++;
        }
        // The whole table is read once (the last may end the file early: as much of it as
        // there is).
        const uint64_t at = m_base + (table << 12);
        if (at != m_hashTableAt)
        {
            m_hashTableAt = UINT64_MAX;
            m_hashTable.assign(kBlock, 0);
            const size_t size = size_t(std::min<uint64_t>(kBlock, m_size > at ? m_size - at : 0));
            if (size == 0 || !ReadAt(at, m_hashTable.data(), size))
            {
                error = "damaged (truncated hash table)";
                return false;
            }
            m_hashTableAt = at;
        }
        const size_t entry = (block % kLevels[0]) * 0x18 + 0x14;
        if (m_hashTableAt + entry + 4 > m_size)
        {
            error = "damaged (truncated hash table)";
            return false;
        }
        next = Be32(&m_hashTable[entry]) & 0xFFFFFF;
        return true;
    }

    Result Package::ReadTable(std::string& error)
    {
        files.clear();
        if (!header.stfs)
        {
            error = "not an STFS package (an SVOD one, as games on demand are)";
            return Result::ExtractFailed;
        }
        if (!header.readOnly)
        {
            error = "a package in the writable format (as saves are), not downloadable content";
            return Result::ExtractFailed;
        }
        if (m_tableBlocks == 0 || m_tableBlocks > kMaxTableBlocks || m_totalBlocks >= kLevels[2])
        {
            error = "damaged (bad volume descriptor)";
            return Result::ExtractFailed;
        }
        struct Folder
        {
            std::string path;
            int depth;
        };
        std::unordered_map<uint32_t, Folder> folders{ { 0xFFFF, { "", 0 } } };
        std::unordered_set<std::string> seen;
        // Files may share blocks: their sizes together are held to the package's.
        uint64_t total = 0;
        std::vector<uint8_t> data(kBlock);
        uint32_t index = 0;
        uint32_t block = m_tableBlock;
        for (uint32_t n = 0; n < m_tableBlocks; n++)
        {
            if (!ReadBlock(block, data.data(), kBlock, error))
                return Result::ExtractFailed;
            for (uint32_t j = 0; j < kBlock / 0x40; j++, index++)
            {
                const uint8_t* e = &data[j * 0x40];
                if (e[0] == 0)
                    break;
                const uint8_t flags = e[40];
                const std::string name(reinterpret_cast<const char*>(e), std::min<size_t>(flags & 0x3F, 40));
                const uint32_t parent = (uint32_t(e[50]) << 8) | e[51];
                const uint64_t length = Be32(e + 52);
                if (!ValidName(name))
                {
                    error = "has a file name that can't be stored";
                    return Result::ExtractFailed;
                }
                const auto folder = folders.find(parent);
                if (folder == folders.end())
                {
                    error = "damaged (entry before its folder)";
                    return Result::ExtractFailed;
                }
                if (folder->second.depth >= kMaxDepth)
                {
                    error = "damaged (folders too deep)";
                    return Result::ExtractFailed;
                }
                std::string path = folder->second.path + name;
                if (!seen.insert(Lower(path)).second)
                {
                    error = "damaged (a file listed twice)";
                    return Result::ExtractFailed;
                }
                if (flags & 0x80)
                {
                    folders[index] = { path + "/", folder->second.depth + 1 };
                }
                else
                {
                    total += length;
                    // (No more than the package holds: a chain looping on one block would
                    // otherwise be read over and over.)
                    if (total > std::min(uint64_t(m_totalBlocks) << 12, m_size - m_base))
                    {
                        error = "damaged (bad file size)";
                        return Result::ExtractFailed;
                    }
                    files.push_back({ std::move(path), length, Le24(e + 47), (flags & 0x40) != 0 });
                }
            }
            if (!NextBlock(block, block, error))
                return Result::ExtractFailed;
        }
        return Result::Ok;
    }

    uint64_t Package::TotalSize() const
    {
        uint64_t total = 0;
        for (const File& file : files)
            total += file.size;
        return total;
    }

    Result Package::Extract(const std::string& into, const std::function<bool(uint64_t, uint64_t)>& progress,
        std::string& error)
    {
        std::error_code ec;
        fs::create_directories(into, ec);
        if (ec)
        {
            error = "can't create " + into;
            return errno == ENOSPC ? Result::NotEnoughSpace : Result::ExtractFailed;
        }
        const uint64_t total = TotalSize();
        const fs::space_info space = fs::space(into, ec);
        if (!ec && space.available < total + kSpaceMargin)
        {
            error = "not enough free space";
            return Result::NotEnoughSpace;
        }
        // Up to a level-0 table's blocks at a time, where a file's blocks are in order.
        std::vector<uint8_t> buffer(size_t(kLevels[0]) * kBlock);
        uint64_t done = 0;
        uint64_t reported = 0;
        if (progress && !progress(0, total))
            return Result::Cancelled;
        for (const File& file : files)
        {
            const fs::path target = fs::path(into) / file.path;
            fs::create_directories(target.parent_path(), ec);
            FILE* out = fopen(target.string().c_str(), "wb");
            if (!out)
            {
                error = "can't write " + file.path;
                return errno == ENOSPC ? Result::NotEnoughSpace : Result::ExtractFailed;
            }
            uint32_t block = file.startBlock;
            uint64_t left = file.size;
            Result result = Result::Ok;
            while (result == Result::Ok && left > 0)
            {
                uint32_t count = 1;
                if (file.contiguous)
                {
                    const uint64_t blocks = (left + kBlock - 1) / kBlock;
                    count = uint32_t(std::min<uint64_t>(blocks, kLevels[0] - block % kLevels[0]));
                }
                const size_t n = size_t(std::min<uint64_t>(uint64_t(count) * kBlock, left));
                if (!ReadRun(block, count, buffer.data(), n, error))
                {
                    result = Result::ExtractFailed;
                    break;
                }
                if (fwrite(buffer.data(), 1, n, out) != n)
                {
                    result = errno == ENOSPC ? Result::NotEnoughSpace : Result::ExtractFailed;
                    error = result == Result::NotEnoughSpace ? "not enough free space" : "can't write " + file.path;
                    break;
                }
                left -= n;
                done += n;
                if (progress && done - reported >= kProgressStep)
                {
                    reported = done;
                    if (!progress(done, total))
                    {
                        result = Result::Cancelled;
                        break;
                    }
                }
                if (left > 0)
                {
                    if (file.contiguous)
                        block += count;
                    else if (!NextBlock(block, block, error))
                        result = Result::ExtractFailed;
                }
            }
            if (fclose(out) != 0 && result == Result::Ok)
            {
                result = errno == ENOSPC ? Result::NotEnoughSpace : Result::ExtractFailed;
                error = "can't write " + file.path;
            }
            if (result != Result::Ok)
                return result;
        }
        if (progress && !progress(done, total))
            return Result::Cancelled;
        return Result::Ok;
    }

    std::string TargetFolder(const Header& header, const std::string& instance)
    {
        const fs::path root(instance);
        switch (header.contentType)
        {
        case kContentArcadeTitle:
            return (root / "game").string();
        case kContentMarketplace:
            return (root / "userdata" / "content" / Hex8(header.titleId) / Hex8(kContentMarketplace) /
                       header.contentId).string();
        case kContentTitleUpdate:
            return (root / "title_update").string();
        default:
            return {};
        }
    }

    bool WriteDisplayName(const Header& header, const std::string& folder)
    {
        uint8_t display[kDisplayNameSize]{};
        for (size_t i = 0; i < header.displayName.size() && i < kDisplayNameSize / 2 - 1; i++)
        {
            display[2 * i] = uint8_t(header.displayName[i] >> 8);
            display[2 * i + 1] = uint8_t(header.displayName[i]);
        }
        const fs::path path = fs::path(folder) / (header.contentId + ".name");
        FILE* f = fopen(path.string().c_str(), "wb");
        if (!f)
            return false;
        const bool ok = fwrite(display, 1, sizeof(display), f) == sizeof(display);
        return fclose(f) == 0 && ok;
    }

    std::string ProbeJson(Package& package)
    {
        const Header& h = package.header;
        uint64_t size = 0;
        std::string ignored;
        if (h.stfs && h.readOnly && package.ReadTable(ignored) == Result::Ok)
            size = package.TotalSize();
        std::u16string magic(h.magic.begin(), h.magic.end());
        char buf[64];
        snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(size));
        return "{\"title_id\":\"" + Hex8(h.titleId) + "\",\"content_type\":\"0x" + Hex8(h.contentType) +
               "\",\"content_id\":\"" + h.contentId + "\",\"media_id\":\"" + Hex8(h.mediaId) +
               "\",\"magic\":" + JsonString(magic) + ",\"display_name\":" + JsonString(h.displayName) +
               ",\"size\":" + buf + "}";
    }
}
