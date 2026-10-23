#pragma once
#include "PhotocraftBackend.h"
#include "PsdFidelity.h"
#include "PsdRecompose.h"
#include "PsdDockStyle.h"
#include "ContrastRatio.h"

struct RecomposeScene {
    PsdLayers::Document doc;
    std::vector<std::vector<uint8_t>> storage;
    RecomposeScene() {doc.valid=true;doc.mode=3;doc.depth=8;doc.width=doc.height=1;}
    PsdLayers::Layer layer(uint8_t r,uint8_t g,uint8_t b,uint8_t a) {
        PsdLayers::Layer layer;layer.right=layer.bottom=1;layer.blend="norm";layer.name=L"test";
        for (int c=0;c<4;++c) {
            const uint8_t values[]={r,g,b,a};
            storage.push_back({0,0,values[c]});
            layer.channels.push_back({c==3?-1:c,storage.back(),3});
        }
        return layer;
    }
};

inline std::vector<uint8_t> makeVisibilityPsd();
inline void expectPhotocraftBlendOracles(){
    const std::filesystem::path root="test/image-tools/photocraft-blend-oracles";
    int count=0;
    for(const auto& entry:std::filesystem::directory_iterator(root)){
        if(entry.path().extension()!=L".psd")continue;++count;
        const auto bytes=readFile(entry.path().string());PhotocraftBackend::Document doc(bytes);
        auto image=doc.render(doc.originalStates());const auto reference=PsdCompositeDecoder::decode(bytes);
        const auto name=entry.path().filename().string();
        if(name=="group-divider-blend-mode.psd"){
            passOrFail("PhotoCraft损坏最终合成图时仍从独立图层恢复："+name,doc.valid()&&!image.bgra.empty());continue;
        }
        bool equal=!image.bgra.empty()&&image.bgra.size()==reference.bgra.size();int maximum=0;double worstBlock=0;
        const bool dissolve=name=="dissolve.psd";
        // 与已有PSD独立参考测试一致：先量化到8位预乘像素，再按2 LSB比较。
        const auto premul=[](const uint8_t* p,int c){return c==3?double(p[3]):double((int(p[c])*p[3]+127)/255);};
        if(equal)for(size_t p=0;p<image.bgra.size();p+=4)for(int c=0;c<4;++c)
            maximum=(std::max)(maximum,int(std::ceil(std::abs(premul(image.bgra.data()+p,c)-premul(reference.bgra.data()+p,c))-1e-7)));
        if(equal&&dissolve){
            // PS随机阈值未公开：沿用PhotoCraft既有的16×16块密度检查(0.1)，
            // 溶解层边界外仍执行逐像素2 LSB，不把随机纹理冒称逐像素一致。
            const auto raw=PsdLayers::read(bytes);
            bool outside=true;
            for(uint32_t y=0;y<image.height;++y)for(uint32_t x=0;x<image.width;++x){
                const bool inside=std::any_of(raw.layers.begin(),raw.layers.end(),[&](const auto& l){return l.visible&&l.blend=="diss"&&int64_t(x)>=l.left&&int64_t(x)<l.right&&int64_t(y)>=l.top&&int64_t(y)<l.bottom;});
                if(!inside){const size_t p=(size_t(y)*image.width+x)*4;for(int c=0;c<4;++c)outside&=std::abs(premul(image.bgra.data()+p,c)-premul(reference.bgra.data()+p,c))<=2.00001;}
            }
            for(uint32_t by=0;by<image.height;by+=16)for(uint32_t bx=0;bx<image.width;bx+=16){
                const auto y1=(std::min)(by+16,image.height),x1=(std::min)(bx+16,image.width);
                for(int c=0;c<4;++c){double total=0;for(uint32_t y=by;y<y1;++y)for(uint32_t x=bx;x<x1;++x){const size_t p=(size_t(y)*image.width+x)*4;total+=premul(image.bgra.data()+p,c)-premul(reference.bgra.data()+p,c);}
                    worstBlock=(std::max)(worstBlock,std::abs(total)/((y1-by)*(x1-bx)));}
            }
            equal=outside&&worstBlock<=25.5;
        }else equal&=maximum<=2;
        passOrFail(std::string(dissolve?"PhotoCraft溶解块密度/边界外像素参考：":"PhotoCraft与Photoshop保存合成逐像素2 LSB参考：")+name,equal);
        if(!equal)std::cout<<"oracle="<<name<<" error="<<doc.error<<" max="<<maximum<<" worstBlock="<<worstBlock<<" warning="<<doc.warning<<"\n";
    }
    passOrFail("PhotoCraft独立混合参考素材清单完整",count==29);
}
inline void expectPhotocraftBackend() {
    const std::array<uint8_t,4> opaque{10,20,30,255},different{10,20,42,255},transparent{10,20,30,0},transparent2{200,220,230,0};
    const auto measured=PsdFidelity::compare(opaque,different);
    passOrFail("有实际合成差异时保留可量化警告，不冒称忠实重建",measured.valid&&measured.maximum==12&&measured.mean==3&&measured.differs());
    passOrFail("全透明无意义RGB不触发合成差异警告",!PsdFidelity::compare(transparent,transparent2).differs());
    passOrFail("缺少独立原合成图时不能报告一致",PsdFidelity::compare({},{}).differs());
    const auto fixture=makeVisibilityPsd();
    PhotocraftBackend::Document backend(fixture);
    passOrFail("PhotoCraft真实DLL及C ABI版本可用，不静默回退旧合成器",backend.valid());
    if(!backend.valid()){std::cout<<"backend error="<<backend.error<<"\n";return;}
    passOrFail("图层缩略图扩展来自实际后端，不用整画布合成充当快路径",backend.hasThumbnails());
    const auto thumbnail=backend.thumbnail(0,false,32);
    passOrFail("图层缩略直接读取原层红色像素，不混入下层绿色",thumbnail.width==32&&thumbnail.height==32&&thumbnail.bgra.size()==4096&&thumbnail.bgra[0]==0&&thumbnail.bgra[1]==0&&thumbnail.bgra[2]==255);
    passOrFail("缩略尺寸越界和取消均拒绝且文档仍可用",backend.thumbnail(0,false,0).bgra.empty()&&backend.thumbnail(0,false,129).bgra.empty()&&backend.thumbnail(0,false,32,[]{return true;}).bgra.empty()&&backend.valid()&&!backend.thumbnail(0).bgra.empty());
    auto states=backend.originalStates();
    passOrFail("PhotoCraft导入三层真实PSD并返回规范图层顺序",states.size()==3&&backend.metadata.width==2&&backend.metadata.height==2);
    if(states.size()!=3)return;
    auto initial=backend.render(states);
    passOrFail("PhotoCraft原始RGB重合成命中独立给定像素",initial.bgra.size()==16&&initial.bgra[0]==0&&initial.bgra[1]==127&&initial.bgra[2]==128&&initial.bgra[3]==255);
    states[0].visible=states[1].visible=0;
    auto image=backend.render(states);
    passOrFail("PhotoCraft多层显隐实时重合成独立蓝色像素",image.bgra.size()==16&&std::equal(image.bgra.begin(),image.bgra.begin()+4,std::array<uint8_t,4>{255,0,0,255}.begin()));
    states[2].visible=0;image=backend.render(states);
    passOrFail("PhotoCraft全层隐藏保留真实透明，不回退原合成",image.bgra.size()==16&&std::all_of(image.bgra.begin(),image.bgra.end(),[](uint8_t v){return v==0;}));
    states=backend.originalStates();states[0].opacity=std::numeric_limits<float>::quiet_NaN();
    passOrFail("PhotoCraft拒绝NaN覆盖参数",backend.render(states).bgra.empty()&&backend.error==1);
    states=backend.originalStates();image=backend.render(states);
    passOrFail("PhotoCraft错误请求不污染后续原始状态",!image.bgra.empty());
    std::ifstream file("test/image-tools/public/ag-psd-group-blend.psd",std::ios::binary);
    const std::vector<uint8_t> grouped((std::istreambuf_iterator<char>(file)),{});
    PhotocraftBackend::Document groups(grouped);
    passOrFail("PhotoCraft导入独立外部PSD分组及真实图层树",groups.valid()&&groups.rows.size()==6&&groups.names[0]==L"Group 2"&&groups.rows[0].section==1&&groups.rows[2].section==3);
    const auto cancelled=backend.render(backend.originalStates(),-1,0,[]{return true;});
    passOrFail("PhotoCraft取消合成不跨ABI抛出异常、不交付过期画面",cancelled.bgra.empty()&&backend.error==3);
    image=backend.render(backend.originalStates());
    passOrFail("PhotoCraft取消后的缓存文档仍能正确重新渲染",image.bgra==initial.bgra);
    PhotocraftBackend::Document cancelledOpen(fixture,[]{return true;});
    passOrFail("PhotoCraft文档导入响应会话取消",!cancelledOpen.valid()&&cancelledOpen.error==3);
    for(bool psb:{false,true})for(int depth:{8,16,32})for(int compression:{0,1,2,3}){
        auto bytes=makeToolPsd(psb,depth,compression);
        // 原夹具刻意隐藏、半不透明且位于负坐标，用于“单层原始栅格”测试。
        // 合成测试显式把它放回画布并打开，不把透明的正确结果当解码错误。
        const std::string marker="8BIMnorm";const auto blend=std::search(bytes.begin(),bytes.end(),marker.begin(),marker.end());
        const auto p=size_t(blend-bytes.begin());bytes[p+8]=255;bytes[p+10]=0;
        const size_t rect=psb?52:44;std::vector<uint8_t> bounds;for(auto v:{0,0,1,2})PsdLayers::integer(bounds,v,4);
        std::copy(bounds.begin(),bounds.end(),bytes.begin()+rect);
        PhotocraftBackend::Document doc(bytes);auto rendered=doc.render(doc.originalStates());
        passOrFail("PhotoCraft真实PSD/PSB 8/16/32位四压缩走同一合成路径",doc.valid()&&rendered.bgra.size()==8&&rendered.bgra[2]==255&&rendered.bgra[3]==255&&rendered.bgra[5]==255&&rendered.bgra[7]==128);
    }
    const auto cmykBytes=readFile("test/image-tools/readonly-cmyk.psd");
    auto old=PsdLayers::read(cmykBytes);std::vector<uint8_t> flags;for(const auto& layer:old.layers)flags.push_back(layer.visible);
    PsdRecompose::Cache legacyCache;
    passOrFail("修复前对照：旧合成器确实拒绝真实CMYK最小夹具",PsdRecompose::render(old,flags,legacyCache).issue==PsdRecompose::Issue::Format);
    PhotocraftBackend::Document cmyk(cmykBytes);auto cmykStates=cmyk.originalStates();
    passOrFail("修复后：CMYK保留文档原始模式并允许显隐",cmyk.valid()&&cmyk.metadata.mode==4&&!cmyk.render(cmykStates).bgra.empty());
    if(!cmykStates.empty()){for(auto& state:cmykStates)state.visible=0;auto empty=cmyk.render(cmykStates);
        passOrFail("CMYK全隐藏保留透明，不能冒称原合成图",empty.bgra.size()==16&&std::all_of(empty.bgra.begin(),empty.bgra.end(),[](uint8_t v){return v==0;}));}
    const auto maskBytes=readFile("test/image-tools/photocraft-mask.psd");PhotocraftBackend::Document masked(maskBytes);auto maskStates=masked.originalStates();
    const auto maskImage=masked.render(maskStates);
    const std::vector<uint8_t> maskExpected{255,0,0,255,0,0,255,255,127,0,128,255,0,0,255,255};
    passOrFail("PhotoCraft像素蒙版及原生图层坐标命中独立四像素参考",masked.valid()&&maskImage.bgra==maskExpected);
    if(maskStates.size()==2){
        const auto mini=masked.thumbnail(0,true,2);
        passOrFail("蒙版缩略直接采四个真实蒙版像素，与上游界面口径一致",mini.width==2&&mini.bgra.size()==16&&mini.bgra[0]==0&&mini.bgra[4]==255&&mini.bgra[8]==128&&mini.bgra[12]==255);
        maskStates[0].mask=0;const auto disabled=masked.render(maskStates);
        passOrFail("关闭蒙版是实时重合成而非仅更改图标",disabled.bgra.size()==16&&disabled.bgra[2]==255&&disabled.bgra[0]==0);
        maskStates[0].mask=1;const auto view=masked.render(maskStates,-1,20000);
        passOrFail("蒙版独立灰度视图来自真实蒙版通道",view.bgra.size()==16&&view.bgra[0]==0&&view.bgra[4]==255&&view.bgra[8]==128&&view.bgra[12]==255);
    }
    expectPhotocraftBlendOracles();
}
inline void expectPsdRecompose() {
    RecomposeScene scene;
    scene.doc.layers={scene.layer(255,0,0,128),scene.layer(0,255,0,255),scene.layer(0,0,255,255)};
    PsdRecompose::Cache cache;
    const auto evaluate=[&](std::vector<uint8_t> visible){return PsdRecompose::render(scene.doc,visible,cache);};
    auto result=evaluate({1,1,1});
    passOrFail("PSD recomposition preserves top-to-bottom order and nonpremultiplied alpha",
        result && result.image.bgra==std::vector<uint8_t>({0,127,128,255}));
    result=evaluate({0,0,1});
    passOrFail("PSD multiple hidden layers reveal the true bottom layer",result && result.image.bgra==std::vector<uint8_t>({255,0,0,255}));
    result=evaluate({0,0,0});
    passOrFail("PSD all hidden layers produce real transparency",result && result.image.bgra==std::vector<uint8_t>({0,0,0,0}));
    passOrFail("PSD visibility flags do not change the file document",scene.doc.layers[0].visible&&scene.doc.layers[1].visible);
    scene.doc.layers[0].blend="unknown";
    passOrFail("PSD unsupported visible blend fails rather than silently using Normal",evaluate({1,1,1}).issue==PsdRecompose::Issue::Blend);
    passOrFail("PSD unsupported hidden layer does not corrupt supported visible composition",bool(evaluate({0,0,1})));
    scene.doc.layers[0].blend="norm";
    scene.doc.layers[0].clipped=true;
    passOrFail("PSD clipping chain is explicitly unsupported instead of rendered incorrectly",evaluate({1,1,1}).issue==PsdRecompose::Issue::Clipping);
    scene.doc.layers[0].clipped=false;scene.doc.layers[0].effects=true;
    passOrFail("PSD layer effects cannot disappear silently during recomposition",evaluate({1,1,1}).issue==PsdRecompose::Issue::Effects);
    scene.doc.layers[0].effects=false;scene.doc.layers[0].vectorMask=true;
    passOrFail("PSD vector masks are not ignored during recomposition",evaluate({1,1,1}).issue==PsdRecompose::Issue::Effects);
    scene.doc.layers[0].vectorMask=false;
    scene.doc.layers[0].opacity=128;
    result=evaluate({1,0,1});
    passOrFail("PSD layer opacity multiplies alpha exactly once",result&&result.image.bgra==std::vector<uint8_t>({191,0,64,255}));
    scene.doc.layers[0].opacity=255;
    scene.doc.layers[0].left=-1;scene.doc.layers[0].right=0;cache.clear();
    result=evaluate({1,0,1});
    passOrFail("PSD negative offsets clip at the document border",result&&result.image.bgra==std::vector<uint8_t>({255,0,0,255}));
    scene.doc.layers[0].left=0;scene.doc.layers[0].right=1;cache.clear();
    scene.doc.depth=16;passOrFail("PSD unsupported interactive depth leaves the original viewing decoder independent",evaluate({1,1,1}).issue==PsdRecompose::Issue::Format);scene.doc.depth=8;
    const std::vector<uint8_t> flags{1,1,1};
    result=PsdRecompose::render(scene.doc,flags,cache,[]{return true;});
    passOrFail("PSD stale generation cancellation returns no partial canvas",result.issue==PsdRecompose::Issue::Cancelled&&result.image.bgra.empty());
    passOrFail("PSD invalid visibility length is rejected",evaluate({1}).issue==PsdRecompose::Issue::Bounds);
    for (const auto& [mode,expected]:std::vector<std::pair<std::string,double>>{
        {"norm",0.8},{"mul ",0.16},{"scrn",0.84},{"over",0.32},{"dark",0.2},{"lite",0.8},
        {"diff",0.6},{"smud",0.68},{"hLit",0.68},{"sLit",0.3488}}) {
        passOrFail("PSD separable blend oracle: "+mode,std::abs(PsdRecompose::blend(0.2,0.8,mode)-expected)<1e-9);
    }
    RecomposeScene groups;
    PsdLayers::Layer header;header.group=true;header.sectionType=1;header.blend="pass";
    PsdLayers::Layer end;end.group=true;end.sectionType=3;
    groups.doc.layers={header,groups.layer(255,0,0,255),end,groups.layer(0,0,255,255)};
    cache.clear();result=PsdRecompose::render(groups.doc,std::vector<uint8_t>{0,1,1,1},cache);
    passOrFail("PSD group eye hides descendants without changing their own flags",result&&result.image.bgra==std::vector<uint8_t>({255,0,0,255}));
    result=PsdRecompose::render(groups.doc,std::vector<uint8_t>{1,1,1,1},cache);
    passOrFail("PSD pass-through group at full opacity renders descendants in order",result&&result.image.bgra==std::vector<uint8_t>({0,0,255,255}));
    groups.doc.layers[0].blend="norm";groups.doc.layers[0].opacity=128;
    result=PsdRecompose::render(groups.doc,std::vector<uint8_t>{1,1,1,1},cache);
    passOrFail("PSD isolated group opacity is applied after its children",result&&result.image.bgra==std::vector<uint8_t>({127,0,128,255}));
    groups.doc.layers.erase(groups.doc.layers.begin()+2);
    passOrFail("PSD unbalanced group records are not flattened silently",PsdRecompose::render(groups.doc,std::vector<uint8_t>{1,1,1},cache).issue==PsdRecompose::Issue::Structure);
    RecomposeScene masked;
    auto top=masked.layer(255,0,0,255);auto bottom=masked.layer(0,0,255,255);
    std::vector<uint8_t> maskData;for(auto v:{0,0,1,1})PsdLayers::integer(maskData,v,4);
    maskData.push_back(255);maskData.push_back(0);top.masked=true;top.maskData=maskData;
    masked.storage.push_back({0,0,128});top.channels.push_back({-2,masked.storage.back(),3});
    masked.doc.layers={top,bottom};cache.clear();result=PsdRecompose::render(masked.doc,std::vector<uint8_t>{1,1},cache);
    passOrFail("PSD raster mask alpha participates in the composite",result&&result.image.bgra==std::vector<uint8_t>({127,0,128,255}));
    maskData.resize(36);masked.doc.layers[0].maskData=maskData;
    result=PsdRecompose::render(masked.doc,std::vector<uint8_t>{1,1},cache);
    passOrFail("PSD extended real-user mask metadata is not silently discarded",result.issue==PsdRecompose::Issue::Mask);
    maskData.resize(18);masked.doc.layers[0].maskData=maskData;
    masked.doc.layers[0].channels.push_back({-3,masked.storage.back(),3});
    result=PsdRecompose::render(masked.doc,std::vector<uint8_t>{1,1},cache);
    passOrFail("PSD separate real-user mask channel is not silently discarded",result.issue==PsdRecompose::Issue::Mask);
    for(bool dark:{false,true}) {
        const auto background=PsdDockStyle::background(dark);
        passOrFail("PSD dock foreground, secondary text and selected tab are readable in both themes",
            ContrastRatio::between(PsdDockStyle::foreground(dark),background)>=4.5&&
            ContrastRatio::between(PsdDockStyle::muted(dark),background)>=4.5&&
            ContrastRatio::between(PsdDockStyle::accent(dark),background)>=4.5);
    }
}

inline std::vector<uint8_t> makeVisibilityPsd() {
    using PsdLayers::integer;
    std::vector<uint8_t> info;integer(info,3,2);
    const uint8_t colors[3][4]={{255,0,0,128},{0,255,0,255},{0,0,255,255}};
    // PSD物理记录自底向顶，不能按界面顺序制作夹具，否则测试会与读取器一起错。
    for(int layer=2;layer>=0;--layer) {
        integer(info,0,4);integer(info,0,4);integer(info,2,4);integer(info,2,4);integer(info,4,2);
        for(int c=0;c<4;++c){integer(info,c==3?65535:c,2);integer(info,6,4);}
        info.insert(info.end(),{'8','B','I','M','n','o','r','m',255,0,0,0});
        std::vector<uint8_t> extra;integer(extra,0,4);integer(extra,0,4);
        const std::string name=layer==0?"Top red":layer==1?"Middle green":"Bottom blue";
        integer(extra,name.size(),1);extra.insert(extra.end(),name.begin(),name.end());
        while(extra.size()%4)extra.push_back(0);
        integer(info,extra.size(),4);info.insert(info.end(),extra.begin(),extra.end());
    }
    for(int layer=2;layer>=0;--layer)for(int c=0;c<4;++c){integer(info,0,2);for(int p=0;p<4;++p)info.push_back(colors[layer][c]);}
    if(info.size()%2)info.push_back(0);
    std::vector<uint8_t> section;integer(section,info.size(),4);section.insert(section.end(),info.begin(),info.end());integer(section,0,4);
    std::vector<uint8_t> file{'8','B','P','S'};integer(file,1,2);integer(file,0,6);integer(file,3,2);
    integer(file,2,4);integer(file,2,4);integer(file,8,2);integer(file,3,2);integer(file,0,4);integer(file,0,4);
    integer(file,section.size(),4);file.insert(file.end(),section.begin(),section.end());integer(file,0,2);
    for(int c=0;c<3;++c)for(int p=0;p<4;++p)file.push_back(c==0?128:c==1?127:0);
    return file;
}

inline void expectPsdRecomposeParser() {
    const auto fixture=makeVisibilityPsd();
    const auto ordered=PsdLayers::read(fixture);
    PsdRecompose::Cache orderCache;
    const auto orderedImage=PsdRecompose::render(ordered,std::vector<uint8_t>{1,1,1},orderCache);
    passOrFail("PSD physical bottom-to-top records become top-to-bottom UI layers",
        ordered.valid&&ordered.layers.size()==3&&ordered.layers.front().name==L"Top red"&&ordered.layers.back().name==L"Bottom blue");
    passOrFail("PSD independent flattened pixel agrees with parsed physical layer order",
        orderedImage&&orderedImage.image.bgra[0]==0&&orderedImage.image.bgra[1]==127&&orderedImage.image.bgra[2]==128);
    const auto externalBytes=readFile("test/image-tools/public/ag-psd-group-blend.psd");
    const auto external=PsdLayers::read(externalBytes);
    const auto reference=PsdCompositeDecoder::decode(externalBytes);
    std::vector<uint8_t> externalFlags;for(const auto& layer:external.layers)externalFlags.push_back(layer.visible);
    PsdRecompose::Cache externalCache;
    const auto composed=PsdRecompose::render(external,externalFlags,externalCache);
    passOrFail("Independent PSD group fixture has the real top-down tree after parsing",
        external.valid&&external.layers.size()==6&&external.layers[0].name==L"Group 2"&&external.layers[0].sectionType==1&&external.layers[2].sectionType==3);
    bool equal=composed&&reference.bgra.size()==composed.image.bgra.size();int maximum=0;
    if(equal)for(size_t p=0;p<reference.bgra.size();p+=4) {
        maximum=(std::max)(maximum,std::abs(int(reference.bgra[p+3])-composed.image.bgra[p+3]));
        for(int c=0;c<3;++c)maximum=(std::max)(maximum,std::abs(
            (int(reference.bgra[p+c])*reference.bgra[p+3]+127)/255-
            (int(composed.image.bgra[p+c])*composed.image.bgra[p+3]+127)/255));
    }
    // 两个8位栅格图层的合成舍入允许2 LSB；比较预乘颜色避免透明像素的无意义RGB。
    passOrFail("Independent PSD saved composite matches grouped recomposition within 2 LSB",
        equal&&maximum<=2);
    if(!equal||maximum>2) {
        std::cout<<"Independent PSD maximum delta="<<maximum<<" issue="<<int(composed.issue)<<"\n";
        if(equal)for(size_t p=0;p<reference.bgra.size();p+=4)if(reference.bgra[p+3]>0&&reference.bgra[p+3]<255) {
            std::cout<<"sample pixel="<<p/4<<" reference=";for(int c=0;c<4;++c)std::cout<<int(reference.bgra[p+c])<<",";
            std::cout<<" composed=";for(int c=0;c<4;++c)std::cout<<int(composed.image.bgra[p+c])<<",";std::cout<<"\n";break;
        }
    }
    const auto read32=[](const std::vector<uint8_t>& bytes,size_t p){uint32_t n=0;for(int i=0;i<4;++i)n=n*256+bytes[p+i];return n;};
    const auto write32=[](std::vector<uint8_t>& bytes,size_t p,uint32_t n){for(int i=3;i>=0;--i){bytes[p+i]=static_cast<uint8_t>(n);n>>=8;}};
    const std::string marker="8BIMnorm";
    const size_t p=static_cast<size_t>(std::search(fixture.begin(),fixture.end(),marker.begin(),marker.end())-fixture.begin());
    auto changed=fixture;changed[p+9]=1;changed[p+8]=123;changed[p+10]=2;
    auto doc=PsdLayers::read(changed);
    passOrFail("PSD index retains clipping, opacity and visibility instead of discarding them",doc.valid&&doc.layers.size()==3&&doc.layers.back().clipped&&!doc.layers.back().visible&&doc.layers.back().opacity==123);
    for(auto name:{"lfx2","vmsk","iOpa","lsct","knko","tsly","artb","brst"}) {
        changed=fixture;
        std::vector<uint8_t> tag{'8','B','I','M'};tag.insert(tag.end(),name,name+4);
        PsdLayers::integer(tag,std::string_view(name)=="lsct"?4:1,4);
        if(std::string_view(name)=="lsct")PsdLayers::integer(tag,1,4);else {tag.push_back(std::string_view(name)=="tsly"?0:128);tag.push_back(0);}
        const uint32_t extra=read32(changed,p+12);
        changed.insert(changed.begin()+p+16+extra,tag.begin(),tag.end());
        write32(changed,p+12,extra+static_cast<uint32_t>(tag.size()));
        write32(changed,34,read32(changed,34)+static_cast<uint32_t>(tag.size()));
        write32(changed,38,read32(changed,38)+static_cast<uint32_t>(tag.size()));
        doc=PsdLayers::read(changed);
        const bool valid=doc.valid&&doc.layers.size()==3;
        bool retained=false;if(valid){const auto& layer=doc.layers.back();
            retained=std::string_view(name)=="lfx2"?layer.effects:std::string_view(name)=="vmsk"?layer.vectorMask:
                std::string_view(name)=="iOpa"?layer.fillOpacity==128:std::string_view(name)=="lsct"?layer.sectionType==1&&layer.group:layer.effects;}
        passOrFail(std::string("PSD index retains composition capability tag: ")+name,valid&&retained);
    }
    for(bool custom:{false,true}) {
        changed=fixture;std::vector<uint8_t> ranges{0,0,255,255,0,0,255,255};if(custom)ranges[0]=64;
        const uint32_t extra=read32(changed,p+12);
        changed.insert(changed.begin()+p+24,ranges.begin(),ranges.end());
        write32(changed,p+20,8);write32(changed,p+12,extra+8);
        write32(changed,34,read32(changed,34)+8);write32(changed,38,read32(changed,38)+8);
        doc=PsdLayers::read(changed);
        passOrFail("PSD default Blend If ranges remain supported and custom ranges are flagged",doc.valid&&doc.layers.back().customBlendRanges==custom);
    }
}
