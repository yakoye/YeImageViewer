#pragma once
#include "ColorValue.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include <vector>
#include <optional>
#include <stdexcept>
#include <chrono>
#include <atomic>
#include <cstdio>
#include <unordered_set>
#pragma comment(lib,"shell32.lib")

// 颜色数据独立存储，不读取或改写看图配置。仅从SYSTEM32加载系统SQLite。
namespace ColorLibrary {
struct sqlite3;struct sqlite3_stmt;struct sqlite3_backup;
struct Api {
    HMODULE module=LoadLibraryExW(L"winsqlite3.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    template<class T>T symbol(const char* name){return reinterpret_cast<T>(GetProcAddress(module,name));}
    int (__cdecl *open)(const char*,sqlite3**,int,const char*)=module?symbol<decltype(open)>("sqlite3_open_v2"):nullptr;
    int (__cdecl *close)(sqlite3*)=module?symbol<decltype(close)>("sqlite3_close_v2"):nullptr;
    int (__cdecl *exec)(sqlite3*,const char*,int(__cdecl*)(void*,int,char**,char**),void*,char**)=module?symbol<decltype(exec)>("sqlite3_exec"):nullptr;
    int (__cdecl *prepare)(sqlite3*,const char*,int,sqlite3_stmt**,const char**)=module?symbol<decltype(prepare)>("sqlite3_prepare_v2"):nullptr;
    int (__cdecl *step)(sqlite3_stmt*)=module?symbol<decltype(step)>("sqlite3_step"):nullptr;
    int (__cdecl *finalize)(sqlite3_stmt*)=module?symbol<decltype(finalize)>("sqlite3_finalize"):nullptr;
    int (__cdecl *bindInt)(sqlite3_stmt*,int,int)=module?symbol<decltype(bindInt)>("sqlite3_bind_int"):nullptr;
    int (__cdecl *bind64)(sqlite3_stmt*,int,long long)=module?symbol<decltype(bind64)>("sqlite3_bind_int64"):nullptr;
    int (__cdecl *bindText)(sqlite3_stmt*,int,const char*,int,void(__cdecl*)(void*))=module?symbol<decltype(bindText)>("sqlite3_bind_text"):nullptr;
    int (__cdecl *columnInt)(sqlite3_stmt*,int)=module?symbol<decltype(columnInt)>("sqlite3_column_int"):nullptr;
    long long (__cdecl *column64)(sqlite3_stmt*,int)=module?symbol<decltype(column64)>("sqlite3_column_int64"):nullptr;
    const unsigned char* (__cdecl *columnText)(sqlite3_stmt*,int)=module?symbol<decltype(columnText)>("sqlite3_column_text"):nullptr;
    long long (__cdecl *lastId)(sqlite3*)=module?symbol<decltype(lastId)>("sqlite3_last_insert_rowid"):nullptr;
    int (__cdecl *timeout)(sqlite3*,int)=module?symbol<decltype(timeout)>("sqlite3_busy_timeout"):nullptr;
    sqlite3_backup* (__cdecl *backupInit)(sqlite3*,const char*,sqlite3*,const char*)=module?symbol<decltype(backupInit)>("sqlite3_backup_init"):nullptr;
    int (__cdecl *backupStep)(sqlite3_backup*,int)=module?symbol<decltype(backupStep)>("sqlite3_backup_step"):nullptr;
    int (__cdecl *backupFinish)(sqlite3_backup*)=module?symbol<decltype(backupFinish)>("sqlite3_backup_finish"):nullptr;
    bool valid()const{return open&&close&&exec&&prepare&&step&&finalize&&bindInt&&bind64&&bindText&&columnInt&&column64&&columnText&&lastId&&timeout&&backupInit&&backupStep&&backupFinish;}
};
inline Api& api(){static Api value;return value;}
inline std::string utf8(std::wstring_view text){if(text.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);if(!n)throw std::runtime_error("text");std::string out(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),out.data(),n,nullptr,nullptr);return out;}
inline std::wstring wide(std::string_view text){if(text.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);if(!n)throw std::runtime_error("text");std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),out.data(),n);return out;}
inline std::filesystem::path defaultPath(){
    wchar_t overridePath[32768]{};const auto n=GetEnvironmentVariableW(L"YEIMAGEVIEWER_COLOR_DATA_DIR",overridePath,32768);
    if(n&&n<32768)return std::filesystem::path(overridePath)/L"colors.sqlite";
    PWSTR root=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&root)))throw std::runtime_error("path");
    const auto path=std::filesystem::path(root)/L"YeImageViewer"/L"ColorAssistant"/L"colors.sqlite";CoTaskMemFree(root);return path;
}
inline std::string eventKey(){static std::atomic<uint64_t> counter{0};return std::to_string(GetCurrentProcessId())+"-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count())+"-"+std::to_string(++counter);}
inline std::string utcNow(){SYSTEMTIME t{};GetSystemTime(&t);char result[32]{};sprintf_s(result,"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);return result;}
struct Record {long long id=0,parent=0,order=0;ColorValue::Rgb color;std::string utc,source,name,tags;std::optional<POINT> coordinate;};
struct Preferences {int capacity=1000;bool recordCopies=false,coordinates=false;int page=0;};
struct Query {std::wstring text;std::string source,fromDate,toDate;int offset=0,limit=100;};
struct Asset {long long id=0;int kind=1,angle=90;bool radial=false,favorite=false;
    std::wstring name,group;std::vector<ColorValue::Rgb> colors;std::vector<int> weights;};
class OpenGate {
    HANDLE handle=nullptr;
public:
    explicit OpenGate(const std::filesystem::path& path){auto key=std::filesystem::absolute(path).lexically_normal().wstring();uint64_t hash=1469598103934665603ULL;for(auto c:key){hash^=towlower(c);hash*=1099511628211ULL;}const auto name=L"Local\\YeImageViewer.ColorOpen."+std::to_wstring(hash);handle=CreateMutexW(nullptr,FALSE,name.c_str());if(!handle)throw std::runtime_error("mutex");const auto state=WaitForSingleObject(handle,1000);if(state!=WAIT_OBJECT_0&&state!=WAIT_ABANDONED){CloseHandle(handle);handle=nullptr;throw std::runtime_error("data busy");}}
    ~OpenGate(){if(handle){ReleaseMutex(handle);CloseHandle(handle);}}
};
class Store {
    sqlite3* db=nullptr;
    std::filesystem::path path_;
    void check(int code){if(code!=0)throw std::runtime_error("sqlite");}
    void exec(const char* sql){check(api().exec(db,sql,nullptr,nullptr,nullptr));}
    struct Statement {
        sqlite3_stmt* value=nullptr;
        Statement(sqlite3* db,const char* sql){if(api().prepare(db,sql,-1,&value,nullptr)!=0)throw std::runtime_error("prepare");}
        ~Statement(){if(value)api().finalize(value);}
        void text(int i,const std::string& value){if(api().bindText(this->value,i,value.c_str(),int(value.size()),reinterpret_cast<void(__cdecl*)(void*)>(-1))!=0)throw std::runtime_error("bind");}
        void number(int i,long long value){if(api().bind64(this->value,i,value)!=0)throw std::runtime_error("bind");}
        bool row(){const int code=api().step(value);if(code!=100&&code!=101)throw std::runtime_error("step");return code==100;}
        std::string string(int i){const auto* p=api().columnText(value,i);return p?reinterpret_cast<const char*>(p):"";}
        long long number(int i){return api().column64(value,i);}
    };
    struct Transaction {Store& store;bool committed=false;Transaction(Store& s):store(s){s.exec("BEGIN IMMEDIATE");}~Transaction(){if(!committed)api().exec(store.db,"ROLLBACK",nullptr,nullptr,nullptr);}void commit(){store.exec("COMMIT");committed=true;}};
    void validate(){Statement version(db,"PRAGMA user_version");if(!version.row()||version.number(0)!=1)throw std::runtime_error("version");Statement check(db,"PRAGMA quick_check");if(!check.row()||check.string(0)!="ok")throw std::runtime_error("corrupt");
        preferences();Statement history(db,"SELECT count(*) FROM history WHERE rgba<0 OR rgba>4294967295 OR length(event)<1 OR length(event)>160 OR length(utc)<>24 OR source NOT IN('screen','editor') OR parent<0");
        if(!history.row()||history.number(0))throw std::runtime_error("history schema");Statement favorites(db,"SELECT count(*) FROM favorites WHERE rgba<0 OR rgba>4294967295 OR length(name)>320 OR length(tags)>640 OR length(utc)<>24 OR length(updated)<>24 OR rank<0");if(!favorites.row()||favorites.number(0))throw std::runtime_error("favorites schema");
        Statement tables(db,"SELECT count(*) FROM sqlite_master WHERE type='table' AND name IN('color_assets','color_asset_stops')");if(!tables.row())throw std::runtime_error("asset schema");const auto count=tables.number(0);if(count==1)throw std::runtime_error("incomplete assets");
        if(count==2){Statement items(db,"SELECT count(*) FROM color_assets a WHERE a.kind NOT IN(1,2) OR length(a.name)<1 OR length(a.name)>512 OR length(a.group_name)>160 OR a.favorite NOT IN(0,1) OR a.radial NOT IN(0,1) OR a.angle<0 OR a.angle>360 OR (SELECT count(*) FROM color_asset_stops WHERE asset=a.id)<CASE a.kind WHEN 1 THEN 2 ELSE 1 END OR (SELECT count(*) FROM color_asset_stops WHERE asset=a.id)>CASE a.kind WHEN 1 THEN 4 ELSE 12 END");if(!items.row()||items.number(0))throw std::runtime_error("asset schema");
            Statement stops(db,"SELECT count(*) FROM color_asset_stops s WHERE rgba<0 OR rgba>4294967295 OR weight<0 OR weight>1000000 OR position<0 OR asset NOT IN(SELECT id FROM color_assets) OR position>=(SELECT count(*) FROM color_asset_stops WHERE asset=s.asset)");if(!stops.row()||stops.number(0))throw std::runtime_error("asset stops");}
    }
    static bool capacityValid(int value){return value==0||value==100||value==500||value==1000||value==5000;}
    void trim(){const auto p=preferences();if(p.capacity){Statement s(db,"DELETE FROM history WHERE id NOT IN(SELECT id FROM history ORDER BY id DESC LIMIT ?)");s.number(1,p.capacity);s.row();}exec("UPDATE history SET parent=0 WHERE parent<>0 AND parent NOT IN(SELECT id FROM history)");}
public:
    explicit Store(const std::filesystem::path& path,bool readOnly=false):path_(path){
        OpenGate gate(path);
        if(!api().valid())throw std::runtime_error("system sqlite unavailable");
        std::error_code ec;const auto bytes=std::filesystem::file_size(path,ec);if(!ec&&bytes>64ULL*1024*1024)throw std::runtime_error("size");
        if(!readOnly)std::filesystem::create_directories(path.parent_path());
        const int rc=api().open(utf8(path.wstring()).c_str(),&db,readOnly?1:(2|4),nullptr);
        if(rc!=0){if(db)api().close(db);db=nullptr;throw std::runtime_error("open");}
        try{
            check(api().timeout(db,300));
            if(!readOnly){
                Transaction transaction(*this);Statement version(db,"PRAGMA user_version");if(!version.row())throw std::runtime_error("version");const int v=int(version.number(0));
                if(v==0){Statement tables(db,"SELECT count(*) FROM sqlite_master WHERE type='table'");if(!tables.row()||tables.number(0)!=0)throw std::runtime_error("foreign schema");
                    exec("CREATE TABLE prefs(id INTEGER PRIMARY KEY CHECK(id=1),capacity INTEGER NOT NULL DEFAULT 1000,copies INTEGER NOT NULL DEFAULT 0,coords INTEGER NOT NULL DEFAULT 0,page INTEGER NOT NULL DEFAULT 0);INSERT INTO prefs(id)VALUES(1);"
                        "CREATE TABLE history(id INTEGER PRIMARY KEY AUTOINCREMENT,event TEXT UNIQUE NOT NULL,rgba INTEGER NOT NULL,utc TEXT NOT NULL,source TEXT NOT NULL,parent INTEGER NOT NULL DEFAULT 0,x INTEGER,y INTEGER);"
                        "CREATE TABLE favorites(id INTEGER PRIMARY KEY AUTOINCREMENT,rgba INTEGER NOT NULL,utc TEXT NOT NULL,updated TEXT NOT NULL,name TEXT NOT NULL,tags TEXT NOT NULL,rank INTEGER NOT NULL);PRAGMA user_version=1;");
                }else if(v!=1)throw std::runtime_error("newer schema");
                else validate(); // 原库不合法时先回滚，不先写入扩展表。
                // 加法扩展：旧候选仍可读取prefs.page(0..2)，不把新导航页码写坏旧口径。
                exec("CREATE TABLE IF NOT EXISTS workspace(id INTEGER PRIMARY KEY CHECK(id=1),page INTEGER NOT NULL);INSERT OR IGNORE INTO workspace(id,page)SELECT 1,page FROM prefs WHERE id=1;"
                    "CREATE TABLE IF NOT EXISTS color_assets(id INTEGER PRIMARY KEY AUTOINCREMENT,kind INTEGER NOT NULL,name TEXT NOT NULL,group_name TEXT NOT NULL,favorite INTEGER NOT NULL,angle INTEGER NOT NULL,radial INTEGER NOT NULL);"
                    "CREATE TABLE IF NOT EXISTS color_asset_stops(asset INTEGER NOT NULL,position INTEGER NOT NULL,rgba INTEGER NOT NULL,weight INTEGER NOT NULL,PRIMARY KEY(asset,position));");transaction.commit();
            }
            validate();
        }catch(...){api().close(db);db=nullptr;throw;}
    }
    ~Store(){if(db)api().close(db);}
    Store(const Store&)=delete;Store& operator=(const Store&)=delete;
    Preferences preferences(){Statement s(db,"SELECT capacity,copies,coords,page FROM prefs WHERE id=1");if(!s.row())throw std::runtime_error("prefs");Preferences p{int(s.number(0)),s.number(1)!=0,s.number(2)!=0,int(s.number(3))};Statement table(db,"SELECT count(*) FROM sqlite_master WHERE type='table' AND name='workspace'");if(table.row()&&table.number(0)){Statement workspace(db,"SELECT page FROM workspace WHERE id=1");if(!workspace.row())throw std::runtime_error("workspace");p.page=int(workspace.number(0));}if(!capacityValid(p.capacity)||p.page<0||p.page>6)throw std::runtime_error("prefs");return p;}
    void page(int value){if(value<0||value>6)throw std::runtime_error("page");Transaction t(*this);Statement s(db,"UPDATE prefs SET page=? WHERE id=1");s.number(1,value<=2?value:0);s.row();Statement workspace(db,"UPDATE workspace SET page=? WHERE id=1");workspace.number(1,value);workspace.row();t.commit();}
    void preferences(Preferences p){if(!capacityValid(p.capacity)||p.page<0||p.page>6)throw std::runtime_error("prefs");Transaction t(*this);Statement s(db,"UPDATE prefs SET capacity=?,copies=?,coords=?,page=? WHERE id=1");s.number(1,p.capacity);s.number(2,p.recordCopies);s.number(3,p.coordinates);s.number(4,p.page<=2?p.page:0);s.row();Statement workspace(db,"UPDATE workspace SET page=? WHERE id=1");workspace.number(1,p.page);workspace.row();trim();t.commit();}
    long long record(ColorValue::Rgb color,const std::string& source,const std::string& key,long long parent=0,std::optional<POINT> coordinate={}){
        if((source!="screen"&&source!="editor")||key.empty()||key.size()>160||parent<0)throw std::runtime_error("record");
        Transaction t(*this);Statement existing(db,"SELECT id FROM history WHERE event=?");existing.text(1,key);if(existing.row()){const auto id=existing.number(0);t.commit();return id;}
        if(parent){Statement p(db,"SELECT id FROM history WHERE id=?");p.number(1,parent);if(!p.row())parent=0;}
        const bool coords=coordinate&&preferences().coordinates;
        Statement s(db,coords?"INSERT INTO history(event,rgba,utc,source,parent,x,y)VALUES(?,?,?,?,?,?,?)":"INSERT INTO history(event,rgba,utc,source,parent)VALUES(?,?,?,?,?)");
        s.text(1,key);s.number(2,(255LL<<24)|(static_cast<long long>(color.r)<<16)|(static_cast<long long>(color.g)<<8)|color.b);s.text(3,utcNow());s.text(4,source);s.number(5,parent);if(coords){s.number(6,coordinate->x);s.number(7,coordinate->y);}s.row();const auto id=api().lastId(db);trim();t.commit();return id;
    }
    long long favorite(ColorValue::Rgb color,std::wstring name,std::wstring tags,long long id=0){
        if(name.size()>80||tags.size()>160||id<0)throw std::runtime_error("favorite");Transaction t(*this);
        if(id){Statement found(db,"SELECT id FROM favorites WHERE id=?");found.number(1,id);if(!found.row())throw std::runtime_error("stale favorite");}
        Statement s(db,id?"UPDATE favorites SET rgba=?,name=?,tags=?,updated=? WHERE id=?":"INSERT INTO favorites(rgba,name,tags,updated,utc,rank)VALUES(?,?,?,?,?,(SELECT coalesce(max(rank),0)+1 FROM favorites))");
        s.number(1,(255LL<<24)|(static_cast<long long>(color.r)<<16)|(static_cast<long long>(color.g)<<8)|color.b);s.text(2,utf8(name));s.text(3,utf8(tags));const auto utc=utcNow();s.text(4,utc);if(id)s.number(5,id);else s.text(5,utc);s.row();if(!id)id=api().lastId(db);t.commit();return id;
    }
    std::vector<Record> list(bool favorites,const Query& q={}){
        if(q.limit<1||q.limit>5000||q.offset<0||q.text.size()>160||q.fromDate.size()>10||q.toDate.size()>10)throw std::runtime_error("query");
        const auto date=[](const std::string& v){if(v.empty())return true;if(v.size()!=10||v[4]!='-'||v[7]!='-')return false;for(size_t i=0;i<v.size();++i)if(i!=4&&i!=7&&(v[i]<'0'||v[i]>'9'))return false;return std::chrono::year_month_day(std::chrono::year(std::stoi(v.substr(0,4))),std::chrono::month(std::stoi(v.substr(5,2))),std::chrono::day(std::stoi(v.substr(8,2)))).ok();};
        if(!date(q.fromDate)||!date(q.toDate)||(!q.fromDate.empty()&&!q.toDate.empty()&&q.fromDate>q.toDate)||(!q.source.empty()&&q.source!="screen"&&q.source!="editor"))throw std::runtime_error("filter");
        const char* sql=favorites?"SELECT id,rgba,utc,'favorite',0,name,tags,rank,NULL,NULL,0 FROM favorites WHERE (?='' OR instr(lower(name||' '||tags||' '||printf('#%06X',rgba&16777215)),lower(?))>0) ORDER BY rank,id LIMIT ? OFFSET ?":
            "SELECT id,rgba,utc,source,parent,'','',id,x,y,(x IS NOT NULL AND y IS NOT NULL) FROM history WHERE (?='' OR instr(lower(printf('#%06X',rgba&16777215)||' '||utc||' '||source),lower(?))>0) AND (?='' OR source=?) AND (?='' OR substr(utc,1,10)>=?) AND (?='' OR substr(utc,1,10)<=?) ORDER BY id DESC LIMIT ? OFFSET ?";
        Statement s(db,sql);const auto text=utf8(q.text);s.text(1,text);s.text(2,text);int p=3;
        if(!favorites){for(const auto& v:{q.source,q.fromDate,q.toDate}){s.text(p++,v);s.text(p++,v);}}
        s.number(p++,q.limit);s.number(p,q.offset);std::vector<Record> result;
        while(s.row()){const auto packed=s.number(1);if(packed<0||packed>0xffffffffLL)throw std::runtime_error("color");Record r;r.id=s.number(0);r.color={uint8_t(packed>>16),uint8_t(packed>>8),uint8_t(packed)};r.utc=s.string(2);r.source=s.string(3);r.parent=s.number(4);r.name=s.string(5);r.tags=s.string(6);r.order=s.number(7);
            // 坐标有NULL判定，不把默认NULL解释成真实(0,0)。
            if(s.number(10))r.coordinate=POINT{LONG(s.number(8)),LONG(s.number(9))};result.push_back(std::move(r));}
        return result;
    }
    long long count(bool favorites){Statement s(db,favorites?"SELECT count(*) FROM favorites":"SELECT count(*) FROM history");if(!s.row())throw std::runtime_error("count");return s.number(0);}
    bool isFavorite(ColorValue::Rgb color){Statement s(db,"SELECT count(*) FROM favorites WHERE rgba=?");s.number(1,(255LL<<24)|(static_cast<long long>(color.r)<<16)|(static_cast<long long>(color.g)<<8)|color.b);return s.row()&&s.number(0)>0;}
    std::unordered_set<uint32_t> favoriteColors(){Statement s(db,"SELECT DISTINCT rgba FROM favorites");std::unordered_set<uint32_t> result;while(s.row())result.insert(uint32_t(s.number(0))&0xFFFFFFu);return result;}
    long long ensureFavorite(ColorValue::Rgb color){
        Transaction t(*this);const auto rgba=(255LL<<24)|(static_cast<long long>(color.r)<<16)|(static_cast<long long>(color.g)<<8)|color.b;
        Statement found(db,"SELECT id FROM favorites WHERE rgba=? ORDER BY id LIMIT 1");found.number(1,rgba);if(found.row()){const auto id=found.number(0);t.commit();return id;}
        Statement s(db,"INSERT INTO favorites(rgba,name,tags,updated,utc,rank)VALUES(?,?,'',?,?,(SELECT coalesce(max(rank),0)+1 FROM favorites))");s.number(1,rgba);s.text(2,utf8(ColorValue::hex(color)));const auto now=utcNow();s.text(3,now);s.text(4,now);s.row();const auto id=api().lastId(db);t.commit();return id;
    }
    long long asset(const Asset& value){
        if(value.id<0||(value.kind!=1&&value.kind!=2)||value.name.empty()||value.name.size()>512||value.group.size()>160||value.angle<0||value.angle>360||
            value.colors.size()<(value.kind==1?2u:1u)||value.colors.size()>(value.kind==1?4u:12u)||(!value.weights.empty()&&value.weights.size()!=value.colors.size()))throw std::runtime_error("asset");
        for(auto weight:value.weights)if(weight<0||weight>1000000)throw std::runtime_error("weight");
        Transaction t(*this);
        if(value.id){Statement found(db,"SELECT id FROM color_assets WHERE id=?");found.number(1,value.id);if(!found.row())throw std::runtime_error("stale asset");}
        Statement s(db,value.id?"UPDATE color_assets SET kind=?,name=?,group_name=?,favorite=?,angle=?,radial=? WHERE id=?":"INSERT INTO color_assets(kind,name,group_name,favorite,angle,radial)VALUES(?,?,?,?,?,?)");
        s.number(1,value.kind);s.text(2,utf8(value.name));s.text(3,utf8(value.group));s.number(4,value.favorite);s.number(5,value.angle);s.number(6,value.radial);if(value.id)s.number(7,value.id);s.row();const auto id=value.id?value.id:api().lastId(db);
        Statement remove(db,"DELETE FROM color_asset_stops WHERE asset=?");remove.number(1,id);remove.row();
        for(size_t i=0;i<value.colors.size();++i){const auto c=value.colors[i];Statement stop(db,"INSERT INTO color_asset_stops(asset,position,rgba,weight)VALUES(?,?,?,?)");stop.number(1,id);stop.number(2,i);stop.number(3,(255LL<<24)|(static_cast<long long>(c.r)<<16)|(static_cast<long long>(c.g)<<8)|c.b);stop.number(4,value.weights.empty()?0:value.weights[i]);stop.row();}
        t.commit();return id;
    }
    std::vector<Asset> assets(int kind=0,bool favorites=false,const Query& query={}){
        if(kind<0||kind>2||query.offset<0||query.limit<1||query.limit>5000||query.text.size()>160)throw std::runtime_error("asset query");
        // readOnly旧备份允许没有扩展表，旧历史/收藏仍完整可读。
        Statement table(db,"SELECT count(*) FROM sqlite_master WHERE type='table' AND name='color_assets'");if(!table.row()||!table.number(0))return {};
        Statement s(db,"SELECT id,kind,name,group_name,favorite,angle,radial FROM color_assets WHERE (?=0 OR kind=?) AND (?=0 OR favorite=1) AND (?='' OR instr(lower(name||' '||group_name),lower(?))>0 OR EXISTS(SELECT 1 FROM color_asset_stops WHERE asset=color_assets.id AND instr(lower(printf('#%06X',rgba&16777215)),lower(?))>0)) ORDER BY id DESC LIMIT ? OFFSET ?");
        s.number(1,kind);s.number(2,kind);s.number(3,favorites);const auto text=utf8(query.text);s.text(4,text);s.text(5,text);s.text(6,text);s.number(7,query.limit);s.number(8,query.offset);
        std::vector<Asset> result;while(s.row()) {Asset value;value.id=s.number(0);value.kind=int(s.number(1));value.name=wide(s.string(2));value.group=wide(s.string(3));value.favorite=s.number(4)!=0;value.angle=int(s.number(5));value.radial=s.number(6)!=0;
            Statement stops(db,"SELECT position,rgba,weight FROM color_asset_stops WHERE asset=? ORDER BY position");stops.number(1,value.id);
            while(stops.row()){if(stops.number(0)!=static_cast<long long>(value.colors.size())||value.colors.size()>=12)throw std::runtime_error("asset stops");const auto c=stops.number(1),weight=stops.number(2);if(c<0||c>0xffffffffLL||weight<0||weight>1000000)throw std::runtime_error("asset color");value.colors.push_back({uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)});value.weights.push_back(int(weight));}
            if(value.id<=0||(value.kind!=1&&value.kind!=2)||value.name.empty()||value.name.size()>512||value.group.size()>160||value.angle<0||value.angle>360||value.colors.size()<(value.kind==1?2u:1u)||value.colors.size()>(value.kind==1?4u:12u))throw std::runtime_error("asset data");result.push_back(std::move(value));}
        return result;
    }
    long long ensureGradientFavorite(Asset value){if(value.kind!=1)throw std::runtime_error("gradient favorite");
        if(!value.id){Query query;query.limit=5000;for(query.offset=0;;query.offset+=query.limit){const auto part=assets(1,true,query);for(const auto& found:part)if(found.colors==value.colors&&found.angle==value.angle&&found.radial==value.radial)return found.id;if(part.size()<size_t(query.limit))break;}}
        value.favorite=true;return asset(value);}
    void eraseAsset(long long id){if(id<=0)throw std::runtime_error("asset id");Transaction t(*this);Statement stops(db,"DELETE FROM color_asset_stops WHERE asset=?");stops.number(1,id);stops.row();Statement item(db,"DELETE FROM color_assets WHERE id=?");item.number(1,id);item.row();t.commit();}
    void erase(bool favorites,long long id){if(id<=0)throw std::runtime_error("id");Transaction t(*this);Statement s(db,favorites?"DELETE FROM favorites WHERE id=?":"DELETE FROM history WHERE id=?");s.number(1,id);s.row();if(!favorites)exec("UPDATE history SET parent=0 WHERE parent NOT IN(SELECT id FROM history)");t.commit();}
    void clearHistory(){exec("DELETE FROM history");}
    void moveFavorite(long long id,int direction){if(direction!=-1&&direction!=1)throw std::runtime_error("direction");Transaction t(*this);Statement a(db,"SELECT rank FROM favorites WHERE id=?");a.number(1,id);if(!a.row())throw std::runtime_error("id");const auto rank=a.number(0);Statement b(db,direction<0?"SELECT id,rank FROM favorites WHERE rank<? ORDER BY rank DESC LIMIT 1":"SELECT id,rank FROM favorites WHERE rank>? ORDER BY rank LIMIT 1");b.number(1,rank);if(b.row()){const auto other=b.number(0),r=b.number(1);Statement s(db,"UPDATE favorites SET rank=CASE WHEN id=? THEN ? ELSE ? END WHERE id IN(?,?)");s.number(1,id);s.number(2,r);s.number(3,rank);s.number(4,id);s.number(5,other);s.row();}t.commit();}
    void backup(const std::filesystem::path& target){
        if(std::filesystem::exists(target))throw std::runtime_error("backup exists");sqlite3* destination=nullptr;const int opened=api().open(utf8(target.wstring()).c_str(),&destination,2|4,nullptr);if(opened!=0){if(destination)api().close(destination);throw std::runtime_error("backup open");}
        const auto task=api().backupInit(destination,"main",db,"main");const int code=task?api().backupStep(task,-1):-1;const int finish=task?api().backupFinish(task):-1;api().close(destination);if(code!=101||finish!=0){std::error_code ec;std::filesystem::remove(target,ec);throw std::runtime_error("backup");}
    }
    void restore(const std::filesystem::path& from){
        Store source(from,true);const auto task=api().backupInit(db,"main",source.db,"main");const int code=task?api().backupStep(task,-1):-1;const int finish=task?api().backupFinish(task):-1;if(code!=101||finish!=0)throw std::runtime_error("restore");validate();
    }
    static void recover(const std::filesystem::path& target,const std::filesystem::path& backup){
        OpenGate openGate(target);Store source(backup,true);const auto suffix=wide(eventKey());const auto temporary=std::filesystem::path(target.wstring()+L".recover-"+suffix);
        source.backup(temporary);
        // SQLite连接仍在其他进程使用时拒绝恢复；不对活动数据库直接文件复制。
        const HANDLE gate=CreateFileW(target.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,OPEN_EXISTING,0,nullptr);
        if(gate==INVALID_HANDLE_VALUE){std::error_code ec;std::filesystem::remove(temporary,ec);throw std::runtime_error("recovery busy");}
        CloseHandle(gate); // 同名OpenGate继续阻止本软件新连接，现有连接已由独占打开检查排除。
        const auto preserved=std::filesystem::path(target.wstring()+L".before-recovery-"+suffix);
        std::vector<std::pair<std::filesystem::path,std::filesystem::path>> companions;
        for(const auto* extension:{L"-journal",L"-wal",L"-shm"}){const auto old=std::filesystem::path(target.wstring()+extension),saved=std::filesystem::path(preserved.wstring()+extension);if(!std::filesystem::exists(old))continue;
            if(!MoveFileExW(old.c_str(),saved.c_str(),0)){for(const auto& moved:companions)MoveFileExW(moved.second.c_str(),moved.first.c_str(),0);std::error_code ec;std::filesystem::remove(temporary,ec);throw std::runtime_error("recovery journal");}companions.emplace_back(old,saved);}
        const bool okay=ReplaceFileW(target.c_str(),temporary.c_str(),preserved.c_str(),0,nullptr,nullptr)!=FALSE;
        if(!okay){for(const auto& moved:companions)MoveFileExW(moved.second.c_str(),moved.first.c_str(),0);std::error_code ec;std::filesystem::remove(temporary,ec);throw std::runtime_error("recovery replace");}
    }
};
}
