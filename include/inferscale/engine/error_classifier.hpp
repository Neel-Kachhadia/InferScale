#pragma once

#include "inferscale/runtime/types.hpp"

namespace inferscale {

inline FailureClass classify_engine_error(int backend_code, bool request_local) noexcept {
  if (request_local) return FailureClass::RequestRecoverable;
  if (backend_code == 1) return FailureClass::EngineRecoverable;
  if (backend_code == 2) return FailureClass::EngineRestartRequired;
  return FailureClass::ProcessFatal;
}

}  // namespace inferscale
