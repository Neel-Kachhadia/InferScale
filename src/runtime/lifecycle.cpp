#include "inferscale/runtime/lifecycle.hpp"

namespace inferscale {

bool terminal_intent(RequestState state) noexcept {
  return state == RequestState::CompletePendingRetire ||
         state == RequestState::CancelPendingRetire ||
         state == RequestState::FailedPendingRetire;
}

bool legal_transition(RequestState from, RequestState to) noexcept {
  if (from == to) return false;
  if (to == RequestState::CancelPendingRetire || to == RequestState::FailedPendingRetire) {
    return from != RequestState::Terminal && !terminal_intent(from);
  }
  switch (from) {
    case RequestState::Received:
      return to == RequestState::PrefillReady || to == RequestState::CompletePendingRetire;
    case RequestState::PrefillReady:
      return to == RequestState::PrefillInFlight;
    case RequestState::PrefillInFlight:
      return to == RequestState::PrefillReady || to == RequestState::DecodeReady;
    case RequestState::DecodeReady:
      return to == RequestState::DecodeInFlight;
    case RequestState::DecodeInFlight:
      return to == RequestState::DecodeReady || to == RequestState::CompletePendingRetire;
    case RequestState::CompletePendingRetire:
    case RequestState::CancelPendingRetire:
    case RequestState::FailedPendingRetire:
      return to == RequestState::Terminal;
    case RequestState::Terminal:
      return false;
  }
  return false;
}

bool transition(RequestState& state, RequestState to) noexcept {
  if (!legal_transition(state, to)) return false;
  state = to;
  return true;
}

}  // namespace inferscale
