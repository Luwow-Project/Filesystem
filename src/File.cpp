#include "File.h"
#include "Utils.h"

#include <string>

namespace Luwow::Fs {
    File::File(std::filesystem::path path, std::fstream stream, bool readable, bool writable)
        : path(std::move(path)), stream(std::move(stream)), readable(readable), writable(writable) {}

    void File::RegisterClass(lua_State* L) {
        luaL_getmetatable(L, Name);
        bool registered = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (registered) return;

        Register(L, {
            { "read",  &File::read },
            { "write", &File::write },
            { "seek",  &File::seek },
            { "tell",  &File::tell },
            { "size",  &File::size },
            { "flush", &File::flush },
            { "close", &File::close }
        }, {
            { "path",   &File::getPath,   nullptr },
            { "closed", &File::getClosed, nullptr }
        });
    }

    void File::checkOpen(lua_State* L) const {
        if (closed) {
            raiseError(L, "Cannot use closed file", path, "file is closed");
        }
    }

    // Reads `count` bytes, or everything until EOF when omitted.
    int File::read(lua_State* L) {
        checkOpen(L);
        if (!readable) raiseError(L, "Cannot read file", path, "file was not opened for reading");
        if (writable) stream.seekg(stream.tellp());

        std::string data;
        if (lua_isnoneornil(L, 2)) {
            char chunk[4096];
            while (stream.read(chunk, sizeof(chunk)) || stream.gcount() > 0) {
                data.append(chunk, static_cast<size_t>(stream.gcount()));
            }
        } else {
            int count = luaL_checkinteger(L, 2);
            if (count < 0) luaL_argerror(L, 2, "count must be non-negative");
            data.resize(static_cast<size_t>(count));
            stream.read(data.data(), count);
            data.resize(static_cast<size_t>(stream.gcount()));
        }

        // Hitting EOF sets failbit, clear it so the stream stays usable.
        if (stream.eof()) stream.clear();
        if (stream.bad()) raiseError(L, "Could not read file", path, "I/O error");

        lua_pushlstring(L, data.data(), data.size());
        return 1;
    }

    int File::write(lua_State* L) {
        checkOpen(L);
        if (!writable) raiseError(L, "Cannot write file", path, "file was not opened for writing");
        if (readable) stream.seekp(stream.tellg());

        size_t length = 0;
        const char* data = checkData(L, 2, &length);
        stream.write(data, static_cast<std::streamsize>(length));
        if (!stream) {
            stream.clear();
            raiseError(L, "Could not write file", path, "I/O error");
        }
        return 0;
    }

    // Moves both read and write positions and returns the new position.
    int File::seek(lua_State* L) {
        checkOpen(L);

        static const char* const options[] = { "set", "cur", "end", nullptr };
        static const std::ios_base::seekdir dirs[] = { std::ios::beg, std::ios::cur, std::ios::end };
        int whence = luaL_checkoption(L, 2, "cur", options);
        std::streamoff offset = static_cast<std::streamoff>(luaL_optnumber(L, 3, 0));

        stream.clear();
        if (readable) stream.seekg(offset, dirs[whence]);
        if (writable) stream.seekp(offset, dirs[whence]);
        if (!stream) {
            stream.clear();
            raiseError(L, "Could not seek file", path, "invalid position");
        }

        std::streampos position = readable ? stream.tellg() : stream.tellp();
        lua_pushnumber(L, static_cast<double>(position));
        return 1;
    }

    int File::tell(lua_State* L) {
        checkOpen(L);
        std::streampos position = readable ? stream.tellg() : stream.tellp();
        lua_pushnumber(L, static_cast<double>(position));
        return 1;
    }

    int File::size(lua_State* L) {
        checkOpen(L);
        if (writable) stream.flush();

        std::error_code ec;
        auto bytes = std::filesystem::file_size(path, ec);
        if (ec) raiseError(L, "Could not get size of", path, ec);

        lua_pushnumber(L, static_cast<double>(bytes));
        return 1;
    }

    int File::flush(lua_State* L) {
        checkOpen(L);
        stream.flush();
        return 0;
    }

    // Closing twice is a no-op.
    int File::close(lua_State* L) {
        if (!closed) {
            stream.close();
            closed = true;
        }
        return 0;
    }

    int File::getPath(lua_State* L) {
        std::string str = pathToString(path);
        lua_pushlstring(L, str.data(), str.size());
        return 1;
    }

    int File::getClosed(lua_State* L) {
        lua_pushboolean(L, closed);
        return 1;
    }
} // namespace Luwow::Fs
