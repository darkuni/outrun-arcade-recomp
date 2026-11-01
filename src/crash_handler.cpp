// Crash reporter for the recompiled game.
//
// The runtime log just stops when the process dies on an unhandled native
// exception. This installs a last-chance handler that writes, next to the
// exe:
//   crash-<date>-<time>.txt  exception code/address, registers, and every
//                            stack value that points into a loaded module
//                            (as module+offset; map offsets in the game exe
//                            to recompiled functions with outrun_arcade.map)
//   crash-<date>-<time>.dmp  minidump (threads, stacks, module list)
//
// Only uses plain Win32 calls in the handler: the heap and the logger may be
// the thing that is broken.

#if defined(_WIN32)

#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

namespace {

wchar_t g_dir[MAX_PATH] = {};
HANDLE g_file = INVALID_HANDLE_VALUE;

void Out(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  int n = _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
  va_end(ap);
  if (n < 0) n = static_cast<int>(strlen(buf));
  DWORD written = 0;
  if (g_file != INVALID_HANDLE_VALUE) WriteFile(g_file, buf, n, &written, nullptr);
  OutputDebugStringA(buf);
}

struct ModRange {
  uintptr_t base, end;
  char name[64];
};
ModRange g_mods[256];
int g_mod_count = 0;

void CollectModules() {
  HMODULE mods[256];
  DWORD needed = 0;
  HANDLE proc = GetCurrentProcess();
  if (!EnumProcessModules(proc, mods, sizeof(mods), &needed)) return;
  int n = static_cast<int>(needed / sizeof(HMODULE));
  if (n > 256) n = 256;
  g_mod_count = 0;
  for (int i = 0; i < n; i++) {
    MODULEINFO mi = {};
    if (!GetModuleInformation(proc, mods[i], &mi, sizeof(mi))) continue;
    ModRange& m = g_mods[g_mod_count++];
    m.base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
    m.end = m.base + mi.SizeOfImage;
    char path[MAX_PATH] = {};
    GetModuleFileNameA(mods[i], path, MAX_PATH);
    const char* slash = strrchr(path, '\\');
    strncpy_s(m.name, slash ? slash + 1 : path, _TRUNCATE);
  }
}

const ModRange* FindModule(uintptr_t a) {
  for (int i = 0; i < g_mod_count; i++)
    if (a >= g_mods[i].base && a < g_mods[i].end) return &g_mods[i];
  return nullptr;
}

void Where(const char* label, uintptr_t a) {
  if (const ModRange* m = FindModule(a)) {
    Out("%s %016llX  %s+0x%llX\r\n", label, (unsigned long long)a, m->name,
        (unsigned long long)(a - m->base));
  } else {
    Out("%s %016llX\r\n", label, (unsigned long long)a);
  }
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
  static volatile LONG once = 0;
  if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;

  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t stem[MAX_PATH], path[MAX_PATH];
  swprintf_s(stem, L"%scrash-%04d%02d%02d-%02d%02d%02d", g_dir, t.wYear, t.wMonth, t.wDay,
             t.wHour, t.wMinute, t.wSecond);

  swprintf_s(path, L"%s.txt", stem);
  g_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);

  CollectModules();
  const EXCEPTION_RECORD* er = ep->ExceptionRecord;
  const CONTEXT* c = ep->ContextRecord;
  Out("Unhandled exception 0x%08lX (thread %lu)\r\n", er->ExceptionCode, GetCurrentThreadId());
  Where("  at  ", reinterpret_cast<uintptr_t>(er->ExceptionAddress));
  if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
    const char* kind = er->ExceptionInformation[0] == 0   ? "read"
                       : er->ExceptionInformation[0] == 1 ? "write"
                                                          : "execute";
    Out("  access violation: %s of %016llX\r\n", kind,
        (unsigned long long)er->ExceptionInformation[1]);
  }
  Out("\r\nRAX %016llX RBX %016llX RCX %016llX RDX %016llX\r\n", c->Rax, c->Rbx, c->Rcx, c->Rdx);
  Out("RSI %016llX RDI %016llX RBP %016llX RSP %016llX\r\n", c->Rsi, c->Rdi, c->Rbp, c->Rsp);
  Out("R8  %016llX R9  %016llX R10 %016llX R11 %016llX\r\n", c->R8, c->R9, c->R10, c->R11);
  Out("R12 %016llX R13 %016llX R14 %016llX R15 %016llX\r\n", c->R12, c->R13, c->R14, c->R15);
  Out("RIP %016llX\r\n", c->Rip);

  // Stack scan: every value on the stack that points into code of a loaded
  // module. Return addresses show up here; so do some stale values.
  Out("\r\nStack values pointing into modules (RSP upward):\r\n");
  const uintptr_t* sp = reinterpret_cast<const uintptr_t*>(c->Rsp);
  MEMORY_BASIC_INFORMATION mbi = {};
  uintptr_t limit = c->Rsp + 64 * 1024;
  if (VirtualQuery(sp, &mbi, sizeof(mbi))) {
    uintptr_t region_end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    if (region_end < limit) limit = region_end;
  }
  int shown = 0;
  for (const uintptr_t* p = sp; reinterpret_cast<uintptr_t>(p + 1) <= limit && shown < 160; p++) {
    if (FindModule(*p)) {
      char label[32];
      sprintf_s(label, "  [rsp+%05llX]", (unsigned long long)(reinterpret_cast<uintptr_t>(p) - c->Rsp));
      Where(label, *p);
      shown++;
    }
  }

  Out("\r\nModules:\r\n");
  for (int i = 0; i < g_mod_count; i++)
    Out("  %016llX-%016llX %s\r\n", (unsigned long long)g_mods[i].base,
        (unsigned long long)g_mods[i].end, g_mods[i].name);

  swprintf_s(path, L"%s.dmp", stem);
  HANDLE dmp = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
  if (dmp != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION mei = {GetCurrentThreadId(), ep, FALSE};
    BOOL ok = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), dmp,
        static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
        &mei, nullptr, nullptr);
    CloseHandle(dmp);
    Out("\r\nMinidump: %s\r\n", ok ? "written" : "FAILED");
  }

  if (g_file != INVALID_HANDLE_VALUE) {
    FlushFileBuffers(g_file);
    CloseHandle(g_file);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

struct Installer {
  Installer() {
    wchar_t exe[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n && n < MAX_PATH) {
      wchar_t* slash = wcsrchr(exe, L'\\');
      if (slash) slash[1] = 0;
      wcsncpy_s(g_dir, exe, _TRUNCATE);
    }
    SetUnhandledExceptionFilter(CrashFilter);
  }
} g_installer;

}  // namespace

// Called again after runtime setup in case anything replaced the filter.
void InstallCrashHandler() { SetUnhandledExceptionFilter(CrashFilter); }

#else
// Linux/macOS: no report writer. The SDK owns SIGSEGV/SIGILL (guest MMIO and
// write-watch traps, SEH emulation) and falls back to SIG_DFL for faults it
// does not handle, so an unhandled crash ends in a normal core dump
// (`coredumpctl gdb outrun_arcade`); outrun_arcade.map maps its addresses to
// recompiled functions. Installing a handler here would sit in front of the
// SDK's and break those traps.
void InstallCrashHandler() {}
#endif
