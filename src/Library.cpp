#include "Library.h"
#include "File.h"
#include "Utils.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace Luwow::Fs {
    ILuauModule* Library::initialize(ILuauHost* host) {
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

    // Writes data to a file, truncating or appending.
    static void writeData(lua_State* L, bool append) {
        fs::path path = checkPath(L, 1);
        size_t length = 0;
        const char* data = checkData(L, 2, &length);

        std::ofstream file(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
        if (!file.is_open()) raiseError(L, "Could not open file", path, openFailure(path));

        file.write(data, static_cast<std::streamsize>(length));
        if (!file) raiseError(L, "Could not write file", path, "I/O error");
    }

    // fs.readFile(path: string) -> string
    static int fs_readFile(lua_State* L) {
        fs::path path = checkPath(L, 1);

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) raiseError(L, "Could not open file", path, openFailure(path));

        std::string data;
        char chunk[4096];
        while (file.read(chunk, sizeof(chunk)) || file.gcount() > 0) {
            data.append(chunk, static_cast<size_t>(file.gcount()));
        }
        if (file.bad()) raiseError(L, "Could not read file", path, "I/O error");

        lua_pushlstring(L, data.data(), data.size());
        return 1;
    }

    // fs.writeFile(path: string, data: string | buffer)
    static int fs_writeFile(lua_State* L) {
        writeData(L, false);
        return 0;
    }

    // fs.appendFile(path: string, data: string | buffer)
    static int fs_appendFile(lua_State* L) {
        writeData(L, true);
        return 0;
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

        auto systemTime = std::chrono::clock_cast<std::chrono::system_clock>(writeTime);
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

        std::error_code ec;
        fs::copy(from, to, options, ec);
        if (ec) raiseError(L, "Could not copy", from, ec);
        return 0;
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

    const char* Library::getModuleName() const {
        return "fs";
    }

    const char* Library::getModuleAlias() const {
        return "Luwow";
    }

    static LuauExport exports[] = {
        { "readFile", fs_readFile },
        { "writeFile", fs_writeFile },
        { "appendFile", fs_appendFile },
        { "exists", fs_exists },
        { "type", fs_type },
        { "stat", fs_stat },
        { "listdir", fs_listdir },
        { "mkdir", fs_mkdir },
        { "remove", fs_remove },
        { "rmdir", fs_rmdir },
        { "copy", fs_copy },
        { "move", fs_move },
        { "open", fs_open },
        { nullptr, nullptr }
    };

    const LuauExport* Library::getExports() const {
        return exports;
    }
} // namespace Luwow::Fs

LUWOW_REGISTER_MODULE(Luwow::Fs::Library)
