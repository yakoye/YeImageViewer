#pragma once
#include "ColorFlowModel.h"
#include "ColorToolHost.h"

inline void expectColorFlow() {
    using namespace ColorLibrary;
    const auto root=std::filesystem::temp_directory_path()/wide("YeColorFlow-"+eventKey());std::filesystem::create_directories(root);
    try{
        const auto path=root/L"flow.sqlite";Store store(path);
        const auto first=store.favorite({1,2,3},L"保留名称",L"标签");
        passOrFail("色块重复星标不复制收藏或覆盖名称标签",store.ensureFavorite({1,2,3})==first&&store.count(true)==1&&store.list(true)[0].name==utf8(L"保留名称"));
        auto prefs=store.preferences();prefs.coordinates=true;store.preferences(prefs);store.record({1,1,1},"screen","origin",0,POINT{0,0});store.record({2,2,2},"screen","unknown");const auto history=store.list(false);
        passOrFail("连续流坐标查询区分原点与无坐标且不逐条查询",!history[0].coordinate&&history[1].coordinate&&history[1].coordinate->x==0&&history[1].coordinate->y==0);
        Asset gradient;gradient.kind=1;gradient.name=L"渐变测试";gradient.group=L"测试";gradient.colors={{1,2,3},{4,5,6},{7,8,9}};gradient.angle=137;gradient.radial=true;
        const auto id=store.ensureGradientFavorite(gradient);gradient.id=id;gradient.favorite=true;auto read=Store(path,true).assets(1,true);
        passOrFail("完整渐变收藏持久化色标、角度和径向类型",read.size()==1&&read[0].colors==gradient.colors&&read[0].angle==137&&read[0].radial&&read[0].favorite);
        gradient.id=0;passOrFail("重复收藏相同渐变不产生多份记录",store.ensureGradientFavorite(gradient)==id&&store.assets(1,true).size()==1);
        Asset image;image.kind=2;image.name=L"同名图片.png";image.colors={{10,20,30},{40,50,60}};image.weights={250000,750000};const auto imageId=store.asset(image);read=store.assets(2);
        passOrFail("图片色卡保存颜色与占比而非原图像素路径",read.size()==1&&read[0].id==imageId&&read[0].colors==image.colors&&read[0].weights==image.weights);
        Query query;query.text=L"#0A141E";passOrFail("全局搜索可检索图片色卡内任一HEX",store.assets(2,false,query).size()==1);
        const auto backup=root/L"all.backup.sqlite";store.backup(backup);store.eraseAsset(id);store.eraseAsset(imageId);store.restore(backup);
        passOrFail("在线备份恢复扩展色卡同时保留历史收藏",store.assets().size()==2&&store.count(false)==2&&store.count(true)==1);
        bool invalid=false;try{image.weights={-1,1};store.asset(image);}catch(...){invalid=true;}passOrFail("无效图片占比拒绝且不留下半条资产",invalid&&store.assets().size()==2);
        store.backup(root/L"legacy.sqlite");sqlite3* db=nullptr;const auto file=utf8((root/L"legacy.sqlite").wstring());bool opened=api().open(file.c_str(),&db,2,nullptr)==0;
        const bool removed=opened&&api().exec(db,"DROP TABLE color_asset_stops;DROP TABLE color_assets;",nullptr,nullptr,nullptr)==0;if(db)api().close(db);
        passOrFail("旧版无扩展表备份只读仍可打开",removed&&Store(root/L"legacy.sqlite",true).count(false)==2&&Store(root/L"legacy.sqlite",true).assets().empty());
        passOrFail("旧库添加空扩展不改历史或命名收藏",Store(root/L"legacy.sqlite").assets().empty()&&Store(root/L"legacy.sqlite",true).list(true)[0].name==utf8(L"保留名称"));
        store.backup(root/L"bad-asset.sqlite");db=nullptr;const auto bad=utf8((root/L"bad-asset.sqlite").wstring());opened=api().open(bad.c_str(),&db,2,nullptr)==0;const bool broken=opened&&api().exec(db,"UPDATE color_asset_stops SET position=99 WHERE asset=(SELECT min(id) FROM color_assets) AND position=0",nullptr,nullptr,nullptr)==0;if(db)api().close(db);
        invalid=false;try{Store badStore(root/L"bad-asset.sqlite",true);}catch(...){invalid=true;}passOrFail("损坏资产顺序在恢复前被明确拒绝",broken&&invalid&&store.assets().size()==2);
    }catch(const std::exception& e){std::cout<<"ColorFlow data exception: "<<e.what()<<" root="<<root<<"\n";passOrFail("连续工作台持久化无意外异常",false);}
    std::vector<ColorFlow::Item> rows;
    for(int i=0;i<5000;++i){ColorFlow::Item item;item.key=std::to_wstring(i);item.group=std::to_wstring(i/30);rows.push_back(std::move(item));}
    ColorFlow::Layout flow;flow.rebuild(rows,556);
    passOrFail("连续色卡跨分组不丢条目，不创建分页",flow.columns()==5&&flow.elements().size()==rows.size()+167&&flow.height()>50000);
    passOrFail("色卡两行高度移除第三行空白，历史不压缩",flow.elements()[1].rect.height==48&&flow.topOf(5)-flow.topOf(0)==56);
    const int middle=flow.topOf(2345);const auto index=flow.firstVisible(middle);
    passOrFail("连续色卡从中间开始仅定位可见元素",index>2000&&index<flow.elements().size()&&flow.elements()[index].rect.y==middle);
    passOrFail("组标题不当作色块且跨系列定位正确",flow.at(1,7,0)==-1&&flow.at(1,37,0)==0&&flow.topOf(30)>flow.topOf(29));
    for(int width:{1,80,180,556,1200}) {
        flow.rebuild(rows,width);
        bool inside=true;for(const auto& e:flow.elements())inside&=e.rect.x>=0&&e.rect.width>0&&e.rect.x+e.rect.width<=std::max(1,width-14);
        passOrFail("连续流窄视口和多列布局均不溢出",inside&&flow.clamp(-1,300)==0&&flow.clamp(INT_MAX,300)==flow.maximum(300));
    }
    flow.rebuild(rows,556,true);passOrFail("历史与收藏共用单列连续流",flow.columns()==1&&flow.topOf(4999)>flow.topOf(4998)&&flow.elements()[1].rect.height==72);
    flow.rebuild({},556);passOrFail("空结果没有虚假滚动范围",flow.height()==0&&flow.maximum(300)==0&&flow.at(1,1,0)==-1);
    ColorToolHost::Header header;header.operation=1;header.color=0x12ABEF;
    COPYDATASTRUCT packet{ColorToolHost::PACKET,sizeof(header),&header};
    const auto message=ColorToolHost::receive(&packet);
    passOrFail("颜色工具IPC只接受有界RGB和明确操作",message&&message->operation==ColorToolHost::Operation::Adopt&&message->color==ColorValue::Rgb{18,171,239});
    for(int invalid=0;invalid<9;++invalid){auto broken=header;packet.lpData=&broken;packet.cbData=sizeof(broken);packet.dwData=ColorToolHost::PACKET;
        if(invalid==0)broken.version=2;if(invalid==1)broken.operation=3;if(invalid==2)broken.color=0xFF000000;
        if(invalid==3)broken.width=513;if(invalid==4)broken.height=1;if(invalid==5)packet.cbData--;if(invalid==6)packet.dwData++;if(invalid==7)broken.mode=3;if(invalid==8)broken.language=3;
        passOrFail("颜色工具拒绝错误版本、越界像素和截断IPC",!ColorToolHost::receive(&packet));}
}
