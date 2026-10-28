// Song-title callout fix.
//
// After you tune the radio, the front end's state machine (sub_82152968,
// state word 0x828934A0) sits in state 20 until all 75 entries of a load
// table (0x8280FC8C, 44-byte stride, status word == 3 when done) report
// ready, then calls sub_82102AF8, which stops the menu voices - including
// the "SPLASH WAVE" callout. On a 360 those loads take over a second, so the
// callout finishes first; here they finish almost at once and cut it off.
//
// State 8 stores 20 and falls straight into the state-20 check in the same
// call, so the hold has to cover calls that start in state 8 as well.
//
// Fix: keep state 20 waiting until it has lasted outrun_callout_hold_ms.
// While holding, one table entry reads "not ready" for the duration of the
// state machine call only (and only if it was already ready), so the game
// simply waits a few more frames exactly as it does for a slow load.

#include "generated/default/outrun_arcade_init.h"

#include <rex/cvar.h>

#include <chrono>
#include <cstdio>

REXCVAR_DEFINE_INT32(outrun_callout_hold_ms, 1200, "OutRun",
                     "Minimum time the front end waits after the song pick before stopping the "
                     "menu sounds, so the song-title callout plays out (real-hardware load time). "
                     "0 = off");

namespace {
constexpr uint32_t kStateAddr = 0x828934A0;
constexpr uint32_t kTableEntry0 = 0x8280FC8C;  // status word of entry 0
constexpr uint32_t kReady = 3;

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint32_t g_last_state = 0xFFFFFFFF;
int64_t g_state20_since = 0;

void Log(const char* fmt, uint32_t a, int64_t b) {
  static std::FILE* f = std::fopen("callout_trace.txt", "w");
  if (!f) return;
  std::fprintf(f, fmt, a, (long long)b);
  std::fflush(f);
}
}  // namespace

extern "C" void __imp__sub_82152968(PPCContext&, uint8_t*);
extern "C" void sub_82152968(PPCContext& ctx, uint8_t* base) {
  const uint32_t state = REX_LOAD_U32(kStateAddr);
  const int64_t now = NowMs();
  if (state != g_last_state) {
    Log("front-end state %u at %lld ms\n", state, now);
    g_last_state = state;
  }
  if (state != 20 && state != 8) g_state20_since = 0;

  const int32_t hold = REXCVAR_GET(outrun_callout_hold_ms);
  bool held = false;
  if ((state == 8 || state == 20) && hold > 0 &&
      (g_state20_since == 0 || now - g_state20_since < hold) &&
      REX_LOAD_U32(kTableEntry0) == kReady) {
    REX_STORE_U32(kTableEntry0, 2);
    held = true;
  }

  __imp__sub_82152968(ctx, base);

  if (held && REX_LOAD_U32(kTableEntry0) == 2) {
    REX_STORE_U32(kTableEntry0, kReady);
  }
  const uint32_t after = REX_LOAD_U32(kStateAddr);
  if (after == 20 && g_state20_since == 0) {
    g_state20_since = now;
    Log("front-end entered state %u at %lld ms (hold starts)\n", after, now);
  }
  if (held && state == 20 && now - g_state20_since + 20 >= hold) {
    Log("front-end hold released in state %u at %lld ms\n", state, now);
  }
}
