#pragma once

#ifdef INFERSCALE_HAS_NVTX
#include <nvtx3/nvToolsExt.h>
#endif

namespace inferscale {

class NvtxRange {
 public:
  explicit NvtxRange(const char* name) noexcept {
#ifdef INFERSCALE_HAS_NVTX
    nvtxRangePushA(name);
#else
    (void)name;
#endif
  }
  ~NvtxRange() {
#ifdef INFERSCALE_HAS_NVTX
    nvtxRangePop();
#endif
  }
  NvtxRange(const NvtxRange&) = delete;
  NvtxRange& operator=(const NvtxRange&) = delete;
};

}  // namespace inferscale

#define INFERSCALE_NVTX_JOIN_IMPL(left, right) left##right
#define INFERSCALE_NVTX_JOIN(left, right) INFERSCALE_NVTX_JOIN_IMPL(left, right)
#define INFERSCALE_NVTX_RANGE(name) \
  ::inferscale::NvtxRange INFERSCALE_NVTX_JOIN(inferscale_nvtx_range_, __COUNTER__){name}
