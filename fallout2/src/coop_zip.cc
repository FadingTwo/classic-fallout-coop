#include "coop_zip.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <vector>

#include <zlib.h>

namespace fs = std::filesystem;

namespace fallout {

typedef struct ZipEntry {
    std::string name;
    int method;
    uint32_t crc;
    uint64_t packedSize;
    uint64_t size;
    uint64_t localOffset;
} ZipEntry;

static int zip_seek(FILE* stream, uint64_t offset, int origin)
{
#ifdef _WIN32
    return _fseeki64(stream, (long long)offset, origin);
#else
    return fseeko(stream, (off_t)offset, origin);
#endif
}

static uint64_t zip_tell(FILE* stream)
{
#ifdef _WIN32
    return (uint64_t)_ftelli64(stream);
#else
    return (uint64_t)ftello(stream);
#endif
}

static uint16_t zip_u16(const unsigned char* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t zip_u32(const unsigned char* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t zip_u64(const unsigned char* p)
{
    return (uint64_t)zip_u32(p) | ((uint64_t)zip_u32(p + 4) << 32);
}

static bool zip_read_at(FILE* stream, uint64_t offset, void* buffer, size_t size)
{
    return zip_seek(stream, offset, SEEK_SET) == 0 && fread(buffer, 1, size, stream) == size;
}

// The central directory's entries.
static bool zip_list(FILE* stream, std::vector<ZipEntry>* entries, std::string* error)
{
    zip_seek(stream, 0, SEEK_END);
    uint64_t fileSize = zip_tell(stream);
    if (fileSize < 22) {
        *error = "Not a zip file.";
        return false;
    }

    // The end of central directory record, within the last 64 KB.
    size_t tailSize = (size_t)(fileSize < 65557 ? fileSize : 65557);
    std::vector<unsigned char> tail(tailSize);
    if (!zip_read_at(stream, fileSize - tailSize, tail.data(), tailSize)) {
        *error = "Could not read the zip file.";
        return false;
    }
    long eocd = -1;
    for (long at = (long)tailSize - 22; at >= 0; at--) {
        if (zip_u32(&tail[at]) == 0x06054b50) {
            eocd = at;
            break;
        }
    }
    if (eocd == -1) {
        *error = "Not a zip file (or a damaged one).";
        return false;
    }

    uint64_t count = zip_u16(&tail[eocd + 10]);
    uint64_t directorySize = zip_u32(&tail[eocd + 12]);
    uint64_t directoryOffset = zip_u32(&tail[eocd + 16]);

    // Zip64: the locator just before it points to the bigger record.
    if ((count == 0xFFFF || directorySize == 0xFFFFFFFF || directoryOffset == 0xFFFFFFFF) && eocd >= 20 && zip_u32(&tail[eocd - 20]) == 0x07064b50) {
        uint64_t recordOffset = zip_u64(&tail[eocd - 20 + 8]);
        unsigned char record[56];
        if (!zip_read_at(stream, recordOffset, record, sizeof(record)) || zip_u32(record) != 0x06064b50) {
            *error = "The zip file is damaged.";
            return false;
        }
        count = zip_u64(record + 32);
        directorySize = zip_u64(record + 40);
        directoryOffset = zip_u64(record + 48);
    }

    if (directoryOffset + directorySize > fileSize || directorySize > 256 * 1024 * 1024) {
        *error = "The zip file is damaged.";
        return false;
    }

    std::vector<unsigned char> directory((size_t)directorySize);
    if (!zip_read_at(stream, directoryOffset, directory.data(), directory.size())) {
        *error = "Could not read the zip file.";
        return false;
    }

    size_t at = 0;
    for (uint64_t index = 0; index < count; index++) {
        if (at + 46 > directory.size() || zip_u32(&directory[at]) != 0x02014b50) {
            *error = "The zip file is damaged.";
            return false;
        }
        const unsigned char* header = &directory[at];
        ZipEntry entry;
        entry.method = zip_u16(header + 10);
        entry.crc = zip_u32(header + 16);
        entry.packedSize = zip_u32(header + 20);
        entry.size = zip_u32(header + 24);
        int nameLength = zip_u16(header + 28);
        int extraLength = zip_u16(header + 30);
        int commentLength = zip_u16(header + 32);
        entry.localOffset = zip_u32(header + 42);
        if (at + 46 + nameLength + extraLength + commentLength > directory.size()) {
            *error = "The zip file is damaged.";
            return false;
        }
        entry.name.assign((const char*)header + 46, nameLength);

        // Zip64 sizes and offset, in that order, for the fields that
        // overflowed.
        const unsigned char* extra = header + 46 + nameLength;
        int extraAt = 0;
        while (extraAt + 4 <= extraLength) {
            int id = zip_u16(extra + extraAt);
            int length = zip_u16(extra + extraAt + 2);
            if (id == 0x0001) {
                const unsigned char* field = extra + extraAt + 4;
                int used = 0;
                if (entry.size == 0xFFFFFFFF && used + 8 <= length) {
                    entry.size = zip_u64(field + used);
                    used += 8;
                }
                if (entry.packedSize == 0xFFFFFFFF && used + 8 <= length) {
                    entry.packedSize = zip_u64(field + used);
                    used += 8;
                }
                if (entry.localOffset == 0xFFFFFFFF && used + 8 <= length) {
                    entry.localOffset = zip_u64(field + used);
                    used += 8;
                }
            }
            extraAt += 4 + length;
        }

        entries->push_back(entry);
        at += 46 + nameLength + extraLength + commentLength;
    }
    return true;
}

// Whether `name` stays inside the folder it's unpacked into.
static bool zip_name_safe(const std::string& name)
{
    if (name.empty() || name[0] == '/' || name[0] == '\\' || name.find(':') != std::string::npos) {
        return false;
    }
    size_t start = 0;
    while (start <= name.size()) {
        size_t end = name.find_first_of("/\\", start);
        if (end == std::string::npos) {
            end = name.size();
        }
        if (name.compare(start, end - start, "..") == 0 && end - start == 2) {
            return false;
        }
        start = end + 1;
    }
    return true;
}

bool coop_zip_extract(const std::string& zipPath, const std::string& prefix, const std::string& dir, CoopZipWant* want, CoopZipProgress* progress, void* data, std::string* error)
{
    FILE* stream = fopen(zipPath.c_str(), "rb");
    if (stream == NULL) {
        *error = "Could not open " + zipPath + ".";
        return false;
    }

    std::vector<ZipEntry> entries;
    if (!zip_list(stream, &entries, error)) {
        fclose(stream);
        return false;
    }

    // What to unpack, and how much that is.
    std::vector<const ZipEntry*> chosen;
    unsigned long long total = 0;
    for (const ZipEntry& entry : entries) {
        if (entry.name.compare(0, prefix.size(), prefix) != 0 || entry.name.size() == prefix.size() || entry.name.back() == '/') {
            continue;
        }
        std::string name = entry.name.substr(prefix.size());
        if (want != NULL && !want(name)) {
            continue;
        }
        if (!zip_name_safe(name)) {
            *error = "The zip file has a file outside its folder: " + entry.name;
            fclose(stream);
            return false;
        }
        if (entry.method != 0 && entry.method != 8) {
            *error = "The zip file packs " + entry.name + " in a way this can't unpack.";
            fclose(stream);
            return false;
        }
        chosen.push_back(&entry);
        total += entry.size;
    }

    std::vector<unsigned char> in(256 * 1024);
    std::vector<unsigned char> out(256 * 1024);
    unsigned long long done = 0;
    bool ok = true;
    for (const ZipEntry* entry : chosen) {
        unsigned char local[30];
        if (!zip_read_at(stream, entry->localOffset, local, sizeof(local)) || zip_u32(local) != 0x04034b50) {
            *error = "The zip file is damaged at " + entry->name + ".";
            ok = false;
            break;
        }
        uint64_t dataOffset = entry->localOffset + 30 + zip_u16(local + 26) + zip_u16(local + 28);
        if (zip_seek(stream, dataOffset, SEEK_SET) != 0) {
            *error = "The zip file is damaged at " + entry->name + ".";
            ok = false;
            break;
        }

        fs::path target = fs::u8path(dir) / fs::u8path(entry->name.substr(prefix.size()));
        std::error_code fsError;
        fs::create_directories(target.parent_path(), fsError);
        std::string targetPath = target.string();
        FILE* output = fopen(targetPath.c_str(), "wb");
        if (output == NULL) {
            *error = "Could not write " + targetPath + ".";
            ok = false;
            break;
        }

        z_stream inflater;
        memset(&inflater, 0, sizeof(inflater));
        if (entry->method == 8 && inflateInit2(&inflater, -MAX_WBITS) != Z_OK) {
            *error = "Could not unpack.";
            fclose(output);
            ok = false;
            break;
        }

        uLong crc = crc32(0L, Z_NULL, 0);
        uint64_t left = entry->packedSize;
        uint64_t written = 0;
        bool finished = false;
        while (ok && !finished) {
            size_t chunk = (size_t)(left < in.size() ? left : in.size());
            if (chunk != 0 && fread(in.data(), 1, chunk, stream) != chunk) {
                *error = "The zip file is cut short at " + entry->name + ".";
                ok = false;
                break;
            }
            left -= chunk;

            if (entry->method == 0) {
                if (chunk != 0 && fwrite(in.data(), 1, chunk, output) != chunk) {
                    *error = "Could not write " + targetPath + " (is the disk full?).";
                    ok = false;
                    break;
                }
                crc = crc32(crc, in.data(), (uInt)chunk);
                written += chunk;
                finished = left == 0;
            } else {
                inflater.next_in = in.data();
                inflater.avail_in = (uInt)chunk;
                do {
                    inflater.next_out = out.data();
                    inflater.avail_out = (uInt)out.size();
                    int rc = inflate(&inflater, Z_NO_FLUSH);
                    // No progress with this chunk used up: the output buffer
                    // was filled just as the input ran out; it wants the next
                    // chunk (this read as a damaged file).
                    if (rc == Z_BUF_ERROR && chunk != 0 && inflater.avail_in == 0) {
                        break;
                    }
                    if (rc != Z_OK && rc != Z_STREAM_END && !(rc == Z_BUF_ERROR && chunk == 0)) {
                        *error = "The zip file is damaged in " + entry->name + ".";
                        ok = false;
                        break;
                    }
                    size_t produced = out.size() - inflater.avail_out;
                    if (produced != 0 && fwrite(out.data(), 1, produced, output) != produced) {
                        *error = "Could not write " + targetPath + " (is the disk full?).";
                        ok = false;
                        break;
                    }
                    crc = crc32(crc, out.data(), (uInt)produced);
                    written += produced;
                    if (rc == Z_STREAM_END) {
                        finished = true;
                        break;
                    }
                    if (chunk == 0 && produced == 0) {
                        *error = "The zip file is cut short at " + entry->name + ".";
                        ok = false;
                        break;
                    }
                } while (inflater.avail_out == 0 || inflater.avail_in != 0);
            }

            if (ok && progress != NULL && !progress(data, done + written, total)) {
                *error = "Cancelled.";
                ok = false;
            }
        }

        if (entry->method == 8) {
            inflateEnd(&inflater);
        }
        if (fclose(output) != 0 && ok) {
            *error = "Could not write " + targetPath + " (is the disk full?).";
            ok = false;
        }
        if (ok && (written != entry->size || (uint32_t)crc != entry->crc)) {
            *error = entry->name + " was not unpacked right (its check failed).";
            ok = false;
        }
        if (!ok) {
            remove(targetPath.c_str());
            break;
        }
        done += entry->size;
    }

    fclose(stream);
    return ok;
}

} // namespace fallout
