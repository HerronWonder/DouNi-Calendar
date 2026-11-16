#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

namespace inlineedit {

enum class Kind { Note, Year, Month, FontSize };
// accept=false cancels the edit. A false return on accept=true leaves it open.
using Commit = bool (*)(Kind, const std::wstring&, bool accept, void*);
using Preview = void (*)(Kind, const std::wstring&, void*);

// bounds are in screen pixels. The host is an undecorated inline overlay.
bool Begin(HWND owner, const RECT& screenBounds, Kind kind,
           const std::wstring& initial, int fontPixels, bool handwritten,
           Commit commit, Preview preview, void* context);
bool Finish(bool save = true);
bool Active();
HWND Handle(); // The native EDIT control; accepts EM_* messages.
void Move(const RECT& screenBounds);
void Shutdown();

} // namespace inlineedit
