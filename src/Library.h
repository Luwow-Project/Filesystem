#pragma once

#include "Engine.h"
#include "lua.h"

namespace Luwow::Fs {
    using ILuauModule = Luwow::Engine::ILuauModule;
    using Engine = Luwow::Engine::Engine;

    class Library : public ILuauModule {
    public:
        Library() = default;
        ~Library() override = default;

        ILuauModule* initialize(Engine* engine) override;

        const char* getModuleName() const override;
        const char* getModuleAlias() const override;
        const LuauExport* getExports() const override;
    };
} // namespace Luwow::Fs
