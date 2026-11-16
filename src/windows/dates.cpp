#include "dates.h"
#include <cstdint>

namespace calendar {
namespace {
// Dates are generated at development time using .NET's
// ChineseLunisolarCalendar.ToDateTime, accounting for GetLeapMonth.
// https://learn.microsoft.com/dotnet/api/system.globalization.chineselunisolarcalendar
// Qingming dates come from the HKO Gregorian/lunar conversion tables:
// https://www.hko.gov.hk/tc/gts/time/conversion.htm
// https://www.hko.gov.hk/tc/gts/time/calendar/text/files/T2026c.txt
// Each entry packs zero-based day-of-year for Spring Festival (6 bits),
// Dragon Boat (8 bits), Mid-Autumn (9 bits), then Qingming's April day - 4.
// Lantern Festival is Spring Festival + 14 days. The 200-year table is
// 800 bytes; the application has no .NET or network dependency.
constexpr std::uint32_t festivals[] = {
    0x00C36AB1u, 0x0140A826u, 0x0145659Cu, 0x00C2EA6Eu, 0x00BFE762u, 0x0144AC18u, 0x0142296Bu, 0x00BF66A0u,
    0x00C3AB15u, 0x01412868u, 0x0145A5DDu, 0x00C32AB0u, 0x00C067E4u, 0x00C52519u, 0x014269ECu, 0x00BFE721u,
    0x00C42B56u, 0x00C168E9u, 0x0146261Fu, 0x00C36AF2u, 0x00C0A826u, 0x00C5659Bu, 0x0142EA2Eu, 0x00C02763u,
    0x00C4ABD7u, 0x00C1E92Bu, 0x013F26A0u, 0x00C3EB56u, 0x00C0E868u, 0x00C5A5DDu, 0x01432AAFu, 0x00C0A7E4u,
    0x00C524D9u, 0x00C269ACu, 0x013FA6E2u, 0x00C46B97u, 0x00C168E9u, 0x00C6261Eu, 0x01436AF1u, 0x00C0E866u,
    0x00C5655Au, 0x00C2AA2Du, 0x01402763u, 0x00C4AC18u, 0x00C1A92Bu, 0x00BF26A0u, 0x00C3EB55u, 0x00C128A8u,
    0x00C5A5DCu, 0x00C32A6Fu, 0x00C067E4u, 0x00C5251Au, 0x00C2296Cu, 0x00BF66E1u, 0x00C42B97u, 0x00C1A92Au,
    0x00BEA61Eu, 0x00C36AF0u, 0x00C0E826u, 0x00C5A55Bu, 0x00C2A9EDu, 0x00BFE723u, 0x00C4ABD8u, 0x00C1E96Bu,
    0x00BF26A0u, 0x00C3EB54u, 0x00C128A7u, 0x00C5E5DDu, 0x00C32A6Fu, 0x00C067A4u, 0x00C4E4DAu, 0x00C269ADu,
    0x00BF66E1u, 0x00C42B96u, 0x00C1A929u, 0x003EE65Eu, 0x00C36AF0u, 0x00C0E825u, 0x00C5655Bu, 0x0042AA2Eu,
    0x00BFA723u, 0x00C46BD8u, 0x00C1E96Bu, 0x003F66E0u, 0x00C3EB32u, 0x00C12867u, 0x00C5E59Cu, 0x00432A6Fu,
    0x00C027A4u, 0x00C4E4DAu, 0x00C229ADu, 0x003FA722u, 0x00C42B96u, 0x00C1A8E8u, 0x00BEE61Eu, 0x0043AAF1u,
    0x00C0A7E5u, 0x00C5655Bu, 0x00C2AA2Eu, 0x003FE763u, 0x00C46BD7u, 0x00C1E96Au, 0x00BF669Fu, 0x0043EB55u,
    0x00C12867u, 0x00C5A59Cu, 0x00C2EA70u, 0x004067E5u, 0x0044E4D9u, 0x00C229ACu, 0x00BFA721u, 0x00446B96u,
    0x004168A8u, 0x00BEA61Eu, 0x00C36AB1u, 0x0040A826u, 0x0045255Bu, 0x00C2AA2Eu, 0x00BFE763u, 0x0044AC18u,
    0x0041E92Au, 0x00BF265Fu, 0x00C3EB15u, 0x00412868u, 0x0045A59Cu, 0x00C2EA6Fu, 0x00C067E4u, 0x00452519u,
    0x004229ABu, 0x00BFA6E1u, 0x00C46B96u, 0x0041A8E9u, 0x003EA5DEu, 0x00C36AB1u, 0x00C0A826u, 0x0045659Bu,
    0x0042AA2Du, 0x00BFE762u, 0x00C4A497u, 0x0041E96Au, 0x003F265Fu, 0x0043AB15u, 0x00C0E868u, 0x0045A5DDu,
    0x0042EA6Fu, 0x004067A4u, 0x00C52519u, 0x004269ACu, 0x003F66A0u, 0x00442B56u, 0x00C168E9u, 0x003EA61Fu,
    0x00432AB1u, 0x0040A826u, 0x00C5655Bu, 0x0042EA2Du, 0x003FE722u, 0x0044ABD7u, 0x00C1E92Au, 0x003F26A0u,
    0x0043AB14u, 0x0040E867u, 0x00C5A5DCu, 0x00432AAFu, 0x004067A3u, 0x0044E4D9u, 0x00C269ACu, 0x003FA6E1u,
    0x0043EB56u, 0x004168E9u, 0x00BEA61Eu, 0x00436AF1u, 0x0040A825u, 0x0045655Au, 0x0042A9EDu, 0x003FE763u,
    0x00446B97u, 0x0041A92Au, 0x003F26A0u, 0x0043EB55u, 0x0040E867u, 0x0045A5DCu, 0x00432A6Fu, 0x004067A4u,
    0x0044E499u, 0x0042296Cu, 0x003F66E1u, 0x00442B97u, 0x004128E8u, 0x003EA61Du, 0x00436AF0u, 0x0040E825u,
    0x0045651Au, 0x0042A9EDu, 0x003FE723u, 0x00446BD8u, 0x0041A92Au, 0x003EE69Fu, 0x0043EB54u, 0x00C128A7u,
};
static_assert(sizeof(festivals) / sizeof(festivals[0]) == 200, "festival coverage");

bool Leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
int MonthDays(int year, int month) {
    constexpr int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return month >= 1 && month <= 12 ? lengths[month - 1] + (month == 2 && Leap(year)) : 0;
}
std::int64_t Serial(Date date) {
    int year = date.year - (date.month <= 2);
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned y = unsigned(year - era * 400);
    const unsigned m = unsigned(date.month + (date.month > 2 ? -3 : 9));
    const unsigned day = (153 * m + 2) / 5 + unsigned(date.day - 1);
    return std::int64_t(era) * 146097 + y * 365 + y / 4 - y / 100 + day;
}
Date FromSerial(std::int64_t serial) {
    const std::int64_t era = (serial >= 0 ? serial : serial - 146096) / 146097;
    const unsigned day = unsigned(serial - era * 146097);
    const unsigned year = (day - day / 1460 + day / 36524 - day / 146096) / 365;
    int y = int(era * 400 + year);
    const unsigned doy = day - (365 * year + year / 4 - year / 100);
    const unsigned month = (5 * doy + 2) / 153;
    const int d = int(doy - (153 * month + 2) / 5 + 1);
    const int m = int(month) + (month < 10 ? 3 : -9);
    return {y + (m <= 2), m, d};
}
}

Date AddDays(Date date, int days) {
    if (date.month < 1 || date.month > 12 || date.day < 1 ||
        date.day > MonthDays(date.year, date.month)) return {0, 0, 0};
    return FromSerial(Serial(date) + days);
}

std::array<Date, 42> MonthCells(int year, int month) {
    std::array<Date, 42> cells = {};
    int offset = MondayOffset(year, month);
    if (offset < 0) return cells;
    Date first = AddDays({year, month, 1}, -offset);
    for (int i = 0; i < 42; ++i) cells[i] = AddDays(first, i);
    return cells;
}

std::wstring HolidayLabel(Date date) {
    if (date.year < 1 || date.year > 9999 || date.month < 1 || date.month > 12 ||
        date.day < 1 || date.day > MonthDays(date.year, date.month)) return L"";
    std::wstring label;
    if (date.month == 1 && date.day == 1) label = L"元旦";
    else if (date.month == 5 && date.day == 1) label = L"劳动节";
    else if (date.month == 10 && date.day == 1) label = L"国庆";
    if (date.year < 1901 || date.year > 2100) return label;
    const std::uint32_t packed = festivals[date.year - 1901];
    const int day = int(Serial(date) - Serial({date.year, 1, 1}));
    const wchar_t* lunar = nullptr;
    if (day == int(packed & 63)) lunar = L"春节";
    else if (day == int(packed & 63) + 14) lunar = L"元宵";
    else if (day == int((packed >> 6) & 255)) lunar = L"端午";
    else if (day == int((packed >> 14) & 511)) lunar = L"中秋";
    else if (date.month == 4 && date.day == int((packed >> 23) & 3) + 4) lunar = L"清明";
    if (lunar) { if (!label.empty()) label += L"·"; label += lunar; }
    return label;
}
}
