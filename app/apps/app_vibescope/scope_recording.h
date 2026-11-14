/**
 * @file scope_recording.h
 * @author 煲仔卤煮(ZealerLu)
 * @date 2026-11-05
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <string>
#include <string_view>
#include <fcntl.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif

namespace vibescope {
inline bool is_recording_name(std::string_view name)
{
    const auto dot = name.rfind('.');
    if (dot == name.npos || dot == 0 || dot > 8 || name.size() != dot + 4) return false;
    const auto ext = name.substr(dot + 1);
    if ((ext[0] != 'V' && ext[0] != 'v') || (ext[1] != 'N' && ext[1] != 'n') ||
        (ext[2] != 'R' && ext[2] != 'r')) return false;
    for (const char c : name.substr(0, dot))
        if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') &&
            c != '_' && c != '-') return false;
    return true;
}

inline bool is_screenshot_name(std::string_view name)
{
    const auto dot = name.rfind('.');
    if (dot == name.npos || dot == 0 || dot > 8 || name.size() != dot + 4) return false;
    const auto ext = name.substr(dot + 1);
    if ((ext[0] != 'B' && ext[0] != 'b') || (ext[1] != 'M' && ext[1] != 'm') ||
        (ext[2] != 'P' && ext[2] != 'p')) return false;
    for (const char c : name.substr(0, dot))
        if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') &&
            c != '_' && c != '-') return false;
    return true;
}

struct RecordingFile {
    FILE* file{};
    std::string path;
    int error{};
};

inline constexpr unsigned kMaxFileSequence = 999999;

inline unsigned file_sequence(std::string_view name, std::string_view prefix)
{
    if (name.size() != prefix.size() + 10 || name.substr(0, prefix.size()) != prefix ||
        name[prefix.size() + 6] != '.') return 0;
    unsigned number = 0;
    for (const char digit : name.substr(prefix.size(), 6)) {
        if (digit < '0' || digit > '9') return 0;
        number = number * 10 + (digit - '0');
    }
    return number;
}

// FatFS is configured without long filenames. Exclusive creation prevents a
// filename collision from truncating an existing file.
inline RecordingFile open_numbered_file(const char* directory, unsigned sequence,
                                        const char* prefix, const char* extension)
{
    RecordingFile result;
    for (; sequence >= 1 && sequence <= kMaxFileSequence; ++sequence) {
        char name[24];
        std::snprintf(name, sizeof(name), "/%s%06u.%s", prefix, sequence, extension);
        result.path = std::string(directory) + name;
#if defined(_WIN32)
        int fd = -1;
        const auto open_error = _sopen_s(&fd, result.path.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
                                         _SH_DENYNO, _S_IREAD | _S_IWRITE);
        if (open_error) errno = open_error;
#else
        const auto fd = ::open(result.path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666);
#endif
        if (fd < 0) {
            result.error = errno;
            if (result.error == EEXIST) continue;
            return result;
        }
#if defined(_WIN32)
        result.file = _fdopen(fd, "wb");
#else
        result.file = fdopen(fd, "wb");
#endif
        if (!result.file) {
            result.error = errno;
#if defined(_WIN32)
            _close(fd);
#else
            ::close(fd);
#endif
            std::remove(result.path.c_str());
        } else result.error = 0;
        return result;
    }
    result.error = ENOSPC;
    return result;
}
} // namespace vibescope
