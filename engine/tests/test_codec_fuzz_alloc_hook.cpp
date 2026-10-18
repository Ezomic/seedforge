#include "codec_fuzz.h"

#include <cstdlib>
#include <new>

// Replacement global operator new for the test binary. Outside an AllocGuard it only forwards to
// malloc. Inside one, a request above the budget is refused with bad_alloc and recorded, so a
// decoder trusting a hostile length fails the fuzz run without really allocating it.

namespace ichor::fuzz::detail {

AllocState& allocState()
{
    static thread_local AllocState state;
    return state;
}

}

namespace {

void* guardedAlloc(std::size_t size)
{
    auto& s = ichor::fuzz::detail::allocState();
    if (s.active) {
        s.worst = std::max(s.worst, size);
        s.total += size;
        if (size > s.maxSingle || s.total > s.maxTotal) {
            s.violated = true;
            return nullptr;
        }
    }
    return std::malloc(size ? size : 1);
}

}

void* operator new(std::size_t size)
{
    if (void* p = guardedAlloc(size)) return p;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    if (void* p = guardedAlloc(size)) return p;
    throw std::bad_alloc();
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return guardedAlloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return guardedAlloc(size);
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
