#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "storage.h"
#include <cerrno>
#include <climits>
#include <cwchar>
#include <string>

namespace calendar {
namespace {

constexpr DWORD kMaxNoteBytes = 1024 * 1024;
constexpr DWORD kMaxSettingsBytes = 16384;

std::wstring Join(const std::wstring& parent, const std::wstring& child) {
    if (parent.empty()) return child;
    wchar_t end = parent.back();
    return parent + ((end == L'\\' || end == L'/') ? L"" : L"\\") + child;
}

std::wstring WinError(const std::wstring& action, const std::wstring& path,
                      DWORD code = GetLastError()) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                   reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring detail = buffer ? buffer : L"未知系统错误";
    if (buffer) LocalFree(buffer);
    while (!detail.empty() && (detail.back() == L'\r' || detail.back() == L'\n'))
        detail.pop_back();
    return action + L"\n" + path + L"\n" + detail + L"（" +
           std::to_wstring(code) + L"）";
}

bool EnsureDirectory(const std::wstring& path, std::wstring& error) {
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    DWORD code = GetLastError();
    if (code == ERROR_ALREADY_EXISTS) {
        DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY)) return true;
    }
    error = WinError(L"无法建立数据文件夹", path, code);
    return false;
}

bool ReadBytes(const std::wstring& path, DWORD limit, std::string& bytes,
               bool& exists, std::wstring& error) {
    bytes.clear();
    exists = false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return true;
        error = WinError(L"无法读取文件", path, code);
        return false;
    }
    exists = true;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size)) {
        error = WinError(L"无法读取文件长度", path);
        CloseHandle(file);
        return false;
    }
    if (size.QuadPart < 0 || size.QuadPart > limit) {
        error = L"文件过大，已保留原文件，未尝试覆盖：\n" + path;
        CloseHandle(file);
        return false;
    }
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    if (!bytes.empty() && (!ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()),
                                    &read, nullptr) || read != bytes.size())) {
        error = WinError(L"未能完整读取文件", path);
        CloseHandle(file);
        bytes.clear();
        return false;
    }
    CloseHandle(file);
    return true;
}

bool Decode(const std::string& bytes, std::wstring& text, std::wstring& error,
            const std::wstring& path) {
    text.clear();
    size_t start = bytes.size() >= 3 &&
                   static_cast<unsigned char>(bytes[0]) == 0xEF &&
                   static_cast<unsigned char>(bytes[1]) == 0xBB &&
                   static_cast<unsigned char>(bytes[2]) == 0xBF ? 3 : 0;
    if (start == bytes.size()) return true;
    const char* data = bytes.data() + start;
    int length = static_cast<int>(bytes.size() - start);
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, length,
                                     nullptr, 0);
    if (needed <= 0) {
        error = L"文件不是有效的 UTF-8 文本，已保留原文件：\n" + path;
        return false;
    }
    text.resize(needed);
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, length,
                            &text[0], needed) != needed ||
        text.find(L'\0') != std::wstring::npos) {
        text.clear();
        error = L"文件含有无效文本，已保留原文件：\n" + path;
        return false;
    }
    return true;
}

bool Encode(const std::wstring& text, std::string& bytes, std::wstring& error) {
    bytes.clear();
    if (text.empty()) return true;
    if (text.size() > kMaxNoteBytes || text.find(L'\0') != std::wstring::npos) {
        error = L"记事过长或含有无效字符，原文件未被修改。";
        return false;
    }
    int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                     static_cast<int>(text.size()), nullptr, 0,
                                     nullptr, nullptr);
    if (needed <= 0 || needed > static_cast<int>(kMaxNoteBytes)) {
        error = L"记事不能转换为有效 UTF-8，或超过 1 MB；原文件未被修改。";
        return false;
    }
    bytes.resize(needed);
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), &bytes[0], needed,
                            nullptr, nullptr) != needed) {
        error = L"记事文本转换失败，原文件未被修改。";
        return false;
    }
    return true;
}

bool AtomicWrite(const std::wstring& path, const std::string& bytes,
                 std::wstring& error) {
    static LONG sequence = 0;
    std::wstring temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 16; ++attempt) {
        temporary = path + L".tmp-" + std::to_wstring(GetCurrentProcessId()) +
                    L"-" + std::to_wstring(InterlockedIncrement(&sequence));
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS) {
            error = WinError(L"无法写入数据，请将软件放到有写入权限的文件夹", path);
            return false;
        }
    }
    if (file == INVALID_HANDLE_VALUE) {
        error = L"无法建立临时文件，原文件未被修改：\n" + path;
        return false;
    }
    DWORD written = 0;
    bool ok = bytes.empty() ||
              (WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                         &written, nullptr) && written == bytes.size());
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    DWORD code = GetLastError();
    if (!CloseHandle(file) && ok) { ok = false; code = GetLastError(); }
    if (ok && !MoveFileExW(temporary.c_str(), path.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ok = false;
        code = GetLastError();
    }
    if (!ok) {
        DeleteFileW(temporary.c_str());
        error = WinError(L"保存失败，原文件未被修改", path, code);
    }
    return ok;
}

bool ValidDate(int year, int month, int day) {
    int count = DaysInMonth(year, month);
    return count > 0 && day >= 1 && day <= count;
}

std::wstring MonthName(int year, int month) {
    wchar_t buffer[16];
    swprintf(buffer, 16, L"%04d-%02d", year, month);
    return buffer;
}

std::wstring DayName(int day) {
    wchar_t buffer[16];
    swprintf(buffer, 16, L"%02d.txt", day);
    return buffer;
}

bool ValidSettings(const Settings& value) {
    bool month = (value.year == 0 && value.month == 0) ||
                 DaysInMonth(value.year, value.month) > 0;
    return month && value.width >= 280 && value.width <= 10000 &&
           value.height >= 240 && value.height <= 10000 &&
           value.x >= -100000 && value.x <= 100000 &&
           value.y >= -100000 && value.y <= 100000 &&
           value.noteFontSize >= 8 && value.noteFontSize <= 32;
}

bool ParseInteger(const std::wstring& input, int& output) {
    if (input.empty()) return false;
    wchar_t* end = nullptr;
    errno = 0;
    long value = wcstol(input.c_str(), &end, 10);
    if (errno == ERANGE || end == input.c_str() || *end != 0 ||
        value < INT_MIN || value > INT_MAX) return false;
    output = static_cast<int>(value);
    return true;
}

bool ParseSettings(const std::wstring& text, Settings& result) {
    unsigned fields = 0;
    size_t offset = 0;
    while (offset < text.size()) {
        size_t end = text.find(L'\n', offset);
        std::wstring line = text.substr(offset, end == std::wstring::npos ?
                                       text.size() - offset : end - offset);
        offset = end == std::wstring::npos ? text.size() : end + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty() || line[0] == L';' || line[0] == L'#' ||
            line == L"[Calendar]") continue;
        size_t equal = line.find(L'=');
        if (equal == std::wstring::npos) return false;
        std::wstring key = line.substr(0, equal);
        int value = 0;
        if (!ParseInteger(line.substr(equal + 1), value)) return false;
        unsigned flag = 0;
        if (key == L"version") { if (value != 1) return false; flag = 1; }
        else if (key == L"x") { result.x = value; flag = 2; }
        else if (key == L"y") { result.y = value; flag = 4; }
        else if (key == L"width") { result.width = value; flag = 8; }
        else if (key == L"height") { result.height = value; flag = 16; }
        else if (key == L"year") { result.year = value; flag = 32; }
        else if (key == L"month") { result.month = value; flag = 64; }
        else if (key == L"followToday") {
            if (value != 0 && value != 1) return false;
            result.followToday = value != 0;
            flag = 128;
        } else if (key == L"handwritten") {
            if (value != 0 && value != 1) return false;
            result.handwritten = value != 0;
            flag = 256;
        } else if (key == L"showHolidays") {
            if (value != 0 && value != 1) return false;
            result.showHolidays = value != 0;
            flag = 512;
        } else if (key == L"noteFontSize") {
            result.noteFontSize = value;
            flag = 1024;
        } else return false;
        if (fields & flag) return false;
        fields |= flag;
    }
    // New appearance fields are optional so existing version-1 files migrate
    // without losing their position, current month, or note directory.
    return (fields & 255) == 255 && ValidSettings(result);
}

} // namespace

Date Today() {
    SYSTEMTIME value;
    GetLocalTime(&value);
    return {value.wYear, value.wMonth, value.wDay};
}

int DaysInMonth(int year, int month) {
    if (year < 1 || year > 9999 || month < 1 || month > 12) return 0;
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month - 1] + (month == 2 && year % 4 == 0 &&
                             (year % 100 != 0 || year % 400 == 0) ? 1 : 0);
}

int MondayOffset(int year, int month) {
    if (!DaysInMonth(year, month)) return -1;
    static const int offsets[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (month < 3) --year;
    return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1]) % 7;
}

Store::Store(const std::wstring& executableDirectory)
    : root(Join(executableDirectory, L"data")) {}

bool Store::Initialize(std::wstring& error) {
    error.clear();
    lastError.clear();
    if (!EnsureDirectory(root, error) ||
        !EnsureDirectory(Join(root, L"notes"), error)) {
        lastError = error;
        return false;
    }
    // A one-time writable-folder probe; no recurring background file I/O.
    std::wstring probe = Join(root, L".write-test-" +
                                   std::to_wstring(GetCurrentProcessId()));
    HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY |
                               FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = WinError(L"数据文件夹不可写，请将整个软件文件夹移到可写位置", root);
        lastError = error;
        return false;
    }
    CloseHandle(file);
    return true;
}

Settings Store::LoadSettings() {
    lastError.clear();
    settingsHealthy_ = true;
    Settings settings;
    std::string bytes;
    std::wstring text;
    bool exists;
    std::wstring path = Join(root, L"settings.ini");
    if (!ReadBytes(path, kMaxSettingsBytes, bytes, exists, lastError) ||
        (exists && !Decode(bytes, text, lastError, path))) {
        settingsHealthy_ = false;
        return settings;
    }
    if (exists && !ParseSettings(text, settings)) {
        settingsHealthy_ = false;
        lastError = L"设置文件格式无效，暂用默认位置；原文件已保留。\n"
                    L"请退出后修复或重命名此文件，再重新启动：\n" + path;
        return Settings();
    }
    return settings;
}

bool Store::SaveSettings(const Settings& settings, std::wstring& error) {
    error.clear();
    if (!settingsHealthy_) {
        error = L"原设置文件无法读取，已暂停覆盖；请退出后修复或重命名：\n" +
                Join(root, L"settings.ini");
        lastError = error;
        return false;
    }
    if (!ValidSettings(settings)) {
        error = L"日历位置、大小或月份无效，未保存设置。";
        lastError = error;
        return false;
    }
    std::wstring text = L"[Calendar]\r\nversion=1\r\nx=" +
        std::to_wstring(settings.x) + L"\r\ny=" + std::to_wstring(settings.y) +
        L"\r\nwidth=" + std::to_wstring(settings.width) + L"\r\nheight=" +
        std::to_wstring(settings.height) + L"\r\nyear=" +
        std::to_wstring(settings.year) + L"\r\nmonth=" +
        std::to_wstring(settings.month) + L"\r\nfollowToday=" +
        (settings.followToday ? L"1" : L"0") + L"\r\nhandwritten=" +
        (settings.handwritten ? L"1" : L"0") + L"\r\nshowHolidays=" +
        (settings.showHolidays ? L"1" : L"0") + L"\r\nnoteFontSize=" +
        std::to_wstring(settings.noteFontSize) + L"\r\n";
    std::string bytes, previous;
    bool exists;
    std::wstring path = Join(root, L"settings.ini");
    if (!Encode(text, bytes, error) ||
        !ReadBytes(path, kMaxSettingsBytes, previous, exists, error)) {
        lastError = error;
        return false;
    }
    // An external modification is checked too, so corrupt files are never replaced.
    if (exists) {
        std::wstring priorText;
        Settings priorSettings;
        if (!Decode(previous, priorText, error, path) ||
            !ParseSettings(priorText, priorSettings)) {
            if (error.empty()) error = L"原设置文件格式无效，已保留：\n" + path;
            lastError = error;
            settingsHealthy_ = false;
            return false;
        }
        if (previous == bytes) { lastError.clear(); return true; }
    }
    bool ok = AtomicWrite(path, bytes, error);
    lastError = error;
    return ok;
}

bool Store::ReadNote(int year, int month, int day, std::wstring& text,
                     std::wstring& error) {
    text.clear();
    error.clear();
    if (!ValidDate(year, month, day)) {
        error = L"记事日期无效。";
        lastError = error;
        return false;
    }
    std::wstring path = Join(Join(Join(root, L"notes"), MonthName(year, month)), DayName(day));
    std::string bytes;
    bool exists;
    bool ok = ReadBytes(path, kMaxNoteBytes, bytes, exists, error) &&
              (!exists || Decode(bytes, text, error, path));
    lastError = error;
    return ok;
}

bool Store::WriteNote(int year, int month, int day, const std::wstring& text,
                      std::wstring& error) {
    error.clear();
    if (!ValidDate(year, month, day)) {
        error = L"记事日期无效。";
        lastError = error;
        return false;
    }
    std::string bytes;
    if (!Encode(text, bytes, error)) { lastError = error; return false; }
    std::wstring folder = Join(Join(root, L"notes"), MonthName(year, month));
    std::wstring path = Join(folder, DayName(day));
    std::string previous;
    std::wstring previousText;
    bool exists;
    if (!ReadBytes(path, kMaxNoteBytes, previous, exists, error) ||
        (exists && !Decode(previous, previousText, error, path))) {
        lastError = error;
        return false;
    }
    if (exists && previousText == text) { lastError.clear(); return true; }
    if (!EnsureDirectory(folder, error)) { lastError = error; return false; }
    if (exists) {
        std::wstring backupRoot = Join(root, L"backup");
        std::wstring backupFolder = Join(backupRoot, MonthName(year, month));
        if (!EnsureDirectory(backupRoot, error) || !EnsureDirectory(backupFolder, error) ||
            !AtomicWrite(Join(backupFolder, DayName(day)), previous, error)) {
            error = L"无法保存上一版备份，本次记事未覆盖。\n" + error;
            lastError = error;
            return false;
        }
    }
    bool ok = AtomicWrite(path, bytes, error);
    lastError = error;
    return ok;
}

} // namespace calendar
