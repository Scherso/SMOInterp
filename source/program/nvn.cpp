#include "nvn.hpp"

#include "config.hpp"

#include "lib.hpp"

#include <cstring>

using ProcFn = void (*)();
using GetProcAddressFn = ProcFn (*)(const void* device, const char* name);
using SetPresentIntervalFn = void (*)(void* windowOrBuilder, int interval);

extern "C" ProcFn nvnBootstrapLoader(const char* name);

namespace smo::nvn {
    namespace {
        GetProcAddressFn s_OrigGetProcAddress = nullptr;
        SetPresentIntervalFn s_OrigWindowSetPresentInterval = nullptr;
        SetPresentIntervalFn s_OrigWindowBuilderSetPresentInterval = nullptr;

        void WindowSetPresentInterval(void* window, int) {
            s_OrigWindowSetPresentInterval(window, PresentInterval());
        }

        void WindowBuilderSetPresentInterval(void* builder, int) {
            s_OrigWindowBuilderSetPresentInterval(builder, PresentInterval());
        }

        /* Stash the real pointer for a function we override and hand back ours instead. */
        template<typename Fn>
        ProcFn Substitute(ProcFn real, Fn& orig, Fn replacement) {
            if (real == nullptr)
                return real;
            orig = reinterpret_cast<Fn>(real);
            return reinterpret_cast<ProcFn>(replacement);
        }

        ProcFn Intercept(const char* name, ProcFn real);

        ProcFn GetProcAddress(const void* device, const char* name) {
            return Intercept(name, s_OrigGetProcAddress(device, name));
        }

        ProcFn Intercept(const char* name, ProcFn real) {
            if (name == nullptr)
                return real;
            if (std::strcmp(name, "nvnDeviceGetProcAddress") == 0)
                return Substitute(real, s_OrigGetProcAddress, &GetProcAddress);
            if (std::strcmp(name, "nvnWindowSetPresentInterval") == 0)
                return Substitute(real, s_OrigWindowSetPresentInterval, &WindowSetPresentInterval);
            if (std::strcmp(name, "nvnWindowBuilderSetPresentInterval") == 0)
                return Substitute(real, s_OrigWindowBuilderSetPresentInterval, &WindowBuilderSetPresentInterval);
            return real;
        }
    }

    HOOK_DEFINE_TRAMPOLINE(BootstrapLoader) {
        static ProcFn Callback(const char* name) {
            return Intercept(name, Orig(name));
        }
    };

    int PresentInterval() {
        return (config::Get().fps * 5 + 1) / 3;
    }

    void InstallHooks() {
        BootstrapLoader::InstallAtFuncPtr(nvnBootstrapLoader);
    }
}
