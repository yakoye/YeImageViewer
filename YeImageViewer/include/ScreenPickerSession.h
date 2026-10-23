#pragma once
#include "PickerPreferences.h"
#include "CurrentColor.h"
#include "ColorLibrary.h"
#include "PsdDockStyle.h"
#include "jarkUtils.h"
#include "StartupTrace.h"
#include "PickerInputPolicy.h"
#include "PickerHandoffWindow.h"
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <functional>
#include <condition_variable>
#pragma comment(lib, "shcore.lib")

// 快照取色。覆盖层只读截图；按键优先钩子只在前台取色会话期间存在，不常驻、不记键。
class ScreenPickerSession {
public:
    static constexpr UINT RESULT_MESSAGE = WM_APP + 67;
    static constexpr UINT QUERY_MESSAGE = WM_APP + 70;
    enum Result { Cancelled, Copied, CaptureFailed, StartFailed, Selected };
private:
    struct Monitor {
        PickerCore::Rect bounds;
        HWND window = nullptr;
        HDC dc = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ previous = nullptr;
        HDC canvas = nullptr;
        HBITMAP canvasBitmap = nullptr;
        HGDIOBJ previousCanvas = nullptr;
        HFONT primaryFont = nullptr, helpFont = nullptr;
        int fontDpi = 0;
        uint8_t* pixels = nullptr;
        int dpi = 96;
        ~Monitor() {
            if (window) DestroyWindow(window);
            if (dc && previous) SelectObject(dc, previous);
            if (bitmap && !DeleteObject(bitmap)) StartupTrace::mark("picker.deleteSnapshotFailed");
            if (dc && !DeleteDC(dc)) StartupTrace::mark("picker.deleteSnapshotDcFailed");
            if (canvas && previousCanvas) SelectObject(canvas, previousCanvas);
            if (canvasBitmap && !DeleteObject(canvasBitmap)) StartupTrace::mark("picker.deleteCanvasFailed");
            if (canvas && !DeleteDC(canvas)) StartupTrace::mark("picker.deleteCanvasDcFailed");
            if(primaryFont)DeleteObject(primaryFont);
            if(helpFont)DeleteObject(helpFont);
        }
        void prepareFonts() {
            if(fontDpi==dpi&&primaryFont&&helpFont)return;
            HFONT primary=CreateFontW(-MulDiv(PickerCore::HUD_PRIMARY_FONT,dpi,96),0,0,0,PickerCore::HUD_PRIMARY_WEIGHT,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
            HFONT help=CreateFontW(-MulDiv(PickerCore::HUD_HELP_FONT,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
            if(primary&&help){if(primaryFont)DeleteObject(primaryFont);if(helpFont)DeleteObject(helpFont);primaryFont=primary;helpFont=help;fontDpi=dpi;}
            else{if(primary)DeleteObject(primary);if(help)DeleteObject(help);}
        }
    };
    inline static std::mutex lifecycle;
    inline static std::thread worker;
    struct Request { HWND owner; std::filesystem::path path; bool dark; bool returnToPanel; };
    inline static std::mutex requestMutex;
    inline static std::condition_variable requested;
    inline static std::optional<Request> pendingRequest;
    inline static std::atomic<bool> running{false}, stopRequested{false};
    inline static std::atomic<DWORD> workerId{0};
    HWND owner = nullptr, previousForeground = nullptr;
    std::filesystem::path settingsPath;
    PickerCore::Settings settings;
    std::vector<std::unique_ptr<Monitor>> monitors;
    POINT point{}, lastMouse{};
    int selected = -1, lastHudMonitor = -1;
    PickerCore::Rect lastHud{};
    bool lastHudTextFits = false;
    bool lastHudHelpLeft=false,lastCoordinateBaselinesAligned=true;
    int lastHudHelpGap=0;
    bool dark = true, valid = false, closing = false, recapturing = false, menuOpen = false;
    bool dirty = false, returnToPanel = false;
    Result result = Cancelled;
    std::wstring status;
    uint32_t accepted = 0;
    std::string confirmationKey=ColorLibrary::eventKey();
    inline static thread_local ScreenPickerSession* inputSession=nullptr;
    static constexpr UINT INPUT_MESSAGE=WM_APP+69;
    HHOOK keyboardHook=nullptr;
    PickerInput::State keyState;
    static LRESULT CALLBACK keyboard(int code,WPARAM message,LPARAM data) {
        auto* self=inputSession;
        if(code==HC_ACTION && self && !self->closing && !self->monitors.empty()) {
            const auto* event=reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
            const HWND foreground=GetForegroundWindow();
            const bool active=std::any_of(self->monitors.begin(),self->monitors.end(),
                [&](const auto& m){return m->window==foreground;});
            const bool system=(event->flags&LLKHF_ALTDOWN)||GetAsyncKeyState(VK_LWIN)<0||GetAsyncKeyState(VK_RWIN)<0;
            const auto route=self->keyState.route(event->vkCode,message==WM_KEYDOWN||message==WM_SYSKEYDOWN,
                active,self->menuOpen,system);
            if(route.consume) {
                if(route.dispatch)PostMessageW(self->monitors.front()->window,INPUT_MESSAGE,route.key,
                    (route.repeated?1:0)|(GetAsyncKeyState(VK_CONTROL)<0?2:0));
                return 1;
            }
        }
        return CallNextHookEx(nullptr,code,message,data);
    }
    void beginInput() {
        // 仅禁用本会话窗口的IME合成，绝不加载或切换系统键盘布局。
        for(const auto& m:monitors)ImmAssociateContext(m->window,nullptr);
        inputSession=this;keyboardHook=SetWindowsHookExW(WH_KEYBOARD_LL,keyboard,GetModuleHandleW(nullptr),0);
        if(!keyboardHook)status=trW(L"快捷键优先不可用 · 可Esc退出重试",L"Priority keys unavailable · Esc to retry",L"快速鍵優先無法啟用 · 可Esc結束重試");
    }
    void endInput() {
        if(keyboardHook){UnhookWindowsHookEx(keyboardHook);keyboardHook=nullptr;}
        if(inputSession==this)inputSession=nullptr;
    }

    static COLORREF colorRef(uint32_t bgra) { return RGB((bgra >> 16) & 255, (bgra >> 8) & 255, bgra & 255); }
    static void fill(HDC dc, RECT r, COLORREF color) {
        HBRUSH brush = CreateSolidBrush(color);
        if (brush) { FillRect(dc, &r, brush); DeleteObject(brush); }
    }
    static bool desktopAvailable() {
        HDESK input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
        if (!input) return false;
        wchar_t name[256]{}, current[256]{};
        DWORD size = 0;
        const bool okay = GetUserObjectInformationW(input, UOI_NAME, name, sizeof(name), &size) &&
            GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, current, sizeof(current), &size) &&
            std::wstring_view(name) == current;
        CloseDesktop(input);
        return okay;
    }
    static BOOL CALLBACK enumerate(HMONITOR handle, HDC, LPRECT, LPARAM context) {
        auto* self = reinterpret_cast<ScreenPickerSession*>(context);
        MONITORINFO info{sizeof(info)};
        if (!GetMonitorInfoW(handle, &info)) return FALSE;
        auto monitor = std::make_unique<Monitor>();
        const auto r = info.rcMonitor;
        monitor->bounds = {r.left, r.top, r.right-r.left, r.bottom-r.top};
        UINT x = 96, y = 96;
        if (SUCCEEDED(GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &x, &y))) monitor->dpi = static_cast<int>(x);
        self->monitors.push_back(std::move(monitor));
        return TRUE;
    }
    bool capture() {
        valid = false;
        if (!desktopAvailable()) return false;
        uint64_t total = 0;
        for (const auto& m : monitors) total += uint64_t(m->bounds.width) * m->bounds.height;
        // 明确的内存预算，空洞不占内存；超限不分配，不复制旧帧。
        MEMORYSTATUSEX memory{sizeof(memory)};
        if (!GlobalMemoryStatusEx(&memory) || total > (128ULL << 20) || total * 8 > memory.ullAvailPhys / 2) return false;
        HDC screen = GetDC(nullptr);
        if (!screen) return false;
        bool okay = true;
        for (auto& m : monitors) {
            if (!m->dc) {
                BITMAPINFO info{};
                info.bmiHeader = {sizeof(BITMAPINFOHEADER), m->bounds.width, -m->bounds.height, 1, 32, BI_RGB};
                m->dc = CreateCompatibleDC(screen);
                m->bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&m->pixels), nullptr, 0);
                if (!m->dc || !m->bitmap || !m->pixels) { okay = false; break; }
                m->previous = SelectObject(m->dc, m->bitmap);
                m->canvas = CreateCompatibleDC(screen);
                m->canvasBitmap = CreateCompatibleBitmap(screen,m->bounds.width,m->bounds.height);
                if(!m->canvas || !m->canvasBitmap) {okay=false;break;}
                m->previousCanvas=SelectObject(m->canvas,m->canvasBitmap);
            }
            if (!BitBlt(m->dc, 0, 0, m->bounds.width, m->bounds.height, screen,
                m->bounds.x, m->bounds.y, SRCCOPY | CAPTUREBLT)) { okay = false; break; }
        }
        GdiFlush();
        ReleaseDC(nullptr, screen);
        valid = okay;
        return okay;
    }
    int monitorAt(int x, int y) const {
        for (size_t i = 0; i < monitors.size(); ++i) if (monitors[i]->bounds.contains(x,y)) return static_cast<int>(i);
        return -1;
    }
    bool syncPointer() {
        POINT current{};
        if(!GetCursorPos(&current) || (current.x==lastMouse.x && current.y==lastMouse.y)) return false;
        lastMouse=current;point=current;return true;
    }
    std::optional<ColorValue::Rgb> pixel(int x, int y) const {
        const int index = monitorAt(x,y);
        if (!valid || index < 0) return {};
        const auto& m = *monitors[index];
        const auto offset = (size_t(y - m.bounds.y) * m.bounds.width + x - m.bounds.x) * 4;
        return ColorValue::Rgb{m.pixels[offset+2], m.pixels[offset+1], m.pixels[offset]};
    }
    void redraw() {
        const int previous=selected;
        selected = monitorAt(point.x,point.y);
        if(previous>=0 && previous!=selected) InvalidateRect(monitors[previous]->window,nullptr,FALSE);
        if(selected>=0) InvalidateRect(monitors[selected]->window,nullptr,FALSE);
        dirty=false;
    }
    void persist() {
        if (!PickerPreferences::save(settingsPath, settings))
            status = trW(L"偏好保存失败（目录可能只读）", L"Preferences not saved (read-only folder?)", L"偏好儲存失敗（目錄可能唯讀）");
    }
    void close(Result value) {
        result = value;
        closing = true;
        PostQuitMessage(0);
    }
    bool record(bool copyOnly=false) {
        const auto value=pixel(point.x,point.y);if(!value)return false;
        try{ColorLibrary::Store store(ColorLibrary::defaultPath());if(copyOnly&&!store.preferences().recordCopies)return true;
            store.record(*value,"screen",copyOnly?ColorLibrary::eventKey():confirmationKey,0,point);return true;
        }catch(...){status=trW(L"颜色历史未保存 · 请重试或Esc取消",L"History not saved · retry or Esc to cancel",L"色彩歷史未儲存 · 請重試或Esc取消");redraw();return false;}
    }
    bool copy(bool confirming=false) {
        auto value = pixel(point.x,point.y);
        if (!value) { status = trW(L"无效采样，不能复制", L"Invalid sample; cannot copy", L"無效採樣，無法複製"); redraw(); return false; }
        const auto text = PickerCore::format(*value, static_cast<PickerCore::Format>(settings.format));
        const SIZE_T bytes = (text.size()+1) * sizeof(wchar_t);
        HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!handle) {status=trW(L"复制失败：内存不足",L"Copy failed: insufficient memory",L"複製失敗：記憶體不足");redraw();return false;}
        void* destination = GlobalLock(handle);
        if (!destination) { GlobalFree(handle);status=trW(L"复制失败，请重试",L"Copy failed; retry",L"複製失敗，請重試");redraw();return false; }
        memcpy(destination, text.c_str(), bytes);
        GlobalUnlock(handle);
        bool opened = false, okay = false;
        for (int retry = 0; retry < 8; ++retry) {
            if (OpenClipboard(monitors.front()->window)) { opened = true; break; }
            Sleep(10);
        }
        if (opened) {
            okay = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, handle);
            CloseClipboard();
        }
        if (!okay) GlobalFree(handle);
        status = okay ? trW(L"已复制 · 保持取色", L"Copied · continue sampling", L"已複製 · 保持取色") :
            trW(L"复制失败，请重试", L"Copy failed; retry", L"複製失敗，請重試");
        if (okay) accepted = (uint32_t(value->r) << 16) | (uint32_t(value->g) << 8) | value->b;
        if (okay) CurrentColor::set(*value);
        if(okay&&!confirming)record(true);
        redraw();
        return okay;
    }
    void refresh() {
        recapturing = true;
        for (auto& m : monitors) ShowWindow(m->window, SW_HIDE);
        DwmFlush();
        // DwmFlush 同步合成，不靠某个像素颜色猜窗口是否已消失。
        const bool okay = capture();
        if (!okay) status = trW(L"桌面读取失败 · F5 重试 / Esc 退出", L"Capture failed · F5 retry / Esc exit", L"桌面讀取失敗 · F5 重試 / Esc 結束");
        else status.clear();
        for (auto& m : monitors) {ShowWindow(m->window, SW_SHOWNOACTIVATE);InvalidateRect(m->window,nullptr,FALSE);}
        if (!monitors.empty()) { SetForegroundWindow(monitors.front()->window); SetFocus(monitors.front()->window); }
        recapturing = false;
        redraw();
    }
    void applyPreference(UINT command) {
        if (command >= 100 && command < 104) settings.grid = PickerCore::GRIDS[command-100];
        if (command >= 200 && command < 206) settings.zoom = PickerCore::ZOOMS[command-200];
        if (command == 300) settings.showGrid = !settings.showGrid;
        if (command == 301) settings.crosshair = !settings.crosshair;
        if (command == 302) settings.showHints = !settings.showHints;
        if(command)persist();
        redraw();
    }
    HMENU preferenceMenu() const {
        HMENU menu = CreatePopupMenu();
        HMENU grids = CreatePopupMenu(), zooms = CreatePopupMenu();
        for (size_t i = 0; i < PickerCore::GRIDS.size(); ++i) {
            const int n = PickerCore::GRIDS[i];
            const auto label = std::to_wstring(n) + L" × " + std::to_wstring(n);
            AppendMenuW(grids, MF_STRING | (settings.grid == n ? MF_CHECKED : 0), 100+i, label.c_str());
        }
        for (size_t i = 0; i < PickerCore::ZOOMS.size(); ++i) {
            const int n = PickerCore::ZOOMS[i];
            const auto label = std::to_wstring(n) + L"×";
            AppendMenuW(zooms, MF_STRING | (settings.zoom == n ? MF_CHECKED : 0), 200+i, label.c_str());
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(grids), trW(L"采样格数", L"Sample grid", L"採樣格數"));
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(zooms), trW(L"像素放大倍率", L"Pixel magnification", L"像素放大倍率"));
        AppendMenuW(menu, MF_STRING | (settings.showGrid ? MF_CHECKED : 0), 300, trW(L"显示网格", L"Show grid", L"顯示網格"));
        AppendMenuW(menu, MF_STRING | (settings.crosshair ? MF_CHECKED : 0), 301, trW(L"显示准星", L"Show crosshair", L"顯示準星"));
        AppendMenuW(menu, MF_STRING | (settings.showHints ? MF_CHECKED : 0), 302, trW(L"显示取色快捷键说明",L"Show color picker shortcuts",L"顯示取色快捷鍵說明"));
        return menu;
    }
    void preferences(HWND hwnd) {
        menuOpen = true;
        HMENU menu=preferenceMenu();
        POINT menuPoint{}; GetCursorPos(&menuPoint);
        const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, menuPoint.x, menuPoint.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        menuOpen = false;
        applyPreference(command);
    }
    void key(HWND hwnd, WPARAM code, bool repeated, int capturedStep=0) {
        if (code == VK_ESCAPE) { close(Cancelled); return; }
        if (code == VK_RETURN) { if (!repeated) confirm(); return; }
        if (code == 'E' && !repeated) { selectForPanel(); return; }
        if (code == 'C') { if (!repeated) copy(); return; }
        if (code == VK_SHIFT && !repeated) { settings.format = PickerCore::nextFormat(settings.format); persist(); }
        if (code == 'P' && !repeated) { settings.relative = !settings.relative; persist(); }
        if (code == VK_F2 && !repeated) { preferences(hwnd); return; }
        if ((code == VK_F5 || code == VK_SPACE) && !repeated) { refresh(); return; }
        if (code >= VK_LEFT && code <= VK_DOWN) {
            POINT next = point;
            const int step = capturedStep?capturedStep:(GetKeyState(VK_CONTROL) < 0 ? 10 : 1);
            if (code == VK_LEFT) next.x -= step;
            if (code == VK_RIGHT) next.x += step;
            if (code == VK_UP) next.y -= step;
            if (code == VK_DOWN) next.y += step;
            if (monitorAt(next.x,next.y) >= 0) point = next;
        }
        redraw();
    }
    std::vector<std::wstring> hudLines(const Monitor& monitor) const {
        const int px=settings.relative?point.x-monitor.bounds.x:point.x,py=settings.relative?point.y-monitor.bounds.y:point.y;
        const auto sample=pixel(point.x,point.y);
        std::vector<std::wstring> rows{
            sample?PickerCore::display(*sample,static_cast<PickerCore::Format>(settings.format)):trW(L"无效像素",L"Invalid pixel",L"無效像素"),
            L"("+std::to_wstring(px)+L", "+std::to_wstring(py)+L")",
            trW(L"Shift：切换颜色格式",L"Shift: Switch color format",L"Shift：切換色彩格式"),
            trW(L"P：切换全局/相对坐标",L"P: Global / relative coordinates",L"P：切換全域/相對座標"),
            trW(L"C：复制颜色值",L"C: Copy color value",L"C：複製色彩值"),
            returnToPanel?trW(L"左键/Enter：送回颜色面板",L"Click / Enter: Return color",L"左鍵/Enter：送回色彩面板"):
                trW(L"左键/Enter：复制退出",L"Click / Enter: Copy and exit",L"左鍵/Enter：複製結束"),
            trW(L"E：送入颜色面板调整",L"E: Adjust in color panel",L"E：送入色彩面板調整"),
            trW(L"Esc/右键：取消",L"Esc / right click: Cancel",L"Esc/右鍵：取消"),
            trW(L"方向键：移动1px",L"Arrows: Move 1 px",L"方向鍵：移動1px"),
            trW(L"Ctrl+方向键：移动10px",L"Ctrl + arrows: Move 10 px",L"Ctrl+方向鍵：移動10px"),
            trW(L"F2：偏好",L"F2: Preferences",L"F2：偏好"),
            trW(L"空格/F5：重新抓屏",L"Space / F5: Recapture",L"空白鍵/F5：重新擷取")};
        if(!settings.showHints)rows.resize(2);
        if(!status.empty())rows.push_back(status);
        return rows;
    }
    void renderHud(Monitor& monitor) {
        HDC dc = monitor.canvas;
        if(!dc)return;
        if (valid) BitBlt(dc,0,0,monitor.bounds.width,monitor.bounds.height,monitor.dc,0,0,SRCCOPY);
        else fill(dc, {0,0,monitor.bounds.width,monitor.bounds.height}, RGB(45,45,45));
        if (selected >= 0 && monitors[selected].get() == &monitor) {
            const auto s = [&](int n) { return MulDiv(n, monitor.dpi, 96); };
            const int pad = s(PickerCore::HUD_PADDING);
            const int idealGrid = settings.grid * s(settings.zoom);
            const int width = std::min(monitor.bounds.width, std::max(s(PickerCore::HUD_WIDTH), idealGrid + pad * 2));
            monitor.prepareFonts();
            const auto rows=hudLines(monitor);
            const std::wstring kind=settings.relative?trW(L"相对坐标",L"Relative coordinates",L"相對座標"):
                trW(L"全局坐标",L"Global coordinates",L"全域座標");
            const auto primary=[&](size_t i){return i<2||(!status.empty()&&i+1==rows.size());};
            const auto fontFor=[&](size_t i){const auto font=primary(i)?monitor.primaryFont:monitor.helpFont;return font?font:static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));};
            const auto oldFont=SelectObject(dc,fontFor(0));SetBkMode(dc,TRANSPARENT);
            std::vector<int> rowHeights;
            int textHeight=0;
            int coordinateWidth=0,kindWidth=0,coordinateHeight=0;bool inlineKind=false;
            lastHudTextFits=true;
            lastHudHelpLeft=rows.size()<=2;lastHudHelpGap=0;lastCoordinateBaselinesAligned=true;
            for(size_t i=0;i<rows.size();++i) {
                const auto& value=rows[i];SelectObject(dc,fontFor(i));
                const int line=s(primary(i)?PickerCore::HUD_PRIMARY_LINE:PickerCore::HUD_LINE);
                const auto painted=i>=2?PickerCore::hudHelpText(value):value;
                RECT measured{0,0,std::max(1,width-pad*2),0};
                DrawTextW(dc,painted.c_str(),-1,&measured,(PickerCore::hudRowCentered(i)?DT_CENTER:DT_LEFT)|DT_WORDBREAK|DT_NOPREFIX|DT_CALCRECT);
                lastHudTextFits &= measured.right<=std::max(1,width-pad*2);
                int h=std::max(line,((static_cast<int>(measured.bottom)+line-1)/line)*line);
                if(i==1) {
                    SIZE extent{};GetTextExtentPoint32W(dc,value.c_str(),int(value.size()),&extent);coordinateWidth=extent.cx;
                    SelectObject(dc,monitor.helpFont);GetTextExtentPoint32W(dc,kind.c_str(),int(kind.size()),&extent);kindWidth=extent.cx;
                    inlineKind=coordinateWidth+s(4)+kindWidth<=width-pad*2;coordinateHeight=h;
                    if(!inlineKind)h+=s(PickerCore::HUD_LINE);
                }
                rowHeights.push_back(h);textHeight+=h;
            }
            const int helpGap=PickerCore::hudHelpGap(monitor.dpi,rows.size()>2);textHeight+=helpGap;lastHudHelpGap=helpGap;
            const int gridSize = std::max(0, std::min({idealGrid, width - pad*2, monitor.bounds.height - textHeight - pad*2}));
            const int height = std::min(monitor.bounds.height, gridSize + textHeight + pad*2);
            const auto placed = PickerCore::placeHud(monitor.bounds, point.x, point.y, width,height,s(24), lastHudMonitor == selected ? &lastHud : nullptr);
            lastHud = placed; lastHudMonitor = selected;
            const int x = placed.x-monitor.bounds.x, y = placed.y-monitor.bounds.y;
            fill(dc, {x,y,x+width,y+height}, colorRef(PsdDockStyle::background(dark)));
            const int gx=x+(width-gridSize)/2, gy=y+pad, half=settings.grid/2;
            for (int row=0; row<settings.grid && gridSize>0; ++row) for (int col=0; col<settings.grid; ++col) {
                const auto value = pixel(point.x+col-half, point.y+row-half);
                RECT cell{gx+col*gridSize/settings.grid,gy+row*gridSize/settings.grid,
                    gx+(col+1)*gridSize/settings.grid,gy+(row+1)*gridSize/settings.grid};
                fill(dc, cell, value ? RGB(value->r,value->g,value->b) : ((row+col)%2 ? RGB(90,90,90) : RGB(145,145,145)));
                if (settings.showGrid && cell.right-cell.left >= 4) {
                    HBRUSH border=CreateSolidBrush(dark?RGB(65,65,65):RGB(190,190,190));
                    FrameRect(dc,&cell,border); DeleteObject(border);
                }
            }
            if (gridSize>0) {
                RECT center{gx+half*gridSize/settings.grid,gy+half*gridSize/settings.grid,
                    gx+(half+1)*gridSize/settings.grid,gy+(half+1)*gridSize/settings.grid};
                InflateRect(&center,1,1); FrameRect(dc,&center,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                InflateRect(&center,1,1); FrameRect(dc,&center,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            }
            int ty=gy+gridSize;
            for(size_t i=0;i<rows.size();++i) {
                if(i==2)ty+=helpGap;
                RECT r{x+pad,ty,x+width-pad,std::min(y+height-pad,ty+rowHeights[i])};
                SelectObject(dc,fontFor(i));
                SetTextColor(dc,colorRef(primary(i)?PsdDockStyle::foreground(dark):PsdDockStyle::muted(dark)));
                if(r.bottom>r.top) {
                    if(i==1) {
                        if(inlineKind) {
                            TEXTMETRICW primaryMetrics{},helpMetrics{};GetTextMetricsW(dc,&primaryMetrics);SelectObject(dc,monitor.helpFont);GetTextMetricsW(dc,&helpMetrics);SelectObject(dc,monitor.primaryFont);
                            const int baseline=r.top+PickerCore::hudBaseline(r.bottom-r.top,std::max(primaryMetrics.tmAscent,helpMetrics.tmAscent),std::max(primaryMetrics.tmDescent,helpMetrics.tmDescent));
                            const UINT previousAlign=SetTextAlign(dc,TA_LEFT|TA_BASELINE);
                            r.left=x+(width-coordinateWidth-kindWidth-s(4))/2;r.right=r.left+coordinateWidth;
                            ExtTextOutW(dc,r.left,baseline,ETO_CLIPPED,&r,rows[i].c_str(),UINT(rows[i].size()),nullptr);
                            r.left=r.right+s(4);r.right=r.left+kindWidth;
                            SelectObject(dc,monitor.helpFont);SetTextColor(dc,colorRef(PsdDockStyle::muted(dark)));
                            ExtTextOutW(dc,r.left,baseline,ETO_CLIPPED,&r,kind.c_str(),UINT(kind.size()),nullptr);
                            lastCoordinateBaselinesAligned=baseline-primaryMetrics.tmAscent>=r.top&&baseline+primaryMetrics.tmDescent<=r.bottom&&baseline-helpMetrics.tmAscent>=r.top&&baseline+helpMetrics.tmDescent<=r.bottom;
                            SetTextAlign(dc,previousAlign);
                        } else {
                            r.bottom=r.top+coordinateHeight;DrawTextW(dc,rows[i].c_str(),-1,&r,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
                            r.top=r.bottom;r.bottom=std::min<LONG>(y+height-pad,r.top+s(PickerCore::HUD_LINE));
                            SelectObject(dc,monitor.helpFont);SetTextColor(dc,colorRef(PsdDockStyle::muted(dark)));
                            DrawTextW(dc,kind.c_str(),-1,&r,DT_CENTER|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
                        }
                    } else {const UINT align=PickerCore::hudRowCentered(i)?DT_CENTER:DT_LEFT;if(i>=2)lastHudHelpLeft=align==DT_LEFT;const auto painted=i>=2?PickerCore::hudHelpText(rows[i]):rows[i];DrawTextW(dc,painted.c_str(),-1,&r,align|DT_WORDBREAK|DT_NOPREFIX);}
                }
                ty+=rowHeights[i];
            }
            lastHudTextFits &= ty<=y+height-pad;
            SelectObject(dc,oldFont); // 字体由Monitor按DPI缓存并在会话结束释放，不在每次绘制中反复创建。
            if (settings.crosshair) {
                const int cx=point.x-monitor.bounds.x, cy=point.y-monitor.bounds.y;
                HPEN pen=CreatePen(PS_SOLID,1,RGB(0,0,0)); HGDIOBJ oldPen=SelectObject(dc,pen);
                for (int offset : {-1,1}) {
                    MoveToEx(dc,cx-s(9),cy+offset,nullptr); LineTo(dc,cx+s(9)+1,cy+offset);
                    MoveToEx(dc,cx+offset,cy-s(9),nullptr); LineTo(dc,cx+offset,cy+s(9)+1);
                }
                SelectObject(dc,oldPen); DeleteObject(pen);
                pen=CreatePen(PS_SOLID,1,RGB(255,255,255)); SelectObject(dc,pen);
                MoveToEx(dc,cx-s(9),cy,nullptr); LineTo(dc,cx+s(9)+1,cy);
                MoveToEx(dc,cx,cy-s(9),nullptr); LineTo(dc,cx,cy+s(9)+1);
                SelectObject(dc,oldPen); DeleteObject(pen);
            }
        }
    }
    void paint(HWND hwnd, Monitor& monitor) {
        PAINTSTRUCT paint{};const HDC screen=BeginPaint(hwnd,&paint);
        renderHud(monitor);
        if(monitor.canvas)BitBlt(screen,paint.rcPaint.left,paint.rcPaint.top,paint.rcPaint.right-paint.rcPaint.left,paint.rcPaint.bottom-paint.rcPaint.top,
            monitor.canvas,paint.rcPaint.left,paint.rcPaint.top,SRCCOPY);
        EndPaint(hwnd,&paint);
    }
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
        auto* self=reinterpret_cast<ScreenPickerSession*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if (message==WM_NCCREATE) {
            self=static_cast<ScreenPickerSession*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd,message,w,l);
        switch(message) {
        // 只读、无指针的状态探针：用于真实输入验收，不改变采样或输入路由。
        case QUERY_MESSAGE:switch(w){case 0:return self->settings.format;case 1:return self->settings.relative;case 2:return self->point.x;case 3:return self->point.y;case 4:return self->keyboardHook!=nullptr;}return -1;
        case INPUT_MESSAGE:self->key(hwnd,w,(l&1)!=0,(l&2)?10:1);return 0;
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEMOVE:
            if(self->syncPointer()) {self->status.clear();self->dirty=true;}return 0;
        case WM_LBUTTONDOWN:
            // 未移动鼠标时保留方向键微调的逻辑采样点；实际移动则同步最后位置。
            self->syncPointer();self->confirm();return 0;
        case WM_RBUTTONDOWN: case WM_CLOSE: self->close(Cancelled); return 0;
        case WM_KEYDOWN: self->key(hwnd,w,(l & (1LL<<30))!=0); return 0;
        case WM_SYSKEYDOWN: if(w==VK_F4) self->close(Cancelled); return 0;
        case WM_ACTIVATEAPP:
            if (!w && !self->recapturing && !self->closing && !self->menuOpen) self->close(Cancelled);
            return 0;
        case WM_DISPLAYCHANGE: self->close(Cancelled); return 0; // 不在已变拓扑上使用旧帧。
        case WM_DPICHANGED:
            for(auto& m:self->monitors) if(m->window==hwnd) m->dpi=HIWORD(w);
            self->redraw(); return 0;
        case WM_TIMER:
            if(stopRequested || !desktopAvailable()) self->close(Cancelled);
            else if(self->dirty) self->redraw();
            return 0;
        case WM_PAINT:
            for(auto& m:self->monitors) if(m->window==hwnd) {self->paint(hwnd,*m); return 0;}
            break;
        }
        return DefWindowProcW(hwnd,message,w,l);
    }
    void run(const std::function<void()>& ready = {}) {
        previousForeground=GetForegroundWindow();
        if(!EnumDisplayMonitors(nullptr,nullptr,enumerate,reinterpret_cast<LPARAM>(this)) || monitors.empty() || !capture()) {result=CaptureFailed; return;}
        if(stopRequested) return;
        WNDCLASSW cls{}; cls.lpfnWndProc=procedure; cls.hInstance=GetModuleHandleW(nullptr);
        cls.lpszClassName=L"YeImageViewer.QuickScreenPicker"; cls.hCursor=LoadCursorW(nullptr,IDC_CROSS);
        RegisterClassW(&cls); GetCursorPos(&point);lastMouse=point;selected=monitorAt(point.x,point.y);
        for(auto& m:monitors) {
            m->window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,cls.lpszClassName,L"YeImageViewer Snapshot Picker",
                WS_POPUP,m->bounds.x,m->bounds.y,m->bounds.width,m->bounds.height,nullptr,nullptr,cls.hInstance,this);
            if(!m->window) {result=StartFailed; return;}
            m->dpi=GetDpiForWindow(m->window);
        }
        // 输入先准备、窗口后显示，避免覆盖层可见但IME/快捷键优先尚未就绪的窗口期。
        beginInput();
        // 先创建所有窗口再显示，首次截图一定早于任何覆盖层。
        recapturing=true;
        for(auto& m:monitors) ShowWindow(m->window,SW_SHOWNOACTIVATE);
        SetForegroundWindow(monitors.front()->window); SetFocus(monitors.front()->window);
        recapturing=false;
        SetTimer(monitors.front()->window,1,33,nullptr); redraw();
        if (ready) ready();
        MSG message{};
        while(!stopRequested && !closing && GetMessageW(&message,nullptr,0,0)>0) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    void finish() {
        closing=true;
        endInput();
        const HWND foreground=GetForegroundWindow();
        const bool restore=std::any_of(monitors.begin(),monitors.end(),[&](const auto& m){return m->window==foreground;});
        if(StartupTrace::isEnabled())StartupTrace::mark(("picker.beforeRelease.gdi="+std::to_string(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS))).c_str());
        monitors.clear(); // 所有窗口和 GDI 对象都由创建线程释放。
        GdiFlush(); // 刷新本线程的GDI批处理，再向主窗口发布会话结束/资源测量状态。
        if(StartupTrace::isEnabled())StartupTrace::mark(("picker.afterRelease.gdi="+std::to_string(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS))).c_str());
        if(restore && !stopRequested && previousForeground && IsWindow(previousForeground))
            SetForegroundWindow(previousForeground);
        if(owner && IsWindow(owner)) PostMessageW(owner,RESULT_MESSAGE,result,accepted);
    }
public:
    ~ScreenPickerSession(){endInput();}
    bool selectForPanel() {
        const auto value=pixel(point.x,point.y);
        if(!value){status=trW(L"无效像素",L"Invalid pixel",L"無效像素");redraw();return false;}
        accepted=CurrentColor::pack(*value);CurrentColor::set(*value);close(Selected);return true;
    }
    void confirm() {if(closing)return;if(returnToPanel){if(record())selectForPanel();}else if(copy(true)&&record())close(Copied);}
    static bool isRunning() {return running.load();}
    static DWORD workerThreadId(){return workerId.load();}
    // 纯离屏布局闸门：只绘制自造色块，不建窗口、不读桌面/剪贴板、不发送输入。
    static bool layoutSelfTest(const std::filesystem::path& evidence) {
        std::ofstream log(evidence);if(!log)return false;
        struct LanguageGuard{uint32_t old=GlobalVar::settingParameter.UI_LANG;~LanguageGuard(){GlobalVar::settingParameter.UI_LANG=old;}} languageGuard;
        int passed=0,failed=0;const auto check=[&](const char* name,bool value){log<<(value?"PASS ":"FAIL ")<<name<<'\n';value?++passed:++failed;};
        log<<"OFFSCREEN ONLY: synthetic pixels; no window, screen capture, clipboard or input\n";
        ScreenPickerSession session;auto monitor=std::make_unique<Monitor>();monitor->bounds={0,0,1920,1080};
        BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),1920,-1080,1,32,BI_RGB};
        monitor->dc=CreateCompatibleDC(nullptr);monitor->bitmap=CreateDIBSection(monitor->dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&monitor->pixels),nullptr,0);
        monitor->canvas=CreateCompatibleDC(nullptr);void* output=nullptr;monitor->canvasBitmap=CreateDIBSection(monitor->canvas,&info,DIB_RGB_COLORS,&output,nullptr,0);
        if(!monitor->dc||!monitor->bitmap||!monitor->pixels||!monitor->canvas||!monitor->canvasBitmap||!output){log<<"FAIL GDI allocation\n";return false;}
        monitor->previous=SelectObject(monitor->dc,monitor->bitmap);monitor->previousCanvas=SelectObject(monitor->canvas,monitor->canvasBitmap);
        std::fill_n(reinterpret_cast<uint32_t*>(monitor->pixels),1920*1080,0xFF12ABEFu);
        session.monitors.push_back(std::move(monitor));session.selected=0;session.point={120,120};session.valid=true;
        auto& display=*session.monitors.front();
        try{for(int language=0;language<3;++language)for(bool dark:{false,true})for(int dpi:{96,192,288})for(int hints:{0,1})for(int format=0;format<PickerCore::FORMAT_COUNT;++format){
            GlobalVar::settingParameter.UI_LANG=language;session.dark=dark;display.dpi=dpi;session.settings.showHints=hints;session.settings.format=format;
            session.lastHudMonitor=-1;session.renderHud(display);GdiFlush();
            const auto lines=session.hudLines(display);
            check("color first; aligned coordinates; left help with half-line gap; all text fits",session.lastHudTextFits&&session.lastCoordinateBaselinesAligned&&session.lastHudHelpLeft&&session.lastHudHelpGap==PickerCore::hudHelpGap(dpi,lines.size()>2)&&
                lines.front()==PickerCore::display({18,171,239},static_cast<PickerCore::Format>(format)));
            check("no live window created",display.window==nullptr);
            if(dpi==96&&format==0){const auto r=session.lastHud;cv::Mat canvas(1080,1920,CV_8UC4,output);cv::Mat image=canvas(cv::Rect(r.x,r.y,r.width,r.height)).clone();for(int y=0;y<image.rows;++y)for(int x=0;x<image.cols;++x)image.at<cv::Vec4b>(y,x)[3]=255;
                const auto name=std::string("hud-")+std::to_string(language)+(dark?"-dark":"-light")+(hints?"-hints.png":"-compact.png");check("preview written",cv::imwrite((evidence.parent_path()/name).string(),image));}
        }}catch(...){check("offscreen rendering completed",false);}
        log<<"passed="<<passed<<" failed="<<failed<<'\n';return failed==0;
    }
    // 无主窗口的独立入口，复用同一生产捕获/偏好；取色结束释放全部窗口与GDI。
    static Result runStandalone(const std::filesystem::path& path,bool dark) {
        stopRequested=false;ScreenPickerSession session;session.settingsPath=path;
        session.settings=PickerPreferences::load(path);session.dark=dark;
        try{session.run();}catch(...){session.result=StartFailed;}
        session.finish();if(session.result==Copied&&PickerHandoff::wait(stopRequested)){CurrentColor::set(CurrentColor::unpack(session.accepted));session.result=Selected;}
        MSG quit{};while(PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){}
        return session.result;
    }
    // 用生产捕获/覆盖层/消息处理验证可读桌面上的已知色块。消息测试不冒充物理输入。
    static bool selfTest(const std::filesystem::path& evidence) {
        std::ofstream log(evidence);
        int passed=0,failed=0;
        auto check=[&](const char* label,bool okay){ log<<(okay?"PASS ":"FAIL ")<<label<<'\n'; okay?++passed:++failed; };
        if(!desktopAvailable() || !GetForegroundWindow()) {log<<"BLOCKED interactive desktop unavailable\n";return false;}
        ColorLibrary::Store colorData(ColorLibrary::defaultPath());const auto historyStart=colorData.count(false);
        POINT previousCursor{}; GetCursorPos(&previousCursor);
        const HWND previousForeground=GetForegroundWindow();
        // 剪贴板为全机器共享；只重试暂时打不开，不重试/替换读到的错误内容。
        const auto clipboardText=[](HWND hwnd)->std::optional<std::wstring> {
            for(int attempt=0;attempt<12;++attempt) {
                if(OpenClipboard(hwnd)) {
                    std::optional<std::wstring> text;
                    if(HANDLE data=GetClipboardData(CF_UNICODETEXT)) {
                        if(auto* value=static_cast<const wchar_t*>(GlobalLock(data))) {text=std::wstring(value);GlobalUnlock(data);}
                    }
                    CloseClipboard();return text;
                }
                Sleep(10);
            }
            return {};
        };
        const auto previousLanguage=GlobalVar::settingParameter.UI_LANG;
        struct RestoreLanguage {uint32_t value;~RestoreLanguage(){GlobalVar::settingParameter.UI_LANG=value;}} restoreLanguage{previousLanguage};
        const auto saveHud=[&](ScreenPickerSession& session,const std::wstring& name) {
            const auto& m=*session.monitors[session.selected];
            const auto& r=session.lastHud;
            BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),r.width,-r.height,1,32,BI_RGB};
            void* pixels=nullptr;HDC copyDc=CreateCompatibleDC(m.canvas);
            HBITMAP bitmap=CreateDIBSection(m.canvas,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
            if(!copyDc || !bitmap) {if(bitmap)DeleteObject(bitmap);if(copyDc)DeleteDC(copyDc);return false;}
            auto old=SelectObject(copyDc,bitmap);
            bool okay=BitBlt(copyDc,0,0,r.width,r.height,m.canvas,r.x-m.bounds.x,r.y-m.bounds.y,SRCCOPY)!=FALSE;
            GdiFlush();
            if(okay) {
                cv::Mat image;cv::cvtColor(cv::Mat(r.height,r.width,CV_8UC4,pixels),image,cv::COLOR_BGRA2BGR);
                std::vector<uchar> bytes;okay=cv::imencode(".png",image,bytes);
                if(okay) {std::ofstream file(evidence.parent_path()/name,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());okay=file.good();}
            }
            SelectObject(copyDc,old);DeleteObject(bitmap);DeleteDC(copyDc);return okay;
        };
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YeImageViewer.PickerTestSurface";
        cls.lpfnWndProc=[](HWND h,UINT m,WPARAM w,LPARAM l)->LRESULT {
            if(m==WM_PAINT) {PAINTSTRUCT p{};HDC dc=BeginPaint(h,&p);RECT r{};GetClientRect(h,&r);
                fill(dc,r,static_cast<COLORREF>(GetWindowLongPtrW(h,GWLP_USERDATA)));SetPixelV(dc,81,70,RGB(0,0,255));EndPaint(h,&p);return 0;}
            return DefWindowProcW(h,m,w,l);
        };
        RegisterClassW(&cls);
        MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&info);
        const int left=info.rcWork.left+40,top=info.rcWork.top+40;
        HWND surface=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,cls.lpszClassName,L"Picker known-color fixture",WS_POPUP,
            left,top,240,180,nullptr,nullptr,cls.hInstance,nullptr);
        if(!surface) {log<<"BLOCKED fixture window creation failed\n";return false;}
        SetWindowLongPtrW(surface,GWLP_USERDATA,RGB(18,171,239));ShowWindow(surface,SW_SHOW);UpdateWindow(surface);SetForegroundWindow(surface);DwmFlush();
        for(int scenario=0;scenario<6;++scenario) {
            GlobalVar::settingParameter.UI_LANG=scenario%3;
            SetWindowLongPtrW(surface,GWLP_USERDATA,RGB(18,171,239));InvalidateRect(surface,nullptr,FALSE);UpdateWindow(surface);DwmFlush();
            ScreenPickerSession session;session.settingsPath=evidence.parent_path()/L"picker-test.ini";session.dark=scenario<3;
            session.settings={};
            SetCursorPos(left+80,top+70);
            session.run([&] {
                HWND overlay=session.monitors.front()->window;
                session.point={left+80,top+70}; session.redraw();
                check("source known RGB before HUD",session.pixel(session.point.x,session.point.y)==ColorValue::Rgb{18,171,239});
                const auto original=session.pixel(session.point.x,session.point.y);
                UpdateWindow(session.monitors[session.selected]->window);GdiFlush();
                check("HUD and crosshair never overwrite source",session.pixel(session.point.x,session.point.y)==original);
                check("one overlay per actual monitor",session.monitors.size()>=1 && std::all_of(session.monitors.begin(),session.monitors.end(),[](auto& m){return IsWindow(m->window)!=FALSE;}));
                const POINT start=session.point;
                SendMessageW(overlay,WM_KEYDOWN,VK_RIGHT,0);
                check("arrow moves sample exactly one physical pixel",session.point.x==start.x+1 && session.point.y==start.y && session.pixel(session.point.x,session.point.y)==ColorValue::Rgb{0,0,255});
                SendMessageW(overlay,WM_KEYDOWN,VK_LEFT,0);
                check("reverse arrow restores same source point",session.point.x==start.x && session.point.y==start.y);
                check("compact HUD omits snapshot header and keeps original shortcuts plus E",session.hudLines(*session.monitors[session.selected]).size()==PickerCore::HUD_TEXT_ROWS+1);
                check("default HUD width is at most 180 DIP",session.lastHud.width<=MulDiv(PickerCore::HUD_WIDTH,session.monitors[session.selected]->dpi,96));
                auto& display=*session.monitors[session.selected];
                const int originalDpi=display.dpi;
                for(int testDpi:{96,192,288}) {
                    display.dpi=testDpi;
                    for(int showHints:{1,0})for(int f=0;f<PickerCore::FORMAT_COUNT;++f) {
                        session.settings.showHints=showHints;session.settings.format=f;session.redraw();UpdateWindow(display.window);
                        check("seven HUD formats fit actual primary/help fonts with hints shown or hidden at simulated 96/192/288 DPI",session.lastHudTextFits);
                        LOGFONTW primary{},help{};GetObjectW(display.primaryFont,sizeof(primary),&primary);GetObjectW(display.helpFont,sizeof(help),&help);
                        check("coordinates and value are semibold and help is smaller regular text at current DPI",primary.lfWeight==PickerCore::HUD_PRIMARY_WEIGHT&&help.lfWeight==FW_NORMAL&&primary.lfHeight==-MulDiv(PickerCore::HUD_PRIMARY_FONT,testDpi,96)&&help.lfHeight==-MulDiv(PickerCore::HUD_HELP_FONT,testDpi,96)&&std::abs(primary.lfHeight)>std::abs(help.lfHeight));
                        if(scenario==0 && testDpi==96 && f==6&&showHints)check("Lab HUD screenshot saved",saveHud(session,L"picker-hud-lab.png"));
                    }
                }
                display.dpi=originalDpi;session.settings.format=0;session.settings.showHints=1;session.redraw();UpdateWindow(display.window);
                const int expandedHeight=session.lastHud.height;
                session.applyPreference(302);UpdateWindow(display.window);
                check("hide hints keeps coordinates and value only and shrinks HUD",session.hudLines(display).size()==2&&session.lastHud.height<expandedHeight&&session.lastHudTextFits);
                check("hide hints preference persists",!PickerPreferences::load(session.settingsPath).showHints);
                check("compact HUD screenshot saved",saveHud(session,L"picker-hud-compact-"+std::to_wstring(scenario)+L".png"));
                HMENU options=session.preferenceMenu();wchar_t label[128]{};GetMenuStringW(options,302,label,128,MF_BYCOMMAND);
                check("F2 uses requested picker-specific label and unchecked state",std::wstring_view(label)==trW(L"显示取色快捷键说明",L"Show color picker shortcuts",L"顯示取色快捷鍵說明")&&!(GetMenuState(options,302,MF_BYCOMMAND)&MF_CHECKED));DestroyMenu(options);
                SendMessageW(overlay,WM_KEYDOWN,VK_RIGHT,0);SendMessageW(overlay,WM_KEYDOWN,VK_LEFT,0);
                check("hidden help keeps keyboard pixel movement",session.point.x==start.x&&session.point.y==start.y);
                SendMessageW(overlay,WM_KEYDOWN,VK_SHIFT,0);check("hidden help keeps format switching",session.settings.format==1);for(int i=1;i<PickerCore::FORMAT_COUNT;++i)SendMessageW(overlay,WM_KEYDOWN,VK_SHIFT,0);
                SendMessageW(overlay,WM_KEYDOWN,'P',0);SendMessageW(overlay,WM_KEYDOWN,'P',0);check("hidden help keeps coordinate toggle",session.settings.relative==0);
                SendMessageW(overlay,WM_KEYDOWN,'C',0);check("hidden help keeps copy without closing",!session.closing&&clipboardText(overlay)==std::optional<std::wstring>(L"#12ABEF"));
                session.status=L"test status";session.redraw();UpdateWindow(display.window);
                check("hidden help never hides operation status",session.hudLines(display).size()==3&&session.hudLines(display).back()==session.status);
                session.status.clear();session.applyPreference(302);UpdateWindow(display.window);
                check("hints can be restored and preference persists",session.settings.showHints&&PickerPreferences::load(session.settingsPath).showHints);
                check("production HUD screenshot saved for language/theme",saveHud(session,L"picker-hud-"+std::to_wstring(scenario)+L".png"));
                for(int mode=1;mode<=PickerCore::FORMAT_COUNT;++mode) {
                    SendMessageW(overlay,WM_KEYDOWN,VK_SHIFT,0);
                    check("Shift cycles seven formats",session.settings.format==mode%PickerCore::FORMAT_COUNT);
                    SendMessageW(overlay,WM_KEYDOWN,VK_SHIFT,1LL<<30);
                    check("held Shift does not repeatedly change format",session.settings.format==mode%PickerCore::FORMAT_COUNT);
                    SendMessageW(overlay,WM_KEYDOWN,'C',0);
                    const auto copied=clipboardText(overlay);
                    check("each of seven formats copies its actual selected color",copied && *copied==PickerCore::format(*original,static_cast<PickerCore::Format>(session.settings.format)));
                }
                SendMessageW(overlay,WM_KEYDOWN,'P',0);
                check("P changes coordinate display not sample",session.settings.relative==1 && session.pixel(start.x,start.y)==original);
                check("preferences persisted independently",PickerPreferences::load(session.settingsPath)==session.settings);
                SendMessageW(overlay,WM_KEYDOWN,'C',0);
                check("C copies without ending session",!session.closing && session.status==trW(L"已复制 · 保持取色",L"Copied · continue sampling",L"已複製 · 保持取色"));
                const auto copied=clipboardText(overlay);
                if(!copied || *copied!=L"#12ABEF")log<<"clipboardRead="<<(copied?"unexpected text":"unavailable")<<" sequence="<<GetClipboardSequenceNumber()<<"\n";
                check("clipboard matches selected RGB formatted HEX",copied && *copied==L"#12ABEF");
                const DWORD sequence=GetClipboardSequenceNumber();
                if(scenario==0) {
                    SendMessageW(overlay,WM_KEYDOWN,VK_ESCAPE,0);
                    check("Escape cancels without additional clipboard write",session.closing && session.result==Cancelled && sequence==GetClipboardSequenceNumber());
                check("cancel and ordinary copies do not add history",colorData.count(false)==historyStart);
                } else if(scenario==1) {
                    SendMessageW(overlay,WM_RBUTTONDOWN,0,0);
                    check("right click cancels without additional clipboard write",session.closing && session.result==Cancelled && sequence==GetClipboardSequenceNumber());
                check("right cancel does not add history",colorData.count(false)==historyStart);
                } else {
                    SetWindowLongPtrW(surface,GWLP_USERDATA,RGB(220,40,90));InvalidateRect(surface,nullptr,FALSE);UpdateWindow(surface);
                    SendMessageW(overlay,WM_KEYDOWN,VK_F5,0);
                    check("F5 recaptures changed underlying desktop not HUD",session.pixel(start.x,start.y)==ColorValue::Rgb{220,40,90});
                    SendMessageW(overlay,WM_KEYDOWN,VK_RETURN,0);
                    check("Enter copies and exits",session.closing && session.result==Copied && session.accepted==0xDC285A);
                }
                log<<"monitors="<<session.monitors.size()<<" dpi="<<session.monitors[session.selected]->dpi<<"\n";
            });
            check("session completed expected message path",session.closing);
            session.finish();
            check("no overlay after session finish",FindWindowW(L"YeImageViewer.QuickScreenPicker",nullptr)==nullptr);
            MSG quit{};while(PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){}
        }
        SetWindowLongPtrW(surface,GWLP_USERDATA,RGB(18,171,239));InvalidateRect(surface,nullptr,FALSE);UpdateWindow(surface);DwmFlush();
        {
            ScreenPickerSession fine;fine.settingsPath=evidence.parent_path()/L"picker-test.ini";
            SetCursorPos(left+80,top+70);
            fine.run([&] {
                HWND overlay=fine.monitors.front()->window;
                fine.point={left+80,top+70};fine.redraw();
                SendMessageW(overlay,WM_KEYDOWN,VK_RIGHT,0);
                check("fine selection points to adjacent blue pixel",fine.pixel(fine.point.x,fine.point.y)==ColorValue::Rgb{0,0,255});
                SendMessageW(overlay,WM_LBUTTONDOWN,0,0);
                check("left confirmation preserves keyboard fine selection",fine.closing && fine.result==Copied && fine.accepted==0x0000FF);
            });
            fine.finish();
            check("fine-selection session releases overlay",FindWindowW(L"YeImageViewer.QuickScreenPicker",nullptr)==nullptr);
            MSG quit{};while(PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){}
        }
        for(bool panelMode:{false,true}) {
            ScreenPickerSession selection;selection.settingsPath=evidence.parent_path()/L"picker-test.ini";selection.returnToPanel=panelMode;selection.settings.showHints=0;
            SetCursorPos(left+40,top+40);
            selection.run([&]{
                const auto sequence=GetClipboardSequenceNumber();
                selection.point={left+40,top+40};
                const auto expected=selection.pixel(selection.point.x,selection.point.y);
                SendMessageW(selection.monitors.front()->window,WM_KEYDOWN,panelMode?VK_RETURN:'E',0);
                check("E/return-to-panel selects exact RGB without copying",selection.closing && selection.result==Selected && expected && CurrentColor::get()==*expected && sequence==GetClipboardSequenceNumber());
            });
            selection.finish();MSG quit{};while(PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){}
        }
        DestroyWindow(surface);SetCursorPos(previousCursor.x,previousCursor.y);
        if(previousForeground && IsWindow(previousForeground)) SetForegroundWindow(previousForeground);
        log<<"passed="<<passed<<" failed="<<failed<<"\n";
        return failed==0;
    }
    static void open(HWND owner,const std::filesystem::path& settingsPath,bool dark,bool returnToPanel=false) {
        std::lock_guard guard(lifecycle);
        if(running.exchange(true)) return;
        stopRequested=false;
        try {
            {std::lock_guard lock(requestMutex);pendingRequest=Request{owner,settingsPath,dark,returnToPanel};}
            if(!worker.joinable())worker=std::thread([] {
                MSG queue{}; PeekMessageW(&queue,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
                workerId=GetCurrentThreadId();
                while(true) {
                    std::optional<Request> request;
                    {std::unique_lock lock(requestMutex);requested.wait(lock,[]{return stopRequested.load()||pendingRequest.has_value();});
                        if(stopRequested)break;request=std::move(pendingRequest);pendingRequest.reset();}
                    try {
                        ScreenPickerSession session;session.owner=request->owner;session.settingsPath=request->path;
                        session.settings=PickerPreferences::load(request->path);session.dark=request->dark;session.returnToPanel=request->returnToPanel;
                        try{session.run();}catch(...){session.result=StartFailed;}
                        session.finish();
                        if(session.result==Copied&&PickerHandoff::wait(stopRequested)&&IsWindow(request->owner)){
                            CurrentColor::set(CurrentColor::unpack(session.accepted));PostMessageW(request->owner,RESULT_MESSAGE,Selected,session.accepted);
                        }
                    }catch(...){if(IsWindow(request->owner))PostMessageW(request->owner,RESULT_MESSAGE,StartFailed,0);}
                    // 结束消息不能流入下一会话。线程可休眠复用，但覆盖层/快照/钩子每次均释放。
                    MSG quit{};while(PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){}
                    running=false;
                }
                workerId=0;running=false;
            });
            requested.notify_one();
        } catch(...) {{std::lock_guard lock(requestMutex);pendingRequest.reset();}running=false; PostMessageW(owner,RESULT_MESSAGE,StartFailed,0);}
    }
    static void requestExit() {
        std::lock_guard guard(lifecycle);
        {std::lock_guard lock(requestMutex);stopRequested=true;}
        requested.notify_one();
        if(const auto id=workerId.load()) PostThreadMessageW(id,WM_QUIT,0,0);
        if(worker.joinable()) worker.join();
        {std::lock_guard lock(requestMutex);pendingRequest.reset();}
    }
};
