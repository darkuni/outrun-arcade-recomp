// Diagnostic: trace the game's audio sample-rate converters (XAudio SRC
// kernels). The 16 per-format dispatchers (0x82496DB8-0x82497A40) are
// wrapped; each takes the voice's SRC state in r3. Each call converts one voice's decoded
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
#include <unordered_map>

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

// Only touch addresses in the guest's allocated heaps.
bool ValidAddr(uint32_t a) { return a >= 0x40000000u && a < 0xFFFF0000u && (a & 3) == 0; }

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

void ProbeStarve(uint8_t* base, const Rec& r);

void Pre(PPCContext& ctx, uint8_t* base, uint32_t fn, Rec& r) {
  r.func = fn;
  r.t_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
  r.r3 = ctx.r3.u32; r.r5 = ctx.r5.u32; r.r6 = ctx.r6.u32; r.r7 = ctx.r3.u32;  // r7 field = state
  std::memset(r.out_first, 0, sizeof(r.out_first));
  std::memset(r.out_last, 0, sizeof(r.out_last));
  r.out_count = 0;
  if (ValidAddr(r.r7)) CopyGuest(base, r.r7, r.before, 64);
}

void Post(PPCContext& ctx, uint8_t* base, Rec& r) {
  if (!ValidAddr(r.r7)) return;
  CopyGuest(base, r.r7, r.after, 64);
  // Output position (samples) lives at state+28, the output base at +20.
  const uint32_t pos0 = __builtin_bswap32(*reinterpret_cast<uint32_t*>(r.before + 28));
  const uint32_t pos1 = __builtin_bswap32(*reinterpret_cast<uint32_t*>(r.after + 28));
  const uint32_t obase = __builtin_bswap32(*reinterpret_cast<uint32_t*>(r.after + 20));
  const uint32_t n = pos1 > pos0 ? pos1 - pos0 : 0;
  r.out_count = n;
  if (n && n <= 256 && ValidAddr(obase)) {
    const uint32_t channels = r.after[13];
    for (int p = 0; p < 2 && p < int(channels); p++) {
      const uint32_t plane = obase + p * 1024;
      for (uint32_t i = 0; i < 4 && i < n; i++) {
        r.out_first[p * 4 + i] = LdF(base, plane + (pos0 + i) * 4);
        r.out_last[p * 4 + i] = LdF(base, plane + (pos1 - std::min<uint32_t>(4, n) + i) * 4);
      }
    }
  }
  std::lock_guard<std::mutex> lk(g_mu);
  ProbeStarve(base, r);
  if (!g_f) g_f = std::fopen("src_trace.bin", "wb");
  if (!g_f) return;
  r.seq = g_seq++;
  std::fwrite(&r, sizeof(r), 1, g_f);
  if ((g_seq & 1023) == 0) std::fflush(g_f);
}


// --- starvation probe ------------------------------------------------------
// When a voice could not fill its 256-sample mix frame (it ran out of decoded
// input), dump the XMA hardware context(s) whose output buffer holds that
// voice's input, so we can see why the decoder had nothing ready.
std::FILE* g_starve = nullptr;

uint32_t VirtToPhys(uint32_t v) {
  if (v >= 0xE0000000u) return v - 0xE0000000u - 0x1000u;
  if (v >= 0xC0000000u) return v - 0xC0000000u;
  if (v >= 0xA0000000u) return v - 0xA0000000u;
  return 0xFFFFFFFFu;
}

struct VoiceInfo { uint32_t opos = 256, in = 0, ipos = 0, icnt = 0; uint64_t t = 0; };
std::unordered_map<uint32_t, VoiceInfo> g_voices;

void DumpContexts(uint8_t* base) {
  const uint32_t arr_phys = REX_LOAD_U32(0x825CE0B0u);
  if (!arr_phys || arr_phys > 0x1FFFFFFFu) return;
  for (uint32_t c = 0; c < 320; c++) {
    const uint32_t ca = 0xA0000000u + arr_phys + c * 64;
    const uint32_t d0 = REX_LOAD_U32(ca + 0), d1 = REX_LOAD_U32(ca + 4), d2 = REX_LOAD_U32(ca + 8);
    const uint32_t optr = REX_LOAD_U32(ca + 28), d9 = REX_LOAD_U32(ca + 36);
    const uint32_t blocks = (d0 >> 22) & 31;
    const uint32_t in_valid = (d0 >> 20) & 3;
    if (!optr || !blocks || (!in_valid && !((d1 >> 31) & 1))) continue;
    std::fprintf(g_starve,
                 "   ctx %u out=%08X blocks=%u wr=%u rd=%u out_valid=%u sdc=%u pad=%u st=%u "
                 "in_valid=%u/%u in_rd=%u err=%u stop=%u intr=%u\n",
                 c, optr, blocks, (d0 >> 27) & 31, d9 & 31, (d1 >> 31) & 1, (d1 >> 20) & 15,
                 (d1 >> 24) & 7, (d1 >> 29) & 1, (d0 >> 20) & 1, (d0 >> 21) & 1, d2 & 0x3FFFFFF,
                 (d2 >> 26) & 31, (d9 >> 30) & 1, (d9 >> 31) & 1);
  }
}

// Called before each SRC call. A voice that starts a new mix frame while its
// previous frame was left unfilled starved: log it with every active context.
void ProbeStarve(uint8_t* base, const Rec& r) {
  const uint32_t opos_b = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(r.before + 28));
  const uint32_t opos_a = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(r.after + 28));
  auto& v = g_voices[r.r7];
  const bool starved = opos_b == 0 && v.opos > 0 && v.opos < 256;
  if (starved) {
    if (!g_starve) g_starve = std::fopen("xma_starve.txt", "w");
    if (g_starve) {
      const uint32_t in = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(r.before + 0));
      const uint32_t ipos = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(r.before + 8));
      const uint32_t icnt = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(r.before + 4));
      std::fprintf(g_starve,
                   "%llu STARVED voice=%08X frame stopped at %u/256 after %.1f ms; last chunk "
                   "%08X pos %u/%u; resumes with %08X pos %u/%u\n",
                   (unsigned long long)r.t_ns, r.r7, v.opos, (r.t_ns - v.t) / 1e6, v.in, v.ipos,
                   v.icnt, in, ipos, icnt);
      DumpContexts(base);
      std::fflush(g_starve);
    }
  }
  v.opos = opos_a;
  v.t = r.t_ns;
  v.in = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(r.after + 0));
  v.ipos = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(r.after + 8));
  v.icnt = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(r.after + 4));
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

SRC_WRAP(sub_82496DB8, 0)
SRC_WRAP(sub_82496E90, 1)
SRC_WRAP(sub_82496F68, 2)
SRC_WRAP(sub_82497040, 3)
SRC_WRAP(sub_82497118, 4)
SRC_WRAP(sub_824971F0, 5)
SRC_WRAP(sub_824972C8, 6)
SRC_WRAP(sub_824973A0, 7)
SRC_WRAP(sub_82497478, 8)
SRC_WRAP(sub_82497548, 9)
SRC_WRAP(sub_82497618, 10)
SRC_WRAP(sub_824976E8, 11)
SRC_WRAP(sub_824977B8, 12)
SRC_WRAP(sub_82497890, 13)
SRC_WRAP(sub_82497968, 14)
SRC_WRAP(sub_82497A40, 15)
