#pragma once

#include "lualib.h"

#include <filesystem>
#include <string>
#include <system_error>

namespace Luwow::Fs {
    // An absolute path too long for MAX_PATH in windows has to use backslashes only.
    inline std::filesystem::path withLongPathSupport(std::filesystem::path path) {
#if defined(_WIN32)
        const std::wstring& native = path.native();
        if (native.size() >= 240 && path.is_absolute() && native.rfind(L"\\\\?\\", 0) != 0) {
            std::filesystem::path normalized = path.lexically_normal();
            normalized.make_preferred();
            const std::wstring& text = normalized.native();
            // A UNC share's form is \\?\UNC\server\share
            if (text.rfind(L"\\\\", 0) == 0) return std::filesystem::path(L"\\\\?\\UNC\\" + text.substr(2));
            return std::filesystem::path(L"\\\\?\\" + text);
        }
#endif
        return path;
    }

    // Reads a UTF-8 path argument from the stack.
    inline std::filesystem::path checkPath(lua_State* L, int index) {
        size_t length = 0;
        const char* str = luaL_checklstring(L, index, &length);
#if defined(_WIN32)
        // Windows paths are UTF-16, so the UTF-8 is converted
        return withLongPathSupport(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(str), length)));
#else
        return std::filesystem::path(std::string(str, length));
#endif
    }

    // Converts a path back to a UTF-8 string for Luau, without the extended-length prefix.
    inline std::string pathToString(const std::filesystem::path& path) {
#if defined(_WIN32)
        std::u8string str = path.u8string();
        std::string text(reinterpret_cast<const char*>(str.data()), str.size());
#else
        std::string text = path.string();
#endif
        if (text.rfind("\\\\?\\UNC\\", 0) == 0) return "\\\\" + text.substr(8);
        if (text.rfind("\\\\?\\", 0) == 0) return text.substr(4);
        return text;
    }

    // An error message in the form "<action> '<path>': <reason>".
    inline std::string errorText(const char* action, const std::filesystem::path& path, const std::string& reason) {
        return std::string(action) + " '" + pathToString(path) + "': " + reason;
    }

    // Raises a Luau error in the form "<action> '<path>': <reason>".
    [[noreturn]] inline void raiseError(lua_State* L, const char* action, const std::filesystem::path& path, const std::string& reason) {
        std::string message = errorText(action, path, reason);
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
