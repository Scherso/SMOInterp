#pragma once

#include "common.hpp"

#define EXL_MODULE_NAME "SMOInterp"

#define EXL_DEBUG
#define EXL_USE_FAKEHEAP

namespace exl::setting {
    constexpr size_t HeapSize = 0x5000;

    /* Trampolines for hooks, 200 bytes each: room for 40. */
    constexpr size_t JitSize = 0x2000;

    constexpr size_t InlinePoolSize = 0x1000;

    /* Formatting buffer for Logging.Log, on the stack. */
    constexpr size_t LogBufferSize = 512;

    static_assert(ALIGN_UP(JitSize, PAGE_SIZE) == JitSize, "");
    static_assert(ALIGN_UP(InlinePoolSize, PAGE_SIZE) == InlinePoolSize, "");
}
