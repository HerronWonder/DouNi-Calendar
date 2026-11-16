#pragma once
#include "storage.h"
#include <array>

namespace calendar {
// Gregorian arithmetic; an invalid input returns {0, 0, 0}. A spillover
// cell may have year 0 or 10000 at the display range boundary.
Date AddDays(Date date, int days);
std::array<Date, 42> MonthCells(int year, int month);
// Festival names only, not statutory leave or make-up workday schedules.
// Lunar festivals and Qingming are available for 1901 through 2100.
std::wstring HolidayLabel(Date date);
}
