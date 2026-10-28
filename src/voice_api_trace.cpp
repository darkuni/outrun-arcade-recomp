// Diagnostic (outrun_voice_trace = true): logs every call to the game's XAudio
// source-voice wrappers (0x824753E0..0x824757E8, one per vtable slot) and every
// voice teardown, with time, voice, args and the caller chain. Writes
// voice_api_trace.txt next to the exe. GetState (slot 48) is logged only when
// its result changes per voice, as it is polled every frame.

#include "generated/default/outrun_arcade_init.h"

#include <rex/cvar.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <unordered_map>

REXCVAR_DEFINE_BOOL(outrun_voice_trace, false, "OutRun",
                    "Write voice_api_trace.txt: every game call into its XAudio voices");

namespace {
std::mutex g_mu;
std::FILE* g_f = nullptr;
std::unordered_map<uint32_t, uint32_t> g_last_state;

inline uint32_t Ld32(uint8_t* base, uint32_t a) { return REX_LOAD_U32(a); }
bool Valid(uint32_t a) { return a >= 0x40000000u && a < 0xFFFF0000u && (a & 3) == 0; }

void Stamp() {
  const auto now = std::chrono::system_clock::now();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  const std::time_t tt = std::chrono::system_clock::to_time_t(now);
  std::tm lt{};
#if defined(_WIN32)
  localtime_s(&lt, &tt);
#else
  localtime_r(&tt, &lt);
#endif
  std::fprintf(g_f, "%02d:%02d:%02d.%03d", lt.tm_hour, lt.tm_min, lt.tm_sec, int(ms % 1000));
}

void Stack(PPCContext& ctx, uint8_t* base, int max) {
  std::fprintf(g_f, " lr=%08X", uint32_t(ctx.lr));
  const uint32_t sp0 = ctx.r1.u32;
  uint32_t sp = sp0;
  for (int i = 0; i < max && Valid(sp); i++) {
    const uint32_t next = Ld32(base, sp);
    if (!Valid(next) || next <= sp || next - sp > 0x10000 || next - sp0 > 0x80000) break;
    const uint32_t ret = Ld32(base, next - 8);
    if (ret < 0x82000000u || ret >= 0x82B20000u) break;
    std::fprintf(g_f, " %08X", ret);
    sp = next;
  }
}

bool Open() {
  if (!REXCVAR_GET(outrun_voice_trace)) return false;
  if (!g_f) g_f = std::fopen("voice_api_trace.txt", "w");
  return g_f != nullptr;
}
}  // namespace

#define VOICE_WRAP(name, slot)                                                        \
  extern "C" void __imp__##name(PPCContext&, uint8_t*);                               \
  extern "C" void name(PPCContext& ctx, uint8_t* base) {                              \
    if (!REXCVAR_GET(outrun_voice_trace)) { __imp__##name(ctx, base); return; }       \
    const uint32_t voice = ctx.r3.u32, a4 = ctx.r4.u32, a5 = ctx.r5.u32;              \
    const double f1 = ctx.f1.f64;                                                     \
    PPCContext saved = ctx;                                                           \
    __imp__##name(ctx, base);                                                         \
    std::lock_guard<std::mutex> lk(g_mu);                                             \
    if (!Open()) return;                                                              \
    if (slot == 48) {                                                                 \
      const uint32_t st = Valid(a4) ? (REX_LOAD_U32(a4) >> 24) : 0xFFu;               \
      auto it = g_last_state.find(voice);                                             \
      if (it != g_last_state.end() && it->second == st) return;                       \
      g_last_state[voice] = st;                                                       \
      Stamp();                                                                        \
      std::fprintf(g_f, " slot48 voice=%08X state=%02X", voice, st);                  \
    } else {                                                                          \
      Stamp();                                                                        \
      std::fprintf(g_f, " slot%d voice=%08X r4=%08X r5=%08X f1=%g ret=%08X", slot,    \
                   voice, a4, a5, f1, ctx.r3.u32);                                    \
    }                                                                                 \
    Stack(saved, base, 8);                                                            \
    std::fprintf(g_f, "\n");                                                          \
    std::fflush(g_f);                                                                 \
  }

VOICE_WRAP(sub_824753E0, 4)
VOICE_WRAP(sub_82475448, 8)
VOICE_WRAP(sub_824754B0, 32)
VOICE_WRAP(sub_82475520, 36)
VOICE_WRAP(sub_82475578, 40)
VOICE_WRAP(sub_824755D0, 48)
VOICE_WRAP(sub_82475628, 28)
VOICE_WRAP(sub_82475728, 60)
VOICE_WRAP(sub_82475780, 56)
VOICE_WRAP(sub_824757E8, 96)

extern "C" void __imp__sub_82475E58(PPCContext&, uint8_t*);
extern "C" void sub_82475E58(PPCContext& ctx, uint8_t* base) {
  if (REXCVAR_GET(outrun_voice_trace)) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (Open()) {
      Stamp();
      std::fprintf(g_f, " RELEASE voice=%08X", ctx.r3.u32);
      Stack(ctx, base, 12);
      std::fprintf(g_f, "\n");
      std::fflush(g_f);
    }
  }
  __imp__sub_82475E58(ctx, base);
}
