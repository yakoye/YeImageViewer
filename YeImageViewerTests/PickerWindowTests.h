#pragma once

inline void printPickerLifecycleTrace(){wchar_t path[32768]{};const auto length=GetEnvironmentVariableW(L"YEIMAGEVIEWER_STARTUP_TRACE",path,32768);if(length&&length<32768){std::ifstream trace{std::filesystem::path(path)};if(trace.peek()!=std::char_traits<char>::eof())std::cout<<trace.rdbuf();}}
inline HWND findIsolatedColorPanel(const std::filesystem::path& viewer){
    struct Query {const std::filesystem::path& viewer;HWND found=nullptr;} query{viewer};
    EnumWindows([](HWND window,LPARAM data)->BOOL{auto& q=*reinterpret_cast<Query*>(data);wchar_t cls[128]{};GetClassNameW(window,cls,128);if(std::wstring_view(cls)!=L"YeImageViewer.ColorPicker"||!IsWindowVisible(window))return TRUE;
        DWORD pid{};GetWindowThreadProcessId(window,&pid);const auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return TRUE;wchar_t path[32768]{};DWORD length=32768;const bool match=QueryFullProcessImageNameW(process,0,path,&length)&&_wcsicmp(path,q.viewer.c_str())==0;CloseHandle(process);if(match){q.found=window;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&query));return query.found;
}
inline bool activatePickerTestWindow(HWND window){
    return activateRegressionWindow(window);
}
inline bool capturePickerTestWindow(HWND window,const std::filesystem::path& target){
    if(!activatePickerTestWindow(window))return false;RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);DwmFlush();Sleep(50);
    RECT rect{};GetClientRect(window,&rect);POINT origin{};ClientToScreen(window,&origin);const int w=rect.right,h=rect.bottom;if(w<=0||h<=0)return false;
    const HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB};void* pixels=nullptr;const auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);const auto previous=bitmap?SelectObject(dc,bitmap):nullptr;cv::Mat snapshot;
    if(bitmap&&pixels&&BitBlt(dc,0,0,w,h,screen,origin.x,origin.y,SRCCOPY|CAPTUREBLT)){GdiFlush();snapshot=cv::Mat(h,w,CV_8UC4,pixels).clone();for(int y=0;y<h;++y)for(int x=0;x<w;++x)snapshot.at<cv::Vec4b>(y,x)[3]=255;}
    if(previous)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);return !snapshot.empty()&&cv::imwrite(target.string(),snapshot);
}
inline int runColorNavigationProbe(const std::filesystem::path& viewer){
    std::cout<<std::unitbuf;
    int passed=0,failed=0;auto check=[&](const char* label,bool okay){std::cout<<(okay?"PASS ":"FAIL ")<<label<<"\n";okay?++passed:++failed;};
    auto launch=[&](PROCESS_INFORMATION& process)->HWND{
        std::wstring command=L"\""+viewer.wstring()+L"\" --color-panel";STARTUPINFOW startup{sizeof(startup)};
        if(!CreateProcessW(viewer.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,viewer.parent_path().c_str(),&startup,&process))return nullptr;
        struct Query{DWORD pid;HWND hwnd=nullptr;} q{process.dwProcessId};
        for(int i=0;i<300&&!q.hwnd;++i){EnumWindows([](HWND h,LPARAM value)->BOOL{auto& q=*reinterpret_cast<Query*>(value);DWORD pid=0;GetWindowThreadProcessId(h,&pid);wchar_t cls[128]{};GetClassNameW(h,cls,128);if(pid==q.pid&&std::wstring_view(cls)==L"YeImageViewer.ColorPicker"&&IsWindowVisible(h)&&GetDlgItem(h,307)){q.hwnd=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&q));Sleep(10);}return q.hwnd;
    };
    auto close=[&](HWND hwnd,PROCESS_INFORMATION& process){if(hwnd)PostMessageW(hwnd,WM_CLOSE,0,0);if(process.hProcess){const bool ended=WaitForSingleObject(process.hProcess,5000)==WAIT_OBJECT_0;check("standalone color tool exits cleanly",ended);if(!ended){std::cout<<"DIAG close pid="<<process.dwProcessId<<" window="<<hwnd<<" alive="<<IsWindow(hwnd)<<" visible="<<IsWindowVisible(hwnd)<<" foreground="<<GetForegroundWindow()<<"\n";printPickerLifecycleTrace();TerminateProcess(process.hProcess,3);}CloseHandle(process.hThread);CloseHandle(process.hProcess);}};
    PROCESS_INFORMATION process{};HWND panel=launch(process);check("native color navigation exists",panel&&GetDlgItem(panel,410)&&GetDlgItem(panel,411)&&GetDlgItem(panel,412));
    if(!panel||!GetDlgItem(panel,411)){close(panel,process);return 2;}
    try{
        constexpr UINT flowMessage=WM_APP+75,flowQuery=WM_APP+76;
        const auto count=[&](int id=424){return SendDlgItemMessageW(panel,id,flowQuery,0,0);};
        const auto choose=[&](int index,int action=0,int id=424){SendMessageW(panel,flowMessage,MAKEWPARAM(id,action),index);};
        ColorLibrary::Store store(ColorLibrary::defaultPath());const auto before=store.count(false),favorite=store.count(true);
        const auto replace=[&](int id,const wchar_t* value){const HWND edit=GetDlgItem(panel,id);SendMessageW(edit,EM_SETSEL,0,-1);SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value));};
        const auto style=GetWindowLongPtrW(panel,GWL_STYLE);check("compact window has no resize or maximize",!(style&(WS_THICKFRAME|WS_MAXIMIZEBOX)));
        RECT client{};GetClientRect(panel,&client);const UINT dpi=GetDpiForWindow(panel);check("compact client is 680 by 430 DIP",std::abs(MulDiv(client.right,96,dpi)-680)<=1&&std::abs(MulDiv(client.bottom,96,dpi)-430)<=1);
        const int nav[]{410,411,480,481,482,483,412};bool ordered=true;LONG last=-1;for(int id:nav){RECT r{};GetWindowRect(GetDlgItem(panel,id),&r);ordered&=IsWindowVisible(GetDlgItem(panel,id))&&r.top>last;last=r.top;}check("seven real navigation pages in requested order",ordered);
        std::cout<<"navigation initial remembered page="<<store.preferences().page<<"\n";
        SendMessageW(panel,WM_COMMAND,410,0); // 收藏星形只在颜色页显示，不能假设上一专项最后保存的也是颜色页。
        check("obsolete record and favorite sidebar controls removed",!GetDlgItem(panel,414)&&!GetDlgItem(panel,415)&&IsWindowVisible(GetDlgItem(panel,126)));
        SendMessageW(panel,WM_COMMAND,MAKEWPARAM(101,EN_SETFOCUS),LPARAM(GetDlgItem(panel,101)));replace(101,L"#123456");
        check("preview and close-independent editing do not record",store.count(false)==before);
        SendMessageW(panel,WM_COMMAND,MAKEWPARAM(101,EN_KILLFOCUS),LPARAM(GetDlgItem(panel,101)));check("completed field edit writes persistent color once",store.count(false)==before+1&&store.list(false)[0].color==ColorValue::Rgb{18,52,86});
        SendMessageW(panel,WM_COMMAND,MAKEWPARAM(101,EN_KILLFOCUS),LPARAM(GetDlgItem(panel,101)));check("duplicate blur does not duplicate history",store.count(false)==before+1);
        wchar_t actualHex[128]{};SendMessageW(GetDlgItem(panel,101),WM_GETTEXT,128,reinterpret_cast<LPARAM>(actualHex));const auto savedHistory=store.list(false);std::cout<<"record diagnostic: hex="<<ColorLibrary::utf8(actualHex)<<" stored="<<(savedHistory.empty()?"<empty>":ColorLibrary::utf8(ColorValue::hex(savedHistory[0].color)))<<"\n";
        SendMessageW(GetDlgItem(panel,126),BM_CLICK,0,0);check("favorite is separate from history",store.count(true)==favorite+1&&store.count(false)==before+1);
        check("recent history first cell is newest persisted color",IsWindowVisible(GetDlgItem(panel,700)));
        SendMessageW(GetDlgItem(panel,411),BM_CLICK,0,0);check("history navigation hides editor and shows continuous list",IsWindowVisible(GetDlgItem(panel,424))&&!IsWindowVisible(GetDlgItem(panel,101))&&count()>0);
        const auto clipboard=GetClipboardSequenceNumber();choose(0);check("history swatch copies without adding history",GetClipboardSequenceNumber()!=clipboard&&store.count(false)==before+1);
        const auto clickSequence=GetClipboardSequenceNumber();const HWND flow=GetDlgItem(panel,424);const auto point=MAKELPARAM(MulDiv(30,dpi,96),MulDiv(55,dpi,96));SendMessageW(flow,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(flow,WM_LBUTTONUP,0,point);for(int i=0;i<100&&GetClipboardSequenceNumber()==clickSequence;++i)Sleep(10);
        check("actual continuous swatch hit test copies selected color",GetClipboardSequenceNumber()!=clickSequence&&SendMessageW(flow,flowQuery,3,0)==0&&store.count(false)==before+1);
        for(int id:{430,434,435,436,443,453,490,491,501,505,506})check("obsolete copy paging refresh save and local search controls absent",!GetDlgItem(panel,id));
        RECT search{},color{};GetWindowRect(GetDlgItem(panel,420),&search);GetWindowRect(GetDlgItem(panel,410),&color);check("single global search is above Color navigation",IsWindowVisible(GetDlgItem(panel,420))&&search.bottom<color.top);
        replace(422,L"bad-date");
        SendMessageW(GetDlgItem(panel,412),BM_CLICK,0,0);choose(int(favorite));SendMessageW(panel,WM_COMMAND,431,0);
        check("invalid history date never leaks into Favorites",count()==favorite+1);
        replace(450,L"跨重启收藏");replace(451,L"深浅主题 标签");replace(452,L"#ABCDEF");SendMessageW(panel,WM_COMMAND,MAKEWPARAM(452,EN_KILLFOCUS),LPARAM(GetDlgItem(panel,452)));
        ColorLibrary::Query q;q.text=L"跨重启收藏";check("native favorite metadata and hex edits saved",store.list(true,q).size()==1&&store.list(true,q)[0].color==ColorValue::Rgb{171,205,239});
        replace(452,L"invalid");SendMessageW(panel,WM_CLOSE,0,0);check("invalid favorite cannot silently disappear on close",IsWindow(panel)&&store.list(true,q)[0].color==ColorValue::Rgb{171,205,239});replace(452,L"#ABCDEF");
        for(const auto& entry:store.list(true)){std::cout<<"favorite diagnostic: id="<<entry.id<<" name="<<entry.name<<" color="<<ColorLibrary::utf8(ColorValue::hex(entry.color))<<"\n";}
        replace(450,L"尚未提交草稿");RECT original{};GetWindowRect(panel,&original);const auto oldDpi=GetDpiForWindow(panel);RECT scaled{original.left,original.top,original.left+1440,original.top+920};SendMessageW(panel,WM_DPICHANGED,MAKEWPARAM(192,192),reinterpret_cast<LPARAM>(&scaled));wchar_t text[128]{};SendMessageW(GetDlgItem(panel,450),WM_GETTEXT,128,reinterpret_cast<LPARAM>(text));check("DPI resize does not erase favorite drafts",std::wstring_view(text)==L"尚未提交草稿");SendMessageW(panel,WM_DPICHANGED,MAKEWPARAM(oldDpi,oldDpi),reinterpret_cast<LPARAM>(&original));
        replace(450,L"跨重启收藏");SendMessageW(panel,WM_COMMAND,MAKEWPARAM(450,EN_KILLFOCUS),LPARAM(GetDlgItem(panel,450)));close(panel,process);process={};panel=launch(process);check("restart restores Favorites page",panel&&IsWindowVisible(GetDlgItem(panel,424))&&!IsWindowVisible(GetDlgItem(panel,101)));check("restart keeps autosaved persistent data",store.list(true,q).size()==1&&store.count(false)==before+1);
        if(panel)SendMessageW(panel,WM_COMMAND,410,0);
        RECT originalSize{};GetWindowRect(panel,&originalSize);SetWindowPos(panel,nullptr,0,0,1600,1000,SWP_NOMOVE|SWP_NOZORDER);RECT locked{};GetWindowRect(panel,&locked);check("programmatic stretch keeps fixed size",locked.right-locked.left==originalSize.right-originalSize.left&&locked.bottom-locked.top==originalSize.bottom-originalSize.top);
        for(int id:{101,102,114,115,116,117,118,105,106,134,135,136,137,138})check("all color value and copy controls visible after refresh",IsWindowVisible(GetDlgItem(panel,id)));
        RECT lab{},copy{},status{},recent{};GetWindowRect(GetDlgItem(panel,118),&lab);GetWindowRect(GetDlgItem(panel,138),&copy);GetWindowRect(GetDlgItem(panel,109),&status);GetWindowRect(GetDlgItem(panel,700),&recent);check("actual status window does not cover Lab row copy icon or recent colors",status.top>=lab.bottom&&status.top>=copy.bottom&&status.bottom<=recent.top);
        SendMessageW(GetDlgItem(panel,480),BM_CLICK,0,0);check("palette includes all series in one continuous flow",count()>300&&SendDlgItemMessageW(panel,424,flowQuery,2,0)>1000);
        SendDlgItemMessageW(panel,424,WM_VSCROLL,SB_BOTTOM,0);const auto offset=SendDlgItemMessageW(panel,424,flowQuery,1,0);check("scroll crosses series without paging",offset>1000);
        SendMessageW(GetDlgItem(panel,481),BM_CLICK,0,0);check("traditional page includes full catalog",count()>1000);
        replace(420,L"立春");for(int i=0;i<300&&(!IsWindowVisible(GetDlgItem(panel,425))||count(425)==0);++i)Sleep(10);check("global search finds traditional colors without a local search box",IsWindowVisible(GetDlgItem(panel,425))&&count(425)>0&&!GetDlgItem(panel,501));
        choose(0,0,425);SendMessageW(panel,WM_COMMAND,439,0);check("search result jumps to actual source page",!IsWindowVisible(GetDlgItem(panel,425))&&IsWindowVisible(GetDlgItem(panel,424))&&SendDlgItemMessageW(panel,424,flowQuery,3,0)>=0);
        SendMessageW(GetDlgItem(panel,480),BM_CLICK,0,0);check("return to series preserves scroll offset",SendDlgItemMessageW(panel,424,flowQuery,1,0)==offset);
        SendMessageW(GetDlgItem(panel,482),BM_CLICK,0,0);check("gradient has all reference presets in continuous view",count()>300);
        choose(0);SendMessageW(panel,WM_COMMAND,431,0);const auto previousSequence=GetClipboardSequenceNumber();replace(524,L"invalid");check("invalid gradient does not replace clipboard with invalid CSS",GetClipboardSequenceNumber()==previousSequence);replace(524,L"#123456");choose(0);check("corrected gradient can copy CSS",GetClipboardSequenceNumber()!=previousSequence);
        SendMessageW(GetDlgItem(panel,483),BM_CLICK,0,0);check("image palette has import paste current and count controls",IsWindowVisible(GetDlgItem(panel,510))&&IsWindowVisible(GetDlgItem(panel,511))&&IsWindowVisible(GetDlgItem(panel,512))&&SendDlgItemMessageW(panel,513,CB_GETCOUNT,0,0)==5);
        const auto fixture=viewer.parent_path()/L"palette-source.png";check("image palette fixture is isolated",std::filesystem::exists(fixture));if(std::filesystem::exists(fixture)&&GetDlgItem(panel,510)){
            PostMessageW(GetDlgItem(panel,510),BM_CLICK,0,0);struct DialogQuery{DWORD pid;HWND window=nullptr;} dialog{process.dwProcessId};
            for(int i=0;i<300&&!dialog.window;++i){EnumWindows([](HWND h,LPARAM data)->BOOL{auto& q=*reinterpret_cast<DialogQuery*>(data);DWORD pid=0;GetWindowThreadProcessId(h,&pid);wchar_t cls[64]{};GetClassNameW(h,cls,64);if(pid==q.pid&&IsWindowVisible(h)&&std::wstring_view(cls)==L"#32770"){q.window=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&dialog));Sleep(10);}
            check("image import opens actual native file dialog",dialog.window!=nullptr);
            if(dialog.window){HWND edit=GetDlgItem(dialog.window,1152);if(!edit)edit=GetDlgItem(dialog.window,1148);check("native file dialog exposes filename control",edit!=nullptr);if(edit){SendMessageW(edit,WM_SETTEXT,0,reinterpret_cast<LPARAM>(fixture.c_str()));PostMessageW(GetDlgItem(dialog.window,IDOK),BM_CLICK,0,0);}else PostMessageW(dialog.window,WM_CLOSE,0,0);}
            for(int i=0;i<300&&count()==0;++i)Sleep(10);wchar_t result[256]{};SendMessageW(GetDlgItem(panel,460),WM_GETTEXT,256,reinterpret_cast<LPARAM>(result));std::cout<<"image import diagnostic="<<ColorLibrary::utf8(result)<<"\n";check("real imported file asynchronously produces persistent palette cells",count()>0&&!store.assets(2).empty());const auto sequence=GetClipboardSequenceNumber();choose(0);SendMessageW(panel,WM_COMMAND,410,0);wchar_t value[32]{};SendDlgItemMessageW(panel,101,WM_GETTEXT,32,reinterpret_cast<LPARAM>(value));check("image palette click copies and reaches current editor",GetClipboardSequenceNumber()!=sequence&&!std::wstring_view(value).empty());
        }
        SendMessageW(panel,WM_COMMAND,410,0);
        for(int i=0;i<42;++i)store.record({uint8_t(i),10,20},"editor",ColorLibrary::eventKey());SendMessageW(panel,WM_COMMAND,410,0);RECT first{},lastCell{};GetWindowRect(GetDlgItem(panel,700),&first);GetWindowRect(GetDlgItem(panel,741),&lastCell);wchar_t recentHex[32]{};SendMessageW(GetDlgItem(panel,700),WM_GETTEXT,32,reinterpret_cast<LPARAM>(recentHex));check("recent swatches wrap three rows newest left-to-right",IsWindowVisible(GetDlgItem(panel,741))&&lastCell.top>first.top&&std::wstring_view(recentHex)==L"#290A14");const auto historyCount=store.count(false);const auto sequence=GetClipboardSequenceNumber();SendMessageW(GetDlgItem(panel,700),BM_CLICK,0,0);check("recent color copies without growing history",GetClipboardSequenceNumber()!=sequence&&store.count(false)==historyCount);
        replace(101,L"#FFFFFF");const auto layout=ColorPanel::compactLayout();POINT previousPoint{},center{MulDiv(layout.disk.x+layout.disk.width/2,dpi,96),MulDiv(layout.disk.y+layout.disk.height/2,dpi,96)};GetCursorPos(&previousPoint);POINT screen=center;ClientToScreen(panel,&screen);SetCursorPos(screen.x,screen.y);Sleep(30);SendMessageW(panel,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(center.x,center.y));SendMessageW(panel,WM_LBUTTONUP,0,MAKELPARAM(center.x,center.y));SetCursorPos(previousPoint.x,previousPoint.y);check("unchanged disk click does not add duplicate history",store.count(false)==historyCount);
        replace(101,L"#79D4BA");for(bool dark:{false,true}){ColorToolHost::Header theme;theme.dark=dark;theme.mode=dark?2:1;COPYDATASTRUCT packet{ColorToolHost::PACKET,sizeof(theme),&theme};check("live appearance IPC updates existing panel",SendMessageW(panel,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&packet))==1);
            SendMessageW(panel,WM_COMMAND,410,0);check("capture actual compact color page",capturePickerTestWindow(panel,viewer.parent_path()/(dark?L"color-page-dark.png":L"color-page-light.png")));
            SendMessageW(panel,WM_COMMAND,480,0);check("capture grouped continuous palette and themed scrollbar",capturePickerTestWindow(panel,viewer.parent_path()/(dark?L"color-flow-dark.png":L"color-flow-light.png")));
            if(dark){const HWND view=GetDlgItem(panel,424);const HDC dc=GetDC(view);RECT r{};GetClientRect(view,&r);const auto pixel=GetPixel(dc,r.right-MulDiv(5,dpi,96),r.bottom/2);ReleaseDC(view,dc);check("dark scrollbar track is not a white system scrollbar",pixel!=CLR_INVALID&&GetRValue(pixel)<128&&GetGValue(pixel)<128);SendMessageW(panel,WM_COMMAND,411,0);replace(422,L"");check("capture dark filters and automatic history settings",capturePickerTestWindow(panel,viewer.parent_path()/L"color-history-dark.png"));
                const HWND search=GetDlgItem(panel,420);const HDC cue=GetDC(search);GetClientRect(search,&r);int brightest=0;for(int y=4;y<r.bottom-4;++y)for(int x=4;x<r.right-4;++x){const auto pixel=GetPixel(cue,x,y);if(pixel!=CLR_INVALID)brightest=std::max(brightest,int(GetRValue(pixel)));}ReleaseDC(search,cue);check("dark global search placeholder remains legible",brightest>=150);
            }
        }
    }catch(const std::exception& e){std::cout<<"native-library exception="<<e.what()<<"\n";check("native library no unexpected exception",false);}
    close(panel,process);std::cout<<"color-navigation passed="<<passed<<" failed="<<failed<<"\n";return failed?2:0;
}

inline int runPickerWindowProbe(const std::filesystem::path& viewer,const std::filesystem::path& source) {
    const auto keyboardLayouts=[] {const int count=GetKeyboardLayoutList(0,nullptr);std::vector<HKL> result(std::max(0,count));if(count>0)result.resize(std::max(0,GetKeyboardLayoutList(count,result.data())));std::sort(result.begin(),result.end());return result;};
    const auto initialLayouts=keyboardLayouts();
    const auto probe=viewer.parent_path()/L"picker-view-probe.txt";
    wchar_t previousEnvironment[32768]{};
    const auto previousLength=GetEnvironmentVariableW(L"YEIMAGEVIEWER_INTERACTION_PROBE",previousEnvironment,32768);
    SetEnvironmentVariableW(L"YEIMAGEVIEWER_INTERACTION_PROBE",probe.c_str());
    std::wstring command=L"\""+viewer.wstring()+L"\" \""+source.wstring()+L"\"";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    const bool started=CreateProcessW(viewer.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,viewer.parent_path().c_str(),&startup,&process)!=FALSE;
    SetEnvironmentVariableW(L"YEIMAGEVIEWER_INTERACTION_PROBE",previousLength?previousEnvironment:nullptr);
    if(!started) return 2;
    POINT previousPoint{};GetCursorPos(&previousPoint);
    const HWND previousForeground=GetForegroundWindow();
    int passed=0,failed=0;
    auto check=[&](const char* label,bool okay){std::cout<<(okay?"PASS ":"FAIL ")<<label<<"\n";okay?++passed:++failed;};
    auto find=[&](const wchar_t* className)->HWND {
        if(className&&std::wstring_view(className)==L"YeImageViewer.ColorPicker")return findIsolatedColorPanel(viewer);
        struct Search {DWORD pid;const wchar_t* cls;HWND found=nullptr;} search{process.dwProcessId,className};
        EnumWindows([](HWND hwnd,LPARAM value)->BOOL {
            auto* s=reinterpret_cast<Search*>(value);DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
            wchar_t cls[256]{};GetClassNameW(hwnd,cls,256);
            if(pid==s->pid && IsWindowVisible(hwnd) && (!s->cls || std::wstring_view(cls)==s->cls)) {s->found=hwnd;return FALSE;}return TRUE;
        },reinterpret_cast<LPARAM>(&search));return search.found;
    };
    HWND main=nullptr;
    for(int i=0;i<300 && !main;++i){main=find(nullptr);Sleep(10);}
    bool ready=false;
    for(int i=0;i<300 && main;++i){auto state=readWindowProbe(probe);if(state.contains("viewSettled") && state.at("viewSettled")==1 && state.contains("loading") && state.at("loading")==0){ready=true;break;}Sleep(10);}
    check("viewer loaded image before picker integration",ready);
    RECT beforeRect{};GetWindowRect(main,&beforeRect);
    const LONG_PTR beforeStyle=GetWindowLongPtrW(main,GWL_STYLE);
    const auto before=readWindowProbe(probe);
    check("picker and color-panel menu entries have distinct standard icons",before.contains("colorMenuIcons")&&before.at("colorMenuIcons")==1);
    check("ordinary image has no tools submenu and no PSD menu item",before.contains("contextTools") && before.at("contextTools")==0 && before.contains("contextLayers") && before.at("contextLayers")==0);
    const DWORD startupGdi=before.contains("gdiObjects")?DWORD(before.at("gdiObjects")):0;
    check("GDI baseline comes from completed viewer frame",startupGdi>0);
    std::cout<<"GDI startup diagnostic: "<<startupGdi<<" loading="<<(before.contains("loading")?before.at("loading"):-1)<<"\n";
    if(ready) {
        RECT client{};GetClientRect(main,&client);
        const auto rect=OverlayLayout::screenPickerRect(client.right,client.bottom,GetDpiForWindow(main));
        check("toolbar right-click owns the activated viewer",activatePickerTestWindow(main));POINT p{rect.x+rect.width/2,rect.y+rect.height/2};ClientToScreen(main,&p);SetCursorPos(p.x,p.y);
        SendMessageW(main,WM_MOUSEMOVE,0,MAKELPARAM(rect.x+rect.width/2,rect.y+rect.height/2));Sleep(100);
        const auto rightState=readWindowProbe(probe);
        std::cout<<"right-click diagnostic: toolbar="<<(rightState.contains("toolbarVisible")?rightState.at("toolbarVisible"):-1)<<" foreground="<<GetForegroundWindow()<<" main="<<main<<"\n";
        SendMessageW(main,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(rect.x+rect.width/2,rect.y+rect.height/2));
        PostMessageW(main,WM_RBUTTONUP,0,MAKELPARAM(rect.x+rect.width/2,rect.y+rect.height/2));
        HWND panel=nullptr;for(int i=0;i<300&&!panel;++i){panel=find(L"YeImageViewer.ColorPicker");Sleep(10);}
        std::cout<<"right-click result: panel="<<panel<<" menu="<<find(L"#32768")<<" HUD="<<find(L"YeImageViewer.QuickScreenPicker")<<"\n";
        check("eyedropper right click directly opens color panel without menu or HUD",panel&&!find(L"#32768")&&!find(L"YeImageViewer.QuickScreenPicker")&&WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT);
        if(!panel&&find(L"#32768"))SendMessageW(main,WM_CANCELMODE,0,0);
        if(panel){
            for(int i=0;i<300&&!GetDlgItem(panel,307);++i)Sleep(10);
            bool fields=true;for(int id:{101,102,114,115,116,117,118})fields &= IsWindowVisible(GetDlgItem(panel,id));
            check("designed color page exposes seven rows simultaneously, no old format dropdown",fields&&!GetDlgItem(panel,110));
            SendMessageW(panel,WM_CLOSE,0,0);Sleep(80);activatePickerTestWindow(main);
        }
        SendMessageW(main,WM_COMMAND,1404,0);Sleep(80);
        check("removed image sampling command is inert",!find(L"YeImageViewer.ColorPicker")&&!find(L"YeImageViewer.QuickScreenPicker"));
    }
    // 颜色面板与HUD是两种资源集合。单独检验面板重复开关，再用同一稳定状态测HUD；
    // 不把第一次加载原生控件/主题的进程级一次性分配误记作每次采屏泄漏。
    DWORD panelBaseline=0;
    for(int cycle=0;ready&&cycle<20;++cycle){
        if(cycle)SendMessageW(main,WM_COMMAND,1403,0);
        HWND panel=nullptr;
        if(cycle)for(int i=0;i<300&&!panel;++i){panel=find(L"YeImageViewer.ColorPicker");Sleep(10);}
        if(cycle){check("color panel repeatedly initializes real controls",panel!=nullptr);if(!panel)break;
            for(int i=0;i<300&&!GetDlgItem(panel,307);++i)Sleep(10);
            DWORD panelPid{};GetWindowThreadProcessId(panel,&panelPid);const HANDLE child=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,panelPid);
            SendMessageW(panel,WM_CLOSE,0,0);check("closed independent panel process releases its own resources",child&&WaitForSingleObject(child,5000)==WAIT_OBJECT_0);if(child)CloseHandle(child);}
        bool idle=false;std::unordered_map<std::string,double> state;
        for(int i=0;i<300&&!idle;++i){state=readWindowProbe(probe);idle=state.contains("colorPanelBusy")&&state.at("colorPanelBusy")==0;if(!idle)Sleep(10);}
        check("color panel worker releases resources before measurement",idle);
        const DWORD gdi=state.contains("gdiObjects")?DWORD(state.at("gdiObjects")):0;
        if(!cycle)panelBaseline=gdi;
        std::cout<<"GDI panel diagnostic: cycle="<<cycle<<" startup="<<startupGdi<<" first-closed="<<panelBaseline<<" completed-frame="<<gdi<<"\n";
        check("repeated color panels release GDI resources",gdi>0&&gdi<=panelBaseline+8);
    }
    const auto pickerStart=readWindowProbe(probe);
    const DWORD baselineGdi=pickerStart.contains("gdiObjects")?DWORD(pickerStart.at("gdiObjects")):0;
    check("HUD resource baseline follows completed panel lifecycle",baselineGdi>0&&pickerStart.contains("colorPanelBusy")&&pickerStart.at("colorPanelBusy")==0);
    DWORD persistentWorker=0;HWND survivingPanel=nullptr;
    const auto physicalKey=[&](WORD key,bool control=false){DWORD foregroundPid{};GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);if(foregroundPid!=process.dwProcessId)return false;
        std::vector<INPUT> inputs;const auto add=[&](WORD vk,bool up){INPUT input{};input.type=INPUT_KEYBOARD;input.ki.wVk=vk;input.ki.dwFlags=up?KEYEVENTF_KEYUP:0;inputs.push_back(input);};if(control)add(VK_CONTROL,false);add(key,false);add(key,true);if(control)add(VK_CONTROL,true);const bool okay=SendInput(UINT(inputs.size()),inputs.data(),sizeof(INPUT))==inputs.size();Sleep(70);return okay;};
    if(ready) for(int cycle=0;cycle<20;++cycle) {
        if(cycle==0) {
            // 真主窗口点击入口，命中几何与绘制共用；不伪造 toolbar 命令。
            RECT client{};GetClientRect(main,&client);
            const auto rect=OverlayLayout::screenPickerRect(client.right,client.bottom,GetDpiForWindow(main));
            POINT p{rect.x+rect.width/2,rect.y+rect.height/2};ClientToScreen(main,&p);SetCursorPos(p.x,p.y);
            SetWindowPos(main,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            check("toolbar primary owns the activated viewer",activatePickerTestWindow(main));
            PostMessageW(main,WM_MOUSEMOVE,0,MAKELPARAM(rect.x+rect.width/2,rect.y+rect.height/2));Sleep(80);
            SendMessageW(main,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(rect.x+rect.width/2,rect.y+rect.height/2));
            SendMessageW(main,WM_LBUTTONUP,0,MAKELPARAM(rect.x+rect.width/2,rect.y+rect.height/2));
            HWND picked=nullptr;for(int i=0;i<200&&!picked;++i){picked=find(L"YeImageViewer.QuickScreenPicker");Sleep(10);}
            check("toolbar primary immediately opens screen picker",picked&&!find(L"YeImageViewer.ColorPicker"));
        } else SendMessageW(main,WM_COMMAND,1401,0);
        HWND overlay=nullptr;
        for(int i=0;i<200 && !overlay;++i){overlay=find(L"YeImageViewer.QuickScreenPicker");Sleep(10);}
        check("direct screen command opens HUD repeatedly",overlay!=nullptr);
        if(!overlay) break;
        const auto active=readWindowProbe(probe);const DWORD worker=active.contains("screenPickerWorker")?DWORD(active.at("screenPickerWorker")):0;if(!cycle)persistentWorker=worker;
        check("sessions reuse one worker without retaining overlay windows",worker&&worker==persistentWorker);
        check("picker never adds or removes system keyboard layouts",keyboardLayouts()==initialLayouts);
        check("priority keyboard hook is active only during picker",SendMessageW(overlay,WM_APP+70,4,0)==1);
        if(cycle==2){const bool safe=activatePickerTestWindow(overlay);check("real input test owns foreground with no held modifiers",safe);
            if(safe){
            const auto format=SendMessageW(overlay,WM_APP+70,0,0),relative=SendMessageW(overlay,WM_APP+70,1,0),x=SendMessageW(overlay,WM_APP+70,2,0);
            check("physical Shift cycles once, not twice",physicalKey(VK_SHIFT)&&SendMessageW(overlay,WM_APP+70,0,0)==(format+1)%PickerCore::FORMAT_COUNT);
            check("physical P reaches picker ahead of IME",physicalKey('P')&&SendMessageW(overlay,WM_APP+70,1,0)!=relative);
            check("physical arrow moves one pixel",physicalKey(VK_RIGHT)&&SendMessageW(overlay,WM_APP+70,2,0)==x+1);
            check("physical Ctrl arrow moves ten pixels",physicalKey(VK_RIGHT,true)&&SendMessageW(overlay,WM_APP+70,2,0)==x+11);
            const auto sequence=GetClipboardSequenceNumber();check("physical C copies without exiting",physicalKey('C')&&IsWindow(overlay)&&GetClipboardSequenceNumber()!=sequence);
            check("physical F2 opens native preferences",physicalKey(VK_F2)&&find(L"#32768"));physicalKey(VK_ESCAPE);check("preferences menu Esc does not exit picker",IsWindow(overlay));
            check("physical F5 refreshes snapshot without losing session",physicalKey(VK_F5)&&IsWindow(overlay));check("physical Space refreshes snapshot",physicalKey(VK_SPACE)&&IsWindow(overlay));
            }
        }
        check("quick path does not open old editor dialog",find(L"YeImageViewer.ColorPicker")==nullptr);
        // 再次启动必须保持已有同一覆盖层，不能把前一会话静默替换。
        SendMessageW(main,WM_COMMAND,1401,0);Sleep(20);
        check("reentrant launch keeps existing session",IsWindow(overlay) && find(L"YeImageViewer.QuickScreenPicker")==overlay);
        if(cycle==3||cycle==4){
            SendMessageW(overlay,WM_KEYDOWN,VK_RETURN,0);
            for(int i=0;i<100&&IsWindow(overlay);++i)Sleep(10);
            check("successful pick removes overlay before handoff",!IsWindow(overlay));
            Sleep(cycle==3?100:2200);
            const auto picked=readWindowProbe(probe);const auto color=picked.contains("currentColor")?uint32_t(picked.at("currentColor")):0;
            const bool safe=activatePickerTestWindow(main);check("right-click handoff test owns viewer foreground",safe);
            if(safe){RECT area{};GetClientRect(main,&area);POINT p{area.right/2,area.bottom/2};ClientToScreen(main,&p);SetCursorPos(p.x,p.y);
                INPUT inputs[2]{};for(auto& input:inputs)input.type=INPUT_MOUSE;inputs[0].mi.dwFlags=MOUSEEVENTF_RIGHTDOWN;inputs[1].mi.dwFlags=MOUSEEVENTF_RIGHTUP;
                check("physical handoff right-click injected as one pair",SendInput(2,inputs,sizeof(INPUT))==2);
                if(cycle==3){HWND panel=nullptr;for(int i=0;i<200&&!panel;++i){panel=find(L"YeImageViewer.ColorPicker");Sleep(10);}
                    check("right click within two seconds opens color panel",panel!=nullptr);
                    if(panel){wchar_t value[64]{};
                        // 独立面板跨进程；等待字段填充，用 WM_GETTEXT 读取编辑框而非 GetWindowText 路径。
                        for(int i=0;i<200;++i){SendDlgItemMessageW(panel,101,WM_GETTEXT,64,reinterpret_cast<LPARAM>(value));if(value[0])break;Sleep(10);}
                        std::cout<<"handoff diagnostic: actual="<<ColorLibrary::utf8(value)<<" expected="<<ColorLibrary::utf8(ColorValue::hex(CurrentColor::unpack(color)))<<"\n";
                        check("handoff panel receives exact picked color",ColorValue::parse(value)==std::optional<ColorValue::Rgb>(CurrentColor::unpack(color)));
                        check("handoff does not also open viewer context menu",find(L"#32768")==nullptr);
                        SendMessageW(panel,WM_CLOSE,0,0);}
                }else{HWND menu=nullptr;for(int i=0;i<100&&!menu;++i){menu=find(L"#32768");Sleep(10);}
                    check("after two seconds ordinary right click still opens menu",menu!=nullptr&&!find(L"YeImageViewer.ColorPicker"));
                    SendMessageW(main,WM_CANCELMODE,0,0);}
            }
            bool idle=false;for(int i=0;i<300&&!idle;++i){const auto state=readWindowProbe(probe);idle=state.contains("screenPickerBusy")&&state.at("screenPickerBusy")==0;if(!idle)Sleep(10);}
            check("handoff hook and worker finish without resident capture",idle);continue;
        }
        if(cycle==19) {
            SendMessageW(main,WM_COMMAND,1403,0);
            survivingPanel=findIsolatedColorPanel(viewer);
            PostMessageW(main,WM_CLOSE,0,0);
            check("closing viewer joins its picker worker",WaitForSingleObject(process.hProcess,5000)==WAIT_OBJECT_0);
            check("closing viewer keeps independent color panel usable",survivingPanel&&IsWindow(survivingPanel)&&GetDlgItem(survivingPanel,307));
            break;
        }
        const auto sequence=GetClipboardSequenceNumber();
        if(cycle%2) SendMessageW(overlay,WM_RBUTTONDOWN,0,0);
        else if(cycle==2)physicalKey(VK_ESCAPE);else SendMessageW(overlay,WM_KEYDOWN,VK_ESCAPE,0);
        for(int i=0;i<200 && IsWindow(overlay);++i) Sleep(10);
        check("cancel removes overlay without clipboard write",!IsWindow(overlay) && sequence==GetClipboardSequenceNumber());
        // 窗口销毁先于创建线程释放全部GDI；必须等待真实会话结束，不能抢在析构中测量。
        bool workerIdle=false;
        for(int i=0;i<300&&!workerIdle;++i){const auto state=readWindowProbe(probe);workerIdle=state.contains("screenPickerBusy")&&state.at("screenPickerBusy")==0;if(!workerIdle)Sleep(10);}
        check("cancel finishes picker worker before resource measurement",workerIdle);
        RECT afterRect{};GetWindowRect(main,&afterRect);
        const auto after=readWindowProbe(probe);
        bool stateOkay=GetWindowLongPtrW(main,GWL_STYLE)==beforeStyle && EqualRect(&beforeRect,&afterRect);
        for(const char* key:{"zoomExact","left","right","top","bottom","shape"})
            stateOkay=stateOkay && before.contains(key) && after.contains(key) && before.at(key)==after.at(key);
        check("cancel preserves image geometry and window mode",stateOkay);
        const DWORD current=after.contains("gdiObjects")?DWORD(after.at("gdiObjects")):0;
        std::cout<<"GDI diagnostic: cycle="<<cycle<<" baseline="<<baselineGdi<<" completed-frame="<<current<<" external="<<GetGuiResources(process.hProcess,GR_GDIOBJECTS)<<" pickerBusy="<<(after.contains("screenPickerBusy")?after.at("screenPickerBusy"):-1)<<"\n";
        check("repeated sessions release GDI resources",current>0&&current<=baselineGdi+8);
    }
    if(WaitForSingleObject(process.hProcess,0)!=WAIT_OBJECT_0){PostMessageW(main,WM_CLOSE,0,0);if(WaitForSingleObject(process.hProcess,3000)!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,3);}
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    if(survivingPanel&&IsWindow(survivingPanel)){
        wchar_t beforeColor[32]{};SendDlgItemMessageW(survivingPanel,101,WM_GETTEXT,32,reinterpret_cast<LPARAM>(beforeColor));
        STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION second{};std::wstring args=L"\""+viewer.wstring()+L"\" --color-panel";const bool launched=CreateProcessW(viewer.c_str(),args.data(),nullptr,nullptr,FALSE,0,nullptr,viewer.parent_path().c_str(),&si,&second)!=FALSE;
        const bool exited=launched&&WaitForSingleObject(second.hProcess,5000)==WAIT_OBJECT_0;wchar_t afterColor[32]{};SendDlgItemMessageW(survivingPanel,101,WM_GETTEXT,32,reinterpret_cast<LPARAM>(afterColor));
        check("reopening color panel reuses same instance and keeps current color",exited&&findIsolatedColorPanel(viewer)==survivingPanel&&std::wstring_view(beforeColor)==afterColor);
        if(second.hThread)CloseHandle(second.hThread);if(second.hProcess)CloseHandle(second.hProcess);
        second={};args=L"\""+viewer.wstring()+L"\" \""+source.wstring()+L"\"";const bool viewerStarted=CreateProcessW(viewer.c_str(),args.data(),nullptr,nullptr,FALSE,0,nullptr,viewer.parent_path().c_str(),&si,&second)!=FALSE;
        struct ViewerQuery{DWORD pid;HWND window=nullptr;} query{second.dwProcessId};
        for(int i=0;viewerStarted&&i<300&&!query.window;++i){EnumWindows([](HWND window,LPARAM data)->BOOL{auto& q=*reinterpret_cast<ViewerQuery*>(data);DWORD pid{};GetWindowThreadProcessId(window,&pid);wchar_t cls[64]{};GetClassNameW(window,cls,64);if(pid==q.pid&&std::wstring_view(cls)==L"D3D11WndClass"){q.window=window;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&query));Sleep(10);}
        if(query.window)SendMessageW(query.window,WM_COMMAND,1403,0);SendDlgItemMessageW(survivingPanel,101,WM_GETTEXT,32,reinterpret_cast<LPARAM>(afterColor));
        check("reopened actual image viewer calls the same surviving panel",query.window&&findIsolatedColorPanel(viewer)==survivingPanel&&std::wstring_view(beforeColor)==afterColor);
        if(query.window)PostMessageW(query.window,WM_CLOSE,0,0);if(second.hProcess){const bool ended=WaitForSingleObject(second.hProcess,5000)==WAIT_OBJECT_0;check("second viewer exits without closing shared panel",ended&&IsWindow(survivingPanel));if(!ended)TerminateProcess(second.hProcess,3);CloseHandle(second.hProcess);}if(second.hThread)CloseHandle(second.hThread);
        DWORD pid{};GetWindowThreadProcessId(survivingPanel,&pid);const HANDLE child=OpenProcess(SYNCHRONIZE,FALSE,pid);PostMessageW(survivingPanel,WM_CLOSE,0,0);check("independent panel explicitly closes with no resident process",child&&WaitForSingleObject(child,5000)==WAIT_OBJECT_0);if(child)CloseHandle(child);
    }
    printPickerLifecycleTrace();
    for(const auto* mode:{L"--screen-picker",L"--color-panel"}) {
        if(std::wstring_view(mode)==L"--screen-picker")ColorLibrary::Store(ColorLibrary::defaultPath()).page(2);
        std::wstring launch=L"\""+viewer.wstring()+L"\" "+mode;
        STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION child{};
        const bool startedTool=CreateProcessW(viewer.c_str(),launch.data(),nullptr,nullptr,FALSE,0,nullptr,viewer.parent_path().c_str(),&si,&child)!=FALSE;
        check("standalone tool launches without image argument",startedTool);
        if(!startedTool)continue;
        const auto findChild=[&](const wchar_t* cls)->HWND {
            struct Query{DWORD pid;const wchar_t* cls;HWND result=nullptr;} q{child.dwProcessId,cls};
            EnumWindows([](HWND hwnd,LPARAM data)->BOOL{auto& q=*reinterpret_cast<Query*>(data);DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);wchar_t c[256]{};GetClassNameW(hwnd,c,256);if(pid==q.pid&&std::wstring_view(c)==q.cls){q.result=hwnd;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&q));return q.result;
        };
        const bool screen=std::wstring_view(mode)==L"--screen-picker";
        HWND tool=nullptr;for(int i=0;i<300&&!tool;++i){tool=findChild(screen?L"YeImageViewer.QuickScreenPicker":L"YeImageViewer.ColorPicker");Sleep(10);}
        check("standalone creates tool only, no D3D viewer",tool && !findChild(L"D3D11WndClass"));
        if(tool && screen) {
            const auto seq=GetClipboardSequenceNumber();SendMessageW(tool,WM_KEYDOWN,'E',0);
            HWND panel=nullptr;for(int i=0;i<300&&!panel;++i){panel=findChild(L"YeImageViewer.ColorPicker");Sleep(10);}
            check("standalone E transfers color to panel without copy or viewer",panel && !findChild(L"D3D11WndClass") && seq==GetClipboardSequenceNumber());
            tool=panel;
        }
        if(tool) {
            bool panelReady=false;for(int i=0;i<300&&!panelReady;++i){panelReady=GetDlgItem(tool,307)&&SendDlgItemMessageW(tool,101,WM_GETTEXTLENGTH,0,0)==7;if(!panelReady)Sleep(10);}
            check("standalone panel fields initialized before interaction",panelReady);
            if(screen)check("E opens editable Color page instead of remembered Favorites",IsWindowVisible(GetDlgItem(tool,101))&&!IsWindowVisible(GetDlgItem(tool,424)));
            check("independent color panel can return through taskbar after minimize",(GetWindowLongPtrW(tool,GWL_EXSTYLE)&WS_EX_APPWINDOW)!=0);
            if(screen) {
                SendMessageW(tool,WM_COMMAND,138,0);
                check("standalone Lab row copy saves shared format preference",PickerPreferences::load(viewer.parent_path()/L"YeImageViewer.color-picker.ini").format==6);
            } else check("independent panel restart keeps all seven format rows",GetDlgItem(tool,118)&&!GetDlgItem(tool,110));
        }
        if(tool)PostMessageW(tool,WM_CLOSE,0,0);
        const bool ended=WaitForSingleObject(child.hProcess,5000)==WAIT_OBJECT_0;
        check("standalone exit leaves no resident process",ended);
        if(!ended)TerminateProcess(child.hProcess,3);
        CloseHandle(child.hThread);CloseHandle(child.hProcess);
    }
    SetCursorPos(previousPoint.x,previousPoint.y);if(previousForeground && IsWindow(previousForeground))SetForegroundWindow(previousForeground);
    std::cout<<"picker-integration passed="<<passed<<" failed="<<failed<<"\n";
    return failed?2:0;
}
