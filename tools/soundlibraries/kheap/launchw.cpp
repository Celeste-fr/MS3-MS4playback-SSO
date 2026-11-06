// launchw <dll> <program> [args...]: starts the program suspended, loads <dll> into it, lets it run and waits for it
// (stdio passed through; prints the exit code). From the livefs launcher (private ableton-optimization repo); sso-load,
// 2026-10-23: kheap.dll into MuseScore3Evo.exe for the null test.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) { fwprintf(stderr, L"usage: launch <dll> <program> [args...]\n"); return 2; }
  wchar_t dll[MAX_PATH]; GetFullPathNameW(argv[1], MAX_PATH, dll, nullptr);
  std::wstring cmd = L"\"" + std::wstring(argv[2]) + L"\"";
  for (int i = 3; i < argc; ++i) cmd += L" \"" + std::wstring(argv[i]) + L"\"";
  SetEnvironmentVariableW(L"KTTRACE_INJECTED", L"1");
  STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
  si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE); si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  if (!CreateProcessW(argv[2], cmd.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
    fwprintf(stderr, L"CreateProcess failed %lu\n", GetLastError()); return 1;
  }
  size_t bytes = (wcslen(dll) + 1) * sizeof(wchar_t);
  void* remote = VirtualAllocEx(pi.hProcess, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  WriteProcessMemory(pi.hProcess, remote, dll, bytes, nullptr);
  auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
  HANDLE t = CreateRemoteThread(pi.hProcess, nullptr, 0, loadLib, remote, 0, nullptr);
  if (!t) { fwprintf(stderr, L"CreateRemoteThread failed %lu\n", GetLastError()); TerminateProcess(pi.hProcess, 1); return 1; }
  WaitForSingleObject(t, 30000);
  DWORD mod = 0; GetExitCodeThread(t, &mod);
  CloseHandle(t);
  ResumeThread(pi.hThread);
  wprintf(L"started pid %lu, dll %s\n", pi.dwProcessId, mod ? L"loaded" : L"NOT loaded"); fflush(stdout);
  WaitForSingleObject(pi.hProcess, INFINITE); DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code); wprintf(L"exit 0x%lx\n", code);
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  return mod ? 0 : 1;
}
