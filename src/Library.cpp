#include "Library.h"
#include "File.h"
#include "Utils.h"
#include "Hash.h"
#include "OffThread.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <memory>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace Luwow::Fs {
    // The host, for the work done off the state
    static ILuauHost* fsHost = nullptr;

    ILuauModule* Library::initialize(ILuauHost* host) {
        fsHost = host;
        return new Library();
    }

    static const char* typeName(fs::file_type type) {
        switch (type) {
            case fs::file_type::regular: return "file";
            case fs::file_type::directory: return "dir";
            case fs::file_type::symlink: return "symlink";
            default: return "other";
        }
    }

    // Picks the most useful reason when a stream fails to open.
    static std::error_code openFailure(const fs::path& path) {
        std::error_code ec;
        fs::file_status status = fs::status(path, ec);
        if (ec || !fs::exists(status)) return std::make_error_code(std::errc::no_such_file_or_directory);
        if (fs::is_directory(status)) return std::make_error_code(std::errc::is_a_directory);
        return std::make_error_code(std::errc::permission_denied);
    }

    // Gets the status of a path, raising an error if it doesn't exist.
    static fs::file_status checkStatus(lua_State* L, const char* action, const fs::path& path, bool noFollow) {
        std::error_code ec;
        fs::file_status status = noFollow ? fs::symlink_status(path, ec) : fs::status(path, ec);
        if (!ec && status.type() == fs::file_type::not_found) ec = std::make_error_code(std::errc::no_such_file_or_directory);
        if (ec) raiseError(L, action, path, ec);
        return status;
    }

    // Files this big are read, written and copied off the state, smaller ones right away
    constexpr uintmax_t OffThreadSize = 1024 * 1024;

    static bool isBig(const fs::path& path) {
        std::error_code ec;
        uintmax_t size = fs::file_size(path, ec);
        return !ec && size >= OffThreadSize;
    }

    // Writes data to a file, truncating or appending.
    static int writeData(lua_State* L, bool append) {
        fs::path path = checkPath(L, 1);
        size_t length = 0;
        const char* data = checkData(L, 2, &length);
        auto contents = std::make_shared<std::string>(data, length);

        return Luwow::Engine::runOffThread(L, fsHost, [path, contents, append]() {
            Luwow::Engine::OffThreadResult result;
            std::ofstream file(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
            if (!file.is_open()) {
                result.error = errorText("Could not open file", path, openFailure(path).message());
                return result;
            }
            file.write(contents->data(), static_cast<std::streamsize>(contents->size()));
            if (!file) result.error = errorText("Could not write file", path, "I/O error");
            return result;
        }, length >= OffThreadSize);
    }

    // fs.readFile(path: string) -> string
    static int fs_readFile(lua_State* L) {
        fs::path path = checkPath(L, 1);

        return Luwow::Engine::runOffThread(L, fsHost, [path]() {
            Luwow::Engine::OffThreadResult result;
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                result.error = errorText("Could not open file", path, openFailure(path).message());
                return result;
            }

            auto data = std::make_shared<std::string>();
            std::streamoff size = file.tellg();
            file.seekg(0);
            if (size > 0) {
                data->resize(static_cast<size_t>(size));
                file.read(data->data(), size);
                data->resize(static_cast<size_t>(file.gcount()));
            }
            // A file without a size up front, such as a pipe's, is read to its end
            char chunk[65536];
            while (file.read(chunk, sizeof(chunk)) || file.gcount() > 0) {
                data->append(chunk, static_cast<size_t>(file.gcount()));
            }
            if (file.bad()) {
                result.error = errorText("Could not read file", path, "I/O error");
                return result;
            }

            result.push = [data](lua_State* L) {
                lua_pushlstring(L, data->data(), data->size());
                return 1;
            };
            return result;
        }, isBig(path));
    }

    // fs.writeFile(path: string, data: string | buffer)
    static int fs_writeFile(lua_State* L) {
        return writeData(L, false);
    }

    // fs.appendFile(path: string, data: string | buffer)
    static int fs_appendFile(lua_State* L) {
        return writeData(L, true);
    }

    // fs.exists(path: string) -> boolean
    static int fs_exists(lua_State* L) {
        fs::path path = checkPath(L, 1);
        std::error_code ec;
        bool exists = fs::exists(path, ec);
        lua_pushboolean(L, exists && !ec);
        return 1;
    }

    // fs.type(path: string) -> "file" | "dir" | "symlink" | "other"
    static int fs_type(lua_State* L) {
        fs::path path = checkPath(L, 1);
        fs::file_status status = checkStatus(L, "Could not get type of", path, true);

        lua_pushstring(L, typeName(status.type()));
        return 1;
    }

    // fs.stat(path: string) -> { type, size, modified, readonly }
    static int fs_stat(lua_State* L) {
        fs::path path = checkPath(L, 1);
        std::error_code ec;

        fs::file_status status = checkStatus(L, "Could not get metadata of", path, false);

        uintmax_t size = fs::is_regular_file(status) ? fs::file_size(path, ec) : 0;
        if (ec) raiseError(L, "Could not get size of", path, ec);

        fs::file_time_type writeTime = fs::last_write_time(path, ec);
        if (ec) raiseError(L, "Could not get modification time of", path, ec);

#if defined(_LIBCPP_VERSION)
        auto fileTimeDuration = writeTime.time_since_epoch();
        auto systemTime = std::chrono::system_clock::time_point(
            std::chrono::duration_cast<std::chrono::system_clock::duration>(fileTimeDuration)
        );
#else
        auto systemTime = std::chrono::clock_cast<std::chrono::system_clock>(writeTime);
#endif
        double modified = std::chrono::duration<double>(systemTime.time_since_epoch()).count();
        bool readonly = (status.permissions() & fs::perms::owner_write) == fs::perms::none;

        lua_createtable(L, 0, 4);
        lua_pushstring(L, typeName(status.type()));
        lua_setfield(L, -2, "type");
        lua_pushnumber(L, static_cast<double>(size));
        lua_setfield(L, -2, "size");
        lua_pushnumber(L, modified);
        lua_setfield(L, -2, "modified");
        lua_pushboolean(L, readonly);
        lua_setfield(L, -2, "readonly");
        return 1;
    }

    // fs.listdir(path: string) -> { { name: string, type: string } }
    static int fs_listdir(lua_State* L) {
        fs::path path = checkPath(L, 1);
        std::error_code ec;

        fs::directory_iterator it(path, ec);
        if (ec) raiseError(L, "Could not list directory", path, ec);

        lua_newtable(L);
        int index = 1;
        for (; it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) break;
            std::string name = pathToString(it->path().filename());
            std::error_code typeEc;
            fs::file_status status = it->symlink_status(typeEc);

            lua_createtable(L, 0, 2);
            lua_pushlstring(L, name.data(), name.size());
            lua_setfield(L, -2, "name");
            lua_pushstring(L, typeEc ? "other" : typeName(status.type()));
            lua_setfield(L, -2, "type");
            lua_rawseti(L, -2, index++);
        }
        if (ec) raiseError(L, "Could not list directory", path, ec);
        return 1;
    }

    // fs.mkdir(path: string, recursive: boolean?)
    static int fs_mkdir(lua_State* L) {
        fs::path path = checkPath(L, 1);
        bool recursive = lua_toboolean(L, 2);
        std::error_code ec;

        if (recursive) {
            fs::create_directories(path, ec);
        } else if (!fs::create_directory(path, ec) && !ec) {
            ec = std::make_error_code(std::errc::file_exists);
        }
        if (ec) raiseError(L, "Could not create directory", path, ec);
        return 0;
    }

    // fs.remove(path: string) removes a single file.
    static int fs_remove(lua_State* L) {
        fs::path path = checkPath(L, 1);
        std::error_code ec;

        if (fs::is_directory(fs::symlink_status(path, ec))) {
            raiseError(L, "Could not remove file", path, "path is a directory, use fs.rmdir");
        }
        if (!fs::remove(path, ec) && !ec) {
            ec = std::make_error_code(std::errc::no_such_file_or_directory);
        }
        if (ec) raiseError(L, "Could not remove file", path, ec);
        return 0;
    }

    // fs.rmdir(path: string, recursive: boolean?)
    static int fs_rmdir(lua_State* L) {
        fs::path path = checkPath(L, 1);
        bool recursive = lua_toboolean(L, 2);
        std::error_code ec;

        fs::file_status status = checkStatus(L, "Could not remove directory", path, true);
        if (!fs::is_directory(status)) raiseError(L, "Could not remove directory", path, "path is not a directory");
        if (recursive) {
            fs::remove_all(path, ec);
        } else {
            fs::remove(path, ec);
        }
        if (ec) raiseError(L, "Could not remove directory", path, ec);
        return 0;
    }

    // fs.copy(from: string, to: string, overwrite: boolean?)
    static int fs_copy(lua_State* L) {
        fs::path from = checkPath(L, 1);
        fs::path to = checkPath(L, 2);
        bool overwrite = lua_toboolean(L, 3);

        fs::copy_options options = fs::copy_options::recursive;
        if (overwrite) options |= fs::copy_options::overwrite_existing;

        return Luwow::Engine::runOffThread(L, fsHost, [from, to, options]() {
            Luwow::Engine::OffThreadResult result;
            std::error_code ec;
            fs::copy(from, to, options, ec);
            if (ec) result.error = errorText("Could not copy", from, ec.message());
            return result;
        }, fs::is_directory(from) || isBig(from));
    }

    // fs.move(from: string, to: string)
    static int fs_move(lua_State* L) {
        fs::path from = checkPath(L, 1);
        fs::path to = checkPath(L, 2);

        std::error_code ec;
        fs::rename(from, to, ec);
        if (ec) raiseError(L, "Could not move", from, ec);
        return 0;
    }

    // fs.open(path: string, mode: "r" | "w" | "a" | "r+" | "w+" | "a+"?) -> File
    static int fs_open(lua_State* L) {
        fs::path path = checkPath(L, 1);
        const char* mode = luaL_optstring(L, 2, "r");

        std::ios::openmode flags = std::ios::binary;
        bool readable = false;
        bool writable = false;
        bool plus = std::strchr(mode, '+') != nullptr;

        switch (mode[0]) {
            case 'r':
                flags |= std::ios::in;
                readable = true;
                break;
            case 'w':
                flags |= std::ios::out | std::ios::trunc;
                writable = true;
                break;
            case 'a':
                flags |= std::ios::out | std::ios::app;
                writable = true;
                break;
            default:
                luaL_argerror(L, 2, "invalid mode, expected 'r', 'w', 'a', 'r+', 'w+' or 'a+'");
        }
        if (plus) {
            flags |= std::ios::in | std::ios::out;
            readable = writable = true;
        }

        std::fstream stream(path, flags);
        if (!stream.is_open()) raiseError(L, "Could not open file", path, openFailure(path));

        File::RegisterClass(L);
        File::Push(L, std::move(path), std::move(stream), readable, writable);
        return 1;
    }

    // fs.hashFile(path: string, algorithm: string?) -> string, the hex digest of a file's contents, MD5 by default
    static int fs_hashFile(lua_State* L) {
        fs::path path = checkPath(L, 1);
        int algorithm = luaL_checkoption(L, 2, "md5", Hash::Algorithms);

        return Luwow::Engine::runOffThread(L, fsHost, [path, algorithm]() {
            Luwow::Engine::OffThreadResult result;
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                result.error = errorText("Could not open file", path, openFailure(path).message());
                return result;
            }
            std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            auto digest = std::make_shared<std::string>(Hash::digest(contents, algorithm));
            result.push = [digest](lua_State* L) {
                lua_pushlstring(L, digest->data(), digest->size());
                return 1;
            };
            return result;
        }, isBig(path));
    }

    // Byte patterns

    // A pattern of hex bytes and ?? wildcards, such as "48 8D 15 ?? ?? ?? ??"
    struct BytePattern {
        std::vector<uint8_t> bytes;
        std::vector<bool> wild;
        // The first byte that isn't a wildcard, searched for with memchr
        size_t anchor = 0;
    };

    static BytePattern checkBytePattern(lua_State* L, int index) {
        std::string text = luaL_checkstring(L, index);
        BytePattern pattern;
        size_t position = 0;
        while (position < text.size()) {
            if (std::isspace(static_cast<unsigned char>(text[position]))) {
                ++position;
                continue;
            }
            std::string token = text.substr(position, 2);
            position += 2;
            if (token == "??" || token == "?") {
                pattern.bytes.push_back(0);
                pattern.wild.push_back(true);
                continue;
            }
            if (token.size() != 2 || !std::isxdigit(static_cast<unsigned char>(token[0])) || !std::isxdigit(static_cast<unsigned char>(token[1]))) {
                luaL_argerror(L, index, "expected hex bytes and ?? wildcards separated by spaces");
            }
            pattern.bytes.push_back(static_cast<uint8_t>(std::stoi(token, nullptr, 16)));
            pattern.wild.push_back(false);
        }
        if (pattern.bytes.empty()) luaL_argerror(L, index, "the pattern is empty");
        while (pattern.anchor < pattern.wild.size() && pattern.wild[pattern.anchor]) ++pattern.anchor;
        if (pattern.anchor == pattern.wild.size()) luaL_argerror(L, index, "the pattern needs a byte that isn't a wildcard");
        return pattern;
    }

    // fs.findBytes(path: string, pattern: string, start: number?, length: number?) -> { number }
    // The 0-based offsets of the pattern in the file's range, each match skipped past before looking for the next one.
    static int fs_findBytes(lua_State* L) {
        fs::path path = checkPath(L, 1);
        BytePattern pattern = checkBytePattern(L, 2);
        double startArgument = luaL_optnumber(L, 3, 0);
        double lengthArgument = luaL_optnumber(L, 4, -1);
        if (startArgument < 0) luaL_argerror(L, 3, "the start can't be negative");

        return Luwow::Engine::runOffThread(L, fsHost, [path, pattern, startArgument, lengthArgument]() {
            Luwow::Engine::OffThreadResult result;
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                result.error = errorText("Could not open file", path, openFailure(path).message());
                return result;
            }

            const uint64_t fileSize = static_cast<uint64_t>(file.tellg());
            const uint64_t start = std::min(fileSize, static_cast<uint64_t>(startArgument));
            const uint64_t end = lengthArgument < 0 ? fileSize : std::min(fileSize, start + static_cast<uint64_t>(lengthArgument));
            const size_t length = pattern.bytes.size();
            const size_t ChunkSize = 4 * 1024 * 1024;

            auto offsets = std::make_shared<std::vector<uint64_t>>();
            std::vector<uint8_t> data;
            uint64_t chunkStart = start;
            // Where the next match may start, past the last one
            uint64_t next = start;

            while (chunkStart < end) {
                // Each chunk reaches a pattern's length into the next, so a match across the edge is found
                size_t want = static_cast<size_t>(std::min<uint64_t>(ChunkSize + length - 1, end - chunkStart));
                data.resize(want);
                file.seekg(static_cast<std::streamoff>(chunkStart));
                file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(want));
                size_t have = static_cast<size_t>(file.gcount());
                if (have < length) break;

                size_t i = next > chunkStart ? static_cast<size_t>(next - chunkStart) : 0;
                const size_t lastStart = have - length;
                while (i <= lastStart) {
                    const uint8_t* from = data.data() + i + pattern.anchor;
                    const void* found = memchr(from, pattern.bytes[pattern.anchor], lastStart - i + 1);
                    if (!found) {
                        i = lastStart + 1;
                        break;
                    }
                    size_t candidate = static_cast<size_t>(static_cast<const uint8_t*>(found) - data.data()) - pattern.anchor;
                    bool matches = true;
                    for (size_t k = 0; k < length; ++k) {
                        if (!pattern.wild[k] && data[candidate + k] != pattern.bytes[k]) {
                            matches = false;
                            break;
                        }
                    }
                    if (matches) {
                        offsets->push_back(chunkStart + candidate);
                        i = candidate + length;
                    } else {
                        i = candidate + 1;
                    }
                }
                next = chunkStart + i;

                if (chunkStart + have >= end) break;
                chunkStart += ChunkSize;
            }

            result.push = [offsets](lua_State* L) {
                lua_createtable(L, static_cast<int>(offsets->size()), 0);
                for (size_t i = 0; i < offsets->size(); ++i) {
                    lua_pushnumber(L, static_cast<double>((*offsets)[i]));
                    lua_rawseti(L, -2, static_cast<int>(i + 1));
                }
                return 1;
            };
            return result;
        });
    }

    const char* Library::getModuleName() const {
        return "fs";
    }

    const char* Library::getModuleAlias() const {
        return "Luwow";
    }

    static LuauExport exports[] = {
        { "readFile", fs_readFile, Luwow::Engine::finishOffThread },
        { "writeFile", fs_writeFile, Luwow::Engine::finishOffThread },
        { "appendFile", fs_appendFile, Luwow::Engine::finishOffThread },
        { "exists", fs_exists },
        { "type", fs_type },
        { "stat", fs_stat },
        { "listdir", fs_listdir },
        { "mkdir", fs_mkdir },
        { "remove", fs_remove },
        { "rmdir", fs_rmdir },
        { "copy", fs_copy, Luwow::Engine::finishOffThread },
        { "move", fs_move },
        { "open", fs_open },
        { "hashFile", fs_hashFile, Luwow::Engine::finishOffThread },
        { "findBytes", fs_findBytes, Luwow::Engine::finishOffThread },
        { nullptr, nullptr }
    };

    Luwow::Engine::RunMode Library::getRunMode() const {
        return LUWOW_MODULE_RUN_MODE;
    }

    const LuauExport* Library::getExports() const {
        return exports;
    }
} // namespace Luwow::Fs

LUWOW_REGISTER_MODULE(Luwow::Fs::Library)
