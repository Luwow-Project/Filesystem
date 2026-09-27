#pragma once

#include "Userdata.h"

#include <filesystem>
#include <fstream>

namespace Luwow::Fs {
    class File : public Luwow::Engine::Userdata<File> {
    public:
        static constexpr const char* Name = "File";

        File(std::filesystem::path path, std::fstream stream, bool readable, bool writable);
        ~File() = default;

        // Registers the File metatable once per Luau state.
        static void RegisterClass(lua_State* L);

        // file:read(count: number?) -> string
        int read(lua_State* L);
        // file:write(data: string | buffer)
        int write(lua_State* L);
        // file:seek(whence: "set" | "cur" | "end"?, offset: number?) -> number
        int seek(lua_State* L);
        int tell(lua_State* L);
        int size(lua_State* L);
        int flush(lua_State* L);
        int close(lua_State* L);

        int getPath(lua_State* L);
        int getClosed(lua_State* L);

    private:
        void checkOpen(lua_State* L) const;

        std::filesystem::path path;
        std::fstream stream;
        bool readable;
        bool writable;
        bool closed = false;
    };
} // namespace Luwow::Fs
