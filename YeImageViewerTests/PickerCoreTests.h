#pragma once
#include "PickerInputPolicy.h"
#include "PickerCore.h"
#include "PickerPreferences.h"
#include "CurrentColor.h"
#include "PsdCapability.h"
#include "ColorCatalog.h"

inline void expectPickerCore() {
    PickerInput::State input;
    for(uint32_t key:{0x10u,0xA0u,0xA1u,uint32_t('P'),uint32_t('C'),uint32_t('E'),0x0Du,0x1Bu,0x20u,0x71u,0x74u,0x25u,0x26u,0x27u,0x28u}) {
        const auto first=input.route(key,true,true,false,false),repeat=input.route(key,true,true,false,false),up=input.route(key,false,true,false,false);
        passOrFail("取色活动会话的所有声明按键优先且去掉单次动作的重复触发",first.consume&&first.dispatch&&!first.repeated&&repeat.repeated&&up.consume&&!up.dispatch);
        passOrFail("取色菜单打开时让方向及确认取消由菜单处理",!input.route(key,true,true,true,false).consume);
        passOrFail("取色失焦和系统组合键不抢输入",!input.route(key,true,false,false,false).consume&&!input.route(key,true,true,false,true).consume);
        input.route(key,false,true,false,false);
    }
    passOrFail("取色不拦截Alt Tab Windows和Ctrl本身",!PickerInput::managed(0x12)&&!PickerInput::managed(0x09)&&!PickerInput::managed(0x5B)&&!PickerInput::managed(0x11));
    using namespace PickerCore;
    {
        const auto l=ColorPanel::compactLayout();passOrFail("固定紧凑颜色客户区680x430且左栏224",l.width==680&&l.height==430&&!l.narrow&&l.left.width==224&&l.disk.width==200);
        bool fits=true;for(int i=0;i<7;++i)fits&=l.fields[i].x+l.fields[i].width<l.copies[i].x&&l.copies[i].x+l.copies[i].width<=680&&l.fields[i].y+l.fields[i].height<l.recent.y;
        for(int i=0;i<42;++i){const auto r=ColorPanel::recentSwatch(l,i);fits&=r.x>=l.recent.x&&r.x+r.width<=l.recent.x+l.recent.width&&r.y+r.height<=l.recent.y+l.recent.height;}passOrFail("七行复制和三行42色历史均有独立区域",fits);
        passOrFail("状态提示不覆盖Lab数值、复制图标或近期色块",l.status.y>=l.fields.back().y+l.fields.back().height&&l.status.y>=l.copies.back().y+l.copies.back().height&&l.status.y+l.status.height<=l.recent.y);
        for(const auto* group:{L"Open Color",L"Flat UI",L"Fluent Design",L"Ant Design",L"Material Design"})passOrFail("五类色卡均为独立可搜索真实数据",ColorCatalog::filter(false,group,L"").size()>=20);
        passOrFail("传统色包含中日色名与24节气搜索",ColorCatalog::filter(true,L"中国",L"立春").size()==16&&ColorCatalog::filter(true,L"日本",L"桜色").size()>=1);
        ColorCatalog::Gradient g;g.colors={{0,0,0},{255,255,255}};passOrFail("渐变sRGB真实端点及中点",g.at(0)==ColorValue::Rgb{}&&g.at(1)==ColorValue::Rgb{255,255,255}&&g.at(.5)==ColorValue::Rgb{128,128,128});g.colors={{255,0,0},{0,255,0},{0,0,255}};passOrFail("三色标等距且CSS不丢中间点",g.at(.5)==ColorValue::Rgb{0,255,0}&&g.css().find(L"#00FF00 50%")!=std::wstring::npos);
        passOrFail("导入渐变全部有效且不截断超过4色标条目",ColorCatalog::gradients().size()==377&&std::all_of(ColorCatalog::gradients().begin(),ColorCatalog::gradients().end(),[](const auto& x){return x.colors.size()>=2&&x.colors.size()<=4;}));
        cv::Mat pixels(8,8,CV_8UC4,cv::Scalar(0,0,255,255));pixels.colRange(0,2).setTo(cv::Scalar(255,0,0,255));const auto d=ColorCatalog::extract(pixels,3);passOrFail("图片主色按实际占比75%与25%排序",d.size()==2&&d[0].color==ColorValue::Rgb{255,0,0}&&std::abs(d[0].fraction-.75)<1e-9&&std::abs(d[1].fraction-.25)<1e-9);pixels.setTo(cv::Scalar(0,255,0,0));passOrFail("全透明图片不伪造主色",ColorCatalog::extract(pixels,6).empty());cv::Mat thin(1,10000,CV_8UC4,cv::Scalar(30,20,10,255));const auto t=ColorCatalog::extract(thin,6);passOrFail("极端长图缩略提色不退化为0像素",t.size()==1&&t[0].color==ColorValue::Rgb{10,20,30});
        const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);try{const auto decoded=ColorCatalog::readImage(L"test/image-tools/orientation-alpha.png");passOrFail("WIC生产提色路径真实读取PNG首帧且输出有界",!decoded.empty()&&decoded.cols<=512&&decoded.rows<=512&&!ColorCatalog::extract(decoded,6).empty());bool refused=false;try{ColorCatalog::readImage(L"test/image-tools/color-compact-cases.tsv");}catch(...){refused=true;}passOrFail("图片提色拒绝非图片而不回用过期像素",refused);}catch(...){passOrFail("WIC提色路径无异常",false);}if(SUCCEEDED(com))CoUninitialize();
    }
    {
        PsdLayers::Document doc;std::ifstream input("test/image-tools/nested-tree.tsv");std::string line;
        while(std::getline(input,line)){if(line.empty()||line[0]=='#')continue;std::istringstream row(line);int section=0,visible=1;std::string name;row>>section>>visible>>name;PsdLayers::Layer layer;layer.sectionType=section;layer.group=section!=0;layer.visible=visible;layer.name.assign(name.begin(),name.end());doc.layers.push_back(layer);}
        std::vector<uint8_t> visibility;for(const auto& layer:doc.layers)visibility.push_back(layer.visible);
        const auto all=PsdTree::rows(doc,{},visibility);
        passOrFail("嵌套树最小夹具包含结束标记但不显示为图层行",doc.layers.size()==9&&all.size()==7);
        passOrFail("嵌套组深度正确且结束后恢复父层级",all.size()==7&&all[0].depth==0&&all[1].depth==1&&all[2].depth==1&&all[3].depth==2&&all[5].depth==1&&all[6].depth==0);
        const auto collapsed=PsdTree::rows(doc,{2},visibility);
        passOrFail("仅折叠嵌套组，后续兄弟与根层保留",collapsed.size()==5&&collapsed[3].index==6&&collapsed[4].index==8);
        if(!visibility.empty())visibility[0]=0;const auto hidden=PsdTree::rows(doc,{},visibility);
        passOrFail("父组隐藏不改子层自身标记，但有效显隐继承",hidden.size()==7&&!hidden[3].effective&&visibility[3]==1&&hidden.back().effective);
        for(int width:{200,300,520})for(int depth:{0,1,2,6,20}){
            const auto group=PsdTree::geometry(width,116,depth,true),leaf=PsdTree::geometry(width,116,depth,false);
            passOrFail("文件夹三角、缩略图、名字互不覆盖且眼睛固定列",group.disclosure.x+group.disclosure.width<=group.thumbnail.x&&group.thumbnail.x+group.thumbnail.width<group.name.x&&group.eye.x==3&&leaf.eye.x==3&&group.name.x+group.name.width<=group.type.x&&group.eye.height==34&&group.thumbnail.width==24);
            passOrFail("嵌套组展开点击热区跟随视觉位置",group.disclosure.contains(group.disclosure.x+5,130)&&!group.disclosure.contains(group.thumbnail.x+5,130));
            const auto masked=PsdTree::geometry(width,116,depth,true,true);
            passOrFail("带蒙版的嵌套分组仍保持缩略图、蒙版、名字独立对齐",masked.thumbnail.x+masked.thumbnail.width<masked.mask.x&&masked.mask.x+masked.mask.width<masked.name.x&&masked.name.x+masked.name.width<=masked.type.x);
        }
    }
    {
        ColorPanel::History model({255,0,0});model.begin();for(int i=0;i<100;++i)model.preview({uint8_t(i),255,0});model.finish();
        const auto last=model.current();model.undo();passOrFail("色盘一次连续拖拽只产生一次撤销事务",model.current()==ColorValue::Rgb{255,0,0}&&!model.canUndo());
        model.redo();passOrFail("重做恢复拖拽最终色",model.current()==last);model.restore();passOrFail("恢复原色可再次撤销",model.current()==model.source()&&model.canUndo());
        model.undo();model.change({1,2,3});passOrFail("新编辑清空重做分支",!model.canRedo());model.reset({4,5,6});passOrFail("新取样重置编辑起点与撤销栈",!model.canUndo()&&model.source()==ColorValue::Rgb{4,5,6});
        passOrFail("互补推荐色使用真实HSV色相偏移",ColorPanel::suggested({255,0,0},0)==ColorValue::Rgb{0,255,255});
        passOrFail("默认颜色面板数值卡收窄到360DIP",ColorPanel::layout(810,600).card.width==360);
        for(int width:{320,400,760,800,810,1000,1400})for(int height:{440,680,900}){
            const auto l=ColorPanel::layout(width,height);
            passOrFail("颜色页宽窄模式按实际宽度切换",l.narrow==(width<800));
            bool fits=true;for(const auto& r:l.fields)fits&=r.x>=l.card.x&&r.x+r.width<=l.card.x+l.card.width&&r.width>40;
            for(const auto& r:l.suggestions)fits&=r.x>=l.card.x&&r.x+r.width<=l.card.x+l.card.width;
            passOrFail("七行数值与推荐色不越出右卡片",fits);
            passOrFail("圆盘两滑杆及原色对照纵向不重叠",l.disk.y+l.disk.height<l.saturation.y&&l.saturation.y+l.saturation.height<l.value.y&&l.value.y+l.value.height<l.original.y);
            const auto middle=ColorPanel::diskPoint(l,l.disk.x+l.disk.width/2,l.disk.y+l.disk.height/2,1);
            passOrFail("完整圆盘中心为无饱和度，盘外不能取样",middle&&middle->s<.01&&!ColorPanel::diskPoint(l,l.disk.x-1,l.disk.y-1,1));
            const auto top=ColorPanel::diskPoint(l,l.disk.x+l.disk.width/2,l.disk.y,1);
            passOrFail("色盘遵循原参考图的顶部红色色相起点",top&&std::abs(top->h)<1);
        }
    }
    std::array<uint32_t,256> old{};
    ShortcutConfig::reset(old.data(),old.size());
    old[ShortcutConfig::VERSION_INDEX]=5;
    old[ShortcutConfig::READER_WHEEL_BASE_INDEX]=static_cast<uint32_t>(ShortcutConfig::WheelAction::PanHorizontal);
    old[ShortcutConfig::BINDING_BASE_INDEX]=ShortcutConfig::binding('X',ShortcutConfig::MODIFIER_ALT);
    old[ShortcutConfig::BINDING_BASE_INDEX+34]=0xFFFFFFFF;
    ShortcutConfig::initialize(old.data(),old.size());
    passOrFail("v5升级保留用户快捷键及阅读滚轮，仅补无绑定取色动作",old[ShortcutConfig::VERSION_INDEX]==6 &&
        old[ShortcutConfig::READER_WHEEL_BASE_INDEX]==static_cast<uint32_t>(ShortcutConfig::WheelAction::PanHorizontal) &&
        old[ShortcutConfig::BINDING_BASE_INDEX]==ShortcutConfig::binding('X',ShortcutConfig::MODIFIER_ALT) && old[ShortcutConfig::BINDING_BASE_INDEX+34]==0);
    passOrFail("快取格式 HEX 六位大写", format({18,171,239},Format::Hex)==L"#12ABEF");
    passOrFail("快取格式 RGB", format({18,171,239},Format::Rgb)==L"rgb(18, 171, 239)");
    const std::array<ColorValue::Rgb,8> colors{{{0,0,0},{255,255,255},{255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},{255,0,255}}};
    const std::array<int,8> hues{0,0,0,120,240,60,180,300};
    for(size_t i=0;i<colors.size();++i) {
        const auto a=hsv(colors[i]), b=hsl(colors[i]);
        passOrFail("HSV/HSL 标准色相与有限范围", std::abs(a.h-hues[i])<1e-8 && std::abs(b.h-hues[i])<1e-8 &&
            std::isfinite(a.s) && std::isfinite(b.s) && a.s>=0 && a.s<=1 && b.s>=0 && b.s<=1);
    }
    passOrFail("HSL 纯红的明度为50%",format({255,0,0},Format::Hsl)==L"hsl(0, 100%, 50%)");
    passOrFail("HSV 纯红的明度为100%",format({255,0,0},Format::Hsv)==L"hsv(0, 100%, 100%)");
    passOrFail("灰阶无色相/饱和度除零",format({128,128,128},Format::Hsl)==L"hsl(0, 0%, 50.2%)");
    passOrFail("HSV 保留两位小数，避免整数导出精度损失",format({83,202,164},Format::Hsv)==L"hsv(160.84, 58.91%, 79.22%)");
    passOrFail("HSL 保留两位小数且小数点不受系统区域影响",format({83,202,164},Format::Hsl)==L"hsl(160.84, 52.89%, 55.88%)");
    passOrFail("非对称色 HSV 数值独立预期",std::abs(hsv({83,202,164}).h-160.84033613445)<1e-8 &&
        std::abs(hsv({83,202,164}).s-119.0/202)<1e-8);
    passOrFail("七格式回归：用户示例CMYK",format({77,46,46},static_cast<Format>(4))==L"cmyk(0%, 40%, 40%, 70%)");
    passOrFail("七格式回归：用户示例HSI",format({77,46,46},static_cast<Format>(5))==L"hsi(0, 18.34%, 22.09%)");
    passOrFail("七格式回归：用户示例Lab D65三位小数",format({77,46,46},static_cast<Format>(6))==L"Lab-D65(22.756, 14.244, 5.978)");
    Settings sevenFormats;sevenFormats.format=6;
    passOrFail("七格式回归：新增格式可持久化",sevenFormats.valid());
    passOrFail("显示只带一种格式及准确标签",display({77,46,46},Format::Hsv)==L"HSV/HSB: 0, 40.26%, 30.2%" &&
        display({77,46,46},Format::Lab)==L"CIE-LAB: 22.756, 14.244, 5.978");
    passOrFail("小数格式化不丢负Lab与前导零，不产生负零",decimal(-.123,3)==L"-0.123" && decimal(-86.184,3)==L"-86.184" &&
        decimal(-.00001,3)==L"0" && decimal(5.005,3)==L"5.005");
    passOrFail("CMYK黑白不除零",format({0,0,0},Format::Cmyk)==L"cmyk(0%, 0%, 0%, 100%)" && format({255,255,255},Format::Cmyk)==L"cmyk(0%, 0%, 0%, 0%)");
    passOrFail("HSI三原色120度顺序与灰阶",std::abs(hsi({0,255,0}).h-120)<1e-8 && std::abs(hsi({0,0,255}).h-240)<1e-8 && hsi({128,128,128}).s==0);
    const auto greenLab=lab({0,255,0});
    passOrFail("Lab D65绿包含负a轴，白/黑边界正确",std::abs(greenLab.l-87.7347)<.001 && std::abs(greenLab.a+86.1827)<.001 &&
        std::abs(greenLab.b-83.1793)<.001 && std::abs(lab({255,255,255}).l-100)<.001 && lab({0,0,0}).l==0);
    bool allFinite=true;
    for(int r=0;r<=255;r+=17)for(int g=0;g<=255;g+=17)for(int b=0;b<=255;b+=17) {
        const ColorValue::Rgb c{static_cast<uint8_t>(r),static_cast<uint8_t>(g),static_cast<uint8_t>(b)};
        const auto a=hsi(c);const auto v=lab(c);
        allFinite &= std::isfinite(a.h)&&a.h>=0&&a.h<360&&a.s>=-1e-12&&a.s<=1&&a.v>=0&&a.v<=1 &&
            std::isfinite(v.l)&&std::isfinite(v.a)&&std::isfinite(v.b)&&v.l>=-1e-8&&v.l<=100.0001;
    }
    passOrFail("4096个RGB格点的新转换均有限且在合法范围",allFinite);
    int current=0;for(int i=0;i<FORMAT_COUNT;++i)current=nextFormat(current);
    passOrFail("Shift完整七档回到HEX，旧索引保持",current==0 && nextFormat(3)==4 && static_cast<int>(Format::Hsv)==3 && nextFormat(6)==0);
    passOrFail("HUD收窄至180DIP且帮助行紧凑，快捷键仍保留",HUD_WIDTH==180 && HUD_LINE==17 && HUD_TEXT_ROWS==11);
    passOrFail("坐标色值半粗体且帮助字号更小",HUD_PRIMARY_WEIGHT>=600&&HUD_PRIMARY_FONT==13&&HUD_HELP_FONT==11&&HUD_HELP_FONT<HUD_PRIMARY_FONT&&HUD_PRIMARY_LINE>HUD_LINE&&HUD_PADDING==8);
    passOrFail("色值坐标居中、帮助左对齐且保留精确半行间距",hudRowCentered(0)&&hudRowCentered(1)&&!hudRowCentered(2)&&!hudRowCentered(11)&&hudHelpGap(96,true)==9&&hudHelpGap(192,true)==17&&hudHelpGap(288,true)==26&&hudHelpGap(192,false)==0);
    passOrFail("混合字号坐标按共同基线对齐不以各自行框居中",hudBaseline(21,13,4)==15&&hudBaseline(42,26,8)==30);
    passOrFail("取色按键说明固定增加四个前导空格",hudHelpText(L"Shift：切换颜色格式")==L"    Shift：切换颜色格式"&&hudHelpText(L"P：切换坐标").find(L"    ")==0);
    for(const auto value:std::array<ColorValue::Rgb,6>{{{0,0,0},{255,255,255},{77,46,46},{18,171,239},{255,0,0},{50,60,70}}}) {
        for(int f=0;f<FORMAT_COUNT;++f) {
            const auto model=static_cast<Format>(f);
            const auto parsed=CurrentColor::parse(CurrentColor::field(value,model),model);
            const int tolerance=model==Format::Cmyk?3:1;
            passOrFail("七格式数值双向转换，不读取显示像素",parsed && std::abs(int(parsed->r)-value.r)<=tolerance &&
                std::abs(int(parsed->g)-value.g)<=tolerance && std::abs(int(parsed->b)-value.b)<=tolerance);
        }
    }
    passOrFail("数值拒绝NaN、越界、尾随垃圾及不完整输入",!CurrentColor::parse(L"nan,0,0",Format::Rgb) &&
        !CurrentColor::parse(L"256,0,0",Format::Rgb) && !CurrentColor::parse(L"0,100,50 junk",Format::Hsl) &&
        !CurrentColor::parse(L"0,100",Format::Hsv) && !CurrentColor::parse(L"0,0,0,101",Format::Cmyk));
    CurrentColor::setImage({18,171,239},64);
    passOrFail("图片颜色与透明度原子共享，不混入棋盘格",CurrentColor::get()==ColorValue::Rgb{18,171,239} && CurrentColor::fromImage() && CurrentColor::alpha()==64);
    CurrentColor::set({77,46,46});
    passOrFail("屏幕确认切回不透明显示色",CurrentColor::get()==ColorValue::Rgb{77,46,46} && !CurrentColor::fromImage() && CurrentColor::alpha()==255);
    const auto fixtures=std::filesystem::path(__FILE__).parent_path().parent_path()/L"test/image-tools";
    passOrFail("普通PNG没有图层入口能力",PsdCapability::inspect((fixtures/L"orientation-alpha.png").wstring())==0);
    passOrFail("有实际图层记录的PSD具备入口能力",PsdCapability::inspect((fixtures/L"two-layers.psd").wstring())==2);
    const auto psdt=(fixtures/L"two-layers.psdt").wstring();
    PsdCapability templateCapability;
    for(int attempt=0;attempt<200&&!templateCapability.available(psdt);++attempt){templateCapability.poll(psdt);Sleep(1);}
    passOrFail("PSD模板同样按实际图层能力显示入口",templateCapability.available(psdt)&&templateCapability.layers()==2);
    templateCapability.reset();
    passOrFail("同路径重新打开先清掉旧能力",!templateCapability.available(psdt));
    templateCapability.poll((fixtures/L"orientation-alpha.png").wstring());
    passOrFail("切换普通图立即移除旧PSD模板入口",!templateCapability.available(psdt));
    for(int rotation=0;rotation<4;++rotation)for(bool horizontal:{false,true})for(bool vertical:{false,true}) {
        const auto transformed=ImageExport::transform(cv::imread((fixtures/L"orientation-alpha.png").string(),cv::IMREAD_UNCHANGED),rotation,horizontal,vertical);
        const auto original=cv::imread((fixtures/L"orientation-alpha.png").string(),cv::IMREAD_UNCHANGED);
        bool exact=!original.empty();
        for(int y=0;y<transformed.rows;++y)for(int x=0;x<transformed.cols;++x) {
            const auto point=ImageColorPoint::locate(50+x*2,40+y*2,100+transformed.cols*2,80+transformed.rows*2,
                original.cols,original.rows,original.cols,original.rows,2,0,0,rotation,horizontal,vertical);
            exact &= point && original.at<cv::Vec4b>(point->y,point->x)==transformed.at<cv::Vec4b>(y,x);
        }
        passOrFail("图片取色逆映射16组旋转镜像与独立变换参考逐像素一致",exact);
    }
    passOrFail("图片外留白不能取成黑色，拒绝无效缩放",!ImageColorPoint::locate(0,0,800,600,2,3,2,3,1,0,0,0,false,false) &&
        !ImageColorPoint::locate(400,300,800,600,2,3,2,3,0,0,0,0,false,false));
    passOrFail("放大图片也不能穿透右侧图层面板取色",!ImageColorPoint::locate(600,300,500,600,2000,2000,2000,2000,1,0,0,0,false,false));
    const std::array<Rect,3> displays{{{-1920,-1080,1920,1080},{0,0,3840,2160},{3840,500,1080,1920}}};
    passOrFail("负坐标显示器采样",monitorAt(displays,-1920,-1080)==0 && monitorAt(displays,-1,-1)==0);
    passOrFail("显示器边缘右下排他",monitorAt(displays,3840,2160)==2 && monitorAt(displays,4920,2420)==-1);
    passOrFail("虚拟桌面空洞不当作黑像素",monitorAt(displays,-1,100)==-1 && monitorAt(displays,4000,0)==-1);
    for(int dpi:{96,120,144,192,288}) for(const auto& monitor:displays) {
        for(auto p:std::array<std::pair<int,int>,4>{{{monitor.x,monitor.y},{monitor.x+monitor.width-1,monitor.y},
            {monitor.x,monitor.y+monitor.height-1},{monitor.x+monitor.width-1,monitor.y+monitor.height-1}}}) {
            const auto r=placeHud(monitor,p.first,p.second,310*dpi/96,280*dpi/96,24*dpi/96);
            passOrFail("HUD 四角在当前显示器内（几何模拟，非硬件验收）",r.x>=monitor.x && r.y>=monitor.y &&
                int64_t(r.x)+r.width<=int64_t(monitor.x)+monitor.width && int64_t(r.y)+r.height<=int64_t(monitor.y)+monitor.height);
        }
    }
    const auto tiny=placeHud({0,0,100,80},95,75,310,280,24);
    passOrFail("HUD 极小显示器收缩到边界",tiny==Rect{0,0,100,80});
    Settings settings;
    passOrFail("默认奇数15格8倍、不写坐标数据",settings==Settings{0,15,8,0,1,1} && settings.valid());
    for(int n:GRIDS) {settings.grid=n; passOrFail("每个允许格数有效且中心唯一",settings.valid() && n%2==1);}
    settings.grid=14;
    passOrFail("拒绝偶数网格",!settings.valid());
    settings={};settings.zoom=7;
    passOrFail("拒绝未登记倍率",!settings.valid());
    settings={};settings.format=FORMAT_COUNT;
    passOrFail("拒绝越界格式",!settings.valid());
    wchar_t temporary[MAX_PATH]{}; GetTempPathW(MAX_PATH,temporary);
    const auto path=std::filesystem::path(temporary)/(L"YeImageViewer-Picker-Test-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    settings={3,21,16,1,0,0};
    passOrFail("独立偏好原子写入并重载",PickerPreferences::save(path,settings) && PickerPreferences::load(path)==settings);
    for(int f=0;f<FORMAT_COUNT;++f) {
        settings.format=f;
        passOrFail("七种格式偏好全部跨重载保留",PickerPreferences::save(path,settings)&&PickerPreferences::load(path)==settings);
    }
    {
        std::ofstream file(path,std::ios::trunc);file<<"YeImageViewer.ColorPicker 1\n3 21 16 1 0 0\n";
    }
    passOrFail("旧v1偏好无损升级保留四种格式及其他字段",PickerPreferences::load(path)==Settings{3,21,16,1,0,0});
    settings={6,21,16,1,0,0};
    passOrFail("v2新增Lab格式原子保存",PickerPreferences::save(path,settings)&&PickerPreferences::load(path)==settings);
    {std::ofstream file(path,std::ios::trunc);file<<"YeImageViewer.ColorPicker 2\n6 21 16 1 0 0\n";}
    passOrFail("旧v2保留七种格式与采样选项且默认显示说明",PickerPreferences::load(path)==Settings{6,21,16,1,0,0,1});
    settings.showHints=0;
    passOrFail("v3隐藏说明开关跨重载保留且不改其他选项",PickerPreferences::save(path,settings)&&PickerPreferences::load(path)==settings);
    Settings badHints=settings;badHints.showHints=2;
    passOrFail("非法说明开关拒绝覆盖旧偏好",!PickerPreferences::save(path,badHints)&&PickerPreferences::load(path)==settings);
    {std::ofstream file(path,std::ios::trunc);file<<"YeImageViewer.ColorPicker 3\n6 21 16 1 0 0\n";}
    passOrFail("v3缺失说明字段拒绝截断数据",PickerPreferences::load(path)==Settings{});
    {std::ofstream file(path,std::ios::trunc);file<<"YeImageViewer.ColorPicker 3\n6 21 16 1 0 0 0 extra\n";}
    passOrFail("v3拒绝额外数据",PickerPreferences::load(path)==Settings{});
    passOrFail("有效v3可恢复写入",PickerPreferences::save(path,settings)&&PickerPreferences::load(path)==settings);
    Settings invalid=settings;invalid.zoom=-1;
    passOrFail("非法保存保留原偏好",!PickerPreferences::save(path,invalid) && PickerPreferences::load(path)==settings);
    {
        std::ofstream file(path,std::ios::trunc); file<<"YeImageViewer.ColorPicker 999\n0 15 8 0 1 1\n";
    }
    passOrFail("未知版本安全回默认，不误读新格式",PickerPreferences::load(path)==Settings{});
    {
        std::ofstream file(path,std::ios::trunc); file<<"YeImageViewer.ColorPicker 1\n0 15";
    }
    passOrFail("截断配置安全回默认",PickerPreferences::load(path)==Settings{});
    DeleteFileW(path.c_str());
    passOrFail("只读/不存在目录保存不静默成功",!PickerPreferences::save(path/L"missing"/L"settings.ini",Settings{}));
    for(int width:{300,800,3840}) for(int dpi:{96,192,288}) {
        const auto rect=OverlayLayout::screenPickerRect(width,2160,dpi);
        const auto flip=OverlayLayout::flipVerticalRect(width,2160,dpi);
        const auto hand=OverlayLayout::panMultiplierRect(width,2160,dpi);
        passOrFail("吸管严格位于上下镜像与手掌之间，不在工具栏尾部",flip.x+flip.width<=rect.x && rect.x+rect.width<=hand.x);
        const auto toolbar=OverlayLayout::toolbarRect(width,2160,dpi);
        const auto previous=OverlayLayout::toolbarPreviousRect(width,2160,dpi);
        const int separator=toolbar.x+OverlayLayout::scaled(OverlayLayout::BASE_TOOLBAR_PADDING+OverlayLayout::TOOLBAR_SEPARATORS[1],OverlayLayout::toolbarScale(width,dpi));
        passOrFail("手掌与导航之间的分隔线不穿过倍率按钮",separator>=hand.x+hand.width && separator<previous.x);
        passOrFail("吸管按钮绘制与命中使用同一几何",OverlayLayout::hitTest(width,2160,rect.x+rect.width/2,rect.y+rect.height/2,dpi)==OverlayLayout::Hit::ScreenPicker &&
            ToolbarCommand::resolve(OverlayLayout::Hit::ScreenPicker)==ToolbarCommand::Command::ScreenPicker);
    }
}
