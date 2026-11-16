#pragma once

#include <string>

namespace calendar {

struct Date { int year, month, day; };
Date Today();
int DaysInMonth(int year, int month);
// Monday = 0, Sunday = 6; returns -1 for an invalid year/month.
int MondayOffset(int year, int month);

struct Settings {
    int x = -1, y = -1, width = 980, height = 660;
    int year = 0, month = 0;
    bool followToday = true;
    bool handwritten = false;
    bool showHolidays = true;
    int noteFontSize = 12;
};

class Store {
public:
    // Absolute data directory, next to the executable; never uses AppData.
    std::wstring root;
    // In particular, check this immediately after LoadSettings().
    std::wstring lastError;

    explicit Store(const std::wstring& executableDirectory);
    bool Initialize(std::wstring& error);
    Settings LoadSettings();
    bool SaveSettings(const Settings& settings, std::wstring& error);
    bool ReadNote(int year, int month, int day, std::wstring& text,
                  std::wstring& error);
    bool WriteNote(int year, int month, int day, const std::wstring& text,
                   std::wstring& error);

private:
    bool settingsHealthy_ = true;
};

} // namespace calendar
