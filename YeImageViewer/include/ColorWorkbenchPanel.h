#pragma once
#include "jarkUtils.h"
#include "ColorFlowView.h"
#include <commdlg.h>
#include <thread>
#include <mutex>
#include <condition_variable>

// 一套导航/全局搜索/连续视口，不创建翻页和重复动作按钮。
class ColorWorkbenchPanel {
    enum {NavColor=410,NavHistory=411,NavFavorites=412,Search=420,Source=421,From=422,To=423,Flow=424,Results=425,
        Edit=431,Delete=432,Favorite=433,Jump=439,Capacity=440,Copies=441,Coordinates=442,Clear=444,Backup=445,Restore=446,
        Name=450,Tags=451,Hex=452,Status=460,NavPalette=480,NavTraditional=481,NavGradient=482,NavImage=483,
        Open=510,Clipboard=511,Current=512,Count=513,Stop0=524,Angle=528,Kind=529};
    HWND owner=nullptr;int dpi=96,page_=0;bool updating=false,searching=false,editing=false,gradientEditing=false;
    std::filesystem::path path_;ColorFlowView view,results;
    std::vector<ColorLibrary::Record> records;std::vector<ColorFlow::Item> rows,searchRows;
    std::vector<ColorLibrary::Asset> assets;
    std::optional<ColorFlow::Item> selected;
    long long editedRecord=0;std::wstring searchText;int offsets[7]{};
    std::jthread imageWorker;std::mutex imageMutex;std::atomic<bool> imageReady=false,imageRunning=false;
    std::vector<ColorLibrary::Asset> pendingImages;bool imageError=false;
    std::jthread searchWorker;std::mutex searchMutex;std::condition_variable searchWake;
    std::wstring pendingSearch;std::vector<ColorFlow::Item> pendingResults;
    uint64_t searchGeneration=0,readyGeneration=0;bool searchPending=false,searchFailed=false;
    std::optional<ColorCatalog::Gradient> editedGradient;
    int s(int n)const{return MulDiv(n,dpi,96);}
    std::wstring text(int id)const{wchar_t value[1024]{};GetDlgItemTextW(owner,id,value,1024);return value;}
    void status(const wchar_t* value){SetDlgItemTextW(owner,Status,value);}
    void failure(){status(trW(L"操作失败，数据未报告保存成功；请检查数值、权限或备份",L"Operation failed; data not reported as saved",L"操作失敗，資料未報告儲存成功；請檢查數值、權限或備份"));}
    void control(const wchar_t* cls,const wchar_t* label,int id,DWORD style=0){const auto child=CreateWindowExW(0,cls,label,WS_CHILD|WS_TABSTOP|style,0,0,1,1,owner,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);if(wcscmp(cls,L"EDIT")==0)SendMessageW(child,EM_SETLIMITTEXT,160,0);}
    bool confirm(const std::wstring& question){return MessageBoxW(owner,question.c_str(),trW(L"颜色数据",L"Color data",L"色彩資料"),MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES;}
    static long long suffix(const std::wstring& key){const auto slash=key.find_last_of(L'/');if(slash==std::wstring::npos)return 0;try{return std::stoll(key.substr(slash+1));}catch(...){return 0;}}
    static ColorCatalog::Gradient gradientOf(const ColorFlow::Item& item){ColorCatalog::Gradient gradient;gradient.colors=item.stops;gradient.angle=item.angle;gradient.radial=item.radial;return gradient;}
    static ColorFlow::Item assetItem(const ColorLibrary::Asset& asset,int destination){ColorFlow::Item item;item.key=L"asset/"+std::to_wstring(asset.id);item.group=asset.group;item.title=asset.name;item.color=asset.colors.front();item.stops=asset.colors;item.favorite=asset.favorite;item.destination=destination;item.angle=asset.angle;item.radial=asset.radial;item.copy=gradientOf(item).css();item.detail=std::to_wstring(asset.colors.size())+trW(L" 色渐变",L" color gradient",L" 色漸層");return item;}
    void appendRecords(std::vector<ColorFlow::Item>& out,ColorLibrary::Store& store,bool favorites,const std::wstring& query={}){
        ColorLibrary::Query filter;filter.text=query;filter.limit=5000;
        if(!favorites&&query.empty()){const auto source=SendDlgItemMessageW(owner,Source,CB_GETCURSEL,0,0);if(source==1)filter.source="screen";if(source==2)filter.source="editor";filter.fromDate=ColorLibrary::utf8(text(From));filter.toDate=ColorLibrary::utf8(text(To));}
        for(filter.offset=0;;filter.offset+=filter.limit){const auto part=store.list(favorites,filter);
            for(const auto& record:part){ColorFlow::Item item;item.key=(favorites?L"favorite/":L"history/")+std::to_wstring(record.id);item.group=favorites?trW(L"收藏颜色",L"Favorite colors",L"收藏色彩"):ColorLibrary::wide(record.utc.substr(0,10));item.color=record.color;item.title=ColorValue::hex(record.color)+(record.name.empty()?L"":L"  "+ColorLibrary::wide(record.name));item.detail=ColorLibrary::wide(favorites?record.tags:record.utc);item.copy=ColorValue::hex(record.color);item.favorite=favorites;item.destination=favorites?2:1;out.push_back(std::move(item));}
            if(part.size()<size_t(filter.limit))break;}
    }
    void appendCatalog(std::vector<ColorFlow::Item>& out,int page,const std::wstring& query={}){
        const auto& catalog=ColorCatalog::entries();for(size_t i=0;i<catalog.size();++i){const auto& entry=catalog[i];if(entry.traditional!=(page==4))continue;
            const auto hex=ColorValue::hex(entry.color);if(!query.empty()&&!ColorCatalog::contains(entry.group+L" "+entry.name+L" "+entry.keywords+L" "+hex,query))continue;
            ColorFlow::Item item;item.key=L"catalog/"+std::to_wstring(i);item.group=entry.group;item.title=entry.name;item.detail=entry.keywords;item.color=entry.color;item.copy=hex;item.destination=page;out.push_back(std::move(item));}
    }
    void appendGradients(std::vector<ColorFlow::Item>& out,ColorLibrary::Store& store,const std::wstring& query={}){
        const ColorValue::Rgb first[]{{83,202,164},{255,94,98},{0,0,0},{19,78,36}},last[]{{0,116,217},{255,195,113},{255,255,255},{168,224,99}};
        const wchar_t* names[]{trW(L"海风",L"Sea",L"海風"),trW(L"日落",L"Sunset",L"日落"),trW(L"黑白",L"Grayscale",L"黑白"),trW(L"森林",L"Forest",L"森林")};
        const auto& reference=ColorCatalog::gradients();for(size_t i=0;i<reference.size()+4;++i){ColorCatalog::Gradient gradient;if(i<4)gradient.colors={first[i],last[i]};else gradient=reference[i-4];
            ColorFlow::Item item;item.key=L"gradient/"+std::to_wstring(i);item.group=i<4?trW(L"基本渐变",L"Basic gradients",L"基本漸層"):trW(L"参考渐变",L"Reference gradients",L"參考漸層");item.title=i<4?names[i]:std::wstring(trW(L"渐变 ",L"Gradient ",L"漸層 "))+std::to_wstring(i-3);item.color=gradient.colors.front();item.stops=gradient.colors;item.angle=gradient.angle;item.radial=gradient.radial;item.copy=gradient.css();item.destination=5;item.detail=std::to_wstring(gradient.colors.size())+trW(L" 色",L" colors",L" 色");
            if(query.empty()||ColorCatalog::contains(item.title+L" "+item.group+L" "+item.copy,query))out.push_back(std::move(item));}
        ColorLibrary::Query filter;filter.text=query;filter.limit=5000;for(filter.offset=0;;filter.offset+=filter.limit){const auto part=store.assets(1,false,filter);for(const auto& asset:part)out.push_back(assetItem(asset,5));if(part.size()<size_t(filter.limit))break;}
    }
    void appendImages(std::vector<ColorFlow::Item>& out,ColorLibrary::Store& store,const std::wstring& query={}){
        ColorLibrary::Query filter;filter.text=query;filter.limit=5000;for(filter.offset=0;;filter.offset+=filter.limit){const auto part=store.assets(2,false,filter);
            for(const auto& asset:part)for(size_t i=0;i<asset.colors.size();++i){ColorFlow::Item item;item.key=L"image/"+std::to_wstring(asset.id)+L"/"+std::to_wstring(i);item.group=asset.name+L" · "+std::to_wstring(asset.id);item.color=asset.colors[i];item.title=ColorValue::hex(item.color);item.copy=item.title;item.destination=6;item.favorite=false;item.detail=PickerCore::decimal2(asset.weights[i]/10000.)+L"%";out.push_back(std::move(item));}
            if(part.size()<size_t(filter.limit))break;}
    }
    void build(std::vector<ColorFlow::Item>& out,int page,const std::wstring& query={}){
        const size_t start=out.size();ColorLibrary::Store store(path_);if(page==1||page==2)appendRecords(out,store,page==2,query);if(page==3||page==4)appendCatalog(out,page,query);if(page==5)appendGradients(out,store,query);if(page==6)appendImages(out,store,query);
        if(page==2){ColorLibrary::Query q;q.text=query;q.limit=5000;for(q.offset=0;;q.offset+=q.limit){const auto part=store.assets(1,true,q);for(const auto& asset:part)out.push_back(assetItem(asset,2));if(part.size()<size_t(q.limit))break;}}
        const auto favorites=store.favoriteColors();for(size_t i=start;i<out.size();++i)if(out[i].stops.empty())out[i].favorite=favorites.contains(CurrentColor::pack(out[i].color));
        if(page==5){std::unordered_set<std::wstring> saved;ColorLibrary::Query q;q.limit=5000;for(q.offset=0;;q.offset+=q.limit){const auto part=store.assets(1,true,q);for(const auto& asset:part)saved.insert(assetItem(asset,5).copy);if(part.size()<size_t(q.limit))break;}for(size_t i=start;i<out.size();++i)out[i].favorite=saved.contains(out[i].copy);}
    }
    void choose(const ColorFlow::Item& item,bool copy){
        parent=0;SendMessageW(owner,COLOR_MESSAGE,0,CurrentColor::pack(item.color));
        if(!copy)return;
        const auto value=item.copy.empty()?ColorValue::hex(item.color):item.copy;
        if(!jarkUtils::copyToClipboard(value)){status(trW(L"复制失败，请重试",L"Copy failed; retry",L"複製失敗，請重試"));return;}
        status(trW(L"已复制",L"Copied",L"已複製"));
        try{ColorLibrary::Store store(path_);if(store.preferences().recordCopies)store.record(item.color,"editor",ColorLibrary::eventKey());}catch(...){status(trW(L"已复制，但历史未保存",L"Copied; history not saved",L"已複製，但歷史未儲存"));}
    }
    void favorite(const ColorFlow::Item& item){ColorLibrary::Store store(path_);if(item.stops.empty())store.ensureFavorite(item.color);
        else{auto asset=ColorLibrary::Asset{};asset.id=item.key.starts_with(L"asset/")?suffix(item.key):0;asset.kind=1;asset.name=item.title;asset.group=trW(L"收藏渐变",L"Favorite gradients",L"收藏漸層");asset.colors=item.stops;asset.angle=item.angle;asset.radial=item.radial;asset.favorite=true;store.ensureGradientFavorite(asset);}status(trW(L"已收藏",L"Favorited",L"已收藏"));reload(true);}
    bool saveEdit(){if(!editing)return true;try{const auto color=ColorValue::parse(text(Hex));if(!color){status(trW(L"HEX无效，修改未保存",L"Invalid HEX; changes not saved",L"HEX無效，修改未儲存"));return false;}
        ColorLibrary::Store(path_).favorite(*color,text(Name),text(Tags),editedRecord);return true;}catch(...){failure();return false;}}
    void savePreferences(){if(updating)return;try{ColorLibrary::Store store(path_);const auto old=store.preferences();auto next=old;const int values[]{100,500,1000,5000,0};const int i=int(SendDlgItemMessageW(owner,Capacity,CB_GETCURSEL,0,0));if(i<0||i>4)return;
        next.capacity=values[i];next.recordCopies=SendDlgItemMessageW(owner,Copies,BM_GETCHECK,0,0)==BST_CHECKED;next.coordinates=SendDlgItemMessageW(owner,Coordinates,BM_GETCHECK,0,0)==BST_CHECKED;
        if(next.capacity&&next.capacity!=old.capacity&&store.count(false)>next.capacity&&!confirm(std::wstring(trW(L"只保留最新历史，超出部分将删除。继续？",L"Keep only newest history and remove older entries?",L"只保留最新歷史，超出部分將刪除。繼續？")))){preferences();return;}
        store.preferences(next);reload(true);
    }catch(...){failure();}}
    void preferences(){try{const auto p=ColorLibrary::Store(path_).preferences();updating=true;const int values[]{100,500,1000,5000,0};for(int i=0;i<5;++i)if(values[i]==p.capacity)SendDlgItemMessageW(owner,Capacity,CB_SETCURSEL,i,0);SendDlgItemMessageW(owner,Copies,BM_SETCHECK,p.recordCopies?BST_CHECKED:BST_UNCHECKED,0);SendDlgItemMessageW(owner,Coordinates,BM_SETCHECK,p.coordinates?BST_CHECKED:BST_UNCHECKED,0);updating=false;}catch(...){updating=false;failure();}}
public:
    static constexpr int NAV_WIDTH=96;
    static constexpr UINT EDIT_MESSAGE=WM_APP+72,COLOR_MESSAGE=WM_APP+73;
    long long parent=0;
    ~ColorWorkbenchPanel(){if(searchWorker.joinable()){{std::lock_guard lock(searchMutex);searchWorker.request_stop();}searchWake.notify_all();searchWorker.join();}if(imageWorker.joinable()){imageWorker.request_stop();imageWorker.join();}}
    int page()const{return searching?7:page_;}
    static int navigationPage(int id){return id>=NavPalette&&id<=NavImage?id-NavPalette+3:id>=NavColor&&id<=NavFavorites?id-NavColor:-1;}
    bool activeNavigation(int id)const{return !searching&&navigationPage(id)==page_;}
    void create(HWND window,bool testMode=false){owner=window;path_=ColorLibrary::defaultPath();
        const std::pair<int,const wchar_t*> navigation[]{{NavColor,trW(L"颜色",L"Color",L"色彩")},{NavHistory,trW(L"取色历史",L"History",L"取色歷史")},{NavPalette,trW(L"色卡",L"Palettes",L"色卡")},{NavTraditional,trW(L"传统色",L"Traditional",L"傳統色")},{NavGradient,trW(L"渐变色",L"Gradients",L"漸層色")},{NavImage,trW(L"图片色卡",L"Image palette",L"圖片色卡")},{NavFavorites,trW(L"收藏颜色",L"Favorites",L"收藏色彩")}};
        for(const auto& [id,label]:navigation)control(L"BUTTON",label,id);control(L"EDIT",L"",Search,ES_AUTOHSCROLL|WS_BORDER);SendDlgItemMessageW(owner,Search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(trW(L"搜索全部",L"Search all",L"搜尋全部")));
        control(L"COMBOBOX",L"",Source,CBS_DROPDOWNLIST);for(const auto* label:{trW(L"全部来源",L"All sources",L"全部來源"),trW(L"屏幕取色",L"Screen",L"螢幕取色"),trW(L"颜色编辑",L"Editor",L"色彩編輯")})SendDlgItemMessageW(owner,Source,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendDlgItemMessageW(owner,Source,CB_SETCURSEL,0,0);
        for(int id:{From,To,Name,Tags,Hex})control(L"EDIT",L"",id,ES_AUTOHSCROLL|WS_BORDER);SendDlgItemMessageW(owner,From,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"≥ YYYY-MM-DD"));SendDlgItemMessageW(owner,To,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"≤ YYYY-MM-DD"));
        SendDlgItemMessageW(owner,Name,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(trW(L"名称",L"Name",L"名稱")));SendDlgItemMessageW(owner,Tags,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(trW(L"标签",L"Tags",L"標籤")));SendDlgItemMessageW(owner,Hex,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"HEX"));
        control(L"COMBOBOX",L"",Capacity,CBS_DROPDOWNLIST);for(const auto* label:{L"100",L"500",L"1000",L"5000",trW(L"无限",L"Unlimited",L"無限")})SendDlgItemMessageW(owner,Capacity,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        control(L"BUTTON",trW(L"复制即记录",L"Record copies",L"複製即記錄"),Copies,BS_AUTOCHECKBOX);control(L"BUTTON",trW(L"保留坐标",L"Keep coordinates",L"保留座標"),Coordinates,BS_AUTOCHECKBOX);
        for(const auto& [id,label]:std::initializer_list<std::pair<int,const wchar_t*>>{{Clear,trW(L"清空历史",L"Clear history",L"清空歷史")},{Backup,trW(L"备份",L"Backup",L"備份")},{Restore,trW(L"恢复备份",L"Restore backup",L"還原備份")},{Open,trW(L"导入图片",L"Open images",L"匯入圖片")},{Clipboard,trW(L"粘贴图片",L"Paste image",L"貼上圖片")},{Current,trW(L"当前图片",L"Current image",L"目前圖片")}})control(L"BUTTON",label,id);
        control(L"COMBOBOX",L"",Count,CBS_DROPDOWNLIST);for(int count:{3,5,6,8,12}){const auto label=std::to_wstring(count)+trW(L" 色",L" colors",L" 色");SendDlgItemMessageW(owner,Count,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}SendDlgItemMessageW(owner,Count,CB_SETCURSEL,2,0);control(L"STATIC",L"",Status);
        for(int i=0;i<4;++i)control(L"EDIT",L"",Stop0+i,ES_AUTOHSCROLL|WS_BORDER);control(L"EDIT",L"90",Angle,ES_AUTOHSCROLL|WS_BORDER);control(L"COMBOBOX",L"",Kind,CBS_DROPDOWNLIST);for(const auto* n:{trW(L"线性",L"Linear",L"線性"),trW(L"径向",L"Radial",L"放射")})SendDlgItemMessageW(owner,Kind,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(n));SendDlgItemMessageW(owner,Kind,CB_SETCURSEL,0,0);
        view.create(owner,Flow);results.create(owner,Results);try{page_=testMode?0:ColorLibrary::Store(path_).preferences().page;}catch(...){page_=0;failure();}preferences();if(page_)reload();
    }
    void layout(int width,int height,int density,HFONT font){dpi=density;const int x=NAV_WIDTH+14,w=width-x-12;
        const auto place=[&](int id,int px,int py,int pw,int ph,bool visible=true){const auto child=GetDlgItem(owner,id);MoveWindow(child,s(px),s(py),s(pw),s(ph),TRUE);ShowWindow(child,visible?SW_SHOW:SW_HIDE);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);};
        place(Search,6,12,NAV_WIDTH-12,28);const int navigation[]{NavColor,NavHistory,NavPalette,NavTraditional,NavGradient,NavImage,NavFavorites};for(int i=0;i<7;++i)place(navigation[i],6,52+i*40,NAV_WIDTH-12,32);
        const int contentControls[]{Source,From,To,Capacity,Copies,Coordinates,Clear,Backup,Restore,Name,Tags,Hex,Status,Open,Clipboard,Current,Count,Stop0,Stop0+1,Stop0+2,Stop0+3,Angle,Kind};
        for(int id:contentControls)ShowWindow(GetDlgItem(owner,id),SW_HIDE);
        const bool history=page_==1&&!searching,favorites=page_==2&&!searching,images=page_==6&&!searching;int top=12,bottom=height-30;
        if(history){place(Source,x,12,110,160);place(From,x+120,12,114,26);place(To,x+244,12,114,26);place(Capacity,x+370,12,88,180);place(Copies,x,48,160,26);place(Coordinates,x+174,48,160,26);top=84;}
        if(history||favorites){place(Clear,x,height-62,100,26,history);place(Backup,x+110,height-62,72,26);place(Restore,x+192,height-62,114,26);bottom=height-70;}
        if(favorites&&editing){place(Name,x,12,150,26);place(Tags,x+158,12,220,26);place(Hex,x+386,12,150,26);top=48;}
        if(images){place(Open,x,12,96,26);place(Clipboard,x+104,12,96,26);place(Current,x+208,12,96,26);place(Count,x+312,12,96,150);top=48;}
        if(page_==5&&gradientEditing&&!searching){for(int i=0;i<4;++i)place(Stop0+i,x+i*108,12,100,26,editedGradient&&i<int(editedGradient->colors.size()));place(Angle,x,46,80,26);place(Kind,x+88,46,120,120);top=84;}
        place(Status,x,height-26,w,22,page()!=0);
        view.place(x,top,w,std::max(40,bottom-top),density,font,!searching&&page_!=0,GlobalVar::isCurrentUIDarkMode);
        results.place(x,12,w,height-44,density,font,searching,GlobalVar::isCurrentUIDarkMode);
    }
    void reload(bool preserve=false){try{rows.clear();if(page_!=0)build(rows,page_);view.set(rows,page_==1||page_==2,false,preserve);if(searching)search();}catch(...){failure();}}
    void search(){searchText=text(Search);searching=!searchText.empty();searchRows.clear();results.set({});
        {std::lock_guard lock(searchMutex);++searchGeneration;pendingSearch=searchText;searchPending=searching;readyGeneration=0;}
        if(searching){status(trW(L"正在搜索全部颜色…",L"Searching all colors…",L"正在搜尋全部色彩…"));SetTimer(owner,73,40,nullptr);
            if(!searchWorker.joinable())searchWorker=std::jthread([this](std::stop_token stop){for(;;){std::wstring query;uint64_t generation;
                {std::unique_lock lock(searchMutex);searchWake.wait(lock,[&]{return stop.stop_requested()||searchPending;});if(stop.stop_requested())return;query=pendingSearch;generation=searchGeneration;searchPending=false;}
                std::vector<ColorFlow::Item> found;bool failed=false;try{for(int page=1;page<=6&&!stop.stop_requested();++page){const auto first=found.size();build(found,page,query);const wchar_t* names[]{L"",trW(L"取色历史",L"History",L"取色歷史"),trW(L"收藏颜色",L"Favorites",L"收藏色彩"),trW(L"色卡",L"Palettes",L"色卡"),trW(L"传统色",L"Traditional",L"傳統色"),trW(L"渐变色",L"Gradients",L"漸層色"),trW(L"图片色卡",L"Image palettes",L"圖片色卡")};for(size_t i=first;i<found.size();++i)found[i].group=std::wstring(names[page])+L" / "+found[i].group;
                    std::lock_guard lock(searchMutex);if(generation!=searchGeneration)break;}}catch(...){failed=true;}
                {std::lock_guard lock(searchMutex);if(generation==searchGeneration){pendingResults=std::move(found);searchFailed=failed;readyGeneration=generation;}}}});
            searchWake.notify_one();
        }else KillTimer(owner,73);SendMessageW(owner,WM_SIZE,0,0);}
    void searchTimer(){std::vector<ColorFlow::Item> found;bool failed;
        {std::lock_guard lock(searchMutex);if(!readyGeneration||readyGeneration!=searchGeneration)return;readyGeneration=0;found=std::move(pendingResults);failed=searchFailed;}
        KillTimer(owner,73);if(failed){failure();return;}searchRows=std::move(found);results.set(searchRows);status((std::to_wstring(searchRows.size())+trW(L" 个结果 · 右键跳到来源",L" results · right-click to locate",L" 個結果 · 右鍵跳到來源")).c_str());}
    void timer(){KillTimer(owner,71);search();}
    void wheel(short delta){if(searching)results.wheel(delta);else if(page_!=0)view.wheel(delta);}
    void theme(bool dark){view.theme(dark);results.theme(dark);}
    bool canClose(){return !editing||saveEdit();}
    bool draw(const DRAWITEMSTRUCT&,bool,HFONT){return false;}
    void addImage(const cv::Mat& image,const std::wstring& name){const int counts[]{3,5,6,8,12};const int choice=std::clamp(int(SendDlgItemMessageW(owner,Count,CB_GETCURSEL,0,0)),0,4);const auto colors=ColorCatalog::extract(image,counts[choice]);
        if(colors.empty()){status(trW(L"图片没有有效非透明颜色",L"No non-transparent image colors",L"圖片沒有有效非透明色彩"));return;}ColorLibrary::Asset asset;asset.kind=2;asset.name=name;asset.group=trW(L"图片色卡",L"Image palettes",L"圖片色卡");for(const auto& c:colors){asset.colors.push_back(c.color);asset.weights.push_back(int(std::lround(c.fraction*1000000)));}ColorLibrary::Store(path_).asset(asset);reload(true);}
    void startImages(std::vector<std::filesystem::path> files){if(files.empty())return;if(imageRunning){status(trW(L"正在处理上一批图片",L"Previous image batch is still running",L"正在處理上一批圖片"));return;}if(imageWorker.joinable())imageWorker.join();
        const int counts[]{3,5,6,8,12};const int count=counts[std::clamp(int(SendDlgItemMessageW(owner,Count,CB_GETCURSEL,0,0)),0,4)];imageRunning=true;imageReady=false;status(trW(L"正在后台提取色卡…",L"Extracting palettes…",L"正在背景擷取色卡…"));SetTimer(owner,72,40,nullptr);
        imageWorker=std::jthread([this,files=std::move(files),count](std::stop_token stop){std::vector<ColorLibrary::Asset> output;bool failed=false;const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            for(const auto& file:files){if(stop.stop_requested())break;try{const auto image=ColorCatalog::readImage(file);const auto colors=ColorCatalog::extract(image,count);if(colors.empty()){failed=true;continue;}ColorLibrary::Asset value;value.kind=2;value.name=file.filename().wstring();value.group=L"image";for(const auto& c:colors){value.colors.push_back(c.color);value.weights.push_back(int(std::lround(c.fraction*1000000)));}output.push_back(std::move(value));}catch(...){failed=true;}}
            {std::lock_guard lock(imageMutex);pendingImages=std::move(output);imageError=failed;}if(SUCCEEDED(com))CoUninitialize();imageRunning=false;imageReady=true;});
    }
    void imageTimer(){if(!imageReady.exchange(false))return;KillTimer(owner,72);if(imageWorker.joinable())imageWorker.join();try{std::vector<ColorLibrary::Asset> batch;bool failed;{std::lock_guard lock(imageMutex);batch=std::move(pendingImages);failed=imageError;}ColorLibrary::Store store(path_);for(const auto& asset:batch)store.asset(asset);reload(true);status(failed?trW(L"部分图片未提取，成功项已保留",L"Some images failed; successful palettes kept",L"部分圖片未擷取，成功項已保留"):trW(L"图片色卡已保存",L"Image palettes saved",L"圖片色卡已儲存"));}catch(...){failure();}}
    void openImages(){std::vector<wchar_t> buffer(65536);OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=owner;dialog.lpstrFile=buffer.data();dialog.nMaxFile=DWORD(buffer.size());dialog.lpstrFilter=L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp\0All files\0*.*\0\0";dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_ALLOWMULTISELECT|OFN_EXPLORER;if(!GetOpenFileNameW(&dialog)){if(CommDlgExtendedError())status(trW(L"无法读取选择结果；数量过多时请拖入图片或分批导入",L"Cannot read selection; drop images or import smaller batches",L"無法讀取選擇結果；數量過多時請拖入圖片或分批匯入"));return;}
        const auto first=std::filesystem::path(buffer.data());const auto* next=buffer.data()+wcslen(buffer.data())+1;std::vector<std::filesystem::path> files;if(!*next)files.push_back(first);else while(*next){files.push_back(first/next);next+=wcslen(next)+1;}startImages(std::move(files));}
    void pasteImage(){if(!OpenClipboard(owner)){status(trW(L"剪贴板忙，请重试",L"Clipboard busy; retry",L"剪貼簿忙碌，請重試"));return;}cv::Mat image;
        const auto bitmap=static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));BITMAP info{};
        if(bitmap&&GetObjectW(bitmap,sizeof(info),&info)&&info.bmWidth>0&&info.bmHeight>0&&uint64_t(info.bmWidth)*info.bmHeight<=16000000){const HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);const auto old=SelectObject(dc,bitmap);BITMAPINFO bi{};const double ratio=std::min(1.,512./std::max(info.bmWidth,info.bmHeight));const int w=std::max(1,int(info.bmWidth*ratio)),h=std::max(1,int(info.bmHeight*ratio));bi.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB};void* pixels=nullptr;const auto dib=CreateDIBSection(screen,&bi,DIB_RGB_COLORS,&pixels,nullptr,0);const HDC target=CreateCompatibleDC(screen);const auto previous=dib?SelectObject(target,dib):nullptr;
            if(dib&&pixels&&StretchBlt(target,0,0,w,h,dc,0,0,info.bmWidth,info.bmHeight,SRCCOPY)){GdiFlush();image=cv::Mat(h,w,CV_8UC4,pixels).clone();for(int y=0;y<h;++y)for(int x=0;x<w;++x)image.at<cv::Vec4b>(y,x)[3]=255;}if(previous)SelectObject(target,previous);if(dib)DeleteObject(dib);SelectObject(dc,old);DeleteDC(dc);DeleteDC(target);ReleaseDC(nullptr,screen);}CloseClipboard();
        if(image.empty()){status(trW(L"剪贴板没有可用位图",L"No supported clipboard bitmap",L"剪貼簿沒有可用點陣圖"));return;}addImage(image,trW(L"剪贴板图片",L"Clipboard image",L"剪貼簿圖片"));}
    void drop(HDROP drop){std::vector<std::filesystem::path> files;if(page_==6){const auto count=DragQueryFileW(drop,0xFFFFFFFF,nullptr,0);for(UINT i=0;i<count;++i){wchar_t file[32768]{};if(DragQueryFileW(drop,i,file,32768))files.emplace_back(file);}}DragFinish(drop);startImages(std::move(files));}
    void editGradient(const ColorFlow::Item& item){editedGradient=gradientOf(item);gradientEditing=true;selected=item;updating=true;for(int i=0;i<4;++i)SetDlgItemTextW(owner,Stop0+i,i<int(editedGradient->colors.size())?ColorValue::hex(editedGradient->colors[i]).c_str():L"");SetDlgItemTextW(owner,Angle,std::to_wstring(item.angle).c_str());SendDlgItemMessageW(owner,Kind,CB_SETCURSEL,item.radial?1:0,0);updating=false;SendMessageW(owner,WM_SIZE,0,0);}
    void updateGradient(){if(updating||!editedGradient||!selected)return;auto value=*editedGradient;for(size_t i=0;i<value.colors.size();++i){const auto color=ColorValue::parse(text(Stop0+int(i)));if(!color){status(trW(L"渐变HEX无效，未应用",L"Invalid gradient HEX; unchanged",L"漸層HEX無效，未套用"));return;}value.colors[i]=*color;}
        const auto angle=text(Angle);wchar_t* end=nullptr;const long degrees=wcstol(angle.c_str(),&end,10);if(angle.empty()||!end||*end||degrees<0||degrees>360){status(trW(L"角度应为0～360",L"Angle must be 0–360",L"角度應為0～360"));return;}value.angle=int(degrees);value.radial=SendDlgItemMessageW(owner,Kind,CB_GETCURSEL,0,0)==1;
        if(selected->key.starts_with(L"asset/")){try{ColorLibrary::Asset asset;asset.id=suffix(selected->key);asset.kind=1;asset.name=selected->title;asset.group=selected->group;asset.favorite=selected->favorite;asset.angle=value.angle;asset.radial=value.radial;asset.colors=value.colors;ColorLibrary::Store(path_).asset(asset);}catch(...){failure();return;}}
        editedGradient=value;selected->stops=value.colors;selected->angle=value.angle;selected->radial=value.radial;selected->color=value.colors.front();selected->copy=value.css();for(auto& row:rows)if(row.key==selected->key)row=*selected;view.set(rows,false,false,true);choose(*selected,false);status(trW(L"渐变已更新；点击色块复制CSS，星标保存收藏",L"Gradient updated; click copies CSS, star saves favorite",L"漸層已更新；點擊色塊複製CSS，星標儲存收藏"));}
    bool flow(WPARAM key,LPARAM index){const auto id=LOWORD(key),action=HIWORD(key);if(id!=Flow&&id!=Results)return false;const auto* row=(id==Results?results:view).item(int(index));if(!row)return true;const auto item=*row;selected=item;
        try{if(action==ColorFlowView::Copy){choose(item,true);return true;}if(action==ColorFlowView::Favorite){favorite(item);return true;}if(action!=ColorFlowView::Context)return true;
            const HMENU menu=CreatePopupMenu();if(id==Results)AppendMenuW(menu,MF_STRING,Jump,trW(L"跳到来源",L"Go to source",L"跳到來源"));
            AppendMenuW(menu,MF_STRING,Edit,trW(L"编辑",L"Edit",L"編輯"));
            if(item.key.starts_with(L"history/")||item.key.starts_with(L"favorite/")||item.key.starts_with(L"asset/"))AppendMenuW(menu,MF_STRING,Delete,trW(L"删除",L"Delete",L"刪除"));
            if(item.destination!=1&&item.destination!=2)AppendMenuW(menu,MF_STRING,Favorite,trW(L"收藏",L"Favorite",L"收藏"));POINT p{};GetCursorPos(&p);const auto chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,p.x,p.y,0,owner,nullptr);DestroyMenu(menu);
            return perform(chosen,item);
        }catch(...){failure();}return true;
    }
    bool perform(int chosen,const ColorFlow::Item item){try{
            if(chosen==Favorite){favorite(item);return true;}
            if(chosen==Jump){command(item.destination==1?NavHistory:item.destination==2?NavFavorites:NavPalette+item.destination-3,0);view.jump(item.key);return true;}
            if(chosen==Delete){if(!confirm(trW(L"删除所选颜色记录？",L"Delete the selected color record?",L"刪除所選色彩記錄？")))return true;ColorLibrary::Store store(path_);if(item.key.starts_with(L"asset/"))store.eraseAsset(suffix(item.key));else store.erase(item.key.starts_with(L"favorite/"),suffix(item.key));reload(true);return true;}
            if(chosen==Edit){if(!item.stops.empty()){if(page_!=5)command(NavGradient,0);editGradient(item);return true;}
                if(item.key.starts_with(L"favorite/")){command(NavFavorites,0);editedRecord=suffix(item.key);editing=true;ColorLibrary::Query query;query.text=item.copy;query.limit=5000;for(const auto& record:ColorLibrary::Store(path_).list(true,query))if(record.id==editedRecord){updating=true;SetDlgItemTextW(owner,Name,ColorLibrary::wide(record.name).c_str());SetDlgItemTextW(owner,Tags,ColorLibrary::wide(record.tags).c_str());SetDlgItemTextW(owner,Hex,ColorValue::hex(record.color).c_str());updating=false;break;}SendMessageW(owner,WM_SIZE,0,0);SetFocus(GetDlgItem(owner,Name));return true;}
                parent=item.key.starts_with(L"history/")?suffix(item.key):0;SendMessageW(owner,EDIT_MESSAGE,WPARAM(parent),CurrentColor::pack(item.color));command(NavColor,0);return true;}
        }catch(...){failure();}return true;
    }
    bool command(int id,int notice){if(updating)return id==Search||id==Name||id==Tags||id==Hex;
        if(selected&&(id==Edit||id==Delete||id==Favorite||id==Jump))return perform(id,*selected);
        if((id>=NavColor&&id<=NavFavorites)||(id>=NavPalette&&id<=NavImage)){if(editing&&!saveEdit())return true;editing=gradientEditing=false;if(!searching)offsets[page_]=view.scroll();page_=id>=NavPalette?id-NavPalette+3:id-NavColor;updating=true;SetDlgItemTextW(owner,Search,L"");updating=false;searching=false;searchRows.clear();{std::lock_guard lock(searchMutex);++searchGeneration;searchPending=false;readyGeneration=0;}KillTimer(owner,71);KillTimer(owner,73);try{ColorLibrary::Store(path_).page(page_);}catch(...){failure();}reload();SendMessageW(owner,WM_SIZE,0,0);view.restoreScroll(offsets[page_]);return true;}
        if(id==Search&&notice==EN_CHANGE){SetTimer(owner,71,250,nullptr);return true;}if((id==From||id==To)&&notice==EN_CHANGE){reload();return true;}if(id==Source&&notice==CBN_SELCHANGE){reload();return true;}
        if((id==Capacity&&notice==CBN_SELCHANGE)||((id==Copies||id==Coordinates)&&notice==BN_CLICKED)){savePreferences();return true;}
        if((id==Name||id==Tags||id==Hex)&&notice==EN_KILLFOCUS){if(saveEdit())reload(true);return true;}
        if((id>=Stop0&&id<Stop0+4&&notice==EN_CHANGE)||(id==Angle&&notice==EN_CHANGE)||(id==Kind&&notice==CBN_SELCHANGE)){updateGradient();return true;}
        try{if(id==Open){openImages();return true;}if(id==Clipboard){pasteImage();return true;}if(id==Current){const auto image=ColorCatalog::getCurrent();if(image.empty())status(trW(L"没有当前图片，请先在看图窗口打开",L"No current image; open in viewer first",L"沒有目前圖片，請先在看圖視窗開啟"));else addImage(image,trW(L"当前图片",L"Current image",L"目前圖片"));return true;}
            if(id==Count)return true;
            if(id==Clear){if(confirm(trW(L"清空全部取色历史？收藏不会删除。",L"Clear all history? Favorites are kept.",L"清空全部取色歷史？收藏不會刪除。"))){ColorLibrary::Store(path_).clearHistory();reload();}return true;}
            if(id==Backup){auto stamp=ColorLibrary::utcNow();for(auto& c:stamp)if(c==':')c='-';ColorLibrary::Store(path_).backup(path_.parent_path()/ColorLibrary::wide("colors-"+stamp+".backup.sqlite"));status(trW(L"备份已保存，旧备份保留",L"Backup saved; old backups kept",L"備份已儲存，舊備份保留"));return true;}
            if(id==Restore){std::filesystem::path latest;for(const auto& entry:std::filesystem::directory_iterator(path_.parent_path())){const auto name=entry.path().filename().wstring();if(entry.is_regular_file()&&name.starts_with(L"colors-")&&name.ends_with(L".backup.sqlite")&&entry.path()>latest)latest=entry.path();}if(latest.empty()){status(trW(L"没有可用备份",L"No backup available",L"沒有可用備份"));return true;}if(confirm(std::wstring(trW(L"用最新备份替换历史、收藏与图片色卡？原库另存：\n",L"Replace color data with latest backup? Original retained:\n",L"用最新備份取代歷史、收藏與圖片色卡？原庫另存：\n"))+latest.filename().wstring())){ColorLibrary::Store::recover(path_,latest);preferences();reload();}return true;}
        }catch(...){failure();return true;}return false;
    }
};
