#pragma once
#include "PsdRecompose.h"
#include "ColorManager.h"
#include "TextDrawer.h"
#include "PsdDockStyle.h"
#include "PsdTreeLayout.h"
#include "PhotocraftBackend.h"
#include "PsdBlendModes.h"
#include "PsdFidelity.h"
#include <format>
#include <future>
#include <atomic>
#include <fstream>
#include <set>
#include <map>

// 主窗口内绘制的停靠面板。所有解析/重合成都在一个后台任务中，快速切换只交付最新代。
class PsdDock {
    struct Source {
        // 元数据先显示、后台导入后再交付新的Source；共享只读文件字节，不复制大PSB。
        std::shared_ptr<std::vector<uint8_t>> bytes=std::make_shared<std::vector<uint8_t>>();
        PsdLayers::Document document;
        PsdRecompose::Cache cache;
        PsdCompositeDecoder::Image original;
        std::unique_ptr<PhotocraftBackend::Document> backend;
        bool backendAttempted=false;
        bool baselineChecked=false;
        PsdFidelity::Difference baseline;
    };
    struct Delivery {
        std::shared_ptr<Source> source;
        std::vector<uint8_t> visible;
        std::vector<PhotocraftBackend::State> properties;
        cv::Mat display, exportPixels;
        PsdRecompose::Issue issue=PsdRecompose::Issue::None;
        int layer=-1;
        uint64_t revision=0;
        bool thumbnailsOnly=false;
        std::vector<std::pair<int,cv::Mat>> thumbnails;
    };
    std::shared_ptr<Source> source;
    std::future<Delivery> job;
    std::shared_ptr<std::atomic<uint64_t>> generation=std::make_shared<std::atomic<uint64_t>>(0);
    std::vector<uint8_t> desired, accepted;
    std::vector<PhotocraftBackend::State> properties,acceptedProperties;
    bool queued=false, open_=false, solo=false;
    bool acceptedSolo=false;
    bool selectedMask=false;
    int acceptedChannel=0;
    int tab=0, selected=-1, channel=0, scroll=0, dpi=96;
    int width_=0,height_=0,sidebar_=0;
    int preferredWidth=300;
    int hoverX=-1,hoverY=-1,scrollGrab=0;bool scrollDragging=false;
    HWND owner_=nullptr;
    std::set<int> collapsed;
    std::set<int> thumbnailAttempted;
    std::map<int,cv::Mat> thumbnails;
    bool thumbnailJob=false;
    std::wstring path;
    cv::Mat display_,export_;
    PsdRecompose::Issue issue_=PsdRecompose::Issue::None;
    int issueLayer=-1;
    int s(int value) const {return MulDiv(value,dpi,96);}
    bool nativeBackend()const{return source&&source->backend&&source->backend->valid();}
    std::vector<std::pair<int,std::string>> channels()const {
        std::vector<std::pair<int,std::string>> result{{0,tr("彩色合成","Composite","彩色合成")}};
        if(!nativeBackend())return {{0,tr("彩色合成","Composite","彩色合成")},{1,"R"},{2,"G"},{3,"B"},{4,"Alpha"}};
        const auto mode=source->document.mode;
        const std::vector<std::string> names=mode==4?std::vector<std::string>{"C","M","Y","K"}:mode==9?std::vector<std::string>{"L","a","b"}:mode==1?std::vector<std::string>{tr("灰度","Gray","灰階")}:std::vector<std::string>{"R","G","B"};
        for(int i=0;i<int(names.size());++i)result.emplace_back(10+i,names[i]);
        result.emplace_back(4,tr("透明度","Transparency","透明度"));
        for(int i=0;i<int(source->backend->channelNames.size());++i)result.emplace_back(100+i,jarkUtils::wstringToUtf8(source->backend->channelNames[i]));
        if(selected>=0&&selected<layerCount()&&source->document.layers[selected].masked)
            result.emplace_back(20000+selected,jarkUtils::wstringToUtf8(source->document.layers[selected].name)+tr(" 蒙版"," Mask"," 遮色片"));
        return result;
    }
    bool editableSelection()const{return nativeBackend()&&selected>=0&&selected<int(properties.size())&&source->document.layers[selected].sectionType!=3;}
    void blendMenu(int x,int y){
        if(!editableSelection()||!owner_)return;
        const HMENU menu=CreatePopupMenu();if(!menu)return;
        const auto& current=properties[selected];
        for(size_t i=0;i<PsdBlendModes::entries.size();++i){const auto& entry=PsdBlendModes::entries[i];
            if(i==0&&!source->document.layers[selected].group)continue;
            const auto name=jarkUtils::utf8ToWstring(tr(entry.chinese,entry.english,entry.traditional));
            AppendMenuW(menu,MF_STRING|(std::string_view(current.blend.data(),4)==entry.key?MF_CHECKED:0),UINT_PTR(i+1),name.c_str());
        }
        POINT point{x,y};ClientToScreen(owner_,&point);
        const auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,point.x,point.y,0,owner_,nullptr);DestroyMenu(menu);
        if(command&&command<=PsdBlendModes::entries.size()){
            setBlend(PsdBlendModes::entries[command-1].key);
        }
    }
    void opacityMenu(int x,int y,bool fill){
        if(!editableSelection()||!owner_)return;
        const HMENU menu=CreatePopupMenu();if(!menu)return;
        const int current=int(std::lround((fill?properties[selected].fill:properties[selected].opacity)*100));
        AppendMenuW(menu,MF_STRING,201,trW(L"增加 1%",L"Increase 1%",L"增加 1%"));
        AppendMenuW(menu,MF_STRING,202,trW(L"减少 1%",L"Decrease 1%",L"減少 1%"));AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        for(int i=0;i<=100;i+=5){const auto name=std::to_wstring(i)+L"%";AppendMenuW(menu,MF_STRING|(i==current?MF_CHECKED:0),i+1,name.c_str());}
        POINT point{x,y};ClientToScreen(owner_,&point);
        const auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,point.x,point.y,0,owner_,nullptr);DestroyMenu(menu);
        if(command){const int value=command==201?(std::min)(100,current+1):command==202?(std::max)(0,current-1):int(command)-1;
            if(value>=0&&value<=100)setPercent(value,fill);}
    }
    void enqueue() {++*generation;queued=true;issue_=PsdRecompose::Issue::None;}
    std::vector<PsdTree::Row> rows() const {return source?PsdTree::rows(source->document,collapsed,desired):std::vector<PsdTree::Row>{};}
    PsdRecompose::Issue readOnlyReason()const {
        if(!source||!source->backendAttempted)return PsdRecompose::Issue::Decode;
        if(source->backend&&source->backend->valid())return PsdRecompose::Issue::None;
        const auto& doc=source->document;
        if(doc.mode!=3||doc.depth!=8)return PsdRecompose::Issue::Format;
        if(uint64_t(doc.width)*doc.height>16ULL*1024*1024)return PsdRecompose::Issue::Bounds;
        for(const auto& layer:doc.layers){if(layer.sectionType==3)continue;
            if(layer.effects||layer.vectorMask||layer.customBlendRanges||layer.fillOpacity!=255)return PsdRecompose::Issue::Effects;
            if(layer.adjustment)return PsdRecompose::Issue::Adjustment;
            if(layer.clipped)return PsdRecompose::Issue::Clipping;
            if(!PsdRecompose::supportsBlend(layer.blend)&&!(layer.group&&layer.blend=="pass"&&layer.opacity==255))return PsdRecompose::Issue::Blend;
        }
        return PsdRecompose::Issue::None;
    }
    static std::shared_ptr<Source> attachBackend(const std::shared_ptr<Source>& raw,const std::function<bool()>& cancel) {
        // 不在工作线程修改UI正在读取的Source，避免unique_ptr/图层数组数据竞争。
        auto value=std::make_shared<Source>();value->bytes=raw->bytes;value->document=raw->document;
        value->backend=std::make_unique<PhotocraftBackend::Document>(*value->bytes,cancel);
        value->backendAttempted=true;
        if(value->backend->valid())value->document=value->backend->tree(&value->document);
        return value->document.valid?value:nullptr;
    }
    static std::shared_ptr<Source> load(const std::wstring& path,const std::function<bool()>& cancel={},bool full=true) {
        auto value=std::make_shared<Source>();
        std::ifstream file(std::filesystem::path(path),std::ios::binary|std::ios::ate);
        if (!file) return {};
        const auto size=file.tellg();
        if (size<=0 || size>256LL*1024*1024) return {};
        value->bytes->resize(static_cast<size_t>(size));file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(value->bytes->data()),size)) return {};
        if(cancel&&cancel())return {};
        value->document=PsdLayers::read(*value->bytes);
        // 元数据读取器不能成为更强后端的前置门槛；它不认识的合法结构仍交给原引擎恢复。
        if(!value->document.valid)return attachBackend(value,cancel);
        return full?attachBackend(value,cancel):value;
    }
    static Delivery calculate(std::shared_ptr<Source> source,const std::wstring& path,
        std::vector<uint8_t> visible,int selected,int channel,bool solo,uint64_t revision,
        const std::shared_ptr<std::atomic<uint64_t>>& generation,HWND owner,
        std::vector<PhotocraftBackend::State> properties={}) {
        Delivery out;out.revision=revision;
        try {
            if (!source) source=load(path,[&]{return generation->load()!=revision;},false);
            else if(!source->backendAttempted)source=attachBackend(source,[&]{return generation->load()!=revision;});
            if (!source) {out.issue=PsdRecompose::Issue::Decode;return out;}
            out.source=source;
            if (visible.size()!=source->document.layers.size()) {
                visible.clear();for(const auto& layer:source->document.layers) visible.push_back(layer.visible);
            }
            out.visible=visible;
            bool original=!solo;
            for(size_t i=0;i<visible.size();++i) original &= bool(visible[i])==source->document.layers[i].visible;
            if(source->backend&&source->backend->valid()){
                const auto defaults=source->backend->originalStates();
                if(properties.size()!=defaults.size())properties=defaults;
                for(size_t i=0;i<properties.size();++i){
                    properties[i].visible=visible[i];
                    if(properties[i].mask!=defaults[i].mask||properties[i].opacity!=defaults[i].opacity||properties[i].fill!=defaults[i].fill||properties[i].blend!=defaults[i].blend)original=false;
                }
                out.properties=properties;
            }
            if(original && channel==0) return out; // 原始状态始终使用文件自带合成图。
            // 初次只显示图层树，既不重解文件合成图，也不做额外全画布合成。
            // 用户首次改变显隐/属性时再核对保真度，不能用永久删除警告换取速度。
            if(source->backend&&source->backend->valid()&&!source->baselineChecked){
                source->original=PsdCompositeDecoder::decode(*source->bytes,256ULL<<20);
                const auto rebuilt=source->backend->render(source->backend->originalStates(),-1,0,[&]{return generation->load()!=revision;});
                if(generation->load()!=revision){out.issue=PsdRecompose::Issue::Cancelled;return out;}
                if(!rebuilt.bgra.empty()&&!source->original.bgra.empty()){
                    cv::Mat reference(source->original.height,source->original.width,CV_8UC4,source->original.bgra.data());reference=reference.clone();
                    ColorManager::applyToMat(reference,source->original.icc,PsdCompositeDecoder::srgbProfile());
                    source->baseline=PsdFidelity::compare({reference.data,reference.total()*4},rebuilt.bgra);
                }
                source->baselineChecked=true;
            }
            PsdCompositeDecoder::Image image;
            const auto& doc=source->document;
            if (uint64_t(doc.width)*doc.height>16ULL*1024*1024) {out.issue=PsdRecompose::Issue::Bounds;return out;}
            bool bakedChannel=false;
            if(source->backend&&source->backend->valid()&&(!original||solo||channel)) {
                if(generation->load()!=revision){out.issue=PsdRecompose::Issue::Cancelled;return out;}
                image=source->backend->render(properties,solo?selected:-1,channel,[&]{return generation->load()!=revision;});
                bakedChannel=channel!=0;
                if(generation->load()!=revision){out.issue=PsdRecompose::Issue::Cancelled;return out;}
                if(image.bgra.empty()){out.issue=PsdRecompose::Issue::Decode;return out;}
            } else if (solo && selected>=0 && selected<static_cast<int>(doc.layers.size())) {
                auto layer=PsdLayers::preview(doc,selected);
                if (layer.bgra.empty()) {out.issue=PsdRecompose::Issue::Decode;out.layer=selected;return out;}
                image.width=doc.width;image.height=doc.height;image.icc=layer.icc;image.hasAlpha=true;
                image.bgra.resize(size_t(doc.width)*doc.height*4);
                const auto& bounds=doc.layers[selected];
                for(int64_t y=(std::max)(0,bounds.top);y<(std::min)(int64_t(doc.height),int64_t(bounds.bottom));++y)
                    for(int64_t x=(std::max)(0,bounds.left);x<(std::min)(int64_t(doc.width),int64_t(bounds.right));++x)
                        std::copy_n(layer.bgra.data()+((y-bounds.top)*layer.width+x-bounds.left)*4,4,
                            image.bgra.data()+(y*doc.width+x)*4);
            } else if (original) {
                if(source->original.bgra.empty()) source->original=PsdCompositeDecoder::decode(*source->bytes,256ULL<<20);
                image=source->original;
            } else {
                auto result=PsdRecompose::render(doc,visible,source->cache,[&]{return generation->load()!=revision;});
                if(!result) {out.issue=result.issue;out.layer=result.layer;return out;}
                image=std::move(result.image);
            }
            if (image.bgra.empty()) {out.issue=PsdRecompose::Issue::Decode;return out;}
            cv::Mat pixels(image.height,image.width,CV_8UC4,image.bgra.data());
            if(channel&&!bakedChannel) {
                // 明确是显示RGB/Alpha，不把CMYK/Lab转出的RGB冒充文件原始通道。
                ColorManager::applyToMat(pixels,image.icc,PsdCompositeDecoder::srgbProfile());
                const int c=channel==1?2:channel==2?1:channel==3?0:3;
                for(int y=0;y<pixels.rows;++y) for(auto& p:std::span(pixels.ptr<cv::Vec4b>(y),pixels.cols))
                    p={p[c],p[c],p[c],255};
                out.display=pixels.clone();out.exportPixels=out.display;
            } else {
                out.exportPixels=pixels.clone();
                ColorManager::applyToMat(out.exportPixels,image.icc,PsdCompositeDecoder::srgbProfile());
                ImageAsset asset;asset.primaryFrame=pixels.clone();asset.iccProfile=image.icc;
                ColorManager manager;manager.setWindow(owner);manager.applyToImageAsset(asset);
                out.display=asset.primaryFrame;
            }
        } catch(...) {out.issue=PsdRecompose::Issue::Decode;}
        return out;
    }
public:
    ~PsdDock(){++*generation;}
    // 只读真实文档能力清点；报告和临时预览写入指定目录，绝不保存源文档。
    static bool audit(const std::wstring& file,const std::filesystem::path& directory) {
        const auto started=std::chrono::steady_clock::now();auto raw=load(file,{},false);if(!raw)return false;
        const auto metadataMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        auto input=attachBackend(raw,{});if(!input)return false;
        const auto loadMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        std::filesystem::create_directories(directory);
        std::ofstream report(directory/"layers.txt",std::ios::binary);
        report<<"metadataMs="<<metadataMs<<" loadMs="<<loadMs<<"\n";
        const auto& doc=input->document;
        report<<"width="<<doc.width<<" height="<<doc.height<<" depth="<<doc.depth<<" mode="<<doc.mode<<" layers="<<doc.layers.size()<<"\n";
        std::vector<uint8_t> visibility;
        for(size_t i=0;i<doc.layers.size();++i) {
            const auto& layer=doc.layers[i];visibility.push_back(layer.visible);
            report<<i<<" name="<<jarkUtils::wstringToUtf8(layer.name)<<" bounds="<<layer.left<<","<<layer.top<<","<<layer.right<<","<<layer.bottom
                <<" visible="<<layer.visible<<" opacity="<<int(layer.opacity)<<" blend="<<layer.blend<<" section="<<layer.sectionType
                <<" clipped="<<layer.clipped<<" effects="<<layer.effects<<" vector="<<layer.vectorMask<<" ranges="<<layer.customBlendRanges
                <<" adjustment="<<layer.adjustment<<" fill="<<int(layer.fillOpacity)<<" mask="<<layer.maskData.size()<<" channels=";
            for(const auto& c:layer.channels)report<<c.id<<",";
            report<<"\n";
        }
        const auto result=PsdRecompose::render(doc,visibility,input->cache);
        report<<"originalVisibilityRecomposeIssue="<<int(result.issue)<<" layer="<<result.layer<<"\n";
        if(input->backend){
            report<<"photocraftBackend="<<input->backend->valid()<<" error="<<input->backend->error<<"\n";
            if(input->backend->valid()) {
                report<<"nativeThumbnailApi="<<input->backend->hasThumbnails()<<"\n";
                std::vector<int> sample;int count=0;const auto miniStart=std::chrono::steady_clock::now();
                for(int index=0;index<int(input->backend->rows.size());++index){const auto mini=input->backend->thumbnail(index,false,32);
                    if(!mini.bgra.empty()){++count;if(sample.size()<3)sample.push_back(index);}}
                report<<"nativeThumbnails="<<count<<" allRowsMs="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-miniStart).count()<<"\n";
                for(int index:sample){std::vector<double> direct,legacy;
                    for(int round=0;round<3;++round){
                        auto begin=std::chrono::steady_clock::now();const auto mini=input->backend->thumbnail(index,false,32);
                        direct.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
                        begin=std::chrono::steady_clock::now();const auto old=input->backend->render(input->backend->originalStates(),index,0);
                        legacy.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
                        if(mini.bgra.empty()||old.bgra.empty())report<<"thumbnailBenchmarkInvalidRow="<<index<<"\n";
                    }
                    std::sort(direct.begin(),direct.end());std::sort(legacy.begin(),legacy.end());
                    report<<"thumbnailRow="<<index<<" nativeMedianMs="<<direct[1]<<" oldFullCanvasMedianMs="<<legacy[1]<<"\n";
                }
                const auto before=std::chrono::steady_clock::now();
                const auto rebuilt=input->backend->render(input->backend->originalStates());
                report<<"photocraftRender="<<!rebuilt.bgra.empty()<<" ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-before).count()<<"\n";
                if(!rebuilt.bgra.empty())cv::imwrite((directory/"photocraft-rebuilt.png").string(),cv::Mat(rebuilt.height,rebuilt.width,CV_8UC4,const_cast<uint8_t*>(rebuilt.bgra.data())));
                report<<"photocraftWarnings="<<input->backend->warning<<"\n";
            }
        }
        const auto composite=PsdCompositeDecoder::decode(*input->bytes,256ULL<<20);
        report<<"originalComposite="<<!composite.bgra.empty()<<"\n";
        if(!composite.bgra.empty()&&input->backend&&input->backend->valid()){
            const auto rebuilt=input->backend->render(input->backend->originalStates());
            cv::Mat reference(composite.height,composite.width,CV_8UC4,const_cast<uint8_t*>(composite.bgra.data()));reference=reference.clone();
            ColorManager::applyToMat(reference,composite.icc,PsdCompositeDecoder::srgbProfile());
            cv::imwrite((directory/"saved-composite-srgb.png").string(),reference);
            if(rebuilt.bgra.size()==composite.bgra.size()){
                double sum=0,maximum=0;size_t components=0;
                for(size_t p=0;p<rebuilt.bgra.size();p+=4){const auto* a=rebuilt.bgra.data()+p;const auto* b=reference.data+p;
                    for(int c=0;c<4;++c){const double delta=std::abs(c==3?double(a[c])-b[c]:double(a[c])*a[3]/255.-double(b[c])*b[3]/255.);sum+=delta;maximum=(std::max)(maximum,delta);++components;}}
                report<<"savedCompositeDifferencePremultiplied mean="<<(components?sum/components:0)<<" max="<<maximum<<"\n";
            }
        }
        auto token=std::make_shared<std::atomic<uint64_t>>(1);
        for(size_t i=0;i<doc.layers.size();++i) {
            const auto& layer=doc.layers[i];
            if(layer.group||layer.adjustment||layer.right<=layer.left||layer.bottom<=layer.top)continue;
            auto hidden=visibility;hidden[i]^=1;
            const auto toggled=calculate(input,file,hidden,static_cast<int>(i),0,false,1,token,nullptr);
            const auto single=calculate(input,file,visibility,static_cast<int>(i),0,true,1,token,nullptr);
            report<<"toggleLayer="<<i<<" issue="<<int(toggled.issue)<<"\nrawLayer="<<i<<" issue="<<int(single.issue)
                <<" canvas="<<single.display.cols<<"x"<<single.display.rows<<"\n";
            break;
        }
        // 真实文档的侧栏渲染证据，与主窗口使用同一绘制路径；不冒充桌面截图。
        const bool oldTheme=GlobalVar::isCurrentUIDarkMode;
        PsdDock dock;dock.source=input;dock.open_=true;dock.path=file;dock.desired=visibility;dock.accepted=visibility;dock.selected=0;
        if(input->backend&&input->backend->valid())dock.properties=dock.acceptedProperties=input->backend->originalStates();
        for(int i=0;i<dock.layerCount();++i)if(doc.layers[i].sectionType==2)dock.collapsed.insert(i);
        dock.layout(1100,850,96);
        // 走生产缩略图任务，而不是用空框截图充当已渲染证据。
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        do{dock.poll(file,nullptr);if(!dock.job.valid())break;Sleep(10);}while(std::chrono::steady_clock::now()<deadline);
        TextDrawer drawer;
        for(bool dark:{false,true}){GlobalVar::isCurrentUIDarkMode=dark;cv::Mat canvas(850,1100,CV_8UC4,cv::Scalar(32,32,32,255));dock.layout(1100,850,96);dock.draw(canvas,drawer);
            cv::imwrite((directory/(dark?"tree-dark.png":"tree-light.png")).string(),canvas(cv::Rect(dock.contentWidth(),0,dock.sidebarWidth(),canvas.rows)));}
        GlobalVar::isCurrentUIDarkMode=oldTheme;
        return bool(report)&&!composite.bgra.empty();
    }
    bool opened() const {return open_;}
    void setBlend(std::string_view key){if(editableSelection()&&PsdBlendModes::find(key)){
        if(key=="pass"&&!source->document.layers[selected].group)return;
        std::copy_n(key.data(),4,properties[selected].blend.data());solo=false;channel=0;enqueue();}}
    void setPercent(int percent,bool fill){if(editableSelection()&&percent>=0&&percent<=100){
        (fill?properties[selected].fill:properties[selected].opacity)=percent/100.f;solo=false;channel=0;enqueue();}}
    void toggleMask(){if(editableSelection()&&source->document.layers[selected].masked){properties[selected].mask^=1;solo=false;enqueue();}}
    bool busy() const {return queued || (job.valid()&&!thumbnailJob);}
    int contentWidth() const {return (std::max)(1,width_-(open_?sidebar_:0));}
    int sidebarWidth() const {return open_?sidebar_:0;}
    int layerCount() const {return source?static_cast<int>(source->document.layers.size()):0;}
    int visibleCount() const {return static_cast<int>(std::count(desired.begin(),desired.end(),uint8_t(1)));}
    int issue() const {return static_cast<int>(issue_);}
    bool usesPhotocraft()const{return nativeBackend();}
    const cv::Mat& image() const {return display_;}
    cv::Mat exportImage() const {return export_.clone();}
    void layout(int width,int height,int newDpi) {
        width_=width;height_=height;dpi=newDpi;
        sidebar_=open_?(std::min)((std::max)(0,width-s(80)),s(preferredWidth)):0;
    }
    bool splitterHit(int x) const {return open_&&std::abs(x-contentWidth())<=s(4);}
    void resize(int x) {preferredWidth=std::clamp(MulDiv(width_-x,96,dpi),200,520);layout(width_,height_,dpi);}
    void open(std::wstring value) {
        path=std::move(value);open_=true;source.reset();desired.clear();accepted.clear();
        properties.clear();acceptedProperties.clear();
        selectedMask=false;
        display_.release();export_.release();selected=-1;channel=acceptedChannel=0;solo=acceptedSolo=false;scroll=0;collapsed.clear();
        thumbnailAttempted.clear();thumbnails.clear();enqueue();
    }
    void close() {scrollDragging=false;hoverX=hoverY=-1;open_=false;queued=false;++*generation;display_.release();export_.release();source.reset();desired.clear();accepted.clear();properties.clear();acceptedProperties.clear();thumbnails.clear();thumbnailAttempted.clear();}
    void reset() {
        desired.clear();if(source)for(const auto& layer:source->document.layers)desired.push_back(layer.visible);
        properties.clear();
        solo=false;channel=0;enqueue();
    }
    void toggle(int index) {if(readOnlyReason()!=PsdRecompose::Issue::None)return;if(index>=0&&index<static_cast<int>(desired.size())){desired[index]^=1;solo=false;enqueue();}}
    void setChannel(int value) {const auto planes=channels();channel=planes[std::clamp(value,0,int(planes.size())-1)].first;enqueue();}
    // 返回true要求主窗口重绘；旧路径/旧请求的结果绝不覆盖当前图片。
    bool poll(const std::wstring& current,HWND owner) {
        owner_=owner;
        bool changed=false;
        if(open_ && current!=path) {
            auto extension=std::filesystem::path(current).extension().wstring();
            std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
            if(extension!=L".psd"&&extension!=L".psb"&&extension!=L".psdt")close();else open(current);
            changed=true;
        }
        if(job.valid() && job.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            auto result=job.get();changed=true;
            thumbnailJob=false;
            if(result.revision==generation->load() && open_) {
                if(result.thumbnailsOnly) {
                    if(thumbnails.size()>128){thumbnails.clear();thumbnailAttempted.clear();}
                    for(auto& [index,mat]:result.thumbnails)thumbnails[index]=std::move(mat);
                } else {
                if(result.source){
                    bool sameTree=source&&source->document.layers.size()==result.source->document.layers.size();
                    if(sameTree)for(size_t i=0;i<source->document.layers.size();++i){const auto& a=source->document.layers[i];const auto& b=result.source->document.layers[i];if(a.name!=b.name||a.sectionType!=b.sectionType){sameTree=false;break;}}
                    source=result.source;
                    if(!sameTree){selected=layerCount()?0:-1;selectedMask=false;collapsed.clear();thumbnails.clear();thumbnailAttempted.clear();for(int i=0;i<layerCount();++i)if(source->document.layers[i].sectionType==2)collapsed.insert(i);}
                }
                if(result.issue==PsdRecompose::Issue::None) {
                    desired=accepted=std::move(result.visible);display_=result.display;export_=result.exportPixels;
                    properties=acceptedProperties=std::move(result.properties);
                    acceptedChannel=channel;acceptedSolo=solo;
                    issue_=PsdRecompose::Issue::None;
                    if(source&&!source->backendAttempted)queued=true; // 下一阶段后台加载引擎；此帧已有图层树。
                } else {
                    issue_=result.issue;issueLayer=result.layer;
                    if(!accepted.empty())desired=accepted;
                    properties=acceptedProperties;
                    channel=acceptedChannel;solo=acceptedSolo;
                }
                }
            } else if(result.thumbnailsOnly) {
                thumbnailAttempted.clear();
            }
        }
        if(open_ && queued && !job.valid()) {
            queued=false; const auto revision=generation->load();
            auto input=source;auto value=path;auto flags=desired;auto token=generation;auto controls=properties;
            const auto row=selected,plane=channel;const auto only=solo;
            try {job=std::async(std::launch::async,[input,value,flags,row,plane,only,revision,token,owner,controls]{
                return calculate(input,value,flags,row,plane,only,revision,token,owner,controls);
            });} catch(...) {issue_=PsdRecompose::Issue::Decode;}
            changed=true;
        }
        // 只为可见行分批生成小缩略图，低优先级；不能阻塞或覆盖用户的新显隐请求。
        if(open_&&source&&!queued&&!job.valid()) {
            std::vector<int> indices;const auto items=rows();
            const int count=(std::max)(1,(height_-s(PsdTree::LIST_TOP+PsdTree::FOOTER))/s(PsdTree::ROW_HEIGHT));
            for(int r=scroll;r<(std::min)(static_cast<int>(items.size()),scroll+count)&&indices.size()<2;++r) {
                const int i=items[r].index;
                if((!source->document.layers[i].group||source->document.layers[i].masked)&&!thumbnailAttempted.contains(i)) {indices.push_back(i);thumbnailAttempted.insert(i);}
            }
            if(!indices.empty()) {
                auto input=source;auto token=generation;const auto revision=token->load();thumbnailJob=true;
                try {job=std::async(std::launch::async,[input,indices,token,revision,owner] {
                    Delivery out;out.revision=revision;out.thumbnailsOnly=true;
                    try {for(int index:indices) {
                        if(token->load()!=revision)break;
                        if(input->backend&&input->backend->valid()){
                            const auto image=input->backend->thumbnail(index,false,32,[&]{return token->load()!=revision;});
                            if(!image.bgra.empty()){
                            cv::Mat raw(image.height,image.width,CV_8UC4,const_cast<uint8_t*>(image.bgra.data())),small;
                            const double ratio=(std::min)(32.0/image.width,32.0/image.height);
                            cv::resize(raw,small,{(std::max)(1,int(image.width*ratio)),(std::max)(1,int(image.height*ratio))},0,0,cv::INTER_AREA);
                            ImageAsset asset;asset.primaryFrame=small;asset.iccProfile=PsdCompositeDecoder::srgbProfile();
                            ColorManager manager;manager.setWindow(owner);manager.applyToImageAsset(asset);
                            out.thumbnails.emplace_back(index,std::move(asset.primaryFrame));
                            }
                            if(input->document.layers[index].masked){
                                const auto mask=input->backend->thumbnail(index,true,32,[&]{return token->load()!=revision;});
                                if(!mask.bgra.empty()){cv::Mat pixels(mask.height,mask.width,CV_8UC4,const_cast<uint8_t*>(mask.bgra.data())),preview;
                                    cv::resize(pixels,preview,{32,32},0,0,cv::INTER_AREA);out.thumbnails.emplace_back(-index-1,std::move(preview));}
                            }
                            continue;
                        }
                        const auto& bounds=input->document.layers[index];
                        const uint64_t pixels=uint64_t(int64_t(bounds.right)-bounds.left)*(int64_t(bounds.bottom)-bounds.top);
                        if(pixels>4ULL*1024*1024)continue;
                        auto image=PsdLayers::preview(input->document,index,64ULL<<20);
                        if(image.bgra.empty())continue;
                        cv::Mat raw(image.height,image.width,CV_8UC4,image.bgra.data()),small;
                        const double ratio=(std::min)(32.0/image.width,32.0/image.height);
                        cv::resize(raw,small,{(std::max)(1,int(image.width*ratio)),(std::max)(1,int(image.height*ratio))},0,0,cv::INTER_AREA);
                        ImageAsset asset;asset.primaryFrame=small;asset.iccProfile=image.icc;
                        ColorManager manager;manager.setWindow(owner);manager.applyToImageAsset(asset);
                        out.thumbnails.emplace_back(index,asset.primaryFrame);
                    }}catch(...){}
                    return out;
                });}catch(...){thumbnailJob=false;}
            }
        }
        return changed;
    }
    bool contains(int x,int y) const {return open_&&x>=contentWidth()&&x<width_&&y>=0&&y<height_;}
    struct Scrollbar {cv::Rect track,thumb;int maximum=0;};
    Scrollbar scrollbar()const{
        const int count=std::max(0,(height_-s(PsdTree::LIST_TOP+PsdTree::FOOTER))/s(PsdTree::ROW_HEIGHT));
        const int total=tab==0?int(rows().size()):int(channels().size());
        if(count<=0||total<=count)return {};
        const int length=count*s(PsdTree::ROW_HEIGHT),thumb=std::min(length,std::max(s(18),length*count/total));
        const int maximum=total-count;
        return {{sidebar_-s(12),s(PsdTree::LIST_TOP),s(12),length},
            {sidebar_-s(12),s(PsdTree::LIST_TOP)+(length-thumb)*std::clamp(scroll,0,maximum)/maximum,s(12),thumb},maximum};
    }
    bool move(int x,int y){
        bool changed=false;
        if(scrollDragging){const auto bar=scrollbar();const int travel=bar.track.height-bar.thumb.height;
            const int next=travel>0?std::clamp(int(std::lround(double(y-scrollGrab-bar.track.y)*bar.maximum/travel)),0,bar.maximum):0;
            changed=scroll!=next;scroll=next;}
        const int nx=contains(x,y)?x-contentWidth():-1,ny=contains(x,y)?y:-1;
        changed|=nx!=hoverX||ny!=hoverY;hoverX=nx;hoverY=ny;return changed;
    }
    bool draggingScroll()const{return scrollDragging;}
    void release(){scrollDragging=false;}
    bool click(int x,int y) {
        if(!contains(x,y))return false;
        const int local=x-contentWidth();
        const auto bar=scrollbar();
        if(bar.maximum&&bar.track.contains(cv::Point{local,y})){
            if(bar.thumb.contains(cv::Point{local,y})){scrollDragging=true;scrollGrab=y-bar.thumb.y;}
            else scroll=std::clamp(scroll+(y<bar.thumb.y?-1:1)*std::max(1,bar.track.height/s(PsdTree::ROW_HEIGHT)),0,bar.maximum);
            return true;
        }
        if(y<s(28)) {if(local>sidebar_-s(80)&&local<sidebar_-s(40))close();return true;}
        if(y<s(64)) {tab=local<sidebar_/2?0:1;scroll=0;return true;}
        if(y<s(90)) {if(local<sidebar_*0.55)blendMenu(x,y);else opacityMenu(x,y,false);return true;}
        if(y<s(116)) {if(local<sidebar_*0.55)reset();else opacityMenu(x,y,true);return true;}
        if(y>=height_-s(PsdTree::FOOTER)) {
            if(y>=height_-s(36)&&selected>=0&&source&&source->document.layers[selected].sectionType!=3){solo=!solo;enqueue();}
            return true;
        }
        const int row=(y-s(PsdTree::LIST_TOP))/s(PsdTree::ROW_HEIGHT)+scroll;
        if(tab==1) {if(row<int(channels().size()))setChannel(row);return true;}
        const auto items=rows();
        if(row>=0&&row<static_cast<int>(items.size())) {
            const auto index=items[row].index;selected=index;
            const auto box=PsdTree::geometry(MulDiv(sidebar_,96,dpi),PsdTree::LIST_TOP+(row-scroll)*PsdTree::ROW_HEIGHT,items[row].depth,source->document.layers[index].group,source->document.layers[index].masked);
            const int logicalX=MulDiv(local,96,dpi),logicalY=MulDiv(y,96,dpi);
            if(box.eye.contains(logicalX,logicalY))toggle(index);
            else if(box.mask.contains(logicalX,logicalY)&&editableSelection()){
                selectedMask=true;
                if(GetKeyState(VK_SHIFT)&0x8000){toggleMask();}
                else if(GetKeyState(VK_MENU)&0x8000){channel=channel==20000+index?0:20000+index;solo=false;enqueue();}
            }
            else if(source->document.layers[index].group&&box.disclosure.contains(logicalX,logicalY)) {
                const bool open=collapsed.contains(index);
                if(GetKeyState(VK_MENU)&0x8000){for(int i=0;i<layerCount();++i)if(source->document.layers[i].group){if(open)collapsed.erase(i);else collapsed.insert(i);}}
                else if(open)collapsed.erase(index);else collapsed.insert(index);
            }
            else {selectedMask=false;if(solo)enqueue();}
        }
        return true;
    }
    bool wheel(int x,int y,int delta) {
        if(!contains(x,y))return false;
        const int rows=(std::max)(1,(height_-s(PsdTree::LIST_TOP+PsdTree::FOOTER))/s(PsdTree::ROW_HEIGHT));
        scroll=std::clamp(scroll+(delta<0?3:-3),0,(std::max)(0,(tab==0?static_cast<int>(this->rows().size()):int(channels().size()))-rows));return true;
    }
    void draw(cv::Mat& canvas,TextDrawer& text) {
        layout(canvas.cols,canvas.rows,dpi);
        if(!open_||sidebar_<=0)return;
        auto panel=canvas(cv::Rect(contentWidth(),0,sidebar_,canvas.rows));
        const bool dark=GlobalVar::isCurrentUIDarkMode;
        panel.setTo(jarkUtils::to_cv_scalar(PsdDockStyle::background(dark)));
        const uint32_t fg=PsdDockStyle::foreground(dark),muted=PsdDockStyle::muted(dark),accent=PsdDockStyle::accent(dark);
        const auto line=[&](cv::Point a,cv::Point b){cv::line(panel,a,b,jarkUtils::to_cv_scalar(PsdDockStyle::separator(dark)),std::max(1,s(1)));};
        const auto field=[&](cv::Rect r){r&=cv::Rect(0,0,panel.cols,panel.rows);if(r.area()>0){cv::rectangle(panel,r,jarkUtils::to_cv_scalar(PsdDockStyle::field(dark)),cv::FILLED);cv::rectangle(panel,r,jarkUtils::to_cv_scalar(PsdDockStyle::fieldBorder(dark)),std::max(1,s(1)));}};
        const auto hover=[&](cv::Rect r,bool enabled=true){r&=cv::Rect(0,0,panel.cols,panel.rows);if(enabled&&r.area()>0&&r.contains(cv::Point{hoverX,hoverY}))cv::rectangle(panel,r,jarkUtils::to_cv_scalar(PsdDockStyle::hovered(dark)),cv::FILLED);};
        const int oldSize=text.getSize();text.setSize(s(13));
        const auto label=[&](cv::Rect rect,const char* value,uint32_t color){
            rect&=cv::Rect(0,0,panel.cols,panel.rows);if(rect.area()>0)text.putAlignLeft(panel,rect,value,color);
        };
        label({s(12),0,sidebar_-s(60),s(28)},tr("图层与通道","Layers & channels","圖層與色版"),fg);
        hover({sidebar_-s(80),0,s(40),s(28)});
        text.putAlignCenter(panel,{sidebar_-s(76),0,s(32),s(28)},"×",fg);
        cv::rectangle(panel,{0,s(28),sidebar_,s(36)},jarkUtils::to_cv_scalar(PsdDockStyle::tabStrip(dark)),cv::FILLED);
        for(int i=0;i<2;++i){const cv::Rect r(i*sidebar_/2,s(28),sidebar_/2,s(36));
            if(i==tab)cv::rectangle(panel,r,jarkUtils::to_cv_scalar(PsdDockStyle::background(dark)),cv::FILLED);
            hover(r);
            if(i==tab)cv::rectangle(panel,{r.x,s(61),r.width,s(3)},jarkUtils::to_cv_scalar(accent),cv::FILLED);
            text.putAlignCenter(panel,r,i==0?tr("图层","Layers","圖層"):tr("通道","Channels","色版"),i==tab?fg:muted);}
        const int split=int(sidebar_*0.55);text.setSize(s(12));
        field({s(6),s(67),split-s(12),s(22)});field({split,s(67),sidebar_-split-s(6),s(22)});
        field({split,s(93),sidebar_-split-s(6),s(20)});
        hover({s(6),s(67),split-s(12),s(22)},editableSelection());hover({split,s(67),sidebar_-split-s(6),s(22)},editableSelection());
        hover({s(6),s(93),split-s(12),s(20)});hover({split,s(93),sidebar_-split-s(6),s(20)},editableSelection());
        const auto* blend=editableSelection()?PsdBlendModes::find(std::string_view(properties[selected].blend.data(),4)):nullptr;
        label({s(10),s(64),split-s(30),s(26)},blend?tr(blend->chinese,blend->english,blend->traditional):"—",fg);
        label({split-s(22),s(64),s(16),s(26)},"▾",muted);
        const auto percent=[&](bool fill){return editableSelection()?std::to_string(int(std::lround((fill?properties[selected].fill:properties[selected].opacity)*100)))+"%":"—";};
        const auto opacity=std::string(tr("不透明度 ","Opacity ","不透明度 "))+percent(false);
        const auto fill=std::string(tr("填充 ","Fill ","填色 "))+percent(true);
        label({split+s(4),s(64),sidebar_-split-s(10),s(26)},opacity.c_str(),fg);
        label({s(12),s(90),split-s(16),s(26)},tr("↺ 恢复原始状态","↺ Restore original","↺ 還原原始狀態"),fg);
        label({split+s(4),s(90),sidebar_-split-s(10),s(26)},fill.c_str(),fg);text.setSize(s(13));
        line({0,s(28)},{sidebar_-1,s(28)});line({0,s(64)},{sidebar_-1,s(64)});line({0,s(PsdTree::LIST_TOP)},{sidebar_-1,s(PsdTree::LIST_TOP)});
        const int rows=(std::max)(0,(height_-s(PsdTree::LIST_TOP+PsdTree::FOOTER))/s(PsdTree::ROW_HEIGHT));
        const auto items=this->rows();
        const auto planes=channels();
        scroll=std::clamp(scroll,0,(std::max)(0,(tab==0?static_cast<int>(items.size()):int(planes.size()))-rows));
        const bool readOnly=readOnlyReason()!=PsdRecompose::Issue::None;
        for(int row=0;row<rows;++row) {
            const int index=row+scroll;const int y=s(PsdTree::LIST_TOP)+row*s(PsdTree::ROW_HEIGHT),rh=s(PsdTree::ROW_HEIGHT);
            if(tab==1) {if(index>=int(planes.size()))break;
                if(planes[index].first==channel)cv::rectangle(panel,{s(1),y,sidebar_-s(2),rh},jarkUtils::to_cv_scalar(PsdDockStyle::selected(dark)),cv::FILLED);
                hover({s(1),y,sidebar_-s(14),rh});
                label({s(38),y,sidebar_-s(50),rh},planes[index].second.c_str(),fg);line({0,y+rh-1},{sidebar_-1,y+rh-1});line({s(30),y},{s(30),y+rh-1});continue;}
            if(index>=static_cast<int>(items.size()))break;
            const int layerIndex=items[index].index;
            const auto& layer=source->document.layers[layerIndex];
            if(layerIndex==selected)cv::rectangle(panel,{s(1),y,sidebar_-s(2),rh},jarkUtils::to_cv_scalar(PsdDockStyle::selected(dark)),cv::FILLED);
            hover({s(1),y,sidebar_-s(14),rh});
            if(layerIndex==selected)cv::rectangle(panel,{s(31),y,s(2),rh},jarkUtils::to_cv_scalar(accent),cv::FILLED);
            line({0,y+rh-1},{sidebar_-1,y+rh-1});line({s(30),y},{s(30),y+rh-1});
            const auto boxes=PsdTree::geometry(MulDiv(sidebar_,96,dpi),PsdTree::LIST_TOP+row*PsdTree::ROW_HEIGHT,items[index].depth,layer.group,layer.masked);
            const cv::Point eye{s(16),y+s(17)};const auto eyeColor=jarkUtils::to_cv_scalar(items[index].effective&&!readOnly?fg:muted);
            cv::ellipse(panel,eye,{s(8),s(5)},0,0,360,eyeColor,(std::max)(1,s(1)),cv::LINE_AA);
            if(layerIndex<static_cast<int>(desired.size())&&desired[layerIndex])cv::circle(panel,eye,s(2),eyeColor,cv::FILLED);
            else cv::line(panel,{eye.x-s(8),eye.y+s(7)},{eye.x+s(8),eye.y-s(7)},eyeColor,(std::max)(1,s(1)),cv::LINE_AA);
            if(layer.group){label({s(boxes.disclosure.x),y,s(18),rh},collapsed.contains(layerIndex)?"▸":"▾",fg);
                cv::rectangle(panel,{s(boxes.thumbnail.x),y+s(11),s(22),s(16)},jarkUtils::to_cv_scalar(muted),std::max(1,s(1)));
                cv::line(panel,{s(boxes.thumbnail.x),y+s(10)},{s(boxes.thumbnail.x+10),y+s(10)},jarkUtils::to_cv_scalar(muted),s(2));}
            else if(const auto thumb=thumbnails.find(layerIndex);thumb!=thumbnails.end()&&!thumb->second.empty()) {
                for(int cy=0;cy<24;cy+=6)for(int cx=0;cx<24;cx+=6)cv::rectangle(panel,{s(boxes.thumbnail.x+cx),y+s(5+cy),s(6),s(6)},((cx+cy)/6)%2?cv::Scalar(120,120,120,255):cv::Scalar(190,190,190,255),cv::FILLED);
                const double scale=double(s(PsdTree::THUMBNAIL))/(std::max)(thumb->second.cols,thumb->second.rows);
                cv::Mat resized;cv::resize(thumb->second,resized,{(std::max)(1,int(thumb->second.cols*scale)),(std::max)(1,int(thumb->second.rows*scale))},0,0,cv::INTER_AREA);
                jarkUtils::overlayImg(panel,resized,s(boxes.thumbnail.x)+(s(24)-resized.cols)/2,y+s(5)+(s(24)-resized.rows)/2);
            } else cv::rectangle(panel,{s(boxes.thumbnail.x),y+s(5),s(24),s(24)},jarkUtils::to_cv_scalar(muted),1);
            if(layer.masked){
                cv::Rect mask(s(boxes.mask.x),s(boxes.mask.y),s(PsdTree::THUMBNAIL),s(PsdTree::THUMBNAIL));mask&=cv::Rect(0,0,panel.cols,panel.rows);
                if(mask.area()>0){cv::rectangle(panel,mask,cv::Scalar(238,238,238,255),cv::FILLED);
                    if(const auto found=thumbnails.find(-layerIndex-1);found!=thumbnails.end()&&!found->second.empty()){cv::Mat scaled;cv::resize(found->second,scaled,mask.size(),0,0,cv::INTER_AREA);scaled.copyTo(panel(mask));}
                    cv::rectangle(panel,mask,jarkUtils::to_cv_scalar(muted),1);
                    if(layerIndex==selected&&selectedMask)cv::rectangle(panel,mask,jarkUtils::to_cv_scalar(fg),s(2));
                    if(layerIndex<int(properties.size())&&!properties[layerIndex].mask){cv::line(panel,mask.tl(),mask.br(),cv::Scalar(70,70,210,255),s(2));cv::line(panel,{mask.x,mask.y+mask.height},{mask.x+mask.width,mask.y},cv::Scalar(70,70,210,255),s(2));}
                }
            }
            const auto name=jarkUtils::wstringToUtf8(layer.name);
            label({s(boxes.name.x),y,s(boxes.name.width),rh},name.c_str(),fg);
            label({s(boxes.type.x),y,s(boxes.type.width),rh},layer.adjustment?"A":layer.effects?"fx":"",muted);
        }
        const int total=tab==0?int(items.size()):int(planes.size());
        if(rows>0&&total>rows){
            const auto bar=scrollbar();cv::rectangle(panel,bar.track,jarkUtils::to_cv_scalar(PsdDockStyle::tabStrip(dark)),cv::FILLED);
            auto thumb=bar.thumb;thumb.x+=s(2);thumb.width-=s(4);
            cv::rectangle(panel,thumb,jarkUtils::to_cv_scalar(scrollDragging||bar.track.contains(cv::Point{hoverX,hoverY})?accent:muted),cv::FILLED);
        }
        std::string status=busy()?tr("正在读取/合成…","Loading/compositing…","正在讀取/合成…"):
            tr("浏览不改原图；通道为显示RGB/Alpha。","Read-only; display RGB/Alpha channels.","瀏覽不改原圖；色版為顯示RGB/Alpha。");
        if(nativeBackend()&&!busy())status=tr("PhotoCraft 合成后端 · 临时浏览，不改原文件。通道按文档颜色模式显示。","PhotoCraft compositor · temporary view, source unchanged. Channels follow document color mode.","PhotoCraft 合成後端 · 暫時瀏覽，不改原檔。色版按文件色彩模式顯示。");
        if(issue_!=PsdRecompose::Issue::None) {
            status=tr("本次操作未应用，保留上一画面。","Change not applied; previous view kept.","本次操作未套用，保留上個畫面。");
            switch(issue_) {
            case PsdRecompose::Issue::Format:status+=tr(" 实时合成暂支持8位RGB。"," Live composition: 8-bit RGB only."," 即時合成暫支援8位RGB。");break;
            case PsdRecompose::Issue::Clipping:status+=tr(" 暂不支持剪贴链。"," Clipping chains not supported yet."," 暫不支援剪裁鏈。");break;
            case PsdRecompose::Issue::Effects:case PsdRecompose::Issue::Adjustment:status+=tr(" 含未支持的效果、高级混合、画板或蒙版。"," Unsupported effects, advanced blending, artboard or mask."," 含未支援的效果、進階混合、工作區域或遮色片。");break;
            case PsdRecompose::Issue::Blend:status+=tr(" 未支持的混合方式。"," Unsupported blend mode."," 未支援的混合方式。");break;
            case PsdRecompose::Issue::Mask:status+=tr(" 未支持的蒙版参数。"," Unsupported mask parameters."," 未支援的遮色片參數。");break;
            case PsdRecompose::Issue::Bounds:status+=tr(" 超出安全预览上限。"," Safe preview limit exceeded."," 超出安全預覽上限。");break;
            default:status+=tr(" 图层无法读取。"," Cannot read layer data."," 圖層無法讀取。");break;
            }
            if(source&&issueLayer>=0&&issueLayer<layerCount())status+=" "+jarkUtils::wstringToUtf8(source->document.layers[issueLayer].name);
        }
        if(!busy()&&source&&layerCount()==0)status=tr("文件没有可读取的图层。","No readable layers in this file.","檔案沒有可讀取的圖層。");
        if(source&&!busy()&&readOnlyReason()!=PsdRecompose::Issue::None){
            const auto& doc=source->document;const char* mode=doc.mode==4?"CMYK":doc.mode==9?"Lab":doc.mode==1?"Gray":"RGB";
            status=std::string(tr("只读图层树 · ","Read-only layer tree · ","唯讀圖層樹 · "))+mode+"/"+std::to_string(doc.depth)+tr("；当前文档不能忠实重合成，显隐已禁用。通道仅为显示RGB/Alpha，非原生通道。","; faithful recomposition unavailable; eyes disabled. Channels are display RGB/Alpha, not native.","；目前文件無法忠實重合成，顯隱已停用。色版僅為顯示RGB/Alpha，非原生色版。");
        }
        if(source&&source->backend&&source->backend->valid()&&!source->backend->warning.empty()&&issue_==PsdRecompose::Issue::None)
            status=tr("PhotoCraft 后端已启用；此文件有导入提示，部分效果可能有差异。浏览不改原文件。","PhotoCraft backend active; import warnings: some effects may differ. Source unchanged.","PhotoCraft 後端已啟用；此檔案有匯入提示，部分效果可能有差異。瀏覽不改原檔。");
        if(nativeBackend()&&source->baselineChecked&&source->baseline.differs()&&!busy()&&issue_==PsdRecompose::Issue::None){
            status=source->baseline.valid?std::string(tr("临时合成与原图有差异：平均 ","Temporary composite differs: mean ","暫時合成與原圖有差異：平均 "))+
                std::format("{:.2f}/255",source->baseline.mean)+tr("。可继续浏览；恢复原始状态返回原图。",". Browsing is allowed; Restore original returns to the saved image.","。可繼續瀏覽；還原原始狀態返回原圖。"):
                tr("文件合成图无法核对，当前为PhotoCraft图层恢复预览；不修改原文件。","Saved composite unavailable for comparison; PhotoCraft layer recovery preview. Source unchanged.","檔案合成圖無法核對，目前為PhotoCraft圖層還原預覽；不修改原檔。");
        }
        line({0,std::max(s(PsdTree::LIST_TOP),height_-s(PsdTree::FOOTER))},{sidebar_-1,std::max(s(PsdTree::LIST_TOP),height_-s(PsdTree::FOOTER))});
        text.setSize(s(11));
        text.putWrappedLeft(panel,{s(8),(std::max)(s(PsdTree::LIST_TOP),height_-s(72)),(std::max)(1,sidebar_-s(16)),s(42)},status.c_str(),muted);
        text.setSize(s(12));
        hover({s(6),std::max(0,height_-s(36)),sidebar_-s(12),s(34)},selected>=0);
        label({s(12),(std::max)(0,height_-s(36)),sidebar_-s(24),s(34)},solo?tr("返回多层合成","Return to composite","返回多層合成"):tr("仅预览所选图层／分组","Preview selected layer/group","僅預覽所選圖層／群組"),fg);
        text.setSize(oldSize);
    }
    // 离屏组件验证，不截取桌面、不创建弹窗、不代替真实窗口像素验收。
    static bool selfTest(const std::wstring& file,const std::filesystem::path& directory) {
        std::filesystem::create_directories(directory);
        PsdDock dock;dock.open(file);dock.layout(900,620,96);
        bool metadataSeen=false;
        const auto wait=[&] {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            do {dock.poll(file,nullptr);if(dock.source&&!dock.source->backendAttempted){metadataSeen=true;if(dock.layerCount()!=3||dock.readOnlyReason()==PsdRecompose::Issue::None)return false;}if(!dock.busy())return dock.issue()==0;Sleep(10);}while(std::chrono::steady_clock::now()<deadline);
            return false;
        };
        if(!wait()||!metadataSeen||!dock.nativeBackend()||dock.layerCount()!=3||dock.visibleCount()!=3||!dock.image().empty()||dock.source->baselineChecked||!dock.source->original.bgra.empty())return false;
        dock.toggle(0);dock.toggle(1);
        if(!wait()||dock.visibleCount()!=1||dock.image().empty()||!dock.source->baselineChecked)return false;
        const auto blue=dock.exportImage().at<cv::Vec4b>(0,0);
        if(blue!=cv::Vec4b(255,0,0,255))return false;
        dock.selected=2;dock.setPercent(50,false);
        if(!wait()||dock.exportImage().at<cv::Vec4b>(0,0)[3]!=128)return false;
        dock.setPercent(100,false);if(!wait())return false;
        dock.toggle(2);
        if(!wait()||dock.exportImage().at<cv::Vec4b>(0,0)[3]!=0)return false;
        dock.reset();if(!wait()||!dock.image().empty())return false;
        dock.selected=0;dock.setBlend("mul ");if(!wait()||dock.image().empty()||dock.exportImage().at<cv::Vec4b>(0,0)!=cv::Vec4b(0,127,0,255))return false;
        dock.setBlend("norm");if(!wait()||!dock.image().empty())return false;
        const bool oldTheme=GlobalVar::isCurrentUIDarkMode;
        TextDrawer text;
        for(int dpi:{96,192,288})for(bool dark:{false,true}) {
            GlobalVar::isCurrentUIDarkMode=dark;
            cv::Mat canvas(MulDiv(620,dpi,96),MulDiv(900,dpi,96),CV_8UC4,cv::Scalar(32,32,32,255));
            dock.layout(canvas.cols,canvas.rows,dpi);dock.draw(canvas,text);
            const auto normal=canvas.clone();dock.move(dock.contentWidth()+MulDiv(130,dpi,96),MulDiv(PsdTree::LIST_TOP+10,dpi,96));dock.draw(canvas,text);
            if(cv::norm(normal,canvas,cv::NORM_INF)==0){GlobalVar::isCurrentUIDarkMode=oldTheme;return false;}
            dock.move(-1,-1);dock.draw(canvas,text);if(cv::norm(normal,canvas,cv::NORM_INF)!=0){GlobalVar::isCurrentUIDarkMode=oldTheme;return false;}
            const auto expected=jarkUtils::to_cv_scalar(PsdDockStyle::separator(dark));
            const auto actual=canvas.at<cv::Vec4b>(MulDiv(PsdTree::LIST_TOP+PsdTree::ROW_HEIGHT,dpi,96)-1,dock.contentWidth()+MulDiv(31,dpi,96));
            if(actual!=cv::Vec4b(uint8_t(expected[0]),uint8_t(expected[1]),uint8_t(expected[2]),255)){GlobalVar::isCurrentUIDarkMode=oldTheme;return false;}
            if(canvas.at<cv::Vec4b>(canvas.rows/2,10)!=cv::Vec4b(32,32,32,255)) {GlobalVar::isCurrentUIDarkMode=oldTheme;return false;}
            const auto name="panel-"+std::to_string(dpi)+(dark?"-dark.png":"-light.png");
            if(!cv::imwrite((directory/name).string(),canvas(cv::Rect(dock.contentWidth(),0,dock.sidebarWidth(),canvas.rows)))) {
                GlobalVar::isCurrentUIDarkMode=oldTheme;return false;
            }
        }
        GlobalVar::isCurrentUIDarkMode=oldTheme;
        dock.layout(900,PsdTree::LIST_TOP+PsdTree::FOOTER+PsdTree::ROW_HEIGHT,96);dock.scroll=0;
        const auto bar=dock.scrollbar();if(bar.maximum!=2)return false;
        dock.click(dock.contentWidth()+bar.thumb.x+2,bar.thumb.y+2);if(!dock.draggingScroll())return false;
        dock.move(dock.contentWidth()+bar.thumb.x+2,bar.track.y+bar.track.height+100);if(dock.scroll!=2)return false;
        dock.release();dock.move(0,0);if(dock.scroll!=2||dock.draggingScroll())return false;
        dock.layout(900,620,96);
        dock.setChannel(1);if(!wait())return false;
        const auto plane=dock.exportImage().at<cv::Vec4b>(0,0);
        if(plane[0]!=plane[1]||plane[1]!=plane[2]||plane[3]!=255)return false;
        dock.close();dock.poll(L"unrelated.png",nullptr);
        return !dock.opened()&&dock.image().empty()&&dock.contentWidth()==dock.width_&&dock.sidebarWidth()==0;
    }
};
