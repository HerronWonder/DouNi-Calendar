#include "inline_editor.h"
#include <imm.h>
#include <algorithm>
#include <vector>

namespace inlineedit {
namespace {
constexpr wchar_t HostClass[] = L"DouNiCalendar.InlineEdit";
constexpr UINT CommitOnBlur = WM_APP + 41;
// A keyed background keeps the native editor and caret visible without
// painting a rectangle over the desktop.
constexpr COLORREF Background = RGB(1, 2, 3);

struct State {
    HWND host = nullptr, edit = nullptr;
    HFONT font = nullptr;
    HBRUSH background = nullptr;
    WNDPROC editProc = nullptr;
    Kind kind = Kind::Note;
    Commit commit = nullptr;
    Preview preview = nullptr;
    void* context = nullptr;
    bool creating = false, finishing = false, composing = false;
} state;

std::wstring Text() {
    int length = GetWindowTextLengthW(state.edit);
    std::vector<wchar_t> text(static_cast<size_t>(length) + 1);
    GetWindowTextW(state.edit, text.data(), length + 1);
    return text.data();
}

LRESULT CALLBACK EditProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    WNDPROC original = state.editProc;
    switch (message) {
    case WM_IME_STARTCOMPOSITION: state.composing = true; break;
    case WM_IME_ENDCOMPOSITION: state.composing = false; break;
    case WM_GETDLGCODE:
        return CallWindowProcW(original, window, message, wp, lp) | DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (!state.composing) {
            if (wp == VK_ESCAPE) { Finish(false); return 0; }
            if (wp == VK_RETURN && !(state.kind == Kind::Note && (GetKeyState(VK_SHIFT) & 0x8000))) {
                Finish(true); return 0;
            }
        }
        break;
    case WM_CHAR:
        if (!state.composing && (wp == VK_ESCAPE ||
            (wp == VK_RETURN && !(state.kind == Kind::Note && (GetKeyState(VK_SHIFT) & 0x8000))))) return 0;
        break;
    case WM_KILLFOCUS:
        // Commit after the focus transition, outside the edit control's stack.
        if (!state.finishing && state.host) PostMessageW(state.host, CommitOnBlur, 0, 0);
        break;
    }
    return CallWindowProcW(original, window, message, wp, lp);
}

LRESULT CALLBACK HostProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_SIZE:
        if (state.edit) MoveWindow(state.edit, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    case WM_ERASEBKGND: {
        RECT rect; GetClientRect(window, &rect);
        FillRect(reinterpret_cast<HDC>(wp), &rect, state.background);
        return 1;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(wp), RGB(246, 250, 251));
        SetBkColor(reinterpret_cast<HDC>(wp), Background);
        SetBkMode(reinterpret_cast<HDC>(wp), OPAQUE);
        return reinterpret_cast<LRESULT>(state.background);
    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE && !state.creating && !state.finishing &&
            state.kind != Kind::Note && state.preview) {
            std::wstring value = Text();
            state.preview(state.kind, value, state.context);
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && !state.finishing)
            PostMessageW(window, CommitOnBlur, 0, 0);
        return 0;
    case CommitOnBlur:
        if (!state.finishing && state.edit && GetFocus() != state.edit) Finish(true);
        return 0;
    case WM_CLOSE:
        Finish(true); return 0;
    case WM_NCDESTROY:
        if (state.font) DeleteObject(state.font);
        if (state.background) DeleteObject(state.background);
        state = {};
        break;
    }
    return DefWindowProcW(window, message, wp, lp);
}
} // namespace

bool Active() { return state.edit && IsWindow(state.edit); }
HWND Handle() { return Active() ? state.edit : nullptr; }

bool Begin(HWND owner, const RECT& bounds, Kind kind, const std::wstring& initial,
           int fontPixels, bool handwritten, Commit commit, Preview preview,
           void* context) {
    if (!owner || !IsWindow(owner) || bounds.right <= bounds.left || bounds.bottom <= bounds.top) return false;
    if (Active() && !Finish(true)) return false;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW cls = { sizeof(cls) };
    cls.hInstance = instance; cls.lpfnWndProc = HostProc;
    cls.hCursor = LoadCursorW(nullptr, IDC_IBEAM); cls.lpszClassName = HostClass;
    if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    state = {};
    state.kind = kind; state.commit = commit; state.preview = preview; state.context = context; state.creating = true;
    state.background = CreateSolidBrush(Background);
    state.font = CreateFontW(-std::clamp(fontPixels, 8, 200), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH,
        handwritten ? (kind == Kind::Note ? L"KaiTi" : L"Segoe Print") : L"Microsoft YaHei UI");
    state.host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        HostClass, L"", WS_POPUP | WS_CLIPCHILDREN, bounds.left, bounds.top,
        bounds.right - bounds.left, bounds.bottom - bounds.top, owner, nullptr, instance, nullptr);
    if (!state.host) {
        if (state.font) DeleteObject(state.font);
        if (state.background) DeleteObject(state.background);
        state = {}; return false;
    }
    DWORD editStyle = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL;
    if (kind == Kind::Note) editStyle = WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
    state.edit = CreateWindowExW(0, L"EDIT", L"", editStyle, 0, 0,
        bounds.right - bounds.left, bounds.bottom - bounds.top, state.host, nullptr, instance, nullptr);
    if (!state.edit) { DestroyWindow(state.host); return false; }
    state.editProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(state.edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditProc)));
    SendMessageW(state.edit, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
    // UTF-8 note files are limited to 1 MiB; this also preserves old long notes.
    SendMessageW(state.edit, EM_SETLIMITTEXT, 1024 * 1024, 0);
    SendMessageW(state.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(1, 1));
    SetWindowTextW(state.edit, initial.c_str());
    if (!SetLayeredWindowAttributes(state.host, Background, 255, LWA_COLORKEY)) {
        DestroyWindow(state.host); return false;
    }
    state.creating = false;
    ShowWindow(state.host, SW_SHOW);
    SetForegroundWindow(state.host);
    SetFocus(state.edit);
    const int end = GetWindowTextLengthW(state.edit);
    SendMessageW(state.edit, EM_SETSEL, kind == Kind::Note ? WPARAM(end) : 0,
                 kind == Kind::Note ? LPARAM(end) : LPARAM(-1));
    return true;
}

bool Finish(bool save) {
    if (!Active()) return true;
    if (state.finishing) return true;
    state.finishing = true;
    if (state.composing) {
        // Finish the native composition before reading the EDIT text. IME
        // result/end messages may reenter this procedure while finishing.
        HIMC context = ImmGetContext(state.edit);
        BOOL notified = FALSE;
        if (context) {
            notified = ImmNotifyIME(context, NI_COMPOSITIONSTR,
                                    save ? CPS_COMPLETE : CPS_CANCEL, 0);
            ImmReleaseContext(state.edit, context);
        }
        // If the IME has not delivered its result/end yet, keep the field
        // intact. A later Enter, blur or Done can finish without losing text.
        if (save && (!notified || state.composing)) {
            state.finishing = false; return false;
        }
    }
    const std::wstring value = Text();
    if (state.commit && !state.commit(state.kind, value, save, state.context) && save) {
        state.finishing = false; return false;
    }
    HWND host = state.host;
    if (host && IsWindow(host)) DestroyWindow(host);
    return true;
}

void Move(const RECT& bounds) {
    if (state.host && bounds.right > bounds.left && bounds.bottom > bounds.top)
        SetWindowPos(state.host, nullptr, bounds.left, bounds.top,
            bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void Shutdown() { Finish(true); }

} // namespace inlineedit
