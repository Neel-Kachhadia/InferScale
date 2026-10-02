#pragma once

#include <cstdint>

namespace inferscale {

enum class RequestState : std::uint8_t {
  Received,
  PrefillReady,
  PrefillInFlight,
  DecodeReady,
  DecodeInFlight,
  CompletePendingRetire,
  CancelPendingRetire,
  FailedPendingRetire,
  Terminal
};

bool legal_transition(RequestState from, RequestState to) noexcept;
bool transition(RequestState& state, RequestState to) noexcept;
bool terminal_intent(RequestState state) noexcept;

}  // namespace inferscale
