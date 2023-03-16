#pragma once

// Top-level Windows functions.

#include <Windows.h>

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    PWSTR pCmdLine, int nCmdShow);

namespace automaton {
  
extern HWND main_window;
extern int window_width;
extern int window_height;

} // namespace automaton