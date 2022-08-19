#include "util.h"

#ifdef USE_HUGEPAGE
#include <sys/mman.h>
#endif

namespace rdma {

auto alloc(uint32_t len) -> void * {
#ifdef USE_HUGEPAGE
  return ::mmap(nullptr, len, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, 0, 0);
#else
  return new char[len];
#endif
}

auto dealloc(void *p, uint32_t len) -> void {
#ifdef USE_HUGEPAGE
  check(::munmap(p, len), "fail to deallocate hugepage");
#else
  delete[](char *) p;
#endif
}

} // namespace rdma