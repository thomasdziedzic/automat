#pragma once

#include "keyboard.hh"

automat::gui::AnsiKey ScanCodeToKey(uint32_t scan_code);
automat::gui::AnsiKey VirtualKeyToKey(uint8_t virtual_key);

uint8_t KeyToVirtualKey(automat::gui::AnsiKey key);
