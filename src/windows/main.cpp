#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "storage.h"
#include "dates.h"
#include "inline_editor.h"

using namespace Gdiplus;
namespace {
constexpr wchar_t AppName[]=L"豆泥日历";
constexpr wchar_t MainClass[]=L"DouNiCalendar.Window";
constexpr wchar_t MenuClass[]=L"DouNiCalendar.Menu";
constexpr UINT TrayMessage=WM_APP+1,BringBack=WM_APP+2,DesktopChanged=WM_APP+3,PaintAgain=WM_APP+4,OutsideClick=WM_APP+5;
constexpr UINT_PTR MidnightTimer=1,ShellRetryTimer=2;
enum class Mode { Locked,Edit,Transform };
enum Command { EditMode=101,TransformMode,Handwritten,FontSizeCommand,Holidays,Autostart,Quit };
struct Cell { calendar::Date date={}; std::wstring note,holiday; bool readable=true; };
HINSTANCE instance;
HWND mainWindow=nullptr,menuWindow=nullptr;
HICON normalIcon=nullptr,editIcon=nullptr;
std::unique_ptr<calendar::Store> store;
calendar::Settings settings;
calendar::Date today;
Mode mode=Mode::Locked;
std::array<Cell,42> cells;
std::wstring executablePath,lastError;
NOTIFYICONDATAW tray={};
UINT taskbarCreated=0;
HWINEVENTHOOK foregroundHook=nullptr,minimizeHook=nullptr;
HHOOK outsideClickHook=nullptr;
bool ready=false,closing=false,menuOpen=false,desktopUpdateQueued=false;
float scale=1.f;
inlineedit::Kind editingKind=inlineedit::Kind::Note;
calendar::Date editingDate={};
int originalYear=0,originalMonth=0;
bool originalFollow=true;

int Width() { RECT r={}; GetClientRect(mainWindow,&r); return r.right; }
int Height() { RECT r={}; GetClientRect(mainWindow,&r); return r.bottom; }
void Render();
void LoadMonth();
bool SaveSettings();
void UpdateTray();
bool SetMode(Mode value);
bool Finish();
#ifdef CALENDAR_TEST
void SaveTestFrame(Bitmap& bitmap);
#endif

std::wstring Number(int number,bool twoDigits=false) {
    wchar_t text[16]; swprintf(text,16,twoDigits?L"%02d":L"%d",number); return text;
}
std::wstring WeekdayLabel(calendar::Date date) {
    static const wchar_t* names[]={L"星期一",L"星期二",L"星期三",L"星期四",L"星期五",L"星期六",L"星期日"};
    int first=calendar::MondayOffset(date.year,date.month);
    if (first<0||date.day<1||date.day>calendar::DaysInMonth(date.year,date.month)) return L"";
    return names[(first+date.day-1)%7];
}
bool SameDate(calendar::Date a,calendar::Date b) { return a.year==b.year&&a.month==b.month&&a.day==b.day; }
bool Interactive() { return mode!=Mode::Locked; }
RECT Box(float x,float y,float w,float h) { return {int(x),int(y),int(x+w),int(y+h)}; }
RectF FloatBox(RECT r) { return RectF(float(r.left),float(r.top),float(r.right-r.left),float(r.bottom-r.top)); }
struct Layout {
    float s,pad,top,col,row;
    RECT finish,previous,next,year,month,today,weekday,fontSize;
    RECT Bounds(int index) const { return Box(pad+index%7*col,top+index/7*row,col,row); }
    RECT NoteBounds(int index) const {
        RECT r=Bounds(index);
        return {r.left+int(9*s),r.top+int(37*s),r.right-int(9*s),r.bottom-int(6*s)};
    }
};
Layout ComputeLayout() {
    Layout l={}; l.s=std::min(Width()/980.f,Height()/660.f);
    l.pad=22*l.s; l.top=86*l.s;
    l.col=(Width()-2*l.pad)/7; l.row=(Height()-l.top-22*l.s)/6;
    float x=l.pad,s=l.s;
    l.previous=Box(x,15*s,25*s,31*s);
    l.year=Box(x+30*s,8*s,76*s,43*s);
    l.month=Box(x+136*s,8*s,40*s,43*s);
    l.next=Box(x+212*s,15*s,26*s,31*s);
    l.today=Box(x+250*s,13*s,33*s,35*s);
    l.weekday=Box(x+300*s,22*s,90*s,24*s);
    l.fontSize=Box(Width()-l.pad-115*s,17*s,40*s,29*s);
    l.finish=Box(Width()-l.pad-59*s,16*s,59*s,29*s);
    return l;
}
RECT ToScreen(RECT rect) { MapWindowPoints(mainWindow,nullptr,reinterpret_cast<POINT*>(&rect),2); return rect; }
RECT EditingBounds() {
    Layout l=ComputeLayout();
    if (editingKind==inlineedit::Kind::Year) return ToScreen(l.year);
    if (editingKind==inlineedit::Kind::Month) return ToScreen(l.month);
    if (editingKind==inlineedit::Kind::FontSize) return ToScreen(l.fontSize);
    for (int i=0;i<42;++i) if (SameDate(cells[i].date,editingDate)) return ToScreen(l.NoteBounds(i));
    return {};
}
void RememberError(const std::wstring& message) { lastError=message; UpdateTray(); }
bool SaveSettings() {
    if (!ready||!store) return true;
    RECT r; GetWindowRect(mainWindow,&r);
    settings.x=r.left; settings.y=r.top; settings.width=r.right-r.left; settings.height=r.bottom-r.top;
    std::wstring error;
    if (!store->SaveSettings(settings,error)) { RememberError(error); return false; }
    return true;
}
void LoadMonth() {
    const auto dates=calendar::MonthCells(settings.year,settings.month);
    for (int i=0;i<42;++i) {
        Cell& cell=cells[i]; cell.date=dates[i]; cell.note.clear();
        cell.holiday=settings.showHolidays?calendar::HolidayLabel(cell.date):L"";
        std::wstring error;
        cell.readable=cell.date.year>=1&&cell.date.year<=9999&&store->ReadNote(cell.date.year,cell.date.month,cell.date.day,cell.note,error);
        if (!cell.readable&&!error.empty()) RememberError(error);
    }
}
void ScheduleMidnight() {
    SYSTEMTIME t; GetLocalTime(&t);
    UINT ms=86400000u-(((t.wHour*60u+t.wMinute)*60u+t.wSecond)*1000u+t.wMilliseconds)+200u;
    SetTimer(mainWindow,MidnightTimer,ms,nullptr);
}
void CheckDate() {
    auto fresh=calendar::Today(); bool changed=!SameDate(fresh,today); today=fresh;
    if (settings.followToday&&(settings.year!=today.year||settings.month!=today.month)) {
        if (!inlineedit::Finish(true)) { ScheduleMidnight(); return; }
        settings.year=today.year; settings.month=today.month; LoadMonth(); SaveSettings(); changed=true;
    }
    if (changed) Render(); ScheduleMidnight();
}
bool ChangeMonth(int year,int month,bool follow=false) {
    if (year<1601||year>9999||month<1||month>12) return false;
    if (!inlineedit::Finish(true)) return false;
    settings.year=year; settings.month=month; settings.followToday=follow;
    LoadMonth(); SaveSettings(); Render(); return true;
}
bool StepMonth(int delta) {
    if (!inlineedit::Finish(true)) return false;
    int absolute=(settings.year-1)*12+settings.month-1+delta;
    return ChangeMonth(absolute/12+1,absolute%12+1);
}
bool GoToToday() { today=calendar::Today(); return ChangeMonth(today.year,today.month,true); }

void ClampToScreen(RECT& r) {
    MONITORINFO mi={sizeof(mi)}; GetMonitorInfoW(MonitorFromRect(&r,MONITOR_DEFAULTTONEAREST),&mi);
    int aw=mi.rcWork.right-mi.rcWork.left,ah=mi.rcWork.bottom-mi.rcWork.top;
    int w=std::clamp<int>(r.right-r.left,std::min(560,aw),aw),h=std::clamp<int>(r.bottom-r.top,std::min(420,ah),ah);
    r.left=std::clamp<int>(r.left,mi.rcWork.left,mi.rcWork.right-w);
    r.top=std::clamp<int>(r.top,mi.rcWork.top,mi.rcWork.bottom-h);
    r.right=r.left+w; r.bottom=r.top+h;
}
void KeepOnDesktop() {
    if (closing||Interactive()||!mainWindow) return;
    if (IsIconic(mainWindow)) ShowWindow(mainWindow,SW_SHOWNOACTIVATE);
    HWND desktop=nullptr;
    EnumWindows([](HWND w,LPARAM data)->BOOL {
        if (FindWindowExW(w,nullptr,L"SHELLDLL_DefView",nullptr)) { *reinterpret_cast<HWND*>(data)=w; return FALSE; }
        return TRUE;
    },reinterpret_cast<LPARAM>(&desktop));
    HWND after=HWND_BOTTOM;
    if (desktop) {
        after=GetWindow(desktop,GW_HWNDPREV); if (after==mainWindow) return;
        if (!after||(GetWindowLongPtrW(after,GWL_EXSTYLE)&WS_EX_TOPMOST)) after=HWND_TOP;
    }
    SetWindowPos(mainWindow,after,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
void CALLBACK ForegroundChanged(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD) {
    if (mainWindow&&!desktopUpdateQueued) { desktopUpdateQueued=true; PostMessageW(mainWindow,DesktopChanged,0,0); }
}

enum TextRole { TitleText,DateText,NoteText,WeekText,SmallText,ControlText };
struct Lettering {
    std::array<std::unique_ptr<Font>,6> text,numbers;
    Lettering() {
        const float sizes[]={20,15,float(settings.noteFontSize),11.5f,9.5f,12};
        for (int i=0;i<6;++i) {
            text[i]=std::make_unique<Font>(settings.handwritten?L"KaiTi":L"Microsoft YaHei UI",sizes[i]*scale,FontStyleRegular,UnitPixel);
            if (settings.handwritten) numbers[i]=std::make_unique<Font>(L"Segoe Print",sizes[i]*scale,FontStyleRegular,UnitPixel);
        }
    }
    Font* Get(const std::wstring& string,TextRole role) {
        bool ascii=settings.handwritten;
        for (wchar_t c:string) if (c>127) { ascii=false; break; }
        return ascii?numbers[role].get():text[role].get();
    }
};
void Text(Graphics& g,Lettering& fonts,const std::wstring& string,TextRole role,Color color,RectF r,StringAlignment align=StringAlignmentNear) {
    if (string.empty()) return;
    StringFormat format; format.SetAlignment(align); format.SetTrimming(StringTrimmingEllipsisCharacter);
    if (role==TitleText) {
        format.SetFormatFlags(StringFormatFlagsNoWrap|StringFormatFlagsNoClip);
        format.SetTrimming(StringTrimmingNone); format.SetLineAlignment(StringAlignmentCenter);
    } else if (role!=NoteText) format.SetFormatFlags(StringFormatFlagsLineLimit);
    SolidBrush shadow(Color(BYTE(color.GetA()*.50f),7,17,22)),brush(color);
    RectF behind(r.X+.6f*scale,r.Y+.8f*scale,r.Width,r.Height);
    g.DrawString(string.c_str(),-1,fonts.Get(string,role),behind,&format,&shadow);
    g.DrawString(string.c_str(),-1,fonts.Get(string,role),r,&format,&brush);
}
void Stroke(Graphics& g,Color color,float width,PointF a,PointF b,int seed=0,bool sketch=false) {
    Pen pen(color,width*scale); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
    if (!(settings.handwritten||sketch)) { g.DrawLine(&pen,a,b); return; }
    float dx=b.X-a.X,dy=b.Y-a.Y,length=std::max(1.f,std::sqrt(dx*dx+dy*dy));
    float amount=.65f*scale,nx=-dy/length,ny=dx/length;
    PointF points[5];
    for (int i=0;i<5;++i) {
        float t=i/4.f,offset=i==0||i==4?0:std::sin(float(seed+i*7))*amount;
        points[i]=PointF(a.X+dx*t+nx*offset,a.Y+dy*t+ny*offset);
    }
    g.DrawCurve(&pen,points,5,.3f);
}
void Star(Graphics& g,float cx,float cy) {
    PointF p[11];
    for (int i=0;i<10;++i) {
        float angle=-1.5707963f+i*.62831853f;
        float r=20*scale*(i%2?.58f:1.f)*(i==4?1.06f:i==8?.95f:1.f);
        p[i]=PointF(cx+std::cos(angle)*r,cy+std::sin(angle)*r);
    }
    p[10]=p[0];
    Pen pen(Color(246,255,216,116),1.8f*scale); pen.SetLineJoin(LineJoinRound); g.DrawLines(&pen,p,11);
    for (int i=0;i<11;++i) { p[i].X+=(i%3-1)*.9f*scale; p[i].Y+=.7f*scale; }
    Pen sketch(Color(135,255,233,163),.8f*scale); g.DrawLines(&sketch,p,11);
}
void Arrow(Graphics& g,RECT rect,bool forward) {
    float cx=(rect.left+rect.right)*.5f,cy=(rect.top+rect.bottom)*.5f,direction=forward?1.f:-1.f;
    Color color(218,231,241,240);
    Stroke(g,color,1.2f,PointF(cx-direction*3*scale,cy-5*scale),PointF(cx+direction*3*scale,cy),12);
    Stroke(g,color,1.2f,PointF(cx+direction*3*scale,cy),PointF(cx-direction*3*scale,cy+5*scale),5);
}
void Alarm(Graphics& g,RECT rect) {
    float cx=(rect.left+rect.right)*.5f,cy=(rect.top+rect.bottom)*.5f+1*scale,r=7.3f*scale;
    Color color(225,231,237,220); Pen pen(color,1.1f*scale);
    g.DrawEllipse(&pen,cx-r,cy-r,r*2,r*2);
    Pen ghost(Color(85,235,239,219),.7f*scale); g.DrawEllipse(&ghost,cx-r-.4f*scale,cy-r+.7f*scale,2*r+.6f*scale,2*r-.5f*scale);
    Stroke(g,color,1.1f,PointF(cx,cy),PointF(cx,cy-4*scale),3,true);
    Stroke(g,color,1.1f,PointF(cx,cy),PointF(cx+3*scale,cy+2*scale),4,true);
    Stroke(g,color,1.4f,PointF(cx-r-2*scale,cy-r+1*scale),PointF(cx-r+2*scale,cy-r-3*scale),6,true);
    Stroke(g,color,1.4f,PointF(cx+r-2*scale,cy-r-3*scale),PointF(cx+r+2*scale,cy-r+1*scale),8,true);
    Stroke(g,color,1.1f,PointF(cx-r*.65f,cy+r*.8f),PointF(cx-r*.95f,cy+r+3*scale),4,true);
    Stroke(g,color,1.1f,PointF(cx+r*.65f,cy+r*.8f),PointF(cx+r*.95f,cy+r+3*scale),6,true);
}
void Render() {
    if (!mainWindow||!ready) return;
    int w=Width(),h=Height(); if (w<=0||h<=0) return;
    Layout l=ComputeLayout(); scale=l.s;
    BITMAPINFO info={}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=w; info.bmiHeader.biHeight=-h; info.bmiHeader.biPlanes=1;
    info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr; HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
    HBITMAP dib=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if (!dib||!dc) { if (dib) DeleteObject(dib); if (dc) DeleteDC(dc); ReleaseDC(nullptr,screen); return; }
    HGDIOBJ old=SelectObject(dc,dib);
    {
        Bitmap bitmap(w,h,w*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
        Graphics g(&bitmap); g.Clear(Interactive()?Color(30,13,28,37):Color(0,0,0,0));
        g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        Lettering fonts; Color white(235,244,248,249),title(205,244,248,249),muted(166,218,230,234),gold(238,255,219,143);
        bool active=inlineedit::Active();
        if (!(active&&editingKind==inlineedit::Kind::Year)) Text(g,fonts,Number(settings.year),TitleText,title,FloatBox(l.year),StringAlignmentCenter);
        Text(g,fonts,L"年",TitleText,title,RectF(l.pad+108*scale,8*scale,24*scale,43*scale));
        if (!(active&&editingKind==inlineedit::Kind::Month)) Text(g,fonts,Number(settings.month),TitleText,title,FloatBox(l.month),StringAlignmentCenter);
        Text(g,fonts,L"月",TitleText,title,RectF(l.pad+180*scale,8*scale,24*scale,43*scale));
        Text(g,fonts,WeekdayLabel(today),WeekText,Color(156,224,234,236),FloatBox(l.weekday));
        if (Interactive()) {
            Arrow(g,l.previous,false); Arrow(g,l.next,true); Alarm(g,l.today);
            Color button=lastError.empty()?Color(76,218,233,236):Color(200,240,143,123);
            Stroke(g,button,.7f,PointF(float(l.finish.left),float(l.finish.bottom)),PointF(float(l.finish.right),float(l.finish.bottom)),7);
            Text(g,fonts,L"完成",ControlText,white,RectF(float(l.finish.left),21*scale,float(l.finish.right-l.finish.left),25*scale),StringAlignmentCenter);
            if (active&&editingKind==inlineedit::Kind::FontSize)
                Text(g,fonts,L"字号",SmallText,muted,RectF(l.fontSize.left-31*scale,24*scale,29*scale,20*scale));
        }
        const wchar_t* week[]={L"周一",L"周二",L"周三",L"周四",L"周五",L"周六",L"周日"};
        for (int c=0;c<7;++c) Text(g,fonts,week[c],WeekText,c>=5?Color(175,244,217,164):muted,RectF(l.pad+c*l.col+9*scale,62*scale,l.col,23*scale));
        Color lines(58,226,240,242);
        for (int r=0;r<=6;++r) Stroke(g,lines,.7f,PointF(l.pad,l.top+r*l.row),PointF(w-l.pad,l.top+r*l.row),r+2);
        for (int c=1;c<7;++c) Stroke(g,lines,.7f,PointF(l.pad+c*l.col,l.top),PointF(l.pad+c*l.col,l.top+6*l.row),c+13);
        for (int i=0;i<42;++i) {
            const Cell& cell=cells[i]; RECT bounds=l.Bounds(i); float x=float(bounds.left),y=float(bounds.top);
            bool current=cell.date.year==settings.year&&cell.date.month==settings.month,isToday=SameDate(cell.date,today);
            if (isToday) Star(g,x+23*scale,y+20*scale);
            Text(g,fonts,Number(cell.date.day,true),DateText,isToday?gold:current?white:Color(112,232,240,242),RectF(x+8*scale,y+9*scale,31*scale,26*scale),StringAlignmentCenter);
            Text(g,fonts,cell.holiday,SmallText,Color(current?116:76,236,227,202),RectF(x+44*scale,y+14*scale,l.col-51*scale,19*scale));
            if (!(active&&editingKind==inlineedit::Kind::Note&&SameDate(cell.date,editingDate))) Text(g,fonts,cell.note,NoteText,current?white:Color(116,232,240,242),FloatBox(l.NoteBounds(i)));
        }
        if (mode==Mode::Transform) for (int i=0;i<3;++i) Stroke(g,muted,1.f,PointF(w-(8.f+i*5)*scale,h-5*scale),PointF(w-5*scale,h-(8.f+i*5)*scale),i+5);
#ifdef CALENDAR_TEST
        g.Flush(FlushIntentionSync); SaveTestFrame(bitmap);
#endif
    }
    RECT rect; GetWindowRect(mainWindow,&rect); POINT dest={rect.left,rect.top},src={0,0}; SIZE size={w,h};
    BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    UpdateLayeredWindow(mainWindow,screen,&dest,&size,dc,&src,0,&blend,ULW_ALPHA);
    SelectObject(dc,old); DeleteObject(dib); DeleteDC(dc); ReleaseDC(nullptr,screen);
    if (inlineedit::Active()) inlineedit::Move(EditingBounds());
}

bool ParseHeader(inlineedit::Kind kind,const std::wstring& value,int& number) {
    if (value.empty()||value.size()>(kind==inlineedit::Kind::Year?4u:2u)) return false;
    number=0; for (wchar_t c:value) { if (c<L'0'||c>L'9') return false; number=number*10+c-L'0'; }
    if (kind==inlineedit::Kind::Year) return number>=1601&&number<=9999;
    if (kind==inlineedit::Kind::FontSize) return number>=8&&number<=32;
    return kind==inlineedit::Kind::Month&&number>=1&&number<=12;
}
void PreviewHeader(inlineedit::Kind kind,const std::wstring& value,void*) {
    int n; bool valid=ParseHeader(kind,value,n);
    int year=valid&&kind==inlineedit::Kind::Year?n:originalYear;
    int month=valid&&kind==inlineedit::Kind::Month?n:originalMonth;
    if (year!=settings.year||month!=settings.month) {
        settings.year=year; settings.month=month;
        settings.followToday=year==originalYear&&month==originalMonth?originalFollow:false;
        LoadMonth(); Render();
    }
}
bool InlineCommit(inlineedit::Kind kind,const std::wstring& value,bool accept,void*) {
    if (kind==inlineedit::Kind::Note) {
        if (accept) {
            if (!value.empty()) {
                int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
                if (bytes<=0||bytes>1024*1024) { PostMessageW(mainWindow,PaintAgain,0,0); return true; }
            }
            std::wstring previous,error;
            if (!store->ReadNote(editingDate.year,editingDate.month,editingDate.day,previous,error)) { RememberError(error); return false; }
            if (previous!=value&&!store->WriteNote(editingDate.year,editingDate.month,editingDate.day,value,error)) { RememberError(error); PostMessageW(mainWindow,PaintAgain,0,0); return false; }
            for (Cell& cell:cells) if (SameDate(cell.date,editingDate)) cell.note=value;
            lastError.clear(); UpdateTray();
        }
    } else if (kind==inlineedit::Kind::FontSize) {
        int number;
        if (accept&&ParseHeader(kind,value,number)) { settings.noteFontSize=number; SaveSettings(); }
    } else {
        int number;
        if (accept&&ParseHeader(kind,value,number)) {
            settings.year=kind==inlineedit::Kind::Year?number:originalYear;
            settings.month=kind==inlineedit::Kind::Month?number:originalMonth;
            settings.followToday=settings.year==originalYear&&settings.month==originalMonth?originalFollow:false;
        } else { settings.year=originalYear; settings.month=originalMonth; settings.followToday=originalFollow; }
        LoadMonth(); SaveSettings();
    }
    PostMessageW(mainWindow,PaintAgain,0,0); return true;
}
bool BeginNote(int index) {
    if (mode!=Mode::Edit||index<0||index>=42||!inlineedit::Finish(true)||!cells[index].readable) return false;
    editingKind=inlineedit::Kind::Note; editingDate=cells[index].date;
    RECT r=ToScreen(ComputeLayout().NoteBounds(index));
    bool ok=inlineedit::Begin(mainWindow,r,editingKind,cells[index].note,std::max(8,int(settings.noteFontSize*scale)),settings.handwritten,InlineCommit,nullptr,nullptr);
    Render(); return ok;
}
bool BeginHeader(inlineedit::Kind kind) {
    if (!Interactive()||(kind!=inlineedit::Kind::Year&&kind!=inlineedit::Kind::Month)||!inlineedit::Finish(true)) return false;
    editingKind=kind; originalYear=settings.year; originalMonth=settings.month; originalFollow=settings.followToday;
    int value=kind==inlineedit::Kind::Year?settings.year:settings.month;
    bool ok=inlineedit::Begin(mainWindow,EditingBounds(),kind,Number(value),std::max(12,int(20*scale)),settings.handwritten,InlineCommit,PreviewHeader,nullptr);
    Render(); return ok;
}
bool BeginFontSize() {
    if (!SetMode(Mode::Edit)) return false;
    editingKind=inlineedit::Kind::FontSize;
    bool ok=inlineedit::Begin(mainWindow,EditingBounds(),editingKind,Number(settings.noteFontSize),
        std::max(10,int(14*scale)),settings.handwritten,InlineCommit,nullptr,nullptr);
    Render(); return ok;
}
HICON MakeTrayIcon(Color color) {
    Bitmap bitmap(32,32,PixelFormat32bppARGB); Graphics g(&bitmap); g.Clear(Color(0,0,0,0)); g.SetSmoothingMode(SmoothingModeAntiAlias);
    SolidBrush body(Color(245,color.GetR(),color.GetG(),color.GetB())); g.FillRectangle(&body,5,7,22,21);
    Pen ring(Color(255,239,248,246),2); g.DrawLine(&ring,11,4,11,11); g.DrawLine(&ring,21,4,21,11);
    SolidBrush ink(Color(255,24,47,54)); g.FillRectangle(&ink,8,14,16,2);
    for (int y=19;y<=24;y+=5) for (int x=10;x<=22;x+=6) g.FillEllipse(&ink,x,y,2,2);
    HICON icon=nullptr; bitmap.GetHICON(&icon); return icon;
}
void UpdateTray() {
    if (!tray.hWnd) return;
    tray.uFlags=NIF_ICON|NIF_TIP|NIF_SHOWTIP; tray.hIcon=Interactive()?editIcon:normalIcon;
    const wchar_t* tip=!lastError.empty()?L"豆泥日历 · 保存失败":mode==Mode::Edit?L"豆泥日历 · 编辑模式":AppName;
    wcsncpy(tray.szTip,tip,ARRAYSIZE(tray.szTip)-1); Shell_NotifyIconW(NIM_MODIFY,&tray);
}
void AddTray() {
    tray={}; tray.cbSize=sizeof(tray); tray.hWnd=mainWindow; tray.uID=1;
    tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP; tray.uCallbackMessage=TrayMessage;
    tray.hIcon=Interactive()?editIcon:normalIcon; wcscpy(tray.szTip,AppName);
    if (!Shell_NotifyIconW(NIM_ADD,&tray)) { SetTimer(mainWindow,ShellRetryTimer,3000,nullptr); return; }
    KillTimer(mainWindow,ShellRetryTimer); tray.uVersion=NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION,&tray); UpdateTray();
}
LRESULT CALLBACK OutsideMouse(int code,WPARAM wp,LPARAM lp) {
    if (code>=0&&!menuOpen&&(wp==WM_LBUTTONDOWN||wp==WM_RBUTTONDOWN||wp==WM_MBUTTONDOWN)) {
        POINT p=reinterpret_cast<MSLLHOOKSTRUCT*>(lp)->pt;
        HWND owner=GetAncestor(WindowFromPoint(p),GA_ROOTOWNER);
        NOTIFYICONIDENTIFIER icon={sizeof(icon)}; icon.hWnd=mainWindow; icon.uID=tray.uID;
        RECT bounds={}; Shell_NotifyIconGetRect(&icon,&bounds);
        if (owner!=mainWindow&&owner!=menuWindow&&!PtInRect(&bounds,p))
            PostMessageW(mainWindow,OutsideClick,0,0);
    }
    return CallNextHookEx(outsideClickHook,code,wp,lp);
}
bool SetMode(Mode value) {
    if (!inlineedit::Finish(true)) return false;
    mode=value;
    if (Interactive()&&!outsideClickHook) outsideClickHook=SetWindowsHookExW(WH_MOUSE_LL,OutsideMouse,instance,0);
    else if (!Interactive()&&outsideClickHook) { UnhookWindowsHookEx(outsideClickHook); outsideClickHook=nullptr; }
    LONG_PTR style=GetWindowLongPtrW(mainWindow,GWL_STYLE); style=mode==Mode::Transform?style|WS_THICKFRAME:style&~WS_THICKFRAME;
    SetWindowLongPtrW(mainWindow,GWL_STYLE,style);
    LONG_PTR ex=GetWindowLongPtrW(mainWindow,GWL_EXSTYLE); ex=Interactive()?ex&~(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE):ex|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE;
    SetWindowLongPtrW(mainWindow,GWL_EXSTYLE,ex);
    SetWindowPos(mainWindow,Interactive()?HWND_TOP:HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    if (Interactive()) SetForegroundWindow(mainWindow); else KeepOnDesktop();
    UpdateTray(); Render(); return true;
}
bool Finish() { if (!inlineedit::Finish(true)) return false; SaveSettings(); return SetMode(Mode::Locked); }
void ToggleEdit() { if (mode==Mode::Edit) Finish(); else SetMode(Mode::Edit); }
std::wstring AutostartCommand() { return L"\""+executablePath+L"\""; }
bool SameExecutableCommand(std::wstring command,const std::wstring& path) {
    if (command.size()>=2&&command.front()==L'"'&&command.back()==L'"')
        command=command.substr(1,command.size()-2);
    std::replace(command.begin(),command.end(),L'/',L'\\');
    return CompareStringOrdinal(command.c_str(),int(command.size()),path.c_str(),int(path.size()),TRUE)==CSTR_EQUAL;
}
bool IsLegacyAutostartCommand(const std::wstring& command) {
    const size_t slash=executablePath.find_last_of(L"\\/");
    if (slash==std::wstring::npos) return false;
    const std::wstring directory=executablePath.substr(0,slash);
    if (SameExecutableCommand(command,directory+L"\\DesktopCalendar.exe")) return true;
    const size_t parentSlash=directory.find_last_of(L"\\/");
    if (parentSlash==std::wstring::npos) return false;
    const std::wstring folder=directory.substr(parentSlash+1);
    if (folder!=L"豆泥日历"&&folder!=L"DouNi Calendar") return false;
    return SameExecutableCommand(command,directory.substr(0,parentSlash)+L"\\桌面日历\\DesktopCalendar.exe");
}
bool HasMatchingLegacyAutostart(HKEY key) {
    wchar_t value[32768]={}; DWORD size=sizeof(value),type=0;
    LONG result=RegQueryValueExW(key,L"DesktopNoteCalendar",nullptr,&type,reinterpret_cast<BYTE*>(value),&size);
    value[ARRAYSIZE(value)-1]=0;
    return result==ERROR_SUCCESS&&type==REG_SZ&&IsLegacyAutostartCommand(value);
}
bool IsAutostart() {
    HKEY key; if (RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS) return false;
    wchar_t value[32768]={}; DWORD size=sizeof(value),type=0;
    LONG result=RegQueryValueExW(key,L"DouNiCalendar",nullptr,&type,reinterpret_cast<BYTE*>(value),&size); RegCloseKey(key);
    value[ARRAYSIZE(value)-1]=0; return result==ERROR_SUCCESS&&type==REG_SZ&&AutostartCommand()==value;
}
void ToggleAutostart() {
    bool enabled=IsAutostart(); HKEY key;
    LONG result=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE|KEY_QUERY_VALUE,nullptr,&key,nullptr);
    if (result==ERROR_SUCCESS) {
        if (enabled) result=RegDeleteValueW(key,L"DouNiCalendar");
        else {
            auto cmd=AutostartCommand(); result=RegSetValueExW(key,L"DouNiCalendar",0,REG_SZ,reinterpret_cast<const BYTE*>(cmd.c_str()),DWORD((cmd.size()+1)*sizeof(wchar_t)));
            if (result==ERROR_SUCCESS&&HasMatchingLegacyAutostart(key))
                result=RegDeleteValueW(key,L"DesktopNoteCalendar");
        }
        RegCloseKey(key);
    }
    if (result!=ERROR_SUCCESS) RememberError(L"无法设置开机启动");
}
void MigrateLegacyAutostart() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,
        KEY_QUERY_VALUE|KEY_SET_VALUE,&key)!=ERROR_SUCCESS) return;
    if (!HasMatchingLegacyAutostart(key)) { RegCloseKey(key); return; }
    wchar_t value[32768]={}; DWORD size=sizeof(value),type=0;
    LONG result=RegQueryValueExW(key,L"DouNiCalendar",nullptr,&type,reinterpret_cast<BYTE*>(value),&size);
    value[ARRAYSIZE(value)-1]=0;
    if (result!=ERROR_FILE_NOT_FOUND&&
        !(result==ERROR_SUCCESS&&type==REG_SZ&&SameExecutableCommand(value,executablePath))) {
        RegCloseKey(key); return;
    }
    if (result==ERROR_FILE_NOT_FOUND) {
        const auto command=AutostartCommand();
        result=RegSetValueExW(key,L"DouNiCalendar",0,REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),DWORD((command.size()+1)*sizeof(wchar_t)));
    }
    if (result==ERROR_SUCCESS&&HasMatchingLegacyAutostart(key))
        result=RegDeleteValueW(key,L"DesktopNoteCalendar");
    RegCloseKey(key);
    if (result!=ERROR_SUCCESS) lastError=L"无法迁移开机启动设置";
}
HMENU CreateTrayMenu() {
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING|(mode==Mode::Edit?MF_CHECKED:0),EditMode,L"编辑模式");
    AppendMenuW(menu,MF_STRING|(mode==Mode::Transform?MF_CHECKED:0),TransformMode,L"移动与缩放");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING|(settings.handwritten?MF_CHECKED:0),Handwritten,L"手绘风格");
    AppendMenuW(menu,MF_STRING,FontSizeCommand,L"字体大小");
    AppendMenuW(menu,MF_STRING|(settings.showHolidays?MF_CHECKED:0),Holidays,L"显示节假日");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING|(IsAutostart()?MF_CHECKED:0),Autostart,L"开机启动");
    AppendMenuW(menu,MF_STRING,Quit,L"退出"); return menu;
}
void ShowMenu() {
    if (menuOpen||!inlineedit::Finish(true)) return;
    CheckDate(); menuOpen=true; HMENU menu=CreateTrayMenu(); POINT p; GetCursorPos(&p);
    // Focus belongs to this invisible menu owner, never to the calendar.
    SetWindowPos(menuWindow,HWND_TOP,p.x,p.y,1,1,SWP_NOACTIVATE|SWP_SHOWWINDOW); SetForegroundWindow(menuWindow);
    UINT command=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,menuWindow,nullptr);
    PostMessageW(menuWindow,WM_NULL,0,0); ShowWindow(menuWindow,SW_HIDE); DestroyMenu(menu); menuOpen=false;
    if (command) PostMessageW(mainWindow,WM_COMMAND,command,0);
}
void Click(POINT p) {
    if (!Interactive()) return;
    Layout l=ComputeLayout();
    if (PtInRect(&l.finish,p)) { Finish(); return; }
    if (inlineedit::Active()) {
        POINT screen=p; ClientToScreen(mainWindow,&screen); RECT bounds=EditingBounds();
        if (PtInRect(&bounds,screen)) {
            HWND edit=inlineedit::Handle(); SetForegroundWindow(GetParent(edit)); SetFocus(edit);
            ScreenToClient(edit,&screen);
            SendMessageW(edit,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(screen.x,screen.y));
            SendMessageW(edit,WM_LBUTTONUP,0,MAKELPARAM(screen.x,screen.y));
            return;
        }
    }
    if (PtInRect(&l.previous,p)) { StepMonth(-1); return; }
    if (PtInRect(&l.next,p)) { StepMonth(1); return; }
    if (PtInRect(&l.today,p)) { GoToToday(); return; }
    if (PtInRect(&l.year,p)) { BeginHeader(inlineedit::Kind::Year); return; }
    if (PtInRect(&l.month,p)) { BeginHeader(inlineedit::Kind::Month); return; }
    if (mode==Mode::Edit&&p.x>=l.pad&&p.x<Width()-l.pad&&p.y>=l.top&&p.y<l.top+6*l.row)
        BeginNote(int((p.y-l.top)/l.row)*7+int((p.x-l.pad)/l.col));
    else inlineedit::Finish(true);
}
LRESULT CALLBACK MainProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if (taskbarCreated&&message==taskbarCreated) { AddTray(); KeepOnDesktop(); return 0; }
    switch (message) {
    case WM_NCCALCSIZE: return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT paint; BeginPaint(hwnd,&paint); EndPaint(hwnd,&paint); return 0; }
    case PaintAgain: Render(); return 0;
    case OutsideClick: if (Interactive()) Finish(); return 0;
    case TrayMessage:
        if (LOWORD(lp)==NIN_SELECT||LOWORD(lp)==NIN_KEYSELECT) ToggleEdit(); else if (LOWORD(lp)==WM_CONTEXTMENU) ShowMenu(); return 0;
    case BringBack: SetMode(Mode::Edit); return 0;
    case DesktopChanged: desktopUpdateQueued=false; if (!menuOpen) KeepOnDesktop(); return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case EditMode: ToggleEdit(); break;
        case TransformMode: if (mode==Mode::Transform) Finish(); else SetMode(Mode::Transform); break;
        case Handwritten: if (inlineedit::Finish(true)) { settings.handwritten=!settings.handwritten; SaveSettings(); Render(); } break;
        case FontSizeCommand: BeginFontSize(); break;
        case Holidays: if (inlineedit::Finish(true)) { settings.showHolidays=!settings.showHolidays; LoadMonth(); SaveSettings(); Render(); } break;
        case Autostart: ToggleAutostart(); break;
        case Quit: SendMessageW(hwnd,WM_CLOSE,0,0); break;
        }
        return 0;
    case WM_NCHITTEST: {
        if (!Interactive()) return HTTRANSPARENT;
        POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; ScreenToClient(hwnd,&p); Layout l=ComputeLayout();
        if (mode==Mode::Transform) {
            int edge=std::max(9,int(11*scale)); bool left=p.x<edge,right=p.x>=Width()-edge,top=p.y<edge,bottom=p.y>=Height()-edge;
            if (p.x>=Width()-28*scale&&p.y>=Height()-28*scale) return HTBOTTOMRIGHT;
            if (top&&left) return HTTOPLEFT; if (top&&right) return HTTOPRIGHT;
            if (bottom&&left) return HTBOTTOMLEFT; if (bottom&&right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT; if (right) return HTRIGHT; if (top) return HTTOP; if (bottom) return HTBOTTOM;
            if (PtInRect(&l.finish,p)||PtInRect(&l.previous,p)||PtInRect(&l.next,p)||PtInRect(&l.today,p)||PtInRect(&l.year,p)||PtInRect(&l.month,p)) return HTCLIENT;
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN: Click(POINT{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}); return 0;
    case WM_KEYDOWN: if (wp==VK_ESCAPE) Finish(); return 0;
    case WM_GETMINMAXINFO: {
        auto p=reinterpret_cast<MINMAXINFO*>(lp); p->ptMinTrackSize={560,420};
        MONITORINFO mi={sizeof(mi)}; GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&mi);
        p->ptMaxTrackSize={mi.rcWork.right-mi.rcWork.left,mi.rcWork.bottom-mi.rcWork.top}; return 0;
    }
    case WM_ENTERSIZEMOVE: inlineedit::Finish(true); return 0;
    case WM_EXITSIZEMOVE: {
        RECT r; GetWindowRect(hwnd,&r); ClampToScreen(r);
        SetWindowPos(hwnd,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE); SaveSettings(); Render(); return 0;
    }
    case WM_SIZE: if (wp!=SIZE_MINIMIZED&&ready) Render(); return 0;
    case WM_MOVE: if (ready&&inlineedit::Active()) inlineedit::Move(EditingBounds()); return 0;
    case WM_DPICHANGED: case WM_DISPLAYCHANGE: {
        inlineedit::Finish(true); RECT r; if (message==WM_DPICHANGED) r=*reinterpret_cast<RECT*>(lp); else GetWindowRect(hwnd,&r);
        ClampToScreen(r); SetWindowPos(hwnd,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE); SaveSettings(); Render(); return 0;
    }
    case WM_TIMECHANGE: CheckDate(); return 0;
    case WM_POWERBROADCAST: if (wp==PBT_APMRESUMEAUTOMATIC||wp==PBT_APMRESUMESUSPEND) CheckDate(); return TRUE;
    case WM_TIMER: if (wp==MidnightTimer) CheckDate(); else if (wp==ShellRetryTimer) AddTray(); return 0;
    case WM_QUERYENDSESSION: if (!inlineedit::Finish(true)) return FALSE; SaveSettings(); return TRUE;
    case WM_ENDSESSION: if (wp) { closing=true; DestroyWindow(hwnd); } return 0;
    case WM_CLOSE: if (!inlineedit::Finish(true)) return 0; closing=true; SaveSettings(); DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        if (outsideClickHook) UnhookWindowsHookEx(outsideClickHook);
        inlineedit::Shutdown(); KillTimer(hwnd,MidnightTimer); KillTimer(hwnd,ShellRetryTimer);
        if (foregroundHook) UnhookWinEvent(foregroundHook); if (minimizeHook) UnhookWinEvent(minimizeHook);
        Shell_NotifyIconW(NIM_DELETE,&tray); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
}

int WINAPI wWinMain(HINSTANCE hInstance,HINSTANCE,PWSTR,int) {
    instance=hInstance;
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\DouNiCalendar.Portable.v1");
    if (mutex&&GetLastError()==ERROR_ALREADY_EXISTS) {
        HWND existing=FindWindowW(MainClass,nullptr); if (existing) PostMessageW(existing,BringBack,0,0); CloseHandle(mutex); return 0;
    }
    // Keep the old instance guard during upgrades so an old executable cannot
    // open a second calendar against the same portable data directory.
    HANDLE legacyMutex=CreateMutexW(nullptr,FALSE,L"Local\\DesktopNoteCalendar.Portable.v1");
    if (legacyMutex&&GetLastError()==ERROR_ALREADY_EXISTS) {
        HWND existing=FindWindowW(L"DesktopNoteCalendar.Window",nullptr);
        if (existing) PostMessageW(existing,BringBack,0,0);
        CloseHandle(legacyMutex); if (mutex) CloseHandle(mutex); return 0;
    }
    std::vector<wchar_t> path(32768); DWORD length=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size())); if (!length||length>=path.size()) return 1;
    executablePath.assign(path.data(),length);
    MigrateLegacyAutostart();
    store=std::make_unique<calendar::Store>(executablePath.substr(0,executablePath.find_last_of(L"\\/")));
    std::wstring error; if (!store->Initialize(error)) lastError=error;
    settings=store->LoadSettings(); if (!store->lastError.empty()) lastError=store->lastError;
    today=calendar::Today();
    if (settings.followToday||settings.year<1601||settings.year>9999||settings.month<1||settings.month>12) {
        settings.year=today.year; settings.month=today.month; settings.followToday=true;
    }
    GdiplusStartupInput startup; ULONG_PTR token; if (GdiplusStartup(&token,&startup,nullptr)!=Ok) return 1;
    normalIcon=MakeTrayIcon(Color(255,129,207,195)); editIcon=MakeTrayIcon(Color(255,247,206,116));
    WNDCLASSEXW wc={sizeof(wc)}; wc.hInstance=instance; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    wc.hIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,0,0,LR_DEFAULTSIZE));
    wc.lpfnWndProc=MainProc; wc.lpszClassName=MainClass; RegisterClassExW(&wc);
    wc.lpfnWndProc=DefWindowProcW; wc.lpszClassName=MenuClass; RegisterClassExW(&wc);
    RECT r={settings.x,settings.y,settings.x+settings.width,settings.y+settings.height};
    if (settings.x==-1&&settings.y==-1) {
        MONITORINFO mi={sizeof(mi)}; GetMonitorInfoW(MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY),&mi);
        int aw=mi.rcWork.right-mi.rcWork.left,ah=mi.rcWork.bottom-mi.rcWork.top;
        int w=std::min(MulDiv(980,GetDpiForSystem(),96),int(aw*.7)),h=std::min(MulDiv(660,GetDpiForSystem(),96),int(ah*.78));
        r={mi.rcWork.right-w-35,mi.rcWork.top+40,mi.rcWork.right-35,mi.rcWork.top+40+h};
    }
    ClampToScreen(r);
    mainWindow=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT,MainClass,AppName,WS_POPUP,r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,instance,nullptr);
    menuWindow=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED,MenuClass,L"",WS_POPUP,0,0,1,1,nullptr,nullptr,instance,nullptr);
    if (!mainWindow||!menuWindow) return 1; SetLayeredWindowAttributes(menuWindow,0,0,LWA_ALPHA);
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    ready=true; LoadMonth(); Render(); AddTray(); ShowWindow(mainWindow,SW_SHOWNOACTIVATE); KeepOnDesktop(); SaveSettings(); ScheduleMidnight();
    foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,ForegroundChanged,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    minimizeHook=SetWinEventHook(EVENT_SYSTEM_MINIMIZEEND,EVENT_SYSTEM_MINIMIZEEND,nullptr,ForegroundChanged,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    MSG message; while (GetMessageW(&message,nullptr,0,0)>0) { TranslateMessage(&message); DispatchMessageW(&message); }
    DestroyWindow(menuWindow); DestroyIcon(normalIcon); DestroyIcon(editIcon); if (wc.hIcon) DestroyIcon(wc.hIcon);
    GdiplusShutdown(token); if (legacyMutex) CloseHandle(legacyMutex); if (mutex) CloseHandle(mutex); return 0;
}
