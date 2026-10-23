#pragma once
#include "ColorLibrary.h"
#include "CurrentColor.h"
#include "jarkUtils.h"

// 工作页的原生导航与数据页。所有持久化仅发生在显式操作，不放进绘制路径。
class ColorLibraryPanel {
    enum {NavColor=410,NavHistory=411,NavFavorites=412,RecordColor=414,AddCurrent=415,NavPalette=480,NavTraditional=481,NavGradient=482,NavImage=483,
        Search=420,Source=421,From=422,To=423,List=424,Copy=430,Edit=431,Delete=432,Favorite=433,Up=434,Down=435,Previous=436,Next=437,Refresh=438,
        Capacity=440,Copies=441,Coordinates=442,SavePrefs=443,Clear=444,Backup=445,Restore=446,Name=450,Tags=451,Hex=452,Save=453,Status=460};
    HWND owner=nullptr;int dpi=96,page_=0,offset=0;
    bool updating=false;
    std::filesystem::path path_;
    std::vector<ColorLibrary::Record> items;
    int s(int x)const{return MulDiv(x,dpi,96);}
    std::wstring text(int id){wchar_t value[512]{};GetDlgItemTextW(owner,id,value,512);return value;}
    void status(const wchar_t* value){SetDlgItemTextW(owner,Status,value);}
    void control(const wchar_t* cls,const wchar_t* label,int id,DWORD style=0){CreateWindowExW(wcscmp(cls,L"EDIT")==0?WS_EX_CLIENTEDGE:0,cls,label,WS_CHILD|WS_TABSTOP|style,0,0,1,1,owner,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);}
    static constexpr int all[]={Search,Source,From,To,List,Copy,Edit,Delete,Favorite,Up,Down,Previous,Next,Refresh,Capacity,Copies,Coordinates,SavePrefs,Clear,Backup,Restore,Name,Tags,Hex,Save,Status};
    void selection(){
        const auto* value=selected();EnableWindow(GetDlgItem(owner,Copy),value!=nullptr);EnableWindow(GetDlgItem(owner,Edit),value!=nullptr);EnableWindow(GetDlgItem(owner,Delete),value!=nullptr);
        EnableWindow(GetDlgItem(owner,Favorite),value!=nullptr);EnableWindow(GetDlgItem(owner,Up),value!=nullptr);EnableWindow(GetDlgItem(owner,Down),value!=nullptr);EnableWindow(GetDlgItem(owner,Save),value!=nullptr);
        updating=true;SetDlgItemTextW(owner,Name,value?ColorLibrary::wide(value->name).c_str():L"");SetDlgItemTextW(owner,Tags,value?ColorLibrary::wide(value->tags).c_str():L"");SetDlgItemTextW(owner,Hex,value?ColorValue::hex(value->color).c_str():L"");updating=false;
    }
    const ColorLibrary::Record* selected(){const auto index=SendDlgItemMessageW(owner,List,LB_GETCURSEL,0,0);return index>=0&&index<static_cast<LRESULT>(items.size())?&items[index]:nullptr;}
    bool confirm(const wchar_t* message){return MessageBoxW(owner,message,trW(L"颜色数据",L"Color data",L"色彩資料"),MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES;}
public:
    static constexpr int NAV_WIDTH=96;
    static constexpr UINT EDIT_MESSAGE=WM_APP+72;
    long long parent=0;
    int page()const{return page_;}
    void create(HWND window,bool testMode=false){
        owner=window;path_=ColorLibrary::defaultPath();
        control(L"BUTTON",trW(L"颜色",L"Color",L"色彩"),NavColor);control(L"BUTTON",trW(L"取色历史",L"History",L"取色歷史"),NavHistory);control(L"BUTTON",trW(L"收藏颜色",L"Favorites",L"收藏色彩"),NavFavorites);
        control(L"BUTTON",trW(L"色卡",L"Palettes",L"色卡"),NavPalette);control(L"BUTTON",trW(L"传统色",L"Traditional",L"傳統色"),NavTraditional);control(L"BUTTON",trW(L"渐变色",L"Gradients",L"漸層色"),NavGradient);control(L"BUTTON",trW(L"图片色卡",L"Image palette",L"圖片色卡"),NavImage);
        control(L"EDIT",L"",Search,ES_AUTOHSCROLL);SendDlgItemMessageW(owner,Search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(trW(L"搜索HEX、名称、标签",L"Search HEX, name, tags",L"搜尋HEX、名稱、標籤")));SendDlgItemMessageW(owner,Search,EM_SETLIMITTEXT,160,0);
        control(L"COMBOBOX",L"",Source,CBS_DROPDOWNLIST|WS_VSCROLL);for(const auto* value:{trW(L"全部来源",L"All sources",L"全部來源"),trW(L"屏幕取色",L"Screen",L"螢幕取色"),trW(L"颜色编辑",L"Editor",L"色彩編輯")})SendDlgItemMessageW(owner,Source,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));SendDlgItemMessageW(owner,Source,CB_SETCURSEL,0,0);
        for(int id:{From,To}){control(L"EDIT",L"",id,ES_AUTOHSCROLL);SendDlgItemMessageW(owner,id,EM_SETLIMITTEXT,10,0);SendDlgItemMessageW(owner,id,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(id==From?L"≥ YYYY-MM-DD":L"≤ YYYY-MM-DD"));}
        control(L"LISTBOX",L"",List,LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|WS_VSCROLL);
        for(const auto& item:std::initializer_list<std::pair<int,const wchar_t*>>{
            {Copy,trW(L"复制",L"Copy",L"複製")},{Edit,trW(L"再编辑",L"Edit",L"再編輯")},{Delete,trW(L"删除",L"Delete",L"刪除")},{Favorite,trW(L"收藏",L"Favorite",L"收藏")},{Up,trW(L"上移",L"Up",L"上移")},{Down,trW(L"下移",L"Down",L"下移")},
            {Previous,trW(L"上一页",L"Previous",L"上一頁")},{Next,trW(L"下一页",L"Next",L"下一頁")},{Refresh,trW(L"刷新",L"Refresh",L"重新整理")},{SavePrefs,trW(L"保存设置",L"Save options",L"儲存設定")},{Clear,trW(L"清空历史",L"Clear history",L"清空歷史")},{Backup,trW(L"备份",L"Backup",L"備份")},{Restore,trW(L"恢复备份",L"Restore backup",L"還原備份")},{Save,trW(L"保存修改",L"Save changes",L"儲存修改")}})control(L"BUTTON",item.second,item.first);
        control(L"COMBOBOX",L"",Capacity,CBS_DROPDOWNLIST|WS_VSCROLL);for(const auto* value:{L"100",L"500",L"1000",L"5000",trW(L"无限",L"Unlimited",L"無限")})SendDlgItemMessageW(owner,Capacity,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));
        control(L"BUTTON",trW(L"复制即记录",L"Record copies",L"複製即記錄"),Copies,BS_AUTOCHECKBOX);control(L"BUTTON",trW(L"保留坐标",L"Keep coordinates",L"保留座標"),Coordinates,BS_AUTOCHECKBOX);
        for(int id:{Name,Tags,Hex}){control(L"EDIT",L"",id,ES_AUTOHSCROLL);SendDlgItemMessageW(owner,id,EM_SETLIMITTEXT,id==Name?80:id==Tags?160:9,0);SendDlgItemMessageW(owner,id,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(id==Name?trW(L"收藏名称",L"Name",L"收藏名稱"):id==Tags?trW(L"标签",L"Tags",L"標籤"):L"#RRGGBB"));}
        control(L"STATIC",L"",Status);
        try{ColorLibrary::Store store(path_);page_=testMode?0:store.preferences().page;}catch(...){page_=0;status(trW(L"颜色数据无法读取；原文件已保留",L"Color data unavailable; file preserved",L"色彩資料無法讀取；原檔保留"));}if(page_)reload();else selection();
    }
    void layout(int width,int height,int density,HFONT font){
        dpi=density;const int x=NAV_WIDTH+14,w=(std::max)(180,width-x-12),bottom=height-12;
        const auto place=[&](int id,int px,int py,int pw,int ph,bool visible=true){const HWND h=GetDlgItem(owner,id);MoveWindow(h,s(px),s(py),s(pw),s(ph),TRUE);ShowWindow(h,visible?SW_SHOW:SW_HIDE);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);};
        const int navigation[]{NavColor,NavHistory,NavPalette,NavTraditional,NavGradient,NavImage,NavFavorites};
        for(int i=0;i<7;++i)place(navigation[i],6,14+i*40,NAV_WIDTH-12,32);
        for(int id:all)ShowWindow(GetDlgItem(owner,id),(page_==1||page_==2)?SW_SHOW:SW_HIDE);if(page_!=1&&page_!=2)return;
        const bool favorites=page_==2;
        const bool wrap=!favorites&&w<640;const int actionY=wrap?98:60,listY=actionY+44;
        place(Search,x,18,(std::min)(260,w),30);place(Source,x+(wrap?0:270),wrap?56:18,120,160,!favorites);
        place(From,x+(wrap?130:400),wrap?56:18,110,28,!favorites);place(To,x+(wrap?248:518),wrap?56:18,110,28,!favorites);
        int col=0;for(int id:{Copy,Edit,Delete,Favorite,Up,Down}){const bool show=(id!=Favorite||!favorites)&&((id!=Up&&id!=Down)||favorites);place(id,x+col*76,actionY,70,30,show);if(show)++col;}
        place(List,x,listY,w,(std::max)(100,bottom-listY-154));SendDlgItemMessageW(owner,List,LB_SETITEMHEIGHT,0,s(54));
        const int y=bottom-142;
        place(Previous,x,y,76,28);place(Next,x+84,y,76,28);place(Refresh,x+168,y,76,28);
        place(Name,x,y+38,140,28,favorites);place(Tags,x+148,y+38,w-368,28,favorites);place(Hex,x+w-212,y+38,106,28,favorites);place(Save,x+w-98,y+38,98,28,favorites);
        place(Capacity,x,y+38,88,180,!favorites);place(Copies,x+100,y+38,142,28,!favorites);place(Coordinates,x+246,y+38,140,28,!favorites);place(SavePrefs,x+394,y+38,110,28,!favorites);
        place(Clear,x,y+76,110,28,!favorites);place(Backup,x+120,y+76,80,28);place(Restore,x+210,y+76,116,28);
        place(Status,x,y+112,w,28);
    }
    void reload(){
        updating=true;
        try{ColorLibrary::Store store(path_);ColorLibrary::Query q;q.text=text(Search);q.offset=offset;
            const auto source=SendDlgItemMessageW(owner,Source,CB_GETCURSEL,0,0);if(source==1)q.source="screen";if(source==2)q.source="editor";
            if(page_==1){q.fromDate=ColorLibrary::utf8(text(From));q.toDate=ColorLibrary::utf8(text(To));}else q.source.clear();
            items=store.list(page_==2,q);SendDlgItemMessageW(owner,List,LB_RESETCONTENT,0,0);
            for(const auto& value:items){const auto title=ColorValue::hex(value.color)+L"  "+ColorLibrary::wide(value.name.empty()?value.utc:value.name)+L"  "+ColorLibrary::wide(value.tags);
                SendDlgItemMessageW(owner,List,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(title.c_str()));}
            const auto p=store.preferences();const int capacities[]={100,500,1000,5000,0};for(int i=0;i<5;++i)if(capacities[i]==p.capacity)SendDlgItemMessageW(owner,Capacity,CB_SETCURSEL,i,0);
            SendDlgItemMessageW(owner,Copies,BM_SETCHECK,p.recordCopies?BST_CHECKED:BST_UNCHECKED,0);SendDlgItemMessageW(owner,Coordinates,BM_SETCHECK,p.coordinates?BST_CHECKED:BST_UNCHECKED,0);
            if(!items.empty())SendDlgItemMessageW(owner,List,LB_SETCURSEL,0,0);selection();
            const auto hint=items.empty()?trW(L"没有匹配的颜色",L"No matching colors",L"沒有符合的色彩"):trW(L"时间为UTC；复制和编辑不会自动增加历史",L"UTC time; copy/edit do not add history",L"時間為UTC；複製與編輯不會自動加歷史");status(hint);
        }catch(...){items.clear();SendDlgItemMessageW(owner,List,LB_RESETCONTENT,0,0);selection();status(trW(L"读取失败，原数据保留；请检查权限/筛选/备份",L"Read failed; data preserved. Check filters/permissions/backup",L"讀取失敗，原資料保留；請檢查權限/篩選/備份"));}updating=false;
    }
    bool command(int id,int notice){
        if(id>=Name&&id<=Hex&&notice==EN_CHANGE)return true;
        if((id>=NavColor&&id<=NavFavorites)||(id>=NavPalette&&id<=NavImage)){KillTimer(owner,71);page_=id>=NavPalette?id-NavPalette+3:id-NavColor;offset=0;try{ColorLibrary::Store(path_).page(page_);}catch(...){}SendMessageW(owner,WM_SIZE,0,0);if(page_==1||page_==2)reload();return true;}
        if((id==Search||id==From||id==To)&&notice==EN_CHANGE&&!updating){SetTimer(owner,71,250,nullptr);return true;}
        if(id==Source&&notice==CBN_SELCHANGE){offset=0;reload();return true;}
        if(id==List){if(notice==LBN_SELCHANGE)selection();if(notice!=LBN_DBLCLK)return true;id=Edit;}
        if(id<RecordColor||id>Status)return false;
        try{
            // 414/415旧命令编号保留但不再创建或执行，避免隐藏命令悄悄记录。
            if(id==RecordColor||id==AddCurrent)return true;
            if(id==Previous||id==Next||id==Refresh){if(id==Previous)offset=(std::max)(0,offset-100);if(id==Next&&items.size()==100)offset+=100;reload();return true;}
            if(id==Restore){std::filesystem::path latest;for(const auto& file:std::filesystem::directory_iterator(path_.parent_path())){const auto name=file.path().filename().wstring();if(file.is_regular_file()&&name.starts_with(L"colors-")&&name.ends_with(L".backup.sqlite")&&file.path()>latest)latest=file.path();}if(latest.empty())throw std::runtime_error("backup missing");
                const auto question=std::wstring(trW(L"用最新备份替换当前历史和收藏？原数据另存，不能撤销：\n",L"Replace history and favorites with latest backup? Original kept separately:\n",L"用最新備份取代目前歷史及收藏？原資料另存，不能復原：\n"))+latest.filename().wstring();
                if(confirm(question.c_str())){ColorLibrary::Store::recover(path_,latest);reload();}return true;}
            ColorLibrary::Store store(path_);
            if(id==SavePrefs){auto p=store.preferences();const auto index=SendDlgItemMessageW(owner,Capacity,CB_GETCURSEL,0,0);if(index<0||index>4)throw std::runtime_error("capacity");const int values[]={100,500,1000,5000,0};p.capacity=values[index];p.recordCopies=SendDlgItemMessageW(owner,Copies,BM_GETCHECK,0,0)==BST_CHECKED;p.coordinates=SendDlgItemMessageW(owner,Coordinates,BM_GETCHECK,0,0)==BST_CHECKED;store.preferences(p);reload();return true;}
            if(id==Clear){if(confirm(trW(L"清空全部取色历史？收藏不会删除。",L"Clear all history? Favorites are kept.",L"清空全部取色歷史？收藏不會刪除。"))){store.clearHistory();offset=0;reload();}return true;}
            if(id==Backup){auto stamp=ColorLibrary::utcNow();for(auto& c:stamp)if(c==':')c='-';store.backup(path_.parent_path()/ColorLibrary::wide("colors-"+stamp+".backup.sqlite"));status(trW(L"备份已保存到颜色数据目录（保留旧备份）",L"Backup saved; older backups kept",L"備份已儲存至色彩資料目錄（保留舊備份）"));return true;}
            const auto* value=selected();if(!value)return true;
            if(id==Copy){if(!jarkUtils::copyToClipboard(ColorValue::hex(value->color)))throw std::runtime_error("clipboard");status(trW(L"已复制HEX",L"HEX copied",L"已複製HEX"));return true;}
            if(id==Edit){parent=page_==1?value->id:0;PostMessageW(owner,EDIT_MESSAGE,WPARAM(parent),LPARAM(CurrentColor::pack(value->color)));command(NavColor,0);return true;}
            if(id==Delete){if(confirm(trW(L"删除选中的颜色记录？",L"Delete the selected color record?",L"刪除所選色彩記錄？"))){store.erase(page_==2,value->id);reload();}return true;}
            if(id==Favorite){store.favorite(value->color,ColorValue::hex(value->color),L"");status(trW(L"已收藏",L"Favorited",L"已收藏"));return true;}
            if(id==Up||id==Down){store.moveFavorite(value->id,id==Up?-1:1);reload();return true;}
            if(id==Save){const auto color=ColorValue::parse(text(Hex));if(!color)throw std::runtime_error("color");store.favorite(*color,text(Name),text(Tags),value->id);reload();return true;}
        }catch(...){status(trW(L"操作失败，未报告保存成功；请检查权限/数值/备份",L"Operation failed; not saved. Check permissions/values/backup",L"操作失敗，未報告儲存成功；請檢查權限/數值/備份"));MessageBeep(MB_ICONWARNING);}
        return id>=RecordColor&&id<=Status;
    }
    void timer(){KillTimer(owner,71);offset=0;reload();}
    bool draw(const DRAWITEMSTRUCT& item,bool dark,HFONT font){
        if(item.CtlID!=List||item.itemID>=items.size())return false;
        const auto& r=items[item.itemID];const auto bg=CreateSolidBrush(item.itemState&ODS_SELECTED?(dark?RGB(52,73,93):RGB(219,234,248)):(dark?RGB(30,33,38):RGB(255,255,255)));FillRect(item.hDC,&item.rcItem,bg);DeleteObject(bg);
        RECT swatch{item.rcItem.left+s(8),item.rcItem.top+s(9),item.rcItem.left+s(42),item.rcItem.top+s(43)};const auto brush=CreateSolidBrush(RGB(r.color.r,r.color.g,r.color.b));FillRect(item.hDC,&swatch,brush);DeleteObject(brush);
        const auto previous=SelectObject(item.hDC,font);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,dark?RGB(235,235,235):RGB(35,43,57));RECT area=item.rcItem;area.left+=s(54);area.top+=s(4);area.bottom=area.top+s(23);
        auto text=ColorValue::hex(r.color)+L"  "+ColorLibrary::wide(r.name);DrawTextW(item.hDC,text.c_str(),-1,&area,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);area.top+=s(23);area.bottom+=s(23);
        text=ColorLibrary::wide(r.utc)+L"  "+(r.source=="screen"?trW(L"屏幕",L"Screen",L"螢幕"):r.source=="editor"?trW(L"编辑",L"Editor",L"編輯"):ColorLibrary::wide(r.tags));if(r.coordinate)text+=L" ("+std::to_wstring(r.coordinate->x)+L", "+std::to_wstring(r.coordinate->y)+L")";
        DrawTextW(item.hDC,text.c_str(),-1,&area,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);SelectObject(item.hDC,previous);if(item.itemState&ODS_FOCUS)DrawFocusRect(item.hDC,&item.rcItem);return true;
    }
};
