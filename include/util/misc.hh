#ifndef __RDMA_EXAMPLE_MISC__
#define __RDMA_EXAMPLE_MISC__

#if defined(__x86_64__)

#include <emmintrin.h>
#define PAUSE _mm_pause()

#elif defined(__aarch64__)

#define PAUSE asm volatile("yield" ::: "memory")

#else

#define PAUSE

#endif

namespace rdma {

inline auto pause() -> void { PAUSE; }

} // namespace rdma

#undef PAUSE

#endif