#include "util.h"

#ifdef USE_HUGEPAGE
#include <sys/mman.h>
#endif

namespace rdma {

auto alloc(uint32_t len) -> void * {
#ifdef USE_HUGEPAGE
  auto p = mmap(nullptr, align(len, HUGE_PAGE_SIZE), PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
  if (p == MAP_FAILED) {
    die("fail to allocate hugepage");
  }
  return p;
#else
  return new char[len];
#endif
}

auto dealloc(void *p, uint32_t len) -> void {
#ifdef USE_HUGEPAGE
  check(munmap(p, align(len, HUGE_PAGE_SIZE)), "fail to deallocate hugepage");
#else
  delete[](char *) p;
#endif
}

} // namespace rdma