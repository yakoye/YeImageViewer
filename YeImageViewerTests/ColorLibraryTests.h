#pragma once
#include "ColorLibrary.h"
#include <fstream>

inline void expectColorLibrary(){
    using namespace ColorLibrary;
    const auto root=std::filesystem::temp_directory_path()/wide("YeColorData-"+eventKey());std::filesystem::create_directories(root);
    const auto path=root/L"colors.sqlite";
    auto throws=[](auto operation){try{operation();return false;}catch(...){return true;}};
    try{
        Store store(path);auto p=store.preferences();
        passOrFail("颜色数据默认只记确认、1000容量且不留坐标",p.capacity==1000&&!p.recordCopies&&!p.coordinates);
        store.page(5);passOrFail("新工作页持久化并与旧prefs页码分离",Store(path,true).preferences().page==5);sqlite3* legacy=nullptr;const int opened=api().open(utf8(path.wstring()).c_str(),&legacy,1,nullptr);int legacyPage=-1;const auto callback=[](void* result,int,char** values,char**)->int{*static_cast<int*>(result)=values&&values[0]?atoi(values[0]):-1;return 0;};const int read=opened==0?api().exec(legacy,"SELECT page FROM prefs WHERE id=1",callback,&legacyPage,nullptr):-1;if(legacy)api().close(legacy);passOrFail("rc10读取的新页仍是合法旧页0不破坏原历史",opened==0&&read==0&&legacyPage==0);store.page(0);
        const auto id=store.record({18,171,239},"screen","same-event",0,POINT{-100,22});
        passOrFail("重复确认事件幂等且不改已存色",store.record({0,0,0},"screen","same-event")==id&&store.count(false)==1&&store.list(false)[0].color==ColorValue::Rgb{18,171,239});
        passOrFail("默认坐标不写磁盘",!store.list(false)[0].coordinate);
        p.coordinates=true;store.preferences(p);const auto child=store.record({1,2,3},"editor","child",id,POINT{-22,33});
        auto rows=store.list(false);passOrFail("历史保存UTC、parent、负物理坐标",rows[0].id==child&&rows[0].parent==id&&rows[0].utc.size()==24&&rows[0].utc.back()=='Z'&&rows[0].coordinate&&rows[0].coordinate->x==-22);
        Query q;q.text=L"#12ABEF";passOrFail("历史HEX查询不分大小写",store.list(false,q).size()==1);q={};q.source="editor";passOrFail("历史按来源筛选",store.list(false,q).size()==1);
        q={};q.fromDate="2099-01-01";passOrFail("历史日期筛选不改记录",store.list(false,q).empty()&&store.count(false)==2);
        const auto favorite=store.favorite({5,6,7},L"中文收藏",L"工作 标签");
        passOrFail("收藏不写历史",store.count(true)==1&&store.count(false)==2);
        q={};q.text=L"标签";passOrFail("收藏Unicode名称与标签可搜索",store.list(true,q).size()==1);
        store.favorite({8,9,10},L"修改名称",L"new",favorite);passOrFail("收藏修改色值不产生第二份",store.list(true)[0].color==ColorValue::Rgb{8,9,10}&&store.count(true)==1);
        const auto second=store.favorite({11,12,13},L"第二",L"");store.moveFavorite(second,-1);passOrFail("收藏排序持久化",store.list(true)[0].id==second);
        store.backup(root/L"backup.sqlite");store.clearHistory();passOrFail("清历史不清收藏",store.count(false)==0&&store.count(true)==2);
        store.restore(root/L"backup.sqlite");passOrFail("在线备份恢复历史与收藏",store.count(false)==2&&store.count(true)==2);
        Store reopened(path);passOrFail("独立连接读到持久化字段",reopened.list(true)[0].id==second&&reopened.list(false)[0].parent==id&&reopened.preferences().coordinates);
        p=store.preferences();p.capacity=100;p.page=2;p.recordCopies=true;store.preferences(p);
        for(int i=0;i<110;++i)store.record({uint8_t(i),0,0},"editor","capacity-"+std::to_string(i));
        passOrFail("历史容量按最新事件裁剪并保留收藏",store.count(false)==100&&store.count(true)==2&&store.list(false)[0].color.r==109);
        p.capacity=0;store.preferences(p);
        passOrFail("无效容量、标签超限、未知来源拒绝",throws([&]{auto bad=p;bad.capacity=9;store.preferences(bad);})&&throws([&]{store.favorite({},std::wstring(81,L'x'),L"");})&&throws([&]{store.record({},"window","bad");}));
        passOrFail("拒绝覆盖现有备份",throws([&]{store.backup(root/L"backup.sqlite");}));
        Store reader(path,true);passOrFail("只读可查看且不能假装记录成功",reader.count(true)==2&&throws([&]{reader.record({},"screen","readonly");}));
        {std::ofstream bad(root/L"bad.sqlite");bad<<"damaged data";}
        passOrFail("损坏数据库拒绝写入且恢复失败保留当前数据",throws([&]{Store bad(root/L"bad.sqlite");})&&throws([&]{store.restore(root/L"bad.sqlite");})&&store.count(true)==2);
        store.erase(true,favorite);passOrFail("删除一条收藏不清历史",store.count(true)==1&&store.count(false)==100);
        passOrFail("更新已删除收藏明确失败",throws([&]{store.favorite({},L"stale",L"",favorite);}));
        Query invalid;invalid.fromDate="2026-02-30";passOrFail("无效日期筛选拒绝",throws([&]{store.list(false,invalid);}));
        const auto orphan=store.record({},"editor","parent-removed",store.list(false)[0].id);const auto parent=store.list(false)[0].parent;store.erase(false,parent);passOrFail("删除父记录不留下悬挂parent",store.list(false)[0].id==orphan&&store.list(false)[0].parent==0);
        const auto originalCount=store.count(false);wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);PROCESS_INFORMATION processes[2]{};bool launched=true;
        for(int i=0;i<2;++i){std::wstring command=L"\""+std::wstring(executable)+L"\" --color-library-writer \""+path.wstring()+L"\" worker"+std::to_wstring(i);STARTUPINFOW startup{sizeof(startup)};launched&=CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&processes[i])!=FALSE;}
        bool exited=launched;for(auto& process:processes)if(process.hProcess){exited&=WaitForSingleObject(process.hProcess,15000)==WAIT_OBJECT_0;DWORD code=1;GetExitCodeProcess(process.hProcess,&code);exited&=code==0;CloseHandle(process.hThread);CloseHandle(process.hProcess);}
        passOrFail("两个真实子进程并发写入均成功且事件无覆盖",exited&&store.count(false)==originalCount+100);
        {std::ofstream damaged(root/L"recovery.sqlite");damaged<<"damaged original";}
        {std::ofstream journal(root/L"recovery.sqlite-journal");journal<<"old interrupted journal";}
        Store::recover(root/L"recovery.sqlite",root/L"backup.sqlite");Store recovered(root/L"recovery.sqlite",true);bool preserved=false;for(const auto& item:std::filesystem::directory_iterator(root))if(item.path().filename().wstring().starts_with(L"recovery.sqlite.before-recovery-"))preserved=true;
        passOrFail("损坏原库显式恢复成功并保留旧文件",recovered.count(true)==2&&recovered.count(false)==2&&preserved);
        passOrFail("恢复不把旧事务日志套进新数据库",!std::filesystem::exists(root/L"recovery.sqlite-journal"));
        passOrFail("拒绝替换仍有活动连接的数据库",throws([&]{Store::recover(path,root/L"backup.sqlite");})&&store.count(true)==1);
        const auto stress=root/L"stress.sqlite";Store big(stress);auto unlimited=big.preferences();unlimited.capacity=0;big.preferences(unlimited);for(int i=0;i<5000;++i)big.record({uint8_t(i),1,2},"screen","stress-"+std::to_string(i));Query all;all.limit=5000;
        passOrFail("5000条历史分页无丢失且最新优先",big.count(false)==5000&&big.list(false,all).size()==5000&&big.list(false)[0].color.r==uint8_t(4999));
    }catch(const std::exception& e){std::cout<<"ColorLibrary exception: "<<e.what()<<" root="<<root<<"\n";passOrFail("颜色库生产存储不抛意外异常",false);}
}
