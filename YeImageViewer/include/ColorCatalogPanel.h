#pragma once
#include "ColorCatalog.h"
#include "ContrastRatio.h"
#include <commdlg.h>
#include <shellapi.h>
#include <thread>

// 固定工作区内的真实离线色库、渐变编辑及异步图片提色，不创建额外对话工作台。
class ColorCatalogPanel {
    enum {Group=500,Search=501,Previous=502,Next=503,Apply=504,Copy=505,Favorite=506,Status=507,
        Open=510,Clipboard=511,Current=512,Count=513,Cancel=514,Preset=520,Type=521,Angle=522,Stops=523,Stop0=524,Preview=530,Cell=600};
    HWND owner=nullptr;int dpi=96,page=0,offset=0,selected=-1;bool updating=false;
    std::vector<ColorCatalog::Entry> rows;std::vector<ColorCatalog::Dominant> dominant;
    ColorCatalog::Gradient gradient;cv::Mat image;
    std::jthread worker;std::mutex resultMutex;std::atomic<bool> ready=false,canceled=false,running=false;
    cv::Mat pending;std::string error;bool validGradient=true;
    static constexpr int all[]{Group,Search,Previous,Next,Apply,Copy,Favorite,Status,Open,Clipboard,Current,Count,Cancel,Preset,Type,Angle,Stops,Stop0,Stop0+1,Stop0+2,Stop0+3,Preview};
    int s(int x)const{return MulDiv(x,dpi,96);}
    std::wstring text(int id){wchar_t value[256]{};GetDlgItemTextW(owner,id,value,256);return value;}
    void status(const wchar_t* value){SetDlgItemTextW(owner,Status,value);}
    void control(const wchar_t* cls,const wchar_t* name,int id,DWORD style=0){CreateWindowExW(wcscmp(cls,L"EDIT")==0?WS_EX_CLIENTEDGE:0,cls,name,WS_CHILD|WS_TABSTOP|style,0,0,1,1,owner,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);if(wcscmp(cls,L"EDIT")==0)SendDlgItemMessageW(owner,id,EM_SETLIMITTEXT,160,0);}
    void move(int id,int x,int y,int w,int h,HFONT font,bool visible=true){const HWND child=GetDlgItem(owner,id);MoveWindow(child,s(x),s(y),s(w),s(h),TRUE);ShowWindow(child,visible?SW_SHOW:SW_HIDE);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}
    void notify(ColorValue::Rgb c){SendMessageW(owner,COLOR_MESSAGE,0,LPARAM(CurrentColor::pack(c)));}
    void load(){
        offset=0;selected=-1;rows.clear();
        if(page==3||page==4){const auto index=SendDlgItemMessageW(owner,Group,CB_GETCURSEL,0,0);const std::wstring groups[]{L"",L"Open Color",L"Flat UI",L"Fluent Design",L"Ant Design",L"Material Design"};std::wstring group=index<=0?L"":page==3?groups[std::clamp(int(index),0,5)]:index==1?L"中国":L"日本";rows=ColorCatalog::filter(page==4,group,text(Search));}
        if(page==6){for(const auto& d:dominant)rows.push_back({L"",PickerCore::decimal2(d.fraction*100)+L"%",L"",d.color});}
        cells();
    }
    void cells(){
        for(int i=0;i<25;++i){const auto index=offset+i;const bool show=(page==3||page==4||page==6)&&index<int(rows.size());const HWND child=GetDlgItem(owner,Cell+i);ShowWindow(child,show?SW_SHOW:SW_HIDE);if(show){const auto name=rows[index].name+L"\n"+ColorValue::hex(rows[index].color);SetWindowTextW(child,name.c_str());InvalidateRect(child,nullptr,TRUE);}}
        EnableWindow(GetDlgItem(owner,Previous),offset>0);EnableWindow(GetDlgItem(owner,Next),offset+25<int(rows.size()));
        for(int id:{Apply,Copy,Favorite})EnableWindow(GetDlgItem(owner,id),page==5?validGradient:selected>=0&&selected<int(rows.size()));
        if(page==3||page==4){const auto count=std::to_wstring(rows.size())+trW(L" 色 · 点击选色，双击复制",L" colors · click selects, double-click copies",L" 色 · 點擊選色，雙擊複製");status(count.c_str());}
    }
    void updateGradient(){
        if(updating)return;const int count=int(SendDlgItemMessageW(owner,Stops,CB_GETCURSEL,0,0))+2;std::vector<ColorValue::Rgb> colors;validGradient=count>=2&&count<=4;
        for(int i=0;i<count&&i<4;++i){const auto c=ColorValue::parse(text(Stop0+i));if(!c)validGradient=false;else colors.push_back(*c);}
        const auto angle=text(Angle);wchar_t* end=nullptr;const long value=wcstol(angle.c_str(),&end,10);validGradient&=!angle.empty()&&end&&!*end&&value>=0&&value<=360;
        if(validGradient){gradient.colors=std::move(colors);gradient.angle=int(value);gradient.radial=SendDlgItemMessageW(owner,Type,CB_GETCURSEL,0,0)==1;status(trW(L"sRGB插值 · 等距色标 · 可复制CSS",L"sRGB · evenly spaced stops · copy CSS",L"sRGB插值 · 等距色標 · 可複製CSS"));}
        else status(trW(L"请输入2～4个有效HEX和0～360角度",L"Enter 2–4 HEX colors and angle 0–360",L"請輸入2～4個有效HEX及0～360角度"));
        for(int i=0;i<4;++i)ShowWindow(GetDlgItem(owner,Stop0+i),i<count?SW_SHOW:SW_HIDE);EnableWindow(GetDlgItem(owner,Copy),validGradient);InvalidateRect(GetDlgItem(owner,Preview),nullptr,FALSE);
    }
    void quantify(){
        if(image.empty())return;try{const int values[]{3,5,6,8,12};const int index=int(SendDlgItemMessageW(owner,Count,CB_GETCURSEL,0,0));dominant=ColorCatalog::extract(image,values[std::clamp(index,0,4)]);load();status(dominant.empty()?trW(L"图片全部透明，没有可提取颜色",L"Fully transparent image",L"圖片全部透明，無可擷取色彩"):trW(L"首帧缩略主色 · 按占比排序 · 非原像素取色",L"First-frame thumbnail palette · by coverage",L"首幀縮圖主色 · 按佔比排序 · 非原像素取色"));}catch(...){status(trW(L"图片提色失败，保留当前颜色",L"Extraction failed; color unchanged",L"圖片提色失敗，保留目前色彩"));}
    }
    void start(const std::filesystem::path& file){
        if(running){status(trW(L"请等待当前提色结束，或取消",L"Wait for extraction, or cancel",L"請等待目前擷取結束，或取消"));return;}if(worker.joinable())worker.join();ready=false;canceled=false;running=true;
        status(trW(L"正在后台提色…",L"Extracting in background…",L"正在背景擷取…"));SetTimer(owner,72,40,nullptr);
        worker=std::jthread([this,file]{const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);cv::Mat output;std::string failure;try{output=ColorCatalog::readImage(file);}catch(...){failure="decode";}{std::lock_guard lock(resultMutex);pending=std::move(output);error=std::move(failure);}if(SUCCEEDED(initialized))CoUninitialize();ready=true;running=false;});
    }
    void open(){wchar_t file[32768]{};OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=owner;dialog.lpstrFile=file;dialog.nMaxFile=32768;dialog.lpstrFilter=L"图片 (PNG/JPEG/BMP/GIF/TIFF/WebP)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp\0所有文件\0*.*\0\0";dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&dialog))start(file);}
    void clipboard(){
        if(!OpenClipboard(owner)){status(trW(L"剪贴板忙，请重试",L"Clipboard busy; retry",L"剪貼簿忙碌，請重試"));return;}
        cv::Mat snapshot;const auto bitmap=static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));BITMAP info{};
        if(bitmap&&GetObjectW(bitmap,sizeof(info),&info)&&info.bmWidth>0&&info.bmHeight>0&&uint64_t(info.bmWidth)*info.bmHeight<=16000000){
            const HDC screen=GetDC(nullptr),source=CreateCompatibleDC(screen),target=CreateCompatibleDC(screen);const auto old=SelectObject(source,bitmap);
            const double factor=(std::min)(1.,512./(std::max)(info.bmWidth,info.bmHeight));const int w=(std::max)(1,int(info.bmWidth*factor)),h=(std::max)(1,int(info.bmHeight*factor));BITMAPINFO bi{};bi.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB};void* bits=nullptr;const auto dib=CreateDIBSection(screen,&bi,DIB_RGB_COLORS,&bits,nullptr,0);const auto oldTarget=SelectObject(target,dib);
            if(dib&&bits&&StretchBlt(target,0,0,w,h,source,0,0,info.bmWidth,info.bmHeight,SRCCOPY)){GdiFlush();snapshot=cv::Mat(h,w,CV_8UC4,bits).clone();for(int y=0;y<h;++y)for(int x=0;x<w;++x)snapshot.at<cv::Vec4b>(y,x)[3]=255;}
            SelectObject(target,oldTarget);SelectObject(source,old);if(dib)DeleteObject(dib);DeleteDC(target);DeleteDC(source);ReleaseDC(nullptr,screen);
        }CloseClipboard();if(snapshot.empty())status(trW(L"剪贴板没有位图，或超过1600万像素限制",L"No clipboard bitmap, or over 16MP budget",L"剪貼簿沒有點陣圖，或超過1600萬像素限制"));else{image=std::move(snapshot);quantify();}
    }
public:
    static constexpr UINT COLOR_MESSAGE=WM_APP+73;
    ~ColorCatalogPanel(){canceled=true;if(worker.joinable())worker.join();}
    void create(HWND window){owner=window;
        control(L"COMBOBOX",L"",Group,CBS_DROPDOWNLIST|WS_VSCROLL);control(L"EDIT",L"",Search,ES_AUTOHSCROLL);SendDlgItemMessageW(owner,Search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(trW(L"搜索色名 / HEX",L"Search name / HEX",L"搜尋色名 / HEX")));
        for(const auto& [id,label]:std::initializer_list<std::pair<int,const wchar_t*>>{{Previous,trW(L"上一页",L"Previous",L"上一頁")},{Next,trW(L"下一页",L"Next",L"下一頁")},{Apply,trW(L"颜色页",L"Edit color",L"色彩頁")},{Copy,trW(L"复制",L"Copy",L"複製")},{Favorite,trW(L"收藏",L"Favorite",L"收藏")},{Open,trW(L"导入图片",L"Open image",L"匯入圖片")},{Clipboard,trW(L"粘贴图片",L"Paste image",L"貼上圖片")},{Current,trW(L"当前图片",L"Current image",L"目前圖片")},{Cancel,trW(L"取消提色",L"Cancel",L"取消擷取")}})control(L"BUTTON",label,id);
        control(L"STATIC",L"",Status);control(L"COMBOBOX",L"",Count,CBS_DROPDOWNLIST);for(const auto* n:{L"3 色",L"5 色",L"6 色",L"8 色",L"12 色"})SendDlgItemMessageW(owner,Count,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(n));SendDlgItemMessageW(owner,Count,CB_SETCURSEL,2,0);
        for(int id:{Preset,Type,Stops})control(L"COMBOBOX",L"",id,CBS_DROPDOWNLIST);
        for(const auto* name:{trW(L"海风",L"Sea",L"海風"),trW(L"日落",L"Sunset",L"日落"),trW(L"黑白",L"Grayscale",L"黑白"),trW(L"森林",L"Forest",L"森林")})SendDlgItemMessageW(owner,Preset,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));for(size_t i=0;i<ColorCatalog::gradients().size();++i){const auto label=std::wstring(trW(L"参考渐变 ",L"Reference ",L"參考漸層 "))+std::to_wstring(i+1);SendDlgItemMessageW(owner,Preset,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
        for(const auto* name:{trW(L"线性",L"Linear",L"線性"),trW(L"径向",L"Radial",L"放射")})SendDlgItemMessageW(owner,Type,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));for(const auto* name:{L"2 色标",L"3 色标",L"4 色标"})SendDlgItemMessageW(owner,Stops,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));for(int id:{Preset,Type,Stops})SendDlgItemMessageW(owner,id,CB_SETCURSEL,0,0);
        control(L"EDIT",L"90",Angle,ES_AUTOHSCROLL);for(int i=0;i<4;++i)control(L"EDIT",ColorValue::hex(i%2?ColorValue::Rgb{0,116,217}:ColorValue::Rgb{83,202,164}).c_str(),Stop0+i,ES_AUTOHSCROLL);control(L"BUTTON",trW(L"渐变预览",L"Gradient preview",L"漸層預覽"),Preview,BS_OWNERDRAW);
        for(int i=0;i<25;++i)control(L"BUTTON",L"",Cell+i,BS_OWNERDRAW|BS_NOTIFY);
    }
    void layout(int nextPage,int density,HFONT font){
        dpi=density;const bool changed=page!=nextPage;if(changed){if(page==6){canceled=true;}page=nextPage;}
        for(int id:all)ShowWindow(GetDlgItem(owner,id),SW_HIDE);for(int i=0;i<25;++i)ShowWindow(GetDlgItem(owner,Cell+i),SW_HIDE);if(page<3)return;
        for(int i=0;i<25;++i)move(Cell+i,110+(i%5)*112,72+(i/5)*59,104,51,font,false);
        move(Status,110,372,556,20,font);move(Previous,110,398,70,24,font,page!=5);move(Next,186,398,70,24,font,page!=5);move(Apply,410,398,78,24,font,page!=5);move(Copy,496,398,78,24,font);move(Favorite,582,398,78,24,font,page!=5);
        if(page==3||page==4){move(Group,110,18,160,180,font);move(Search,282,18,378,26,font);if(changed){updating=true;SetDlgItemTextW(owner,Search,L"");SendDlgItemMessageW(owner,Group,CB_RESETCONTENT,0,0);for(const auto* group:page==3?std::initializer_list<const wchar_t*>{trW(L"全部系列",L"All series",L"全部系列"),L"Open Color",L"Flat UI",L"Fluent Design",L"Ant Design",L"Material Design"}:std::initializer_list<const wchar_t*>{trW(L"全部传统色",L"All traditional",L"全部傳統色"),L"中国",L"日本"})SendDlgItemMessageW(owner,Group,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(group));SendDlgItemMessageW(owner,Group,CB_SETCURSEL,0,0);updating=false;load();}}
        if(page==5){move(Preset,110,18,112,160,font);move(Type,230,18,112,130,font);move(Stops,350,18,112,140,font);move(Angle,478,18,90,26,font);move(Preview,110,64,552,264,font);for(int i=0;i<4;++i)move(Stop0+i,110+i*140,342,132,25,font,i<int(gradient.colors.size()));updateGradient();}
        if(page==6){move(Open,110,18,86,26,font);move(Clipboard,204,18,86,26,font);move(Current,298,18,86,26,font);move(Count,392,18,90,150,font);move(Cancel,490,18,90,26,font);if(changed){load();status(trW(L"导入、拖入或粘贴图片；仅本地处理，不保存源图路径",L"Open, drop or paste · local only · no source path saved",L"匯入、拖入或貼上圖片；僅本機處理，不儲存來源路徑"));}}
        cells();
    }
    bool command(int id,int notice){
        if(page<3||((id<500||id>530)&&(id<Cell||id>=Cell+25)))return false;
        if(updating)return true;
        if(id==Search&&notice==EN_CHANGE){load();return true;}if(id==Group&&notice==CBN_SELCHANGE){load();return true;}
        if(id>=Cell&&id<Cell+25){selected=offset+id-Cell;if(selected<int(rows.size())){notify(rows[selected].color);if(notice==BN_DOUBLECLICKED)jarkUtils::copyToClipboard(ColorValue::hex(rows[selected].color));}cells();return true;}
        if(id==Previous||id==Next){offset=(std::max)(0,offset+(id==Next?25:-25));selected=-1;cells();return true;}
        if(id==Open){open();return true;}if(id==Clipboard){clipboard();return true;}if(id==Current){image=ColorCatalog::getCurrent();if(image.empty())status(trW(L"没有当前图片，请先在看图窗口打开",L"No current image; open in viewer first",L"沒有目前圖片，請先在看圖視窗開啟"));else quantify();return true;}
        if(id==Count&&notice==CBN_SELCHANGE){quantify();return true;}if(id==Cancel){canceled=true;status(trW(L"已取消；后台结果不会应用",L"Canceled; pending result ignored",L"已取消；背景結果不會套用"));return true;}
        if(id==Preset&&notice==CBN_SELCHANGE){const ColorValue::Rgb a[]{{83,202,164},{255,94,98},{0,0,0},{19,78,36}},b[]{{0,116,217},{255,195,113},{255,255,255},{168,224,99}};const int index=(std::max)(0,int(SendDlgItemMessageW(owner,Preset,CB_GETCURSEL,0,0)));const auto colors=index<4?std::vector<ColorValue::Rgb>{a[index],b[index]}:ColorCatalog::gradients().at(index-4).colors;updating=true;for(size_t i=0;i<colors.size();++i)SetDlgItemTextW(owner,Stop0+int(i),ColorValue::hex(colors[i]).c_str());SendDlgItemMessageW(owner,Stops,CB_SETCURSEL,colors.size()-2,0);updating=false;updateGradient();return true;}
        if((id>=Type&&id<=Stops&&notice==CBN_SELCHANGE)||((id==Angle||(id>=Stop0&&id<Stop0+4))&&notice==EN_CHANGE)){updateGradient();return true;}
        if(id==Copy){if(page==5&&!validGradient)return true;const auto output=page==5?gradient.css():selected>=0&&selected<int(rows.size())?ColorValue::hex(rows[selected].color):L"";status(!output.empty()&&jarkUtils::copyToClipboard(output)?trW(L"已复制",L"Copied",L"已複製"):trW(L"复制失败",L"Copy failed",L"複製失敗"));return true;}
        if(selected>=0&&selected<int(rows.size())){if(id==Apply){PostMessageW(owner,WM_COMMAND,410,0);return true;}if(id==Favorite){try{ColorLibrary::Store(ColorLibrary::defaultPath()).favorite(rows[selected].color,rows[selected].name,L"");status(trW(L"已收藏",L"Favorited",L"已收藏"));}catch(...){status(trW(L"收藏失败，未保存",L"Favorite failed; not saved",L"收藏失敗，未儲存"));}return true;}}
        return true;
    }
    void timer(){if(!ready.exchange(false))return;KillTimer(owner,72);if(worker.joinable())worker.join();if(canceled||page!=6)return;std::lock_guard lock(resultMutex);if(!error.empty()){status(trW(L"无法读取此图片，原颜色未改变；请尝试PNG/JPEG/TIFF",L"Cannot decode image; color unchanged. Try PNG/JPEG/TIFF",L"無法讀取此圖片，原色彩未變；請嘗試PNG/JPEG/TIFF"));return;}image=std::move(pending);quantify();}
    void drop(HDROP drop){if(page==6){wchar_t file[32768]{};if(DragQueryFileW(drop,0,file,32768))start(file);}DragFinish(drop);}
    bool draw(const DRAWITEMSTRUCT& item,bool dark,HFONT font){
        if(item.CtlID==Preview){cv::Mat preview((std::max)(1,int(item.rcItem.bottom-item.rcItem.top)),(std::max)(1,int(item.rcItem.right-item.rcItem.left)),CV_8UC4);for(int y=0;y<preview.rows;++y)for(int x=0;x<preview.cols;++x){const auto c=gradient.at(gradient.position(x,y,preview.cols,preview.rows));preview.at<cv::Vec4b>(y,x)={c.b,c.g,c.r,255};}BITMAPINFO bi{};bi.bmiHeader={sizeof(BITMAPINFOHEADER),preview.cols,-preview.rows,1,32,BI_RGB};SetDIBitsToDevice(item.hDC,0,0,preview.cols,preview.rows,0,0,0,preview.rows,preview.data,&bi,DIB_RGB_COLORS);return true;}
        if(item.CtlID<Cell||item.CtlID>=Cell+25)return false;const int index=offset+item.CtlID-Cell;if(index>=int(rows.size()))return true;const auto& e=rows[index];const auto brush=CreateSolidBrush(RGB(e.color.r,e.color.g,e.color.b));FillRect(item.hDC,&item.rcItem,brush);DeleteObject(brush);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,ContrastRatio::between(0xff000000,CurrentColor::pack(e.color))>=4.5?RGB(0,0,0):RGB(255,255,255));const auto previous=SelectObject(item.hDC,font);RECT text=item.rcItem;InflateRect(&text,-s(4),-s(5));const auto label=e.name+L"\n"+ColorValue::hex(e.color);DrawTextW(item.hDC,label.c_str(),-1,&text,DT_CENTER|DT_NOPREFIX|DT_END_ELLIPSIS);SelectObject(item.hDC,previous);if(index==selected||item.itemState&ODS_FOCUS){RECT border=item.rcItem;InflateRect(&border,-2,-2);DrawFocusRect(item.hDC,&border);}return true;
    }
};
