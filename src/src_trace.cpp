// Diagnostic: trace the game's audio sample-rate converters (XAudio SRC
// kernels, 0x824891D8-0x82491D40). Each call converts one voice's decoded
// PCM into the mixer's 256-sample float frame. Enabled with the environment
// variable OUTRUN_SRC_TRACE=1; writes src_trace.bin in the working folder.
//
// Record (little-endian host order):
//   u32 func_index, u32 seq, u64 time_ns,
//   u32 r3 (input ptr), r5 (output ptr), r6 (count), r7 (state ptr),
//   u8 state_before[64], u8 state_after[64]   (raw guest bytes, big-endian)
//   f32 out_first[8], f32 out_last[8]          (output plane 0 / plane 1 ends)
//   u32 out_count                              (samples written this call)

#include "generated/default/outrun_arcade_init.h"

#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {

struct Rec {
  uint32_t func, seq;
  uint64_t t_ns;
  uint32_t r3, r5, r6, r7;
  uint8_t before[64], after[64];
  float out_first[8], out_last[8];
  uint32_t out_count;
};

std::mutex g_mu;
std::FILE* g_f = nullptr;
uint32_t g_seq = 0;
int g_enabled = -1;

bool Enabled() {
  if (g_enabled < 0) {
    const char* e = std::getenv("OUTRUN_SRC_TRACE");
    g_enabled = (e && *e && *e != '0') ? 1 : 0;
  }
  return g_enabled == 1;
}

inline uint32_t Ld32(uint8_t* base, uint32_t a) { return REX_LOAD_U32(a); }
inline float LdF(uint8_t* base, uint32_t a) { return std::bit_cast<float>(REX_LOAD_U32(a)); }

void CopyGuest(uint8_t* base, uint32_t a, uint8_t* dst, size_t n) {
  for (size_t i = 0; i < n; i += 4) {
    const uint32_t v = __builtin_bswap32(REX_LOAD_U32(a + uint32_t(i)));
    std::memcpy(dst + i, &v, 4);  // keep guest (big-endian) byte order
  }
}

void Pre(PPCContext& ctx, uint8_t* base, uint32_t fn, Rec& r) {
  r.func = fn;
  r.t_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
  r.r3 = ctx.r3.u32; r.r5 = ctx.r5.u32; r.r6 = ctx.r6.u32; r.r7 = ctx.r7.u32;
  std::memset(r.out_first, 0, sizeof(r.out_first));
  std::memset(r.out_last, 0, sizeof(r.out_last));
  r.out_count = 0;
  if (r.r7) CopyGuest(base, r.r7, r.before, 64);
}

void Post(PPCContext& ctx, uint8_t* base, Rec& r) {
  if (!r.r7) return;
  CopyGuest(base, r.r7, r.after, 64);
  // Output position (samples) lives at state+28, the output base at +20.
  const uint32_t pos0 = __builtin_bswap32(*reinterpret_cast<uint32_t*>(r.before + 28));
  const uint32_t pos1 = __builtin_bswap32(*reinterpret_cast<uint32_t*>(r.after + 28));
  const uint32_t obase = __builtin_bswap32(*reinterpret_cast<uint32_t*>(r.after + 20));
  const uint32_t n = pos1 > pos0 ? pos1 - pos0 : 0;
  r.out_count = n;
  if (n && obase) {
    for (int p = 0; p < 2; p++) {
      const uint32_t plane = obase + p * 1024;
      for (uint32_t i = 0; i < 4 && i < n; i++) {
        r.out_first[p * 4 + i] = LdF(base, plane + (pos0 + i) * 4);
        r.out_last[p * 4 + i] = LdF(base, plane + (pos1 - std::min<uint32_t>(4, n) + i) * 4);
      }
    }
  }
  std::lock_guard<std::mutex> lk(g_mu);
  if (!g_f) g_f = std::fopen("src_trace.bin", "wb");
  if (!g_f) return;
  r.seq = g_seq++;
  std::fwrite(&r, sizeof(r), 1, g_f);
  if ((g_seq & 1023) == 0) std::fflush(g_f);
}

}  // namespace

#define SRC_WRAP(name, idx)                                  \
  extern "C" void __imp__##name(PPCContext&, uint8_t*);      \
  extern "C" void name(PPCContext& ctx, uint8_t* base) {     \
    if (!Enabled()) { __imp__##name(ctx, base); return; }    \
    Rec r;                                                   \
    Pre(ctx, base, idx, r);                                  \
    __imp__##name(ctx, base);                                \
    Post(ctx, base, r);                                      \
  }

SRC_WRAP(sub_824891D8, 0)
SRC_WRAP(sub_82489440, 1)
SRC_WRAP(sub_82489710, 2)
SRC_WRAP(sub_82489AB0, 3)
SRC_WRAP(sub_82489F50, 4)
SRC_WRAP(sub_8248A0F8, 5)
SRC_WRAP(sub_8248A2F8, 6)
SRC_WRAP(sub_8248A528, 7)
SRC_WRAP(sub_8248A7C0, 8)
SRC_WRAP(sub_8248AAD0, 9)
SRC_WRAP(sub_8248ADE8, 10)
SRC_WRAP(sub_8248B1D8, 11)
SRC_WRAP(sub_8248B888, 12)
SRC_WRAP(sub_8248C180, 13)
SRC_WRAP(sub_8248C408, 14)
SRC_WRAP(sub_8248C6F0, 15)
SRC_WRAP(sub_8248CAB0, 16)
SRC_WRAP(sub_8248CF70, 17)
SRC_WRAP(sub_8248D2D0, 18)
SRC_WRAP(sub_8248D5B0, 19)
SRC_WRAP(sub_8248D8D8, 20)
SRC_WRAP(sub_8248DCD0, 21)
SRC_WRAP(sub_8248E1D8, 22)
SRC_WRAP(sub_8248E570, 23)
SRC_WRAP(sub_8248E888, 24)
SRC_WRAP(sub_8248EC20, 25)
SRC_WRAP(sub_8248F0E8, 26)
SRC_WRAP(sub_8248F718, 27)
SRC_WRAP(sub_8248FAB8, 28)
SRC_WRAP(sub_8248FDF0, 29)
SRC_WRAP(sub_824901C8, 30)
SRC_WRAP(sub_824906E8, 31)
SRC_WRAP(sub_82490D80, 32)
SRC_WRAP(sub_82491138, 33)
SRC_WRAP(sub_82491468, 34)
SRC_WRAP(sub_82491828, 35)
SRC_WRAP(sub_82491D40, 36)
