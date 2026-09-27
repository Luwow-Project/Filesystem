#pragma once

#include "lualib.h"

#include <filesystem>
#include <string>
#include <system_error>

namespace Luwow::Fs {
    // Reads a UTF-8 path argument from the stack.
    inline std::filesystem::path checkPath(lua_State* L, int index) {
        size_t length = 0;
        const char* str = luaL_checklstring(L, index, &length);
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(str), length));
    }

    // Converts a path back to a UTF-8 string for Luau.
    inline std::string pathToString(const std::filesystem::path& path) {
        std::u8string str = path.u8string();
        return std::string(reinterpret_cast<const char*>(str.data()), str.size());
    }

    // Raises a Luau error in the form "<action> '<path>': <reason>".
    [[noreturn]] inline void raiseError(lua_State* L, const char* action, const std::filesystem::path& path, const std::string& reason) {
        std::string message = std::string(action) + " '" + pathToString(path) + "': " + reason;
        luaL_error(L, "%s", message.c_str());
    }

    [[noreturn]] inline void raiseError(lua_State* L, const char* action, const std::filesystem::path& path, const std::error_code& ec) {
        raiseError(L, action, path, ec.message());
    }

    // Reads a string or buffer argument without copying.
    inline const char* checkData(lua_State* L, int index, size_t* length) {
        if (lua_isbuffer(L, index)) {
            return static_cast<const char*>(lua_tobuffer(L, index, length));
        }
        return luaL_checklstring(L, index, length);
    }
} // namespace Luwow::Fs
