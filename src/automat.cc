#if defined(_WIN32)

#include "win_main.hh"

int main() { return WinMain(GetModuleHandle(NULL), NULL, GetCommandLine(), SW_SHOW); }

#elif defined(__linux__)

#include "linux_main.hh"

int main(int argc, char* argv[]) { return LinuxMain(argc, argv); }

#endif
