// First-run setup: extracts the player's own XBLA package (STFS "LIVE"/"PIRS"/"CON " file) into the
// assets folder, so no Python or other tools are needed. Port of tools/stfs_extract.py.
#include "package_setup.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr uint32_t kBlock = 0x1000;

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint32_t le24(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16; }
uint32_t be24(const uint8_t* p) { return (uint32_t)p[0] << 16 | p[1] << 8 | p[2]; }

void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
  auto block = [&](const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | p[4 * i + 1] << 16 | p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  };
  size_t full = len / 64 * 64;
  for (size_t o = 0; o < full; o += 64) block(data + o);
  uint8_t tail[128] = {0};
  size_t rem = len - full;
  memcpy(tail, data + full, rem);
  tail[rem] = 0x80;
  size_t tl = rem + 9 <= 64 ? 64 : 128;
  uint64_t bits = (uint64_t)len * 8;
  for (int i = 0; i < 8; i++) tail[tl - 1 - i] = (uint8_t)(bits >> (8 * i));
  block(tail);
  if (tl == 128) block(tail + 64);
  for (int i = 0; i < 5; i++)
    for (int k = 0; k < 4; k++) out[4 * i + k] = (uint8_t)(h[i] >> (24 - 8 * k));
}

struct Stfs {
  FILE* f = nullptr;
  uint64_t file_size = 0;
  uint8_t hdr[0x1800] = {0};
  uint32_t header_size = 0, title_id = 0, content_type = 0, ft_block_count = 0, ft_block_num = 0, sex = 0;
  uint32_t step0 = 0;
  uint64_t first_ht = 0;
  int bad_hashes = 0;

  ~Stfs() { if (f) fclose(f); }

  bool open(const fs::path& p) {
    f = fopen(p.string().c_str(), "rb");
    if (!f) return false;
    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr) return false;
    if (memcmp(hdr, "LIVE", 4) && memcmp(hdr, "PIRS", 4) && memcmp(hdr, "CON ", 4)) return false;
    seek(0, SEEK_END);
    file_size = tell();
    header_size = be32(hdr + 0x340);
    title_id = be32(hdr + 0x360);
    content_type = be32(hdr + 0x344);
    const uint8_t* vd = hdr + 0x379;
    uint8_t block_sep = vd[2];
    ft_block_count = vd[3] | vd[4] << 8;
    ft_block_num = le24(vd + 5);
    sex = (~block_sep) & 1;  // 0 = female (one hash table), 1 = male (two)
    step0 = sex == 0 ? 0xAB : 0xAC;
    first_ht = (header_size + 0xFFF) & 0xFFFFF000u;
    return true;
  }
  void seek(uint64_t o, int whence = SEEK_SET) {
#ifdef _WIN32
    _fseeki64(f, (long long)o, whence);
#else
    fseeko(f, (off_t)o, whence);
#endif
  }
  uint64_t tell() {
#ifdef _WIN32
    return (uint64_t)_ftelli64(f);
#else
    return (uint64_t)ftello(f);
#endif
  }
  void read_at(uint64_t off, uint8_t* out, size_t n) {
    memset(out, 0, n);
    if (off >= file_size) return;
    seek(off);
    size_t got = fread(out, 1, (size_t)std::min<uint64_t>(n, file_size - off), f);
    (void)got;
  }
  uint64_t backing_block(uint64_t n) const {
    uint64_t r = (((n + 0xAA) / 0xAA) << sex) + n;
    if (n < 0xAA) return r;
    if (n < 0x70E4) return r + (((n + 0x70E4) / 0x70E4) << sex);
    return (1ull << sex) + r + (((n + 0x70E4) / 0x70E4) << sex);
  }
  uint64_t block_addr(uint64_t n) const { return (backing_block(n) << 12) + first_ht; }
  uint64_t l0_hash_block(uint64_t n) const {
    if (n < 0xAA) return 0;
    uint64_t num = (n / 0xAA) * step0;
    num += ((n / 0x70E4) + 1) << sex;
    if (n / 0x70E4 == 0) return num;
    return num + (1ull << sex);
  }
  // hash entry: sha1[20], status, next block (24-bit)
  void hash_entry(uint64_t n, uint8_t e[24]) {
    uint64_t addr = (l0_hash_block(n) << 12) + first_ht + (n % 0xAA) * 0x18;
    read_at(addr, e, 24);
    if (sex == 1) {
      static const uint8_t zero[20] = {0};
      uint8_t e2[24];
      read_at(addr + 0x1000, e2, 24);
      if (!memcmp(e, zero, 20) && memcmp(e2, zero, 20)) memcpy(e, e2, 24);
    }
  }
  void read_block(uint64_t n, uint8_t* out) {
    read_at(block_addr(n), out, kBlock);
    uint8_t e[24], h[20];
    hash_entry(n, e);
    sha1(out, kBlock, h);
    if (memcmp(h, e, 20)) bad_hashes++;
  }
  std::vector<uint64_t> chain(uint64_t start, uint64_t count, bool consecutive) {
    std::vector<uint64_t> v;
    uint64_t n = start;
    for (uint64_t i = 0; i < count; i++) {
      v.push_back(n);
      if (consecutive) n++;
      else { uint8_t e[24]; hash_entry(n, e); n = be24(e + 21); }
    }
    return v;
  }
};

bool write_all(const fs::path& p, const std::vector<uint8_t>& d) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  FILE* w = fopen(p.string().c_str(), "wb");
  if (!w) return false;
  bool ok = d.empty() || fwrite(d.data(), 1, d.size(), w) == d.size();
  return fclose(w) == 0 && ok;
}
}  // namespace

bool package_extract(const fs::path& package, const fs::path& out, std::string& err, int* bad_hashes) {
  Stfs s;
  if (!s.open(package)) { err = package.filename().string() + " is not an Xbox 360 package"; return false; }
  // file table
  std::vector<uint8_t> raw;
  std::vector<uint8_t> blk(kBlock);
  for (uint64_t b : s.chain(s.ft_block_num, s.ft_block_count, false)) {
    s.read_block(b, blk.data());
    raw.insert(raw.end(), blk.begin(), blk.end());
  }
  struct Ent { std::string name; bool consecutive, dir; uint32_t start, size; int parent; };
  std::map<int, Ent> ents;
  std::vector<int> order;
  for (size_t i = 0; i + 0x40 <= raw.size(); i += 0x40) {
    const uint8_t* e = raw.data() + i;
    int nl = e[0x28] & 0x3F;
    if (!nl) continue;
    Ent en{std::string((const char*)e, nl), (e[0x28] & 0x40) != 0, (e[0x28] & 0x80) != 0, le24(e + 0x2F), be32(e + 0x34),
           (int16_t)(e[0x32] << 8 | e[0x33])};
    ents[(int)(i / 0x40)] = en;
    order.push_back((int)(i / 0x40));
  }
  auto path_of = [&](int idx) {
    std::vector<std::string> parts;
    for (int p = idx, guard = 0; p != -1 && ents.count(p) && guard < 64; guard++) { parts.push_back(ents[p].name); p = ents[p].parent; }
    fs::path r = out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) r /= *it;
    return r;
  };
  std::error_code ec;
  fs::create_directories(out, ec);
  for (int idx : order) {
    const Ent& e = ents[idx];
    fs::path p = path_of(idx);
    if (e.dir) { fs::create_directories(p, ec); continue; }
    uint64_t nb = (e.size + kBlock - 1) / kBlock;
    std::vector<uint8_t> data;
    data.reserve((size_t)nb * kBlock);
    for (uint64_t b : s.chain(e.start, nb, e.consecutive)) {
      s.read_block(b, blk.data());
      data.insert(data.end(), blk.begin(), blk.end());
    }
    data.resize(e.size);
    if (!write_all(p, data)) { err = "cannot write " + p.string(); return false; }
  }
  if (bad_hashes) *bad_hashes = s.bad_hashes;
  return true;
}

int package_setup_auto(const fs::path& exe_dir, const fs::path& assets, uint32_t title_id, std::string& err, std::string& used) {
  std::error_code ec;
  // The package is usually copied as the console stores it (58410A06\000D0000\<hash>, no extension),
  // so look in the exe folder and a few levels below it, skipping the assets folder itself.
  std::vector<fs::path> cands;
  auto it = fs::recursive_directory_iterator(exe_dir, fs::directory_options::skip_permission_denied, ec);
  for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    const auto& e = *it;
    if (e.is_directory(ec)) {
      if (it.depth() >= 3 || e.path() == assets) it.disable_recursion_pending();
      continue;
    }
    if (!e.is_regular_file(ec) || e.file_size(ec) < 0x2000) continue;
    Stfs s;
    // 0x000D0000 = Xbox Live Arcade title (not add-ons / marketplace items of the same game)
    if (s.open(e.path()) && s.title_id == title_id && s.content_type == 0x000D0000) cands.push_back(e.path());
  }
  if (cands.empty()) return 0;
  std::sort(cands.begin(), cands.end());
  used = cands[0].filename().string();
  int bad = 0;
  if (!package_extract(cands[0], assets, err, &bad)) return -1;
  if (!fs::exists(assets / "default.xex", ec)) { err = used + " has no default.xex"; return -1; }
  if (bad) fprintf(stderr, "[setup] %s: %d blocks failed their hash check\n", used.c_str(), bad);
  return 1;
}

#ifdef PACKAGE_SETUP_TOOL
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s <package> <outdir> | --auto <exe_dir> <assets> <title_id_hex>\n", argv[0]); return 2; }
  std::string err;
  if (std::string(argv[1]) == "--auto" && argc >= 5) {
    std::string used;
    int r = package_setup_auto(argv[2], argv[3], (uint32_t)strtoul(argv[4], nullptr, 16), err, used);
    fprintf(stderr, "auto: %d used '%s' err '%s'\n", r, used.c_str(), err.c_str());
    return r > 0 ? 0 : 1;
  }
  int bad = 0;
  if (!package_extract(argv[1], argv[2], err, &bad)) { fprintf(stderr, "failed: %s\n", err.c_str()); return 1; }
  fprintf(stderr, "ok, %d bad block hashes\n", bad);
  return 0;
}
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
static void show_message(const std::string& title, const std::string& text, bool error) {
  fprintf(stderr, "%s\n", text.c_str());
#ifdef _WIN32
  MessageBoxA(nullptr, text.c_str(), title.c_str(), error ? MB_ICONERROR : MB_ICONINFORMATION);
#else
  (void)title; (void)error;
#endif
}

bool package_setup_first_run(const fs::path& exe_dir, const fs::path& assets, uint32_t title_id, const std::string& game) {
  std::error_code ec;
  if (fs::exists(assets / "default.xex", ec)) return true;
  std::string err, used;
  int r = package_setup_auto(exe_dir, assets, title_id, err, used);
  if (r > 0) return true;
  if (r < 0) show_message(game, "Could not set up the game from " + used + ":\n" + err, true);
  else show_message(game, "Game files not found.\n\nPut your own " + game + " XBLA package (the Xbox 360 LIVE file "
                          "from your console's storage) in the same folder as this exe and start it again: the "
                          "\"assets\" folder is made from it on the first start.\n\n(Already extracted? Put default.xex "
                          "and the other game files in an \"assets\" folder next to the exe.)", true);
  return false;
}
