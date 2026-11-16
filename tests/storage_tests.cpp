#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/windows/storage.h"
#include "../src/windows/dates.h"
#include <cstdio>
#include <string>
#include <vector>

namespace {
int checks = 0;
void Check(bool value, const char* name) {
    ++checks;
    if (!value) {
        std::fprintf(stderr, "FAIL: %s (Win32 %lu)\n", name, GetLastError());
        ExitProcess(1);
    }
}

void WriteRaw(const std::wstring& path, const std::string& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "test fixture write open");
    DWORD written = 0;
    Check(WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written,
                    nullptr) && written == text.size(), "test fixture write");
    CloseHandle(file);
}

std::string ReadRaw(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "test fixture read open");
    DWORD size = GetFileSize(file, nullptr), read = 0;
    std::string text(size, '\0');
    Check(size == 0 || (ReadFile(file, &text[0], size, &read, nullptr) &&
                        size == read), "test fixture read");
    CloseHandle(file);
    return text;
}

void CheckNoTemporaryFiles(const std::wstring& folder) {
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW((folder + L"\\*.tmp-*").c_str(), &data);
    if (search != INVALID_HANDLE_VALUE) FindClose(search);
    Check(search == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND,
          "atomic writes leave no temporary files");
}

} // namespace

int main() {
    using namespace calendar;
    Check(DaysInMonth(2024, 2) == 29, "leap year");
    Check(DaysInMonth(1900, 2) == 28, "century is not leap year");
    Check(DaysInMonth(2000, 2) == 29, "four-century leap year");
    Check(DaysInMonth(2026, 0) == 0 && DaysInMonth(2026, 13) == 0 &&
          DaysInMonth(0, 1) == 0 && DaysInMonth(10000, 1) == 0,
          "invalid months and years");
    Check(MondayOffset(1, 1) == 0, "Gregorian epoch Monday");
    Check(MondayOffset(2026, 11) == 6, "November 2026 starts Sunday");
    Check(MondayOffset(2026, 6) == 0, "June 2026 starts Monday");
    Check(MondayOffset(2000, 1) == 5, "January 2000 starts Saturday");
    Check(MondayOffset(2026, 13) == -1, "invalid offset");
    int days = 0;
    for (int y = 1600; y < 2000; ++y) {
        for (int m = 1; m <= 12; ++m) {
            int count = DaysInMonth(y, m);
            int nextY = m == 12 ? y + 1 : y;
            int nextM = m == 12 ? 1 : m + 1;
            Check((MondayOffset(y, m) + count) % 7 == MondayOffset(nextY, nextM),
                  "400-year month boundary weekday");
            days += count;
        }
    }
    Check(days == 146097, "complete Gregorian 400-year cycle");
    auto sameDate = [](Date a, Date b) { return a.year == b.year && a.month == b.month && a.day == b.day; };
    Check(sameDate(AddDays({2024,2,28}, 1), {2024,2,29}), "add into leap day");
    Check(sameDate(AddDays({2024,3,1}, -1), {2024,2,29}), "subtract into leap day");
    Check(sameDate(AddDays({2026,1,1}, -1), {2025,12,31}), "subtract across year");
    Check(sameDate(AddDays({9999,12,31}, 1), {10000,1,1}), "display upper-bound spillover");
    Check(sameDate(AddDays({2026,2,29}, 0), {0,0,0}), "reject invalid arithmetic input");
    const auto november = MonthCells(2026,11);
    Check(sameDate(november.front(), {2026,10,26}) &&
          sameDate(november.back(), {2026,12,6}), "month grid includes previous and next months");
    Check(sameDate(MonthCells(2026,6).front(), {2026,6,1}) &&
          sameDate(MonthCells(2026,6).back(), {2026,7,12}), "Monday-start month still has 42 cells");
    for (int y = 1600; y < 2000; ++y) for (int m = 1; m <= 12; ++m) {
        const auto grid = MonthCells(y,m);
        bool complete = grid[MondayOffset(y,m)].day == 1;
        for (int i = 0; i < 42; ++i) {
            complete = complete && grid[i].day >= 1 &&
                grid[i].day <= DaysInMonth(grid[i].year, grid[i].month);
            if (i) complete = complete && sameDate(AddDays(grid[i-1],1), grid[i]);
        }
        Check(complete, "400-year grids have 42 consecutive valid dates");
    }
    Check(HolidayLabel({2026,2,17}) == L"春节" && HolidayLabel({2026,3,3}) == L"元宵" &&
          HolidayLabel({2026,6,19}) == L"端午" && HolidayLabel({2026,9,25}) == L"中秋",
          "2026 lunar festivals match HKO calendar");
    Check(HolidayLabel({2027,2,6}) == L"春节" && HolidayLabel({2027,2,20}) == L"元宵" &&
          HolidayLabel({2027,6,9}) == L"端午" && HolidayLabel({2027,9,15}) == L"中秋",
          "2027 lunar festivals match HKO calendar");
    Check(HolidayLabel({2026,4,5}) == L"清明" && HolidayLabel({2027,4,5}) == L"清明" &&
          HolidayLabel({2008,4,4}) == L"清明", "Qingming checked dates");
    Check(HolidayLabel({2020,10,1}) == L"国庆·中秋", "overlapping festivals are preserved");
    Check(HolidayLabel({2026,1,1}) == L"元旦" && HolidayLabel({2026,5,1}) == L"劳动节" &&
          HolidayLabel({2026,10,1}) == L"国庆" && HolidayLabel({2026,11,16}).empty(),
          "only common festivals are annotated");
    Check(HolidayLabel({1800,4,5}).empty() && HolidayLabel({2200,2,1}).empty() &&
          HolidayLabel({2026,2,30}).empty(), "unsupported lunar years and invalid dates have no invented labels");
    for (int y = 1901; y <= 2100; ++y) {
        int spring=0, lantern=0, dragon=0, mid=0, qing=0;
        const int yearDays = 365 + (DaysInMonth(y,2) == 29);
        for (int n=0; n < yearDays; ++n) {
            const auto label = HolidayLabel(AddDays({y,1,1},n));
            spring += label.find(L"春节") != std::wstring::npos;
            lantern += label.find(L"元宵") != std::wstring::npos;
            dragon += label.find(L"端午") != std::wstring::npos;
            mid += label.find(L"中秋") != std::wstring::npos;
            qing += label.find(L"清明") != std::wstring::npos;
        }
        Check(spring == 1 && lantern == 1 && dragon == 1 && mid == 1 && qing == 1,
              "each supported year has exactly one instance of each lunar/solar festival");
    }

    wchar_t temporary[MAX_PATH];
    Check(GetTempPathW(MAX_PATH, temporary) != 0, "temporary directory");
    std::wstring fixture = std::wstring(temporary) + L"Calendar-test-中文-" +
                           std::to_wstring(GetCurrentProcessId()) + L"-" +
                           std::to_wstring(GetTickCount64());
    Check(CreateDirectoryW(fixture.c_str(), nullptr) != FALSE, "create isolated fixture");
    std::wstring error, note;
    Store store(fixture);
    Check(store.Initialize(error) && error.empty(), "initialize portable Unicode directory");
    Settings defaults = store.LoadSettings();
    Check(store.lastError.empty() && defaults.width == 980 && defaults.followToday,
          "missing settings use defaults");
    Check(store.ReadNote(2026, 11, 16, note, error) && note.empty(),
          "missing note is empty");
    Check(!store.ReadNote(2026, 2, 29, note, error) && !error.empty(),
          "reject non-existent day read");
    Check(!store.WriteNote(2026, 2, 29, L"invalid", error),
          "reject non-existent day write");

    const std::wstring first = L"中文日历\r\n第一行：会议 10:30\r\n第二行 \U0001F31F café";
    const std::wstring second = L"修改后的记事\r\n保留换行和 Unicode \U0001F4C5";
    const std::wstring folder = store.root + L"\\notes\\2026-11";
    const std::wstring path = folder + L"\\16.txt";
    const std::wstring backup = store.root + L"\\backup\\2026-11\\16.txt";
    Check(store.WriteNote(2026, 11, 16, first, error), "write Unicode note");
    Check(store.ReadNote(2026, 11, 16, note, error) && note == first,
          "Unicode and newline round trip");
    std::string firstBytes = ReadRaw(path);
    Check(store.WriteNote(2026, 11, 16, second, error), "replace existing note");
    Check(ReadRaw(backup) == firstBytes, "previous version preserved exactly");
    Check(store.WriteNote(2026, 11, 16, second, error) && ReadRaw(backup) == firstBytes,
          "unchanged save does not replace backup");
    std::string secondBytes = ReadRaw(path);
    Check(store.WriteNote(2026, 11, 16, L"", error), "save empty note");
    Check(store.ReadNote(2026, 11, 16, note, error) && note.empty() &&
          ReadRaw(backup) == secondBytes, "empty note still preserves prior version");

    Check(store.WriteNote(2026, 11, 16, first, error), "restore note");
    Check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY), "make target read only");
    Check(!store.WriteNote(2026, 11, 16, second, error) && !error.empty(),
          "read only target returns visible error");
    Check(ReadRaw(path) == firstBytes, "failed write preserves original bytes");
    Check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL), "restore target permissions");
    CheckNoTemporaryFiles(folder);

    const std::wstring corruptPath = folder + L"\\17.txt";
    const std::string invalidUtf8 = "invalid\xC0\xAF";
    WriteRaw(corruptPath, invalidUtf8);
    Check(!store.ReadNote(2026, 11, 17, note, error), "reject invalid UTF8 read");
    Check(!store.WriteNote(2026, 11, 17, L"would overwrite", error),
          "reject overwrite of damaged UTF8 file");
    Check(ReadRaw(corruptPath) == invalidUtf8, "damaged file left untouched");
    WriteRaw(corruptPath, std::string("abc\0def", 7));
    Check(!store.ReadNote(2026, 11, 17, note, error), "reject embedded NUL");
    std::wstring loneSurrogate(1, static_cast<wchar_t>(0xD800));
    Check(!store.WriteNote(2026, 11, 18, loneSurrogate, error), "reject invalid UTF16 input");
    WriteRaw(corruptPath, "\xEF\xBB\xBF" + firstBytes);
    Check(store.ReadNote(2026, 11, 17, note, error) && note == first,
          "accept optional UTF8 BOM");

    Settings settings;
    settings.x = -1920; settings.y = 42;
    settings.width = 1100; settings.height = 700;
    settings.year = 1999; settings.month = 12; settings.followToday = false;
    settings.handwritten = true; settings.showHolidays = false; settings.noteFontSize = 20;
    Check(store.SaveSettings(settings, error), "save settings");
    Store reopened(fixture);
    Settings loaded = reopened.LoadSettings();
    Check(reopened.lastError.empty() && loaded.x == -1920 && loaded.y == 42 &&
          loaded.width == 1100 && loaded.height == 700 && loaded.year == 1999 &&
          loaded.month == 12 && !loaded.followToday && loaded.handwritten &&
          !loaded.showHolidays && loaded.noteFontSize == 20,
          "settings, appearance, and note font size survive restart");
    const std::wstring config = store.root + L"\\settings.ini";
    const std::string savedConfig = ReadRaw(config);
    settings.noteFontSize = 7;
    Check(!store.SaveSettings(settings,error), "font size below minimum is rejected");
    settings.noteFontSize = 33;
    Check(!store.SaveSettings(settings,error), "font size above maximum is rejected");
    Check(ReadRaw(config) == savedConfig, "invalid font size leaves settings unchanged");
    for (int size : {8,32}) {
        settings.noteFontSize = size;
        Check(store.SaveSettings(settings,error), "boundary note font size can be saved");
        Check(reopened.LoadSettings().noteFontSize == size, "boundary note font size survives reload");
    }
    settings.noteFontSize = 20;
    settings.month = 13;
    Check(!store.SaveSettings(settings, error), "invalid settings rejected");
    WriteRaw(config, "[Calendar]\nversion=1\nx=-1920\ny=42\nwidth=1100\nheight=700\n"
                     "year=1999\nmonth=12\nfollowToday=0\n");
    Settings legacy = reopened.LoadSettings();
    Check(reopened.lastError.empty() && legacy.x == -1920 && legacy.year == 1999 &&
          !legacy.followToday && !legacy.handwritten && legacy.showHolidays &&
          legacy.noteFontSize == 12,
          "old version-1 settings migrate with appearance defaults");
    Check(reopened.SaveSettings(legacy,error), "save migrated version-1 settings");
    Check(ReadRaw(config).find("handwritten=0") != std::string::npos &&
          ReadRaw(config).find("showHolidays=1") != std::string::npos &&
          ReadRaw(config).find("noteFontSize=12") != std::string::npos,
          "migrated file persists appearance settings");
    const std::string migratedConfig = ReadRaw(config);
    std::string invalidFontConfig = migratedConfig;
    invalidFontConfig.replace(invalidFontConfig.find("noteFontSize=12"),14,"noteFontSize=33");
    WriteRaw(config,invalidFontConfig);
    Check(reopened.LoadSettings().noteFontSize == 12 && !reopened.lastError.empty(),
          "invalid persisted font size reports damage and uses defaults");
    Check(!reopened.SaveSettings(defaults,error) && ReadRaw(config) == invalidFontConfig,
          "invalid persisted font size is preserved without overwrite");
    WriteRaw(config, "[Calendar]\nversion=1\nwidth=bad\n");
    reopened.LoadSettings();
    Check(!reopened.lastError.empty(), "corrupt config reported");
    Check(!reopened.SaveSettings(defaults, error), "corrupt config not overwritten");
    Check(ReadRaw(config) == "[Calendar]\nversion=1\nwidth=bad\n", "corrupt config preserved");
    CheckNoTemporaryFiles(store.root);

    // Delete only the known files and folders this test created.
    for (const auto& file : std::vector<std::wstring>{path, corruptPath, backup, config})
        Check(DeleteFileW(file.c_str()), "remove own fixture file");
    for (const auto& dir : std::vector<std::wstring>{folder,
             store.root + L"\\notes", store.root + L"\\backup\\2026-11",
             store.root + L"\\backup", store.root, fixture})
        Check(RemoveDirectoryW(dir.c_str()), "remove own fixture directory");
    std::printf("PASS: %d checks; calendar arithmetic, Unicode persistence, backups, "
                "read-only failure, corrupt-data protection, settings.\n", checks);
    return 0;
}
