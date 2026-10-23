#pragma once
#include "ColorPanelModel.h"
#include "ScreenPickerSession.h"
#include "ContrastRatio.h"
#include "jarkUtils.h"
#include "SvgRenderer.h"
#include "../resource.h"
#include "ColorWorkbenchPanel.h"
#include "StartupTrace.h"
#include "ColorToolHost.h"
#include "ControlFeedback.h"
#include <thread>
#include <map>

// 原生颜色工作页：大圆盘、S/V滑杆、推荐色、七行编辑与逐行复制。
// 不加载浏览器运行时，不展示尚未实现的色卡/AI空页面。
class ScreenColorWindow {
    inline static std::string testDiagnostics;
    inline static constexpr std::array<int,7> edits{101,102,114,115,116,117,118};
    inline static constexpr std::array<int,7> copies{105,106,134,135,136,137,138};
    inline static constexpr std::array<PickerCore::Format,7> formats{PickerCore::Format::Hex,PickerCore::Format::Rgb,
        PickerCore::Format::Hsv,PickerCore::Format::Hsl,PickerCore::Format::Cmyk,PickerCore::Format::Hsi,PickerCore::Format::Lab};
    inline static constexpr std::array<const wchar_t*,7> names{L"HEX",L"RGB",L"HSV/HSB",L"HSL",L"CMYK",L"HSI",L"CIE-LAB"};
    enum {Sample=107,Status=109,Restore=121,Undo=122,Redo=123,Apply=124,Cancel=125,FavoriteCurrent=126,Original=127,Current=128,ColorTab=201,ValueTab=202,Chip=300,Recent=700};
    HWND window=nullptr;
    HFONT font=nullptr,boldFont=nullptr;
    HWND tooltip=nullptr;
    std::map<int,std::wstring> tooltips;
    HBRUSH cardBrush=nullptr,editBrush=nullptr,paperBrush=nullptr;
    bool dark=false,updating=false,selecting=false,testMode=false,testResult=false,standalone=false,startEditing=false,hostInitialized=true,offscreenTest=false;
    int dpi=96,density=96,scroll=0,tab=0,drag=0,hoverSurface=0;
    double hue=0;
    uint64_t observed=CurrentColor::packed.load(),originalPacked=observed;
    ColorPanel::History history{CurrentColor::get()};
    ColorPanel::Layout geometry;
    ColorWorkbenchPanel library;
    std::vector<ColorLibrary::Record> recent;
    ColorValue::Rgb editAnchor{};
    ColorValue::Rgb dragAnchor{};
    bool fieldEditing=false;
    std::filesystem::path settingsPath;
    cv::Mat diskCache,pickerIcon;
    int cachedValue=-1;
    int s(int value)const{return MulDiv(value,density,96);}
    SIZE fixedSize(UINT targetDpi)const{RECT bounds{0,0,MulDiv(ColorPanel::WINDOW_WIDTH,targetDpi,96),MulDiv(ColorPanel::WINDOW_HEIGHT,targetDpi,96)};AdjustWindowRectExForDpi(&bounds,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),targetDpi);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);return {(std::min)(bounds.right-bounds.left,monitor.rcWork.right-monitor.rcWork.left),(std::min)(bounds.bottom-bounds.top,monitor.rcWork.bottom-monitor.rcWork.top)};}
    static COLORREF rgb(ColorValue::Rgb c){return RGB(c.r,c.g,c.b);}
    COLORREF ink()const{return dark?RGB(230,234,242):RGB(35,43,57);}
    COLORREF card()const{return dark?RGB(36,39,44):RGB(255,255,255);}
    COLORREF paper()const{return dark?RGB(24,26,30):RGB(244,246,248);}
    RECT rect(PickerCore::Rect r)const{return {s(r.x),s(r.y),s(r.x+r.width),s(r.y+r.height)};}
    std::wstring text(int id)const{wchar_t value[256]{};GetDlgItemTextW(window,id,value,256);return value;}
    void status(const wchar_t* value){SetDlgItemTextW(window,Status,value);}
    void setTip(int id,std::wstring value){
        if(!tooltip)return;const bool exists=tooltips.contains(id);tooltips[id]=std::move(value);
        TOOLINFOW tool{sizeof(tool)};tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tool.hwnd=window;
        tool.uId=reinterpret_cast<UINT_PTR>(GetDlgItem(window,id));tool.lpszText=tooltips[id].data();
        SendMessageW(tooltip,exists?TTM_UPDATETIPTEXTW:TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));
    }
    static std::wstring localTime(const std::string& utc){
        SYSTEMTIME time{},local{};unsigned y=0,m=0,d=0,h=0,n=0,s=0;
        if((utc.size()!=20&&utc.size()!=24)||utc.back()!='Z'||utc[4]!='-'||utc[7]!='-'||utc[10]!='T'||utc[13]!=':'||utc[16]!=':'||
            (utc.size()==24&&(utc[19]!='.'||!isdigit(uint8_t(utc[20]))||!isdigit(uint8_t(utc[21]))||!isdigit(uint8_t(utc[22])))))return trW(L"时间未知",L"Time unknown",L"時間未知");
        if(sscanf_s(utc.c_str(),"%u-%u-%uT%u:%u:%u",&y,&m,&d,&h,&n,&s)!=6)return trW(L"时间未知",L"Time unknown",L"時間未知");
        time.wYear=WORD(y);time.wMonth=WORD(m);time.wDay=WORD(d);time.wHour=WORD(h);time.wMinute=WORD(n);time.wSecond=WORD(s);
        FILETIME checked{};if(!SystemTimeToFileTime(&time,&checked)||!SystemTimeToTzSpecificLocalTime(nullptr,&time,&local))return trW(L"时间未知",L"Time unknown",L"時間未知");
        wchar_t output[40]{};swprintf_s(output,L"%04u-%02u-%02u %02u:%02u:%02u",local.wYear,local.wMonth,local.wDay,local.wHour,local.wMinute,local.wSecond);return output;
    }
    bool valid()const{for(int i=0;i<7;++i)if(!CurrentColor::parse(text(edits[i]),formats[i]))return false;return true;}
    void publish(){if(CurrentColor::get()!=history.current())CurrentColor::set(history.current());observed=CurrentColor::packed.load();}
    void refresh(int keep=0){
        updating=true;
        for(int i=0;i<7;++i){if(edits[i]!=keep)SetDlgItemTextW(window,edits[i],CurrentColor::field(history.current(),formats[i]).c_str());
            EnableWindow(GetDlgItem(window,copies[i]),CurrentColor::parse(text(edits[i]),formats[i]).has_value());}
        for(int i=0;i<8;++i)SetDlgItemTextW(window,Chip+i,ColorValue::hex(ColorPanel::suggested(history.current(),i)).c_str());
        EnableWindow(GetDlgItem(window,Undo),history.canUndo());EnableWindow(GetDlgItem(window,Redo),history.canRedo());
        updating=false;publish();InvalidateRect(window,nullptr,FALSE);InvalidateRect(GetDlgItem(window,Sample),nullptr,FALSE);InvalidateRect(GetDlgItem(window,FavoriteCurrent),nullptr,FALSE);
        for(int id:{Original,Current})InvalidateRect(GetDlgItem(window,id),nullptr,FALSE);
        setTip(Original,ColorValue::hex(history.source())+trW(L"\n点击恢复原色",L"\nClick to restore original",L"\n點擊還原原色"));
        setTip(Current,ColorValue::hex(history.current())+trW(L"\n点击复制HEX",L"\nClick to copy HEX",L"\n點擊複製HEX"));
    }
    void adopt(){library.parent=0;history.reset(CurrentColor::get());originalPacked=observed=CurrentColor::packed.load();hue=PickerCore::hsv(history.current()).h;refresh();}
    void loadRecent(){try{ColorLibrary::Query q;q.limit=42;recent=ColorLibrary::Store(ColorLibrary::defaultPath()).list(false,q);}catch(...){recent.clear();status(trW(L"历史无法读取，原数据保留",L"History unavailable; data preserved",L"歷史無法讀取，原資料保留"));}for(int i=0;i<42;++i){const HWND h=GetDlgItem(window,Recent+i);if(h){ShowWindow(h,library.page()==0&&i<int(recent.size())?SW_SHOW:SW_HIDE);if(i<int(recent.size())){SetWindowTextW(h,ColorValue::hex(recent[i].color).c_str());setTip(Recent+i,L"HEX: "+ColorValue::hex(recent[i].color)+L"\n"+(recent[i].source=="screen"?std::wstring(trW(L"取色时间：",L"Captured: ",L"取色時間：")):std::wstring(trW(L"记录时间：",L"Recorded: ",L"記錄時間：")))+localTime(recent[i].utc));}InvalidateRect(h,nullptr,TRUE);}}}
    void record(){try{library.parent=ColorLibrary::Store(ColorLibrary::defaultPath()).record(history.current(),"editor",ColorLibrary::eventKey(),library.parent);loadRecent();}catch(...){status(trW(L"历史保存失败，当前色仍可复制",L"History save failed; color still copyable",L"歷史儲存失敗，目前色彩仍可複製"));}}
    void change(ColorValue::Rgb color,bool committed=false){const auto before=history.current();history.change(color);hue=PickerCore::hsv(color).h;refresh();if(committed&&color!=before)record();}
    void finishField(){if(fieldEditing){fieldEditing=false;if(valid()&&history.current()!=editAnchor){finish();record();}}}
    void finish(){history.finish();EnableWindow(GetDlgItem(window,Undo),history.canUndo());EnableWindow(GetDlgItem(window,Redo),history.canRedo());}
    void copy(int row){
        if(!CurrentColor::parse(text(edits[row]),formats[row]))return;
        bool okay=false;for(int i=0;i<8&&!okay;++i){okay=jarkUtils::copyToClipboard(PickerCore::format(history.current(),formats[row]));if(!okay)Sleep(10);}
        status(okay?trW(L"已复制",L"Copied",L"已複製"):trW(L"复制失败，请重试",L"Copy failed; retry",L"複製失敗，請重試"));
        if(okay&&!settingsPath.empty()){auto prefs=PickerPreferences::load(settingsPath);prefs.format=int(formats[row]);
            if(!PickerPreferences::save(settingsPath,prefs))status(trW(L"已复制，格式偏好未能保存",L"Copied; preference not saved",L"已複製，格式偏好未能儲存"));}
        if(okay)try{ColorLibrary::Store store(ColorLibrary::defaultPath());if(store.preferences().recordCopies){store.record(history.current(),"editor",ColorLibrary::eventKey());loadRecent();}}catch(...){status(trW(L"已复制，但历史未保存",L"Copied; history not saved",L"已複製，但歷史未儲存"));}
    }
    void control(const wchar_t* cls,const wchar_t* label,DWORD style,int id){
        const bool edit=std::wstring_view(cls)==L"EDIT";
        const HWND child=CreateWindowExW(0,cls,label,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|style|(edit?WS_BORDER:0),0,0,1,1,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        if(edit){SendMessageW(child,EM_SETLIMITTEXT,128,0);ImmAssociateContextEx(child,nullptr,0);}
    }
    void createControls(){
        updating=true;
        for(int i=0;i<7;++i){control(L"EDIT",L"",ES_AUTOHSCROLL,edits[i]);control(L"BUTTON",trW(L"复制",L"Copy",L"複製"),0,copies[i]);}
        // 保留原生按钮名称供辅助技术读取；外观仅画吸管，不画文字。
        control(L"BUTTON",trW(L"取色",L"Pick color",L"取色"),BS_OWNERDRAW,Sample);
        control(L"BUTTON",trW(L"收藏颜色",L"Favorite color",L"收藏色彩"),BS_OWNERDRAW,FavoriteCurrent);
        tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,
            CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,400);setTip(Sample,trW(L"取色",L"Pick color",L"取色"));
        control(L"BUTTON",trW(L"原色",L"Original",L"原色"),BS_OWNERDRAW,Original);
        control(L"BUTTON",trW(L"当前色",L"Current",L"目前色"),BS_OWNERDRAW,Current);
        control(L"STATIC",L"",0,Status);
        control(L"BUTTON",trW(L"恢复原色",L"Restore",L"還原原色"),0,Restore);
        control(L"BUTTON",trW(L"撤销",L"Undo",L"復原"),0,Undo);control(L"BUTTON",trW(L"重做",L"Redo",L"重做"),0,Redo);
        control(L"BUTTON",trW(L"选色",L"Color",L"選色"),0,ColorTab);control(L"BUTTON",trW(L"数值",L"Values",L"數值"),0,ValueTab);
        for(int i=0;i<8;++i)control(L"BUTTON",L"",BS_OWNERDRAW,Chip+i);
        for(int i=0;i<42;++i)control(L"BUTTON",L"",BS_OWNERDRAW,Recent+i);
        library.create(window,testMode||startEditing);DragAcceptFiles(window,TRUE);updating=false;theme();position();refresh();loadRecent();
    }
    void theme(){
        dark=GlobalVar::isCurrentUIDarkMode;
        if(cardBrush)DeleteObject(cardBrush);if(editBrush)DeleteObject(editBrush);if(paperBrush)DeleteObject(paperBrush);
        cardBrush=CreateSolidBrush(card());editBrush=CreateSolidBrush(dark?RGB(46,49,55):RGB(255,255,255));
        paperBrush=CreateSolidBrush(paper());
        const BOOL useDark=dark;DwmSetWindowAttribute(window,20,&useDark,sizeof(useDark));
        EnumChildWindows(window,[](HWND child,LPARAM data)->BOOL{auto* self=reinterpret_cast<ScreenColorWindow*>(data);wchar_t cls[32]{};GetClassNameW(child,cls,32);
            if(_wcsicmp(cls,L"BUTTON")==0){const auto style=GetWindowLongPtrW(child,GWL_STYLE),type=style&BS_TYPEMASK;
                if(type==BS_PUSHBUTTON||type==BS_DEFPUSHBUTTON)SetWindowLongPtrW(child,GWL_STYLE,(style&~BS_TYPEMASK)|BS_OWNERDRAW);
                ControlFeedback::attach(child);SetWindowTheme(child,L"",L"");InvalidateRect(child,nullptr,TRUE);
            }else if(_wcsicmp(cls,L"COMBOBOX")==0){SetWindowTheme(child,L"",L"");SetWindowSubclass(child,comboProcedure,1,reinterpret_cast<DWORD_PTR>(self));InvalidateRect(child,nullptr,TRUE);}
            else if(_wcsicmp(cls,L"EDIT")==0){SetWindowTheme(child,L"",L"");SetWindowSubclass(child,editProcedure,1,reinterpret_cast<DWORD_PTR>(self));InvalidateRect(child,nullptr,TRUE);}
            return TRUE;},reinterpret_cast<LPARAM>(this));library.theme(dark);
        if(const HWND button=GetDlgItem(window,Sample))InvalidateRect(button,nullptr,FALSE);
    }
    static LRESULT CALLBACK editProcedure(HWND child,UINT message,WPARAM key,LPARAM param,UINT_PTR id,DWORD_PTR data){
        const auto result=DefSubclassProc(child,message,key,param);if(message==WM_NCDESTROY){RemoveWindowSubclass(child,editProcedure,id);return result;}
        if(message==WM_PAINT&&GetWindowTextLengthW(child)==0){wchar_t cue[160]{};if(SendMessageW(child,EM_GETCUEBANNER,reinterpret_cast<WPARAM>(cue),160)&&*cue){
            auto* self=reinterpret_cast<ScreenColorWindow*>(data);const bool caret=GetFocus()==child&&HideCaret(child);const HDC dc=GetDC(child);RECT r{};SendMessageW(child,EM_GETRECT,0,reinterpret_cast<LPARAM>(&r));FillRect(dc,&r,self->editBrush);const auto oldFont=SelectObject(dc,self->font?self->font:GetStockObject(DEFAULT_GUI_FONT));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,self->dark?RGB(162,174,192):RGB(100,110,126));DrawTextW(dc,cue,-1,&r,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);SelectObject(dc,oldFont);ReleaseDC(child,dc);if(caret)ShowCaret(child);
        }}return result;
    }
    static LRESULT CALLBACK comboProcedure(HWND child,UINT message,WPARAM key,LPARAM param,UINT_PTR id,DWORD_PTR data){
        const auto result=DefSubclassProc(child,message,key,param);if(message==WM_NCDESTROY){RemoveWindowSubclass(child,comboProcedure,id);return result;}
        if(message==WM_PAINT){auto* self=reinterpret_cast<ScreenColorWindow*>(data);const HDC dc=GetDC(child);RECT r{};GetClientRect(child,&r);const auto brush=CreateSolidBrush(self->dark?RGB(46,49,55):RGB(255,255,255));FillRect(dc,&r,brush);DeleteObject(brush);
            const auto pen=CreatePen(PS_SOLID,1,self->dark?RGB(96,104,118):RGB(175,184,196));const auto oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));Rectangle(dc,r.left,r.top,r.right,r.bottom);SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(pen);
            const auto oldFont=SelectObject(dc,self->font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,self->ink());wchar_t text[256]{};GetWindowTextW(child,text,256);auto label=r;label.left+=self->s(5);label.right-=self->s(20);DrawTextW(dc,text,-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
            const int x=r.right-self->s(10),y=(r.top+r.bottom)/2;POINT triangle[]{{x-self->s(3),y-self->s(2)},{x+self->s(3),y-self->s(2)},{x,y+self->s(2)}};const auto arrow=CreateSolidBrush(self->ink());const auto previous=SelectObject(dc,arrow),previousPen=SelectObject(dc,GetStockObject(NULL_PEN));Polygon(dc,triangle,3);SelectObject(dc,previous);SelectObject(dc,previousPen);DeleteObject(arrow);
            if(GetFocus()==child){InflateRect(&label,-1,-self->s(3));DrawFocusRect(dc,&label);}SelectObject(dc,oldFont);ReleaseDC(child,dc);}
        return result;
    }
    void drawButton(const DRAWITEMSTRUCT& item){
        const bool active=library.activeNavigation(item.CtlID),pressed=item.itemState&ODS_SELECTED,disabled=item.itemState&ODS_DISABLED;
        const bool hovered=!disabled&&ControlFeedback::hot(item.hwndItem);
        const bool swatch=item.CtlID==Original||item.CtlID==Current;
        const auto color=item.CtlID==Original?history.source():history.current();
        const COLORREF background=swatch?rgb(color):ControlFeedback::background(dark,active,hovered,pressed,disabled);
        const auto brush=CreateSolidBrush(background);FillRect(item.hDC,&item.rcItem,brush);DeleteObject(brush);const auto border=CreateSolidBrush(hovered||(!disabled&&pressed)?ControlFeedback::accent(dark):dark?RGB(81,89,103):RGB(191,201,213));FrameRect(item.hDC,&item.rcItem,border);DeleteObject(border);
        if(swatch&&!disabled&&(hovered||pressed))ControlFeedback::outline(item.hDC,item.rcItem,pressed,s(1));
        if(active){RECT bar=item.rcItem;bar.right=bar.left+s(3);const auto b=CreateSolidBrush(dark?RGB(118,191,249):RGB(0,95,175));FillRect(item.hDC,&bar,b);DeleteObject(b);}
        SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,swatch?(ContrastRatio::between(0xFF000000,CurrentColor::pack(color))>=4.5?RGB(0,0,0):RGB(255,255,255)):disabled?(dark?RGB(123,130,143):RGB(139,147,157)):ink());const auto oldFont=SelectObject(item.hDC,active&&boldFont?boldFont:font);auto area=item.rcItem;InflateRect(&area,-s(3),-s(2));
        if(std::find(copies.begin(),copies.end(),int(item.CtlID))!=copies.end()){
            const int x=(area.left+area.right)/2,y=(area.top+area.bottom)/2;const auto pen=CreatePen(PS_SOLID,std::max(1,s(1)),GetTextColor(item.hDC));const auto oldPen=SelectObject(item.hDC,pen),oldBrush=SelectObject(item.hDC,GetStockObject(HOLLOW_BRUSH));Rectangle(item.hDC,x-s(5),y-s(6),x+s(3),y+s(4));Rectangle(item.hDC,x-s(2),y-s(3),x+s(6),y+s(7));SelectObject(item.hDC,oldPen);SelectObject(item.hDC,oldBrush);DeleteObject(pen);
        }else{wchar_t text[160]{};GetWindowTextW(item.hwndItem,text,160);DrawTextW(item.hDC,text,-1,&area,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);}
        if(item.itemState&ODS_FOCUS)DrawFocusRect(item.hDC,&area);SelectObject(item.hDC,oldFont);
    }
    void position(){
        hoverSurface=0;
        RECT client{};GetClientRect(window,&client);
        density=(std::max)(1,(std::min)({dpi,int(client.right*96/ColorPanel::WINDOW_WIDTH),int(client.bottom*96/ColorPanel::WINDOW_HEIGHT)}));
        geometry=ColorPanel::compactLayout();scroll=0;
        const HFONT previous=font;font=CreateFontW(-s(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Segoe UI");
        if(boldFont)DeleteObject(boldFont);boldFont=CreateFontW(-s(12),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Segoe UI");
        const auto place=[&](int id,PickerCore::Rect box,bool show=true){const auto r=rect(box);const HWND child=GetDlgItem(window,id);
            MoveWindow(child,r.left,r.top,r.right-r.left,r.bottom-r.top,TRUE);ShowWindow(child,show?SW_SHOW:SW_HIDE);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);};
        const bool colorPage=library.page()==0,left=colorPage&&(!geometry.narrow||tab==0),right=colorPage&&(!geometry.narrow||tab==1);
        for(int i=0;i<7;++i){auto field=geometry.fields[i],button=geometry.copies[i];
            place(edits[i],field,right);place(copies[i],button,right);}
        for(int i=0;i<8;++i)place(Chip+i,geometry.suggestions[i],right);
        const auto& l=geometry.left;const auto& c=geometry.card;
        place(Sample,{l.x+8,l.y+8,32,32},left);place(FavoriteCurrent,{l.x+l.width-40,l.y+8,32,32},left);
        const int baseY=c.y+c.height-44;
        // 即时联动不需要额外应用/取消行；恢复与撤销在两个窄屏Tab中始终可访问。
        place(Restore,{c.x+12,baseY+10,86,24},false);place(Undo,{c.x+12,baseY+10,62,24},colorPage);place(Redo,{c.x+82,baseY+10,62,24},colorPage);
        place(Original,geometry.original,left);place(Current,geometry.current,left);
        place(Status,geometry.status,right);
        for(int i=0;i<42;++i)place(Recent+i,ColorPanel::recentSwatch(geometry,i),colorPage&&i<int(recent.size()));
        place(ColorTab,{ColorWorkbenchPanel::NAV_WIDTH+16,12,90,32},colorPage&&geometry.narrow);place(ValueTab,{ColorWorkbenchPanel::NAV_WIDTH+114,12,90,32},colorPage&&geometry.narrow);
        library.layout(ColorPanel::WINDOW_WIDTH,ColorPanel::WINDOW_HEIGHT,density,font);
        if(previous)DeleteObject(previous);
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    }
    void scrollTo(int value){scroll=value;position();}
    void sample(){if(ScreenPickerSession::isRunning())return;finish();selecting=true;ShowWindow(window,SW_HIDE);DwmFlush();
        ScreenPickerSession::open(window,settingsPath,GlobalVar::isCurrentUIDarkMode,true);}
    void dragAt(int x,int y,bool start){
        x=MulDiv(x,96,density);y=MulDiv(y,96,density);const auto hsv=PickerCore::hsv(history.current());
        if(start){drag=ColorPanel::diskPoint(geometry,x,y,hsv.v)?1:geometry.saturation.contains(x,y)?2:geometry.value.contains(x,y)?3:0;if(drag){finish();dragAnchor=history.current();history.begin();SetCapture(window);}}
        if(!drag)return;
        if(drag==1){const double radius=geometry.disk.width/2.,dx=x-geometry.disk.x-radius,dy=y-geometry.disk.y-radius;
            hue=std::fmod(std::atan2(dy,dx)*180/std::numbers::pi+450,360.);history.preview(CurrentColor::fromHsv(hue,std::clamp(std::hypot(dx,dy)/radius,0.,1.),hsv.v));}
        else {const auto& r=drag==2?geometry.saturation:geometry.value;const double t=std::clamp(double(x-r.x)/r.width,0.,1.);
            history.preview(CurrentColor::fromHsv(hue,drag==2?t:hsv.s,drag==3?t:hsv.v));}
        refresh();
    }
    void label(HDC dc,PickerCore::Rect box,const std::wstring& value,COLORREF color,UINT align=DT_LEFT){auto r=rect(box);SetTextColor(dc,color);DrawTextW(dc,value.c_str(),-1,&r,align|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);}
    void fill(HDC dc,PickerCore::Rect box,COLORREF color,int radius=0){auto r=rect(box);const auto b=CreateSolidBrush(color);const auto oldB=SelectObject(dc,b),oldP=SelectObject(dc,GetStockObject(NULL_PEN));
        if(radius)RoundRect(dc,r.left,r.top,r.right,r.bottom,s(radius),s(radius));else FillRect(dc,&r,b);SelectObject(dc,oldB);SelectObject(dc,oldP);DeleteObject(b);}
    void marker(HDC dc,int x,int y,int surface){const bool emphasized=hoverSurface==surface||drag==surface;const auto oldB=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));for(int radius:{7,5}){const int actual=radius+(emphasized?2:0);const auto pen=CreatePen(PS_SOLID,s(2),radius==7?RGB(0,0,0):RGB(255,255,255));const auto oldP=SelectObject(dc,pen);Ellipse(dc,s(x-actual),s(y-actual),s(x+actual)+1,s(y+actual)+1);SelectObject(dc,oldP);DeleteObject(pen);}SelectObject(dc,oldB);}
    void hoverAt(int x,int y){
        x=MulDiv(x,96,density);y=MulDiv(y,96,density);
        const int next=library.page()!=0?0:ColorPanel::diskPoint(geometry,x,y,1.)?1:geometry.saturation.contains(x,y)?2:geometry.value.contains(x,y)?3:0;
        if(next!=hoverSurface){hoverSurface=next;InvalidateRect(window,nullptr,FALSE);}
    }
    static void bitmap(HDC dc,const cv::Mat& mat,int x,int y){BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),mat.cols,-mat.rows,1,32,BI_RGB};SetDIBitsToDevice(dc,x,y,mat.cols,mat.rows,0,0,0,mat.rows,mat.data,&info,DIB_RGB_COLORS);}
    void drawPicker(const DRAWITEMSTRUCT& item){
        const int side=item.rcItem.right-item.rcItem.left,iconSize=s(20);
        if(pickerIcon.cols!=iconSize){
            const auto resource=jarkUtils::GetResource(IDR_SVG_PICKER_ICON,L"SVG");
            const auto svg=SvgRenderer::create({static_cast<const uint8_t*>(resource.ptr),resource.size});
            if(svg){const auto rendered=svg->renderToBitmap(iconSize,iconSize);
                if(!rendered.empty())pickerIcon=cv::Mat(rendered.height,rendered.width,CV_8UC4,const_cast<uint8_t*>(rendered.bgra.data())).clone();}
        }
        const auto c=history.current();cv::Mat image(side,side,CV_8UC4,cv::Scalar(c.b,c.g,c.r,255));
        const bool hovered=ControlFeedback::hot(item.hwndItem),pressed=item.itemState&ODS_SELECTED;
        const int shade=dark?(pressed?100:hovered?80:55):(pressed?190:hovered?218:239);
        cv::circle(image,{side/2,side/2},s(15),cv::Scalar(shade,shade,shade,255),cv::FILLED,cv::LINE_AA);
        if(item.CtlID==FavoriteCurrent){std::vector<cv::Point> points;for(int i=0;i<10;++i){const double angle=(-90+i*36)*std::numbers::pi/180.,radius=s(i%2?5:10);points.emplace_back(side/2+int(radius*cos(angle)),side/2+int(radius*sin(angle)));}const int value=dark?235:35;cv::fillPoly(image,std::vector<std::vector<cv::Point>>{points},cv::Scalar(value,value,value,255),cv::LINE_AA);}
        else if(!pickerIcon.empty()){
            cv::Mat icon=pickerIcon.clone();const int value=dark?235:35;
            for(int y=0;y<icon.rows;++y)for(int x=0;x<icon.cols;++x){auto& p=icon.at<cv::Vec4b>(y,x);p[0]=p[1]=p[2]=static_cast<uint8_t>(value);}
            jarkUtils::overlayImg(image,icon,(side-icon.cols)/2,(side-icon.rows)/2);
        }
        bitmap(item.hDC,image,item.rcItem.left,item.rcItem.top);
        if(item.itemState&ODS_FOCUS)DrawFocusRect(item.hDC,&item.rcItem);
    }
    void paint(HDC dc){
        RECT client{};GetClientRect(window,&client);auto bg=CreateSolidBrush(paper());FillRect(dc,&client,bg);DeleteObject(bg);
        const auto oldFont=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);
        const bool left=library.page()==0&&(!geometry.narrow||tab==0),right=library.page()==0&&(!geometry.narrow||tab==1);
        if(left){
            fill(dc,geometry.left,rgb(history.current()),18);
            const auto hsv=PickerCore::hsv(history.current());const auto disk=rect(geometry.disk);const int diameter=disk.right-disk.left;
            const int value=std::lround(hsv.v*255);
            if(diskCache.cols!=diameter||cachedValue!=value){diskCache.create(diameter,diameter,CV_8UC4);cachedValue=value;
                const double radius=diameter/2.;for(int y=0;y<diameter;++y)for(int x=0;x<diameter;++x){const double dx=x-radius,dy=y-radius;
                    auto c=CurrentColor::fromHsv(std::atan2(dy,dx)*180/std::numbers::pi+90,std::clamp(std::hypot(dx,dy)/radius,0.,1.),hsv.v);diskCache.at<cv::Vec4b>(y,x)={c.b,c.g,c.r,255};}}
            const int saved=SaveDC(dc);const auto region=CreateEllipticRgn(disk.left,disk.top,disk.right,disk.bottom);SelectClipRgn(dc,region);bitmap(dc,diskCache,disk.left,disk.top);RestoreDC(dc,saved);DeleteObject(region);
            const double angle=(hue-90)*std::numbers::pi/180,radius=geometry.disk.width/2.;
            marker(dc,int(geometry.disk.x+radius+radius*hsv.s*std::cos(angle)),int(geometry.disk.y+radius+radius*hsv.s*std::sin(angle)),1);
            const COLORREF contrast=ContrastRatio::between(0xFF000000,CurrentColor::pack(history.current()))>=4.5?RGB(0,0,0):RGB(255,255,255);
            for(int slider=0;slider<2;++slider){const auto& box=slider?geometry.value:geometry.saturation;const auto r=rect(box);cv::Mat gradient(std::max(1,int(r.bottom-r.top)),std::max(1,int(r.right-r.left)),CV_8UC4);
                for(int x=0;x<gradient.cols;++x){const double t=double(x)/std::max(1,gradient.cols-1);const auto c=CurrentColor::fromHsv(hue,slider?hsv.s:t,slider?t:hsv.v);gradient.col(x).setTo(cv::Scalar(c.b,c.g,c.r,255));}
                const int saved=SaveDC(dc);const auto region=CreateRoundRectRgn(r.left,r.top,r.right+1,r.bottom+1,s(22),s(22));SelectClipRgn(dc,region);bitmap(dc,gradient,r.left,r.top);RestoreDC(dc,saved);DeleteObject(region);
                marker(dc,box.x+int(box.width*(slider?hsv.v:hsv.s)),box.y+box.height/2,slider?3:2);
                label(dc,{box.x,box.y-24,box.width,22},std::wstring(slider?L"V ":L"S ")+PickerCore::decimal2((slider?hsv.v:hsv.s)*100)+L"%",contrast);}
            fill(dc,geometry.original,rgb(history.source()),8);fill(dc,geometry.current,rgb(history.current()),8);
            const auto oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));const auto pen=CreatePen(PS_SOLID,s(1),contrast);const auto oldPen=SelectObject(dc,pen);
            for(const auto& box:{geometry.original,geometry.current}){const auto r=rect(box);RoundRect(dc,r.left,r.top,r.right,r.bottom,s(8),s(8));}SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
            const auto sourceInk=ContrastRatio::between(0xFF000000,CurrentColor::pack(history.source()))>=4.5?RGB(0,0,0):RGB(255,255,255);
            label(dc,geometry.original,trW(L"原色",L"Original",L"原色"),sourceInk,DT_CENTER);label(dc,geometry.current,trW(L"当前色",L"Current",L"目前色"),contrast,DT_CENTER);
        }
        if(right){
            fill(dc,geometry.card,card(),18);const auto& c=geometry.card;const int half=(c.width-40)/2;
            label(dc,{c.x+12,c.y+16,52,24},trW(L"互补色",L"Comp.",L"互補色"),ink());
            label(dc,{c.x+12,c.y+53,52,24},trW(L"类似色",L"Analog.",L"類似色"),ink());
            label(dc,{c.x+175,c.y+16,52,24},trW(L"对比色",L"Triad",L"對比色"),ink());
            label(dc,{c.x+175,c.y+53,52,24},trW(L"中差色",L"Tetrad",L"中差色"),ink());
            for(int i=0;i<7;++i)label(dc,{c.x+12,geometry.fields[i].y,64,24},names[i],ink());
            label(dc,{c.x+12,c.y+84,c.width-24,18},trW(L"CMYK 数学近似 · Lab D65",L"CMYK approximation · Lab D65",L"CMYK 數學近似 · Lab D65"),dark?RGB(172,181,195):RGB(92,101,116));
            if(recent.empty())label(dc,geometry.recent,trW(L"尚无颜色历史",L"No color history yet",L"尚無色彩歷史"),ink());
        }
        SelectObject(dc,oldFont);
    }
    static LRESULT CALLBACK proc(HWND hwnd,UINT message,WPARAM key,LPARAM param){
        auto* self=reinterpret_cast<ScreenColorWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<ScreenColorWindow*>(reinterpret_cast<CREATESTRUCTW*>(param)->lpCreateParams);self->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(hwnd,message,key,param);
        switch(message){
        case WM_COPYDATA: {
            const auto incoming=ColorToolHost::receive(reinterpret_cast<const COPYDATASTRUCT*>(param));if(!incoming)return FALSE;
            if(incoming->imageProvided)ColorCatalog::setCurrent(incoming->image);
            GlobalVar::settingParameter.UI_Mode=incoming->mode;GlobalVar::settingParameter.UI_LANG=incoming->language;GlobalVar::isCurrentUIDarkMode=incoming->dark;self->theme();
            if(incoming->operation==ColorToolHost::Operation::Adopt||
                (incoming->operation==ColorToolHost::Operation::Initialize&&!self->hostInitialized)) {
                self->hostInitialized=true;CurrentColor::set(incoming->color);self->library.command(410,0);self->adopt();
            }
            ShowWindow(hwnd,SW_RESTORE);SetForegroundWindow(hwnd);return TRUE;
        }
        case WM_GETMINMAXINFO:{auto* info=reinterpret_cast<MINMAXINFO*>(param);const auto size=self->fixedSize(self->dpi);info->ptMinTrackSize=info->ptMaxTrackSize={size.cx,size.cy};return 0;}
        case WM_WINDOWPOSCHANGING:if(GetDlgItem(hwnd,307)){auto* position=reinterpret_cast<WINDOWPOS*>(param);if(!(position->flags&SWP_NOSIZE)){const auto size=self->fixedSize(self->dpi);position->cx=size.cx;position->cy=size.cy;}}break;
        case WM_CREATE:if(self->standalone)SetPropW(hwnd,ColorToolHost::PROPERTY,reinterpret_cast<HANDLE>(ColorToolHost::identity()));self->dpi=GetDpiForWindow(hwnd);self->hue=PickerCore::hsv(self->history.current()).h;self->createControls();return 0;
        case WM_SIZE:if(GetDlgItem(hwnd,Chip+7))self->position();return 0;
        case WM_DPICHANGED:{self->dpi=HIWORD(key);const auto* r=reinterpret_cast<RECT*>(param);const auto size=self->fixedSize(self->dpi);SetWindowPos(hwnd,nullptr,r->left,r->top,size.cx,size.cy,SWP_NOZORDER);self->position();return 0;}
        case WM_SETTINGCHANGE:GlobalVar::isSystemDarkMode=jarkUtils::getSystemDarkMode();GlobalVar::isCurrentUIDarkMode=GlobalVar::settingParameter.UI_Mode==0?GlobalVar::isSystemDarkMode:GlobalVar::settingParameter.UI_Mode==2;self->theme();InvalidateRect(hwnd,nullptr,TRUE);return 0;
        case WM_APP+68:if(key)self->library.command(410,0);self->adopt();ShowWindow(hwnd,SW_SHOWNORMAL);SetForegroundWindow(hwnd);return 0;
        case WM_TIMER:if(key==71){self->library.timer();return 0;}if(key==72){self->library.imageTimer();return 0;}if(key==73){self->library.searchTimer();return 0;}if(!self->selecting&&self->observed!=CurrentColor::packed.load())self->adopt();return 0;
        case WM_DROPFILES:self->library.drop(reinterpret_cast<HDROP>(key));return 0;
        case ColorWorkbenchPanel::EDIT_MESSAGE:CurrentColor::packed=uint64_t(param);self->adopt();self->library.parent=static_cast<long long>(key);return 0;
        case ColorWorkbenchPanel::COLOR_MESSAGE:self->library.parent=0;self->change(CurrentColor::unpack(uint64_t(param)));return 0;
        case ColorFlowView::MESSAGE:self->library.flow(key,param);return 0;
        case WM_MOUSEWHEEL:self->library.wheel(GET_WHEEL_DELTA_WPARAM(key));return 0;
        case ScreenPickerSession::RESULT_MESSAGE:
            self->selecting=false;if(key==ScreenPickerSession::Selected||key==ScreenPickerSession::Copied){self->history.change(CurrentColor::get());self->hue=PickerCore::hsv(self->history.current()).h;self->refresh();}
            self->loadRecent();ShowWindow(hwnd,SW_SHOWNORMAL);SetForegroundWindow(hwnd);if(key==ScreenPickerSession::CaptureFailed||key==ScreenPickerSession::StartFailed)self->status(trW(L"无法读取当前桌面",L"Cannot capture this desktop",L"無法讀取目前桌面"));return 0;
        case WM_COMMAND:{const int id=LOWORD(key),notice=HIWORD(key);
            if(self->library.command(id,notice)){if(id==410)self->loadRecent();return 0;}
            if(!self->updating)for(int row=0;row<7;++row)if(id==edits[row]){
                if(notice==EN_SETFOCUS){self->finish();self->editAnchor=self->history.current();self->fieldEditing=true;self->history.begin();}
                if(notice==EN_KILLFOCUS){self->finishField();self->finish();}
                if(notice==EN_CHANGE){const auto value=CurrentColor::parse(self->text(id),formats[row]);if(value){self->history.preview(*value);self->hue=PickerCore::hsv(*value).h;self->refresh(id);self->status(L"");}
                    else{EnableWindow(GetDlgItem(hwnd,copies[row]),FALSE);EnableWindow(GetDlgItem(hwnd,Apply),FALSE);self->status(trW(L"数值无效，未应用到当前颜色",L"Invalid value; current color unchanged",L"數值無效，未套用到目前色彩"));}}
                return 0;}
            for(int row=0;row<7;++row)if(id==copies[row]){self->copy(row);return 0;}
            if(id>=Chip&&id<Chip+8){self->change(ColorPanel::suggested(self->history.current(),id-Chip));self->copy(0);return 0;}
            if(id>=Recent&&id<Recent+int(self->recent.size())){self->change(self->recent[id-Recent].color);self->copy(0);return 0;}
            if(id==FavoriteCurrent){try{ColorLibrary::Store(ColorLibrary::defaultPath()).ensureFavorite(self->history.current());self->status(trW(L"已收藏",L"Favorited",L"已收藏"));}catch(...){self->status(trW(L"收藏失败，未保存",L"Favorite failed; not saved",L"收藏失敗，未儲存"));}return 0;}
            if(id==Restore||id==Original){self->history.restore();self->hue=PickerCore::hsv(self->history.current()).h;self->refresh();}
            if(id==Current){self->copy(0);return 0;}
            if(id==Undo||id==Redo){if(id==Undo)self->history.undo();else self->history.redo();self->hue=PickerCore::hsv(self->history.current()).h;self->refresh();}
            if(id==Sample)self->sample();
            if(id==Apply&&self->valid()){self->finish();self->publish();DestroyWindow(hwnd);}
            if(id==Cancel&&self->library.canClose()){CurrentColor::packed=self->originalPacked;DestroyWindow(hwnd);}
            if(id==ColorTab||id==ValueTab){self->tab=id==ValueTab;self->position();}
            return 0;}
        case WM_CONTEXTMENU:{const int id=GetDlgCtrlID(reinterpret_cast<HWND>(key));if((id>=Chip&&id<Chip+8)||(id>=Recent&&id<Recent+int(self->recent.size()))){const auto c=id>=Recent?self->recent[id-Recent].color:ColorPanel::suggested(self->history.current(),id-Chip);try{ColorLibrary::Store(ColorLibrary::defaultPath()).ensureFavorite(c);self->status(trW(L"已收藏",L"Favorited",L"已收藏"));}catch(...){self->status(trW(L"收藏失败，未保存",L"Favorite failed; not saved",L"收藏失敗，未儲存"));}return 0;}break;}
        case WM_LBUTTONDOWN:if(self->library.page()==0)self->dragAt(GET_X_LPARAM(param),GET_Y_LPARAM(param),true);return 0;
        case WM_MOUSEMOVE:{self->hoverAt(GET_X_LPARAM(param),GET_Y_LPARAM(param));TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);if(self->drag)self->dragAt(GET_X_LPARAM(param),GET_Y_LPARAM(param),false);return 0;}
        case WM_MOUSELEAVE:self->hoverSurface=0;InvalidateRect(hwnd,nullptr,FALSE);return 0;
        case WM_LBUTTONUP:if(self->drag){self->dragAt(GET_X_LPARAM(param),GET_Y_LPARAM(param),false);const bool changed=self->history.current()!=self->dragAnchor;self->drag=0;self->finish();if(changed)self->record();if(GetCapture()==hwnd)ReleaseCapture();}return 0;
        case WM_CAPTURECHANGED:self->drag=0;self->finish();if(GetCapture()==hwnd)ReleaseCapture();return 0;
        case WM_VSCROLL:{SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(hwnd,SB_VERT,&info);int value=self->scroll;switch(LOWORD(key)){case SB_LINEUP:value-=24;break;case SB_LINEDOWN:value+=24;break;case SB_PAGEUP:value-=info.nPage;break;case SB_PAGEDOWN:value+=info.nPage;break;case SB_THUMBTRACK:value=info.nTrackPos;break;}self->scrollTo(value);return 0;}
        case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:{
            const bool library=GetDlgCtrlID(reinterpret_cast<HWND>(param))>=410,edit=message==WM_CTLCOLOREDIT,list=message==WM_CTLCOLORLISTBOX;
            SetTextColor(reinterpret_cast<HDC>(key),self->ink());SetBkColor(reinterpret_cast<HDC>(key),edit?(self->dark?RGB(46,49,55):RGB(255,255,255)):list?self->card():library?self->paper():self->card());
            return reinterpret_cast<LRESULT>(edit?self->editBrush:list?self->cardBrush:library?self->paperBrush:self->cardBrush);}
        case WM_DRAWITEM:{const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(param);if(self->library.draw(*item,self->dark,self->font))return TRUE;
            if(item->CtlID==Sample||item->CtlID==FavoriteCurrent){self->drawPicker(*item);return TRUE;}
            if((item->CtlID>=Chip&&item->CtlID<Chip+8)||(item->CtlID>=Recent&&item->CtlID<Recent+self->recent.size())){
                const auto color=item->CtlID>=Recent?self->recent[item->CtlID-Recent].color:ColorPanel::suggested(self->history.current(),item->CtlID-Chip);
                const auto b=CreateSolidBrush(rgb(color));FillRect(item->hDC,&item->rcItem,b);DeleteObject(b);
                const auto border=CreatePen(PS_SOLID,1,self->dark?RGB(120,125,130):RGB(180,185,190));const auto oldPen=SelectObject(item->hDC,border),oldBrush=SelectObject(item->hDC,GetStockObject(HOLLOW_BRUSH));Rectangle(item->hDC,item->rcItem.left,item->rcItem.top,item->rcItem.right,item->rcItem.bottom);SelectObject(item->hDC,oldPen);SelectObject(item->hDC,oldBrush);DeleteObject(border);
                if(!(item->itemState&ODS_DISABLED)&&(ControlFeedback::hot(item->hwndItem)||(item->itemState&ODS_SELECTED)))ControlFeedback::outline(item->hDC,item->rcItem,(item->itemState&ODS_SELECTED)!=0,self->s(1));
                if(item->itemState&ODS_FOCUS)DrawFocusRect(item->hDC,&item->rcItem);return TRUE;
            }
            if(item->CtlType==ODT_BUTTON){self->drawButton(*item);return TRUE;}break;}
        case WM_PAINT:{PAINTSTRUCT paint{};const auto dc=BeginPaint(hwnd,&paint);self->paint(dc);EndPaint(hwnd,&paint);return 0;}
        case WM_ERASEBKGND:return 1;
        case WM_CLOSE:if(!self->library.canClose())return 0;self->finishField();StartupTrace::mark("colorPanel.close");DestroyWindow(hwnd);return 0;
        case WM_DESTROY:StartupTrace::mark("colorPanel.destroy");if(self->selecting)ScreenPickerSession::requestExit();return 0;
        // WM_DESTROY之后子控件仍在销毁；原生控件的嵌套消息循环可能消费提前投递的退出消息。
        // 等到最后一个销毁阶段再发WM_QUIT，并清除实例句柄作为消息循环的独立终止条件。
        case WM_NCDESTROY:{self->window=nullptr;SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);const auto result=DefWindowProcW(hwnd,message,key,param);PostQuitMessage(0);StartupTrace::mark("colorPanel.quitPosted");return result;}
        }
        return DefWindowProcW(hwnd,message,key,param);
    }
    bool test(){
        ShowWindow(window,SW_SHOWNOACTIVATE);
        SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);UpdateWindow(window);
        bool okay=true;const auto check=[&](const char* name,bool value){testDiagnostics+=std::string(name)+"="+std::to_string(value)+",";okay&=value;};
        testDiagnostics+="initial-client="+std::to_string(geometry.width)+"x"+std::to_string(geometry.height)+",";
        check("seven-visible",std::all_of(edits.begin(),edits.end(),[&](int id){return IsWindowVisible(GetDlgItem(window,id));}));
        check("picker-icon",(GetWindowLongPtrW(GetDlgItem(window,Sample),GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW);
        check("picker-name",text(Sample)==trW(L"取色",L"Pick color",L"取色"));
        const auto checkWidths=[&]{
            const HDC dc=GetDC(window);const auto old=SelectObject(dc,font);
            for(const auto* value:{L"100%, 100%, 100%, 100%",L"359.99, 100%, 100%",L"32.297, 79.188, -107.86"}){
                SIZE size{};GetTextExtentPoint32W(dc,value,int(wcslen(value)),&size);RECT area{};SendDlgItemMessageW(window,116,EM_GETRECT,0,reinterpret_cast<LPARAM>(&area));
                check("compact-values-fit",size.cx<=area.right-area.left);
            }
            SelectObject(dc,old);ReleaseDC(window,dc);
        };checkWidths();
        UpdateWindow(GetDlgItem(window,Sample));
        check("picker-vector-loaded",!pickerIcon.empty());
        SetDlgItemTextW(window,101,L"#12ABEF");check("hex-rgb",history.current()==ColorValue::Rgb{18,171,239}&&text(102)==L"18, 171, 239");
        const HWND picker=GetDlgItem(window,Sample);UpdateWindow(picker);const HDC pickerDc=GetDC(picker);
        check("picker-background-follows-color",GetPixel(pickerDc,0,0)==RGB(18,171,239));ReleaseDC(picker,pickerDc);
        finish();const auto before=history.current();SetDlgItemTextW(window,102,L"999, 0, 0");check("invalid",history.current()==before&&!IsWindowEnabled(GetDlgItem(window,106)));
        const HWND field=GetDlgItem(window,102);SendMessageW(field,EM_SETSEL,1,3);const int initialDpi=dpi;
        for(int next:{192,288,96,initialDpi}){RECT r{60,60,60+MulDiv(1000,next,96),60+MulDiv(680,next,96)};SendMessageW(window,WM_DPICHANGED,MAKEWPARAM(next,next),reinterpret_cast<LPARAM>(&r));
            checkWidths();
            DWORD start=0,end=0;SendMessageW(field,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));check("dpi-draft",GetDlgItem(window,102)==field&&text(102)==L"999, 0, 0"&&start==1&&end==3&&!IsWindowEnabled(GetDlgItem(window,106)));}
        SendMessageW(window,WM_TIMER,1,0);check("timer-draft",text(102)==L"999, 0, 0");
        for(int row=0;row<7;++row){SetDlgItemTextW(window,edits[row],CurrentColor::field({18,171,239},formats[row]).c_str());finish();const auto c=history.current();check("seven-edit",std::abs(int(c.r)-18)<=3&&std::abs(int(c.g)-171)<=3&&std::abs(int(c.b)-239)<=3);}
        change({255,0,0});const auto anchor=history.current();const auto disk=geometry.disk;dragAt(s(disk.x+disk.width/2),s(disk.y+disk.height/2),true);dragAt(s(disk.x+disk.width/2),s(disk.y+disk.height-1),false);drag=0;finish();if(GetCapture()==window)ReleaseCapture();
        const auto result=history.current();history.undo();check("drag-undo",history.current()==anchor);history.redo();check("drag-redo",history.current()==result);refresh();
        try{
            ColorLibrary::Store store(ColorLibrary::defaultPath());const auto initial=store.count(false),favorites=store.count(true);
            change({17,34,51});check("preview-no-history",store.count(false)==initial);
            record();check("completed-record",store.count(false)==initial+1);
            SendMessageW(window,WM_COMMAND,FavoriteCurrent,0);check("favorite-independent",store.count(true)==favorites+1&&store.count(false)==initial+1);
            SendMessageW(window,WM_COMMAND,411,0);check("history-real-page",IsWindowVisible(GetDlgItem(window,424))&&!IsWindowVisible(GetDlgItem(window,101))&&SendDlgItemMessageW(window,424,ColorFlowView::QUERY,ColorFlowView::Count,0)>0);
            const auto sequence=GetClipboardSequenceNumber();library.flow(MAKEWPARAM(424,ColorFlowView::Copy),0);check("history-copy-no-new",store.count(false)==initial+1&&GetClipboardSequenceNumber()!=sequence);
            SendMessageW(window,WM_COMMAND,412,0);library.flow(MAKEWPARAM(424,ColorFlowView::Copy),favorites);library.command(431,0);SetDlgItemTextW(window,450,L"原生收藏测试");SetDlgItemTextW(window,451,L"回归 标签");SetDlgItemTextW(window,452,L"#A1B2C3");library.command(452,EN_KILLFOCUS);
            ColorLibrary::Query query;query.text=L"原生收藏测试";const auto edited=store.list(true,query);check("favorite-native-edit",!edited.empty()&&edited[0].color==ColorValue::Rgb{161,178,195});
            SendMessageW(window,WM_COMMAND,410,0);check("navigation-restores-editor",IsWindowVisible(GetDlgItem(window,101))&&!IsWindowVisible(GetDlgItem(window,424)));
        }catch(...){check("library-native",false);}
        for(bool useDark:{false,true}){const bool old=GlobalVar::isCurrentUIDarkMode;GlobalVar::isCurrentUIDarkMode=useDark;theme();check("theme",dark==useDark);
            if(useDark){const HWND favorite=GetDlgItem(window,FavoriteCurrent);InvalidateRect(favorite,nullptr,TRUE);UpdateWindow(favorite);RECT area{};GetClientRect(favorite,&area);const HDC dc=GetDC(favorite);const auto pixel=GetPixel(dc,area.right/2,area.bottom/2);ReleaseDC(favorite,dc);check("dark-favorite-icon-contrast",pixel!=CLR_INVALID&&GetRValue(pixel)>=180);}
            if(useDark){SendMessageW(window,WM_COMMAND,412,0);const HWND list=GetDlgItem(window,424);InvalidateRect(list,nullptr,TRUE);UpdateWindow(list);const HDC dc=GetDC(list);RECT bounds{};GetClientRect(list,&bounds);const auto pixel=GetPixel(dc,8,bounds.bottom-8);ReleaseDC(list,dc);check("dark-library-no-white-blank",pixel!=CLR_INVALID&&pixel!=RGB(255,255,255));SendMessageW(window,WM_COMMAND,410,0);}
            GlobalVar::isCurrentUIDarkMode=old;}
        SetWindowPos(window,nullptr,60,60,s(1000),s(720),SWP_NOZORDER);position();theme();change({18,171,239});
        ShowWindow(window,SW_SHOWNOACTIVATE);SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);UpdateWindow(window);GdiFlush();Sleep(80);
        POINT point{s(geometry.current.x+8),s(geometry.current.y+8)};ClientToScreen(window,&point);const HDC screen=GetDC(nullptr);const auto pixel=GetPixel(screen,point.x,point.y);ReleaseDC(nullptr,screen);check("known-swatch",pixel==RGB(18,171,239));
        return okay;
    }
    bool testOffscreen(){
        bool okay=true;int passed=0;const auto check=[&](const char* name,bool value){testDiagnostics+=std::string(value?"PASS ":"FAIL ")+name+"\n";okay&=value;if(value)++passed;};
        check("parent always hidden",!IsWindowVisible(window));
        history.reset({12,34,56});change({77,88,99});SendMessageW(window,WM_COMMAND,Original,0);
        check("original button restores current session source",history.current()==ColorValue::Rgb{12,34,56}&&CurrentColor::get()==history.current());
        check("original/current tooltips show real values",tooltips[Original].find(L"#0C2238")!=std::wstring::npos&&tooltips[Current].find(L"#0C2238")!=std::wstring::npos);
        check("duplicate restore button hidden",!(GetWindowLongPtrW(GetDlgItem(window,Restore),GWL_STYLE)&WS_VISIBLE));
        check("unknown time not fabricated",localTime("")==trW(L"时间未知",L"Time unknown",L"時間未知")&&localTime("2026-99-99T99:99:99Z")==trW(L"时间未知",L"Time unknown",L"時間未知"));
        check("UTC converted to actual local time",localTime("2026-10-22T00:01:02.123Z").size()==19);
        BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),300,-80,1,32,BI_RGB};void* bits=nullptr;
        const auto bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);const auto dc=CreateCompatibleDC(nullptr);const auto previous=SelectObject(dc,bitmap);
        const bool savedTheme=GlobalVar::isCurrentUIDarkMode;
        for(bool useDark:{false,true}){
            GlobalVar::isCurrentUIDarkMode=useDark;theme();
            for(int nav:{410,411,480,481,482,483,412}){
                SendMessageW(window,WM_COMMAND,nav,0);int active=0;
                for(int item:{410,411,480,481,482,483,412})active+=library.activeNavigation(item);
                check("one persistent active navigation page",active==1&&library.activeNavigation(nav));
                DRAWITEMSTRUCT draw{};draw.CtlID=nav;draw.hwndItem=GetDlgItem(window,nav);draw.hDC=dc;draw.rcItem={0,0,200,40};drawButton(draw);GdiFlush();
                check("active navigation visibly colored without focus",GetPixel(dc,20,4)==(dark?RGB(48,76,108):RGB(215,232,249)));
            }
            LOGFONTW weight{};GetObjectW(boldFont,sizeof(weight),&weight);check("active navigation bold",weight.lfWeight==FW_BOLD);
            SendMessageW(window,WM_COMMAND,410,0);
            for(int id:std::array<int,8>{410,411,105,Sample,FavoriteCurrent,Original,Current,Chip}){
                const HWND child=GetDlgItem(window,id);SendMessageW(child,WM_MOUSELEAVE,0,0);
                DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.CtlID=id;item.hwndItem=child;item.hDC=dc;
                item.rcItem={0,0,40,40};
                const auto render=[&]{SendMessageW(window,WM_DRAWITEM,id,reinterpret_cast<LPARAM>(&item));GdiFlush();return std::vector<uint8_t>(static_cast<uint8_t*>(bits),static_cast<uint8_t*>(bits)+300*80*4);};
                const auto normal=render();const auto current=history.current();
                SendMessageW(child,WM_MOUSEMOVE,0,MAKELPARAM(4,4));const auto over=render();
                check("mouse enter visibly changes every custom button",ControlFeedback::hot(child)&&normal!=over);
                check("hover never changes current color",history.current()==current);
                item.itemState=ODS_SELECTED;const auto down=render();check("pressed differs from hover",down!=over);
                item.itemState=0;SendMessageW(child,WM_MOUSELEAVE,0,0);check("mouse leave restores original pixels",render()==normal&&!ControlFeedback::hot(child));
                EnableWindow(child,FALSE);SendMessageW(child,WM_MOUSEMOVE,0,MAKELPARAM(4,4));check("disabled controls never become hot",!ControlFeedback::hot(child));EnableWindow(child,TRUE);
                SendMessageW(child,WM_MOUSEMOVE,0,MAKELPARAM(4,4));SendMessageW(child,WM_CANCELMODE,0,0);check("cancel clears hot state",!ControlFeedback::hot(child));
            }
            const HWND copy=GetDlgItem(window,105);SendMessageW(copy,WM_MOUSEMOVE,0,MAKELPARAM(4,4));ShowWindow(copy,SW_HIDE);check("hiding controls clears hover",!ControlFeedback::hot(copy));position();
            for(const auto& surface:{geometry.disk,geometry.saturation,geometry.value}){
                hoverAt(s(surface.x+surface.width/2),s(surface.y+surface.height/2));check("wheel and sliders have hover targets",hoverSurface!=0);
                SendMessageW(window,WM_MOUSELEAVE,0,0);check("canvas leave clears wheel and slider hover",hoverSurface==0);
            }
            // 用真实色卡窗口消息和内存DC验收，不读取桌面或改变剪贴板。
            {ColorFlowView flow;flow.create(window,999);flow.place(0,0,280,70,96,font,false,dark);
                ColorFlow::Item color;color.color={12,34,56};color.title=L"测试";flow.set({color},false);
                SendMessageW(flow.window(),WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);GdiFlush();
                const auto normal=GetPixel(dc,0,6),center=GetPixel(dc,50,50);
                SendMessageW(flow.window(),WM_MOUSEMOVE,0,MAKELPARAM(10,10));SendMessageW(flow.window(),WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);GdiFlush();
                check("continuous swatch gains real hover border",SendMessageW(flow.window(),ColorFlowView::QUERY,ColorFlowView::Hover,0)==0&&GetPixel(dc,0,6)!=normal);
                check("swatch interior remains exact RGB while hovered",center==RGB(12,34,56)&&GetPixel(dc,50,50)==center);
                SendMessageW(flow.window(),WM_MOUSELEAVE,0,0);SendMessageW(flow.window(),WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);GdiFlush();
                check("continuous swatch leave restores border",GetPixel(dc,0,6)==normal&&SendMessageW(flow.window(),ColorFlowView::QUERY,ColorFlowView::Hover,0)==-1);
            }
        }
        GlobalVar::isCurrentUIDarkMode=savedTheme;SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
        SendMessageW(window,WM_COMMAND,410,0);
        {ColorLibrary::Store store(ColorLibrary::defaultPath());store.record({91,92,93},"screen",ColorLibrary::eventKey());loadRecent();
            check("recent tooltip includes HEX and real capture timestamp",!recent.empty()&&tooltips[Recent].find(L"HEX: #5B5C5D")!=std::wstring::npos&&tooltips[Recent].find(localTime(recent[0].utc))!=std::wstring::npos);}
        check("tests never showed the parent",!IsWindowVisible(window));testDiagnostics+="passed="+std::to_string(passed)+"\n";return okay;
    }
    void run(){
        WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YeImageViewer.ColorPicker";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);
        dpi=density=GetDpiForSystem();const DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,extended=standalone?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW;
        RECT bounds{0,0,s(ColorPanel::WINDOW_WIDTH),s(ColorPanel::WINDOW_HEIGHT)};AdjustWindowRectExForDpi(&bounds,style,FALSE,extended,dpi);
        RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        window=CreateWindowExW(extended,cls.lpszClassName,trW(L"颜色面板 · YeImageViewer",L"Color panel · YeImageViewer",L"色彩面板 · YeImageViewer"),style,CW_USEDEFAULT,CW_USEDEFAULT,
            std::min(int(bounds.right-bounds.left),int(work.right-work.left)),std::min(int(bounds.bottom-bounds.top),int(work.bottom-work.top)),nullptr,nullptr,cls.hInstance,this);
        if(window){if(!testMode)SetTimer(window,1,100,nullptr);
            if(testMode){testDiagnostics.clear();testResult=offscreenTest?testOffscreen():test();DestroyWindow(window);}
            else{ShowWindow(window,SW_SHOW);MSG msg{};while(window&&GetMessageW(&msg,nullptr,0,0)>0){
                if(!window)break; // GetMessage也会分派跨线程SendMessage，返回前窗口可能已经销毁。
                if(msg.message==WM_KEYDOWN&&GetActiveWindow()==window){
                    if(msg.wParam==VK_ESCAPE){SendMessageW(window,WM_COMMAND,Cancel,0);continue;}
                    if(library.page()==0&&msg.wParam==VK_RETURN){finishField();continue;}
                    if(library.page()==0&&(GetKeyState(VK_CONTROL)&0x8000)&&msg.wParam=='Z'){SendMessageW(window,WM_COMMAND,(GetKeyState(VK_SHIFT)&0x8000)?Redo:Undo,0);continue;}
                    if(library.page()==0&&(GetKeyState(VK_CONTROL)&0x8000)&&msg.wParam=='Y'){SendMessageW(window,WM_COMMAND,Redo,0);continue;}
                }
                if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}}
        }
        StartupTrace::mark("colorPanel.loopEnded");
        if(font)DeleteObject(font);if(boldFont)DeleteObject(boldFont);if(cardBrush)DeleteObject(cardBrush);if(editBrush)DeleteObject(editBrush);if(paperBrush)DeleteObject(paperBrush);
        GdiFlush();
        StartupTrace::mark("colorPanel.gdiReleased");
    }
public:
    static bool isRunning(){return ColorToolHost::find()!=nullptr;}
    static std::string diagnostics(){return testDiagnostics;}
    static bool selfTest(){ScreenColorWindow panel;panel.testMode=true;panel.run();return panel.testResult;}
    static bool offscreenSelfTest(){ScreenColorWindow panel;panel.testMode=panel.offscreenTest=true;panel.run();return panel.testResult;}
    static bool open(bool edit=false){return ColorToolHost::launch(edit,GlobalVar::isCurrentUIDarkMode,GlobalVar::settingParameter.UI_Mode,GlobalVar::settingParameter.UI_LANG);}
    static void runStandalone(bool edit=false,bool service=false){
        ColorToolHost::Guard instance;if(!instance.owns()){if(const HWND existing=ColorToolHost::find())ColorToolHost::send(existing,edit?ColorToolHost::Operation::Adopt:ColorToolHost::Operation::Show,CurrentColor::get(),{},GlobalVar::isCurrentUIDarkMode,false,GlobalVar::settingParameter.UI_Mode,GlobalVar::settingParameter.UI_LANG);return;}
        ScreenColorWindow panel;panel.standalone=true;panel.startEditing=edit;panel.hostInitialized=!service;wchar_t file[32768]{};GetModuleFileNameW(nullptr,file,32768);panel.settingsPath=std::filesystem::path(file).parent_path()/L"YeImageViewer.color-picker.ini";panel.run();ScreenPickerSession::requestExit();
    }
};
