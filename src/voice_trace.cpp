// Diagnostic: log every XAudio voice teardown (the function that releases a
// voice's XMA contexts, 0x82475E58) with the guest call stack, so we can see
// which game code stops a sound. Writes voice_trace.txt next to the exe.
// Always on: it fires only when a voice is destroyed.

#include "generated/default/outrun_arcade_init.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace {
std::mutex g_mu;
std::FILE* g_f = nullptr;

inline uint32_t Ld32(uint8_t* base, uint32_t a) { return REX_LOAD_U32(a); }
bool Valid(uint32_t a) { return a >= 0x40000000u && a < 0xFFFF0000u && (a & 3) == 0; }
}  // namespace

extern "C" void __imp__sub_82475E58(PPCContext&, uint8_t*);
extern "C" void sub_82475E58(PPCContext& ctx, uint8_t* base) {
  {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_f) g_f = std::fopen("voice_trace.txt", "w");
    if (g_f) {
      const auto now = std::chrono::system_clock::now();
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now.time_since_epoch()).count();
      const std::time_t tt = std::chrono::system_clock::to_time_t(now);
      std::tm lt{};
#if defined(_WIN32)
      localtime_s(&lt, &tt);
#else
      localtime_r(&tt, &lt);
#endif
      std::fprintf(g_f, "%02d:%02d:%02d.%03d release voice=%08X lr=%08X stack:", lt.tm_hour,
                   lt.tm_min, lt.tm_sec, int(ms % 1000), ctx.r3.u32, uint32_t(ctx.lr));
      std::fflush(g_f);
      // Walk only inside this thread's stack: each back-chain link must move
      // up by less than 64 KB and stay within 512 KB of the starting frame.
      const uint32_t sp0 = ctx.r1.u32;
      uint32_t sp = sp0;
      for (int i = 0; i < 20 && Valid(sp); i++) {
        const uint32_t next = Ld32(base, sp);
        if (!Valid(next) || next <= sp || next - sp > 0x10000 || next - sp0 > 0x80000) break;
        std::fprintf(g_f, " [%08X]", next);
        std::fflush(g_f);
        std::fprintf(g_f, " %08X", Ld32(base, next - 8));
        std::fflush(g_f);
        sp = next;
      }
      std::fprintf(g_f, "\n");
      std::fflush(g_f);
    }
  }
  __imp__sub_82475E58(ctx, base);
}
