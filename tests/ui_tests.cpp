// Native checks use hidden test-owned windows and isolated temporary data.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {
int checks = 0, unexpectedDialogs = 0;
bool captureNextFrame = false, frameSaved = false;
double transparentFraction = 0;
std::wstring previewPath;
std::vector<HWND> positionedWindows, foregroundWindows;
HWND popupOwner=nullptr;
std::vector<std::wstring> popupLabels;
void Check(bool condition, const char* description) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s (Win32 %lu)\n", description, GetLastError()); ExitProcess(1); }
}
BOOL HiddenSetWindowPos(HWND window, HWND, int x, int y, int width, int height, UINT flags) {
    DWORD owner = 0; GetWindowThreadProcessId(window, &owner);
    Check(owner == GetCurrentProcessId(), "position only a test-owned window");
    positionedWindows.push_back(window);
    return ::SetWindowPos(window, nullptr, x, y, width, height,
        (flags & ~SWP_SHOWWINDOW) | SWP_NOACTIVATE | SWP_NOZORDER);
}
BOOL HiddenSetForegroundWindow(HWND window) { foregroundWindows.push_back(window); return TRUE; }
BOOL HiddenShowWindow(HWND, int) { return FALSE; }
int UnexpectedMessageBox(HWND, LPCWSTR, LPCWSTR, UINT) { ++unexpectedDialogs; return IDCANCEL; }
BOOL FakeTrackPopupMenuEx(HMENU menu,UINT,int,int,HWND owner,LPTPMPARAMS) {
    popupOwner=owner; popupLabels.clear();
    for(int i=0;i<GetMenuItemCount(menu);++i) {
        wchar_t label[128]={}; GetMenuStringW(menu,i,label,128,MF_BYPOSITION);
        if(label[0]) popupLabels.emplace_back(label);
    }
    return 0;
}
}

#define SetWindowPos HiddenSetWindowPos
#define SetForegroundWindow HiddenSetForegroundWindow
#define ShowWindow HiddenShowWindow
#define MessageBoxW UnexpectedMessageBox
#define TrackPopupMenuEx FakeTrackPopupMenuEx
#define wWinMain ProductionEntry
#define CALENDAR_TEST
#include "../src/windows/inline_editor.cpp"
#include "../src/windows/main.cpp"
#undef CALENDAR_TEST
#undef wWinMain
#undef MessageBoxW
#undef TrackPopupMenuEx
#undef ShowWindow
#undef SetForegroundWindow
#undef SetWindowPos

namespace {
void SaveTestFrame(Gdiplus::Bitmap& source) {
    if (!captureNextFrame) return;
    captureNextFrame = false;
    const UINT width = source.GetWidth(), height = source.GetHeight();
    Gdiplus::Rect bounds(0, 0, width, height); Gdiplus::BitmapData data = {};
    Check(source.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) == Gdiplus::Ok,
          "inspect rendered transparency");
    size_t transparent = 0;
    size_t yearPixels=0,monthPixels=0;
    const auto layout=ComputeLayout();
    for (UINT y = 0; y < height; ++y) {
        const auto* row = static_cast<const BYTE*>(data.Scan0) + y * data.Stride;
        for (UINT x = 0; x < width; ++x) {
            const BYTE alpha=row[x*4+3]; transparent += alpha==0;
            POINT point={LONG(x),LONG(y)};
            if(alpha>100&&PtInRect(&layout.year,point)) ++yearPixels;
            if(alpha>100&&PtInRect(&layout.month,point)) ++monthPixels;
        }
    }
    Check(yearPixels>8&&monthPixels>4,"year and month digits are visibly rendered");
    Lettering fonts; Gdiplus::Bitmap measurement(1,1); Gdiplus::Graphics measure(&measurement);
    for(const auto item:{std::make_pair(Number(settings.year),layout.year),std::make_pair(Number(settings.month),layout.month)}) {
        Gdiplus::RectF extent;
        Check(measure.MeasureString(item.first.c_str(),-1,fonts.Get(item.first,TitleText),Gdiplus::PointF(0,0),&extent)==Gdiplus::Ok,
              "measure native title glyphs");
        Check(extent.Width<=float(item.second.right-item.second.left)+1,"title digits fit their layout width");
    }
    transparentFraction = double(transparent) / (double(width) * height); source.UnlockBits(&data);
    Gdiplus::Bitmap preview(width, height, PixelFormat32bppARGB);
    { Gdiplus::Graphics graphics(&preview); graphics.Clear(Gdiplus::Color(255, 34, 55, 62));
      Check(graphics.DrawImage(&source, 0, 0, width, height) == Gdiplus::Ok, "compose preview"); }
    const CLSID png = {0x557cf406, 0x1a04, 0x11d3, {0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    frameSaved = preview.Save(previewPath.c_str(), &png, nullptr) == Gdiplus::Ok;
    Check(frameSaved, "save native calendar preview");
}
LRESULT Hit(int x, int y) {
    POINT point = {x,y}; ClientToScreen(mainWindow,&point);
    return SendMessageW(mainWindow,WM_NCHITTEST,0,MAKELPARAM(short(point.x),short(point.y)));
}
int FindDay(calendar::Date date) {
    for (int i=0;i<42;++i) if(SameDate(cells[i].date,date)) return i;
    return -1;
}
void Preview(const std::filesystem::path& output, const wchar_t* name) {
    previewPath=(output/name).wstring(); frameSaved=false; captureNextFrame=true; Render();
    Check(frameSaved && std::filesystem::file_size(previewPath)>10000,"render complete visual preview");
}
}

int main() {
    Check(std::wstring(AppName)==L"豆泥日历","Chinese product name is DouNi Calendar");
    const std::wstring originalExecutable=executablePath;
    executablePath=L"D:\\apps\\豆泥日历\\DouNiCalendar.exe";
    Check(IsLegacyAutostartCommand(L"\"D:\\apps\\桌面日历\\DesktopCalendar.exe\""),"renamed neighboring legacy startup is recognized");
    Check(IsLegacyAutostartCommand(L"\"D:\\apps\\豆泥日历\\DesktopCalendar.exe\""),"same-folder legacy startup is recognized");
    Check(!IsLegacyAutostartCommand(L"\"D:\\other\\桌面日历\\DesktopCalendar.exe\""),"unrelated portable startup is preserved");
    Check(!IsLegacyAutostartCommand(L"\"D:\\apps\\桌面日历\\DesktopCalendar.exe\" --other"),"legacy command with extra arguments is preserved");
    executablePath=originalExecutable;
    instance=GetModuleHandleW(nullptr);
    Gdiplus::GdiplusStartupInput input; ULONG_PTR token=0;
    Check(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok,"initialize GDI+");
    wchar_t temporary[MAX_PATH+1]={}; Check(GetTempPathW(MAX_PATH,temporary)>0,"resolve TEMP");
    const auto tempRoot=std::filesystem::path(temporary)/(L"douni-calendar-ui-tests-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Check(std::filesystem::create_directory(tempRoot),"create isolated fixture");
    store=std::make_unique<calendar::Store>(tempRoot.wstring()); std::wstring error;
    Check(store->Initialize(error),"initialize test storage"); settings=store->LoadSettings();
    today=calendar::Today(); settings.year=today.year; settings.month=today.month;
    settings.width=980; settings.height=660;
    WNDCLASSEXW wc={sizeof(wc)}; wc.hInstance=instance; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    wc.lpszClassName=MainClass; wc.lpfnWndProc=MainProc;
    Check(RegisterClassExW(&wc)!=0,"register production calendar procedure");
    wc.lpszClassName=MenuClass; wc.lpfnWndProc=DefWindowProcW;
    Check(RegisterClassExW(&wc)!=0,"register hidden menu procedure");
    mainWindow=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,
        MainClass,L"Calendar test",WS_POPUP,100,100,980,660,nullptr,nullptr,instance,nullptr);
    Check(mainWindow!=nullptr,"create hidden calendar"); ready=true; LoadMonth(); Render();
    menuWindow=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED,MenuClass,L"",WS_POPUP,0,0,1,1,nullptr,nullptr,instance,nullptr);
    Check(menuWindow!=nullptr,"create hidden menu owner");

    positionedWindows.clear(); foregroundWindows.clear();
    SendMessageW(mainWindow,TrayMessage,0,MAKELPARAM(WM_CONTEXTMENU,0));
    Check(popupOwner==menuWindow,"right click menu uses separate owner");
    Check(positionedWindows.size()==1&&positionedWindows[0]==menuWindow,"right click never raises main calendar");
    Check(foregroundWindows.size()==1&&foregroundWindows[0]==menuWindow,"right click focuses only menu owner");
    const std::vector<std::wstring> expectedMenu={L"编辑模式",L"移动与缩放",L"手绘风格",L"字体大小",L"显示节假日",L"开机启动",L"退出"};
    Check(popupLabels==expectedMenu,"menu contains only concise requested commands");
    SendMessageW(mainWindow,TrayMessage,0,MAKELPARAM(NIN_SELECT,0));
    Check(mode==Mode::Edit,"tray left click enters edit");
    SendMessageW(mainWindow,TrayMessage,0,MAKELPARAM(NIN_SELECT,0));
    Check(mode==Mode::Locked,"tray left click again finishes");

    LONG_PTR ex=GetWindowLongPtrW(mainWindow,GWL_EXSTYLE);
    Check((ex&(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE))==
          (WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE),"locked native styles");
    Check(Hit(400,350)==HTTRANSPARENT,"locked hit passes through");
    Check(SetMode(Mode::Edit),"enter calendar editing and navigation");
    Check(Hit(400,350)==HTCLIENT,"editing is interactive");
    Check(SetMode(Mode::Transform),"enter combined move and resize");
    Check(Hit(Width()-2,Height()-2)==HTBOTTOMRIGHT,"combined lower right resize");
    Check(Hit(1,Height()/2)==HTLEFT,"combined edge resize");
    Check(Hit(400,350)==HTCAPTION,"combined interior drag");
    Check((GetWindowLongPtrW(mainWindow,GWL_STYLE)&WS_THICKFRAME)!=0,"native sizing frame");
    Check(SetMode(Mode::Edit),"enter note editing");
    Check(!(GetWindowLongPtrW(mainWindow,GWL_STYLE)&WS_THICKFRAME),"editing removes sizing frame");
    Check(Finish()&&mode==Mode::Locked,"finish restores locked mode");

    Check(ChangeMonth(2026,11),"display requested month");
    Check(SameDate(cells.front().date,{2026,10,26})&&SameDate(cells.back().date,{2026,12,6}),"all six rows include adjacent months");
    Check(StepMonth(1)&&settings.year==2026&&settings.month==12,"next month navigation");
    Check(StepMonth(1)&&settings.year==2027&&settings.month==1,"next year boundary");
    Check(StepMonth(-1)&&settings.year==2026&&settings.month==12,"previous year boundary");
    Check(!ChangeMonth(2026,13)&&settings.month==12,"invalid requested month leaves display unchanged");

    Check(ChangeMonth(2026,11)&&SetMode(Mode::Edit),"start inline note fixture");
    int noteIndex=FindDay({2026,11,4}); Check(noteIndex>=0&&BeginNote(noteIndex),"begin inline day editor");
    Check(inlineedit::Active()&&inlineedit::Handle()!=nullptr,"native caret editor exists");
    Check(!IsWindowVisible(GetParent(inlineedit::Handle())),"inline test editor remains hidden");
    Check(!(GetWindowLongPtrW(GetParent(inlineedit::Handle()),GWL_STYLE)&(WS_CAPTION|WS_SYSMENU)),"inline editor has no dialog title or frame");
    COLORREF transparentKey=0; BYTE editorAlpha=0; DWORD editorFlags=0;
    Check(GetLayeredWindowAttributes(GetParent(inlineedit::Handle()),&transparentKey,&editorAlpha,&editorFlags)!=FALSE,
          "query inline editor transparency");
    Check((editorFlags&LWA_COLORKEY)!=0&&(editorFlags&LWA_ALPHA)==0,"editor removes its background instead of a dark translucent rectangle");
    RECT inlineBounds={}; GetWindowRect(GetParent(inlineedit::Handle()),&inlineBounds);
    const auto expectedBounds=ToScreen(ComputeLayout().NoteBounds(noteIndex));
    Check(EqualRect(&inlineBounds,&expectedBounds),"inline editor sits in the selected day cell");
    const std::wstring note=L"中文记事\r\n第二行";
    Check(SetWindowTextW(inlineedit::Handle(),note.c_str())!=FALSE,"input multiline Unicode note");
    const HWND sameEditor=inlineedit::Handle();
    const auto noteBounds=ComputeLayout().NoteBounds(noteIndex);
    Click(POINT{noteBounds.left+3,noteBounds.top+3});
    Check(inlineedit::Active()&&inlineedit::Handle()==sameEditor,"transparent-background click keeps the same note editor");
    std::vector<wchar_t> sameDraft(GetWindowTextLengthW(sameEditor)+1);
    GetWindowTextW(sameEditor,sameDraft.data(),int(sameDraft.size()));
    Check(std::wstring(sameDraft.data())==note,"transparent-background click retains the full draft");
    SendMessageW(inlineedit::Handle(),WM_KEYDOWN,VK_RETURN,0);
    Check(!inlineedit::Active(),"Enter commits inline editor");
    std::wstring saved;
    Check(store->ReadNote(2026,11,4,saved,error)&&saved==note,"Enter persists correct day");
    Check(cells[noteIndex].note==note,"inline saved note updates grid");
    editingDate=cells[noteIndex].date;
    Check(InlineCommit(inlineedit::Kind::Note,std::wstring(1,wchar_t(0xD800)),true,nullptr),"invalid UTF-16 input is silently discarded");
    Check(store->ReadNote(2026,11,4,saved,error)&&saved==note&&cells[noteIndex].note==note,"invalid UTF-16 preserves previous note");
    Check(InlineCommit(inlineedit::Kind::Note,std::wstring(400000,L'中'),true,nullptr),"over-limit UTF-8 input is silently discarded");
    Check(store->ReadNote(2026,11,4,saved,error)&&saved==note&&cells[noteIndex].note==note,"over-limit input preserves previous note");

    Check(BeginNote(noteIndex),"begin save-failure fixture");
    const std::wstring failedDraft=L"保存失败后保留的草稿";
    SetWindowTextW(inlineedit::Handle(),failedDraft.c_str());
    const auto readOnlyPath=tempRoot/L"data/notes/2026-11/04.txt";
    Check(SetFileAttributesW(readOnlyPath.c_str(),FILE_ATTRIBUTE_READONLY)!=FALSE,"make isolated note read-only");
    Check(!inlineedit::Finish(true)&&inlineedit::Active(),"failed save leaves inline editor open");
    std::vector<wchar_t> draft(GetWindowTextLengthW(inlineedit::Handle())+1);
    GetWindowTextW(inlineedit::Handle(),draft.data(),int(draft.size()));
    Check(std::wstring(draft.data())==failedDraft,"failed save retains complete draft");
    Check(store->ReadNote(2026,11,4,saved,error)&&saved==note,"failed save retains previous disk data");
    Check(SetFileAttributesW(readOnlyPath.c_str(),FILE_ATTRIBUTE_NORMAL)!=FALSE,"restore isolated note permissions");
    Check(inlineedit::Finish(false),"cancel failed draft without popup");
    Check(BeginNote(noteIndex),"begin existing note");
    SetWindowTextW(inlineedit::Handle(),L"取消内容");
    SendMessageW(inlineedit::Handle(),WM_KEYDOWN,VK_ESCAPE,0);
    Check(!inlineedit::Active()&&cells[noteIndex].note==note,"Escape leaves original note");
    Check(BeginNote(0),"adjacent-month dates are editable");
    SetWindowTextW(inlineedit::Handle(),L"上月记事");
    Check(Finish(),"Done commits inline note");
    Check(store->ReadNote(2026,10,26,saved,error)&&saved==L"上月记事","adjacent-month note keeps actual date");

    Check(SetMode(Mode::Edit)&&BeginHeader(inlineedit::Kind::Month),"begin inline month field");
    SetWindowTextW(inlineedit::Handle(),L"12");
    Check(settings.month==12,"valid month immediately previews");
    SetWindowTextW(inlineedit::Handle(),L"99");
    Check(settings.year==2026&&settings.month==11,"invalid preview immediately restores original month");
    Check(inlineedit::Finish(true)&&settings.year==2026&&settings.month==11,"invalid month reverts transaction");
    Check(BeginHeader(inlineedit::Kind::Month),"start invalid then arrow regression");
    SetWindowTextW(inlineedit::Handle(),L"12");
    SetWindowTextW(inlineedit::Handle(),L"99");
    Check(StepMonth(1)&&settings.year==2026&&settings.month==12,"arrow uses restored month after invalid preview");
    Check(ChangeMonth(2026,11),"restore year editing fixture");
    Check(BeginHeader(inlineedit::Kind::Year),"begin inline year field");
    SetWindowTextW(inlineedit::Handle(),L"2027");
    Check(settings.year==2027,"valid year immediately previews");
    SendMessageW(inlineedit::Handle(),WM_KEYDOWN,VK_RETURN,0);
    Check(!inlineedit::Active()&&settings.year==2027,"Enter commits header field");
    Check(GoToToday(),"alarm returns to today");
    const auto current=calendar::Today();
    Check(settings.year==current.year&&settings.month==current.month&&settings.followToday,"today restores automatic month tracking");
    Check(BeginHeader(inlineedit::Kind::Year)&&inlineedit::Finish(true)&&settings.followToday,"unchanged year retains automatic tracking");
    Check(BeginHeader(inlineedit::Kind::Month)&&inlineedit::Finish(true)&&settings.followToday,"unchanged month retains automatic tracking");

    Check(WeekdayLabel({2026,11,16})==L"星期一"&&WeekdayLabel({2026,11,22})==L"星期日","header weekday uses today's actual weekday");
    settings.noteFontSize=12; Render();
    {
    Lettering beforeSize;
    const REAL titleBefore=beforeSize.Get(L"2026",TitleText)->GetSize();
    const REAL dateBefore=beforeSize.Get(L"16",DateText)->GetSize();
    const REAL noteBefore=beforeSize.Get(L"记事",NoteText)->GetSize();
    const REAL controlBefore=beforeSize.Get(L"完成",ControlText)->GetSize();
    SendMessageW(mainWindow,WM_COMMAND,FontSizeCommand,0);
    Check(inlineedit::Active()&&editingKind==inlineedit::Kind::FontSize,"font-size menu opens an inline numeric field");
    SetWindowTextW(inlineedit::Handle(),L"18");
    Check(settings.noteFontSize==12,"font-size input waits for confirmation");
    SendMessageW(inlineedit::Handle(),WM_KEYDOWN,VK_RETURN,0);
    Check(!inlineedit::Active()&&settings.noteFontSize==18,"Enter confirms note font size");
    Lettering afterSize;
    Check(afterSize.Get(L"记事",NoteText)->GetSize()>noteBefore,"note font grows after size confirmation");
    Check(afterSize.Get(L"2026",TitleText)->GetSize()==titleBefore&&afterSize.Get(L"16",DateText)->GetSize()==dateBefore,
          "note font setting leaves title and dates unchanged");
    Check(afterSize.Get(L"完成",ControlText)->GetSize()==controlBefore,"note font setting leaves Done unchanged");
    }
    Check(BeginFontSize(),"begin invalid size fixture"); SetWindowTextW(inlineedit::Handle(),L"0");
    Check(inlineedit::Finish(true)&&settings.noteFontSize==18,"too-small size silently retains previous value");
    Check(BeginFontSize(),"begin oversized fixture"); SetWindowTextW(inlineedit::Handle(),L"33");
    Check(inlineedit::Finish(true)&&settings.noteFontSize==18,"too-large size silently retains previous value");
    Check(BeginFontSize(),"begin malformed size fixture"); SetWindowTextW(inlineedit::Handle(),L"12x");
    Check(inlineedit::Finish(true)&&settings.noteFontSize==18,"malformed size silently retains previous value");
    Check(BeginFontSize(),"begin canceled size fixture"); SetWindowTextW(inlineedit::Handle(),L"22");
    Check(inlineedit::Finish(false)&&settings.noteFontSize==18,"Escape retains previous note font size");

    settings.handwritten=true; settings.showHolidays=false; SaveSettings();
    calendar::Store restarted(tempRoot.wstring()); Check(restarted.Initialize(error),"restart storage");
    const auto persisted=restarted.LoadSettings();
    Check(persisted.handwritten&&!persisted.showHolidays&&persisted.noteFontSize==18,"style, holiday and note font choices persist");
    Check(restarted.ReadNote(2026,11,4,saved,error)&&saved==note,"inline note survives restart");

    // Preview notes are in memory only, never in the user's data directory.
    Check(ChangeMonth(2026,11),"prepare preview month");
    for(auto& cell:cells) cell.note.clear();
    cells[8].note=L"整理月度计划"; cells[16].note=L"读书 30 分钟"; cells[23].note=L"完成初稿\r\n备份文件";
    today={2026,11,16}; settings.showHolidays=true; settings.noteFontSize=12;
    const auto output=std::filesystem::absolute(L"tests/output"); std::filesystem::create_directories(output);
    Check(Finish(),"lock preview"); settings.handwritten=false; Preview(output,L"calendar-normal.png");
    Check(transparentFraction>0.80,"normal calendar remains mostly transparent");
    settings.handwritten=true; Preview(output,L"calendar-handdrawn.png");
    Check(transparentFraction>0.80,"handdrawn calendar remains mostly transparent");
    Check(SetMode(Mode::Edit),"preview edit navigation"); Preview(output,L"calendar-edit.png");
    Check(ChangeMonth(2026,2),"preview common holidays"); Preview(output,L"calendar-holidays.png");
    int spring=FindDay({2026,2,17});
    Check(spring>=0&&cells[spring].holiday==L"春节","common lunar festival is populated");
    SendMessageW(mainWindow,WM_COMMAND,Holidays,0);
    Check(!settings.showHolidays&&cells[spring].holiday.empty(),"holiday menu toggle hides labels");
    SendMessageW(mainWindow,WM_COMMAND,Holidays,0);
    Check(settings.showHolidays&&cells[spring].holiday==L"春节","holiday menu toggle restores labels");
    SendMessageW(mainWindow,WM_COMMAND,Handwritten,0);
    Check(!settings.handwritten,"style menu toggles normal lettering");
    SendMessageW(mainWindow,WM_COMMAND,Handwritten,0);
    Check(settings.handwritten,"style menu toggles handwritten lettering");
    Check(Finish(),"finish holiday preview");
    ::SetWindowPos(mainWindow,nullptr,100,100,560,420,SWP_NOACTIVATE|SWP_NOZORDER);
    Check(Width()==560&&Height()==420,"minimum layout has requested dimensions");
    settings.handwritten=true; Check(SetMode(Mode::Edit),"small layout shows navigation"); Preview(output,L"calendar-handdrawn-small.png");
    const auto layout=ComputeLayout();
    Check(layout.today.right<layout.weekday.left&&layout.weekday.right<layout.finish.left,"small header weekday and Done do not overlap");
    for(int i=0;i<42;++i) {
        const auto bounds=layout.NoteBounds(i);
        Check(bounds.right>bounds.left&&bounds.bottom>bounds.top,"minimum layout retains note area");
    }
    Check(unexpectedDialogs==0,"no popup dialogs");
    Check(!IsWindowVisible(mainWindow),"test main window stays hidden");

    inlineedit::Finish(false); closing=true; ready=false; DestroyWindow(mainWindow); DestroyWindow(menuWindow); mainWindow=nullptr; menuWindow=nullptr; store.reset();
    Gdiplus::GdiplusShutdown(token);
    const auto normalized=std::filesystem::weakly_canonical(tempRoot);
    const auto tempParent=std::filesystem::weakly_canonical(std::filesystem::path(temporary));
    Check(normalized.parent_path()==tempParent&&normalized.filename().wstring().find(L"douni-calendar-ui-tests-")==0,"safe fixture cleanup");
    std::filesystem::remove_all(normalized);
    std::printf("PASS: %d native checks; previews in tests/output.\n",checks);
    return 0;
}
