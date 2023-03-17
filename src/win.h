#pragma once

// Windows utility functions.

#include <Windows.h>

namespace automaton {

static const char kWindowClass[] = "Automaton";
static const char kWindowTitle[] = "Automaton";

HINSTANCE GetInstance();
WNDCLASSEX &GetWindowClass();
HWND CreateAutomatonWindow();

} // namespace automaton
