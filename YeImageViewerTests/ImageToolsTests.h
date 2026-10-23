#pragma once
#include "ImageOriginalSave.h"
#include "TransformSavePrompt.h"
#include "PickerHandoff.h"
#pragma comment(lib, "opencv_world4130.lib")
#pragma comment(lib, "IlmImf.lib")
#pragma comment(lib, "libjpeg-turbo.lib")
#pragma comment(lib, "libopenjp2.lib")
#pragma comment(lib, "libpng.lib")
#pragma comment(lib, "libtiff.lib")
#pragma comment(lib, "libwebp.lib")
#pragma comment(lib, "ittnotify.lib")

inline std::vector<uint8_t> makeToolPsd(bool psb, int depth, int compression, bool multiple = false)
{
    using PsdLayers::integer;
    std::vector<std::vector<uint8_t>> channels;
    // 两像素：不透明红、半透明绿。
    for (int c = 0; c < 4; ++c)
    {
        std::vector<uint8_t> raw;
        for (int x = 0; x < 2; ++x)
        {
            const int v = c == 0   ? (x == 0 ? 255 : 0)
                          : c == 1 ? (x == 1 ? 255 : 0)
                          : c == 2 ? 0
                                   : (x == 0 ? 255 : 128);
            if (depth == 8)
                raw.push_back(static_cast<uint8_t>(v));
            else if (depth == 16)
                integer(raw, v * 257, 2);
            else
                integer(raw, std::bit_cast<uint32_t>(v / 255.0f), 4);
        }
        if (compression == 3)
        {
            if (depth == 16)
            {
                uint16_t previous = 0;
                for (int x = 0; x < 2; ++x)
                {
                    const uint16_t value = static_cast<uint16_t>(raw[x * 2] * 256 + raw[x * 2 + 1]);
                    const uint16_t delta = value - previous;
                    raw[x * 2] = static_cast<uint8_t>(delta >> 8);
                    raw[x * 2 + 1] = static_cast<uint8_t>(delta);
                    previous = value;
                }
            }
            else
            {
                if (depth == 32)
                {
                    auto saved = raw;
                    for (int x = 0; x < 2; ++x)
                        for (int b = 0; b < 4; ++b)
                            raw[b * 2 + x] = saved[x * 4 + b];
                }
                for (size_t x = raw.size() - 1; x > 0; --x)
                    raw[x] = static_cast<uint8_t>(raw[x] - raw[x - 1]);
            }
        }
        std::vector<uint8_t> encoded;
        integer(encoded, compression, 2);
        if (compression == 0)
            encoded.insert(encoded.end(), raw.begin(), raw.end());
        else if (compression == 1)
        {
            integer(encoded, raw.size() + 1, psb ? 4 : 2);
            encoded.push_back(static_cast<uint8_t>(raw.size() - 1));
            encoded.insert(encoded.end(), raw.begin(), raw.end());
        }
        else
        {
            uLongf size = compressBound(static_cast<uLong>(raw.size()));
            std::vector<uint8_t> zipped(size);
            compress2(zipped.data(), &size, raw.data(), static_cast<uLong>(raw.size()), 6);
            zipped.resize(size);
            encoded.insert(encoded.end(), zipped.begin(), zipped.end());
        }
        channels.push_back(std::move(encoded));
    }
    std::vector<uint8_t> info;
    integer(info, 1, 2);
    integer(info, static_cast<uint32_t>(-1), 4);
    integer(info, static_cast<uint32_t>(-2), 4);
    integer(info, 0, 4);
    integer(info, 0, 4);
    integer(info, 4, 2);
    for (size_t c = 0; c < 4; ++c)
    {
        integer(info, c == 3 ? 65535 : c, 2);
        integer(info, channels[c].size(), psb ? 8 : 4);
    }
    info.insert(info.end(), {'8', 'B', 'I', 'M', 'n', 'o', 'r', 'm', 128, 0, 2, 0});
    std::vector<uint8_t> extra;
    integer(extra, 0, 4);
    integer(extra, 0, 4);
    integer(extra, 0, 4);
    extra.insert(extra.end(), {'8', 'B', 'I', 'M', 'l', 'u', 'n', 'i'});
    integer(extra, 8, 4);
    integer(extra, 2, 4);
    integer(extra, 0x6D4B, 2);
    integer(extra, 0x8BD5, 2);
    integer(info, extra.size(), 4);
    info.insert(info.end(), extra.begin(), extra.end());
    if (multiple)
    {
        std::vector<uint8_t> records;
        integer(records, 2, 2);
        records.insert(records.end(), info.begin() + 2, info.end());
        records.insert(records.end(), info.begin() + 2, info.end());
        info = std::move(records);
    }
    for (auto &channel : channels)
        info.insert(info.end(), channel.begin(), channel.end());
    if (multiple)
    { // 第二层交换红、绿通道，验证不是总显示第一层。
        info.insert(info.end(), channels[1].begin(), channels[1].end());
        info.insert(info.end(), channels[0].begin(), channels[0].end());
        info.insert(info.end(), channels[2].begin(), channels[2].end());
        info.insert(info.end(), channels[3].begin(), channels[3].end());
    }
    if (info.size() % 2)
        info.push_back(0);
    std::vector<uint8_t> section;
    integer(section, info.size(), psb ? 8 : 4);
    section.insert(section.end(), info.begin(), info.end());
    integer(section, 0, 4);
    std::vector<uint8_t> file{'8', 'B', 'P', 'S'};
    integer(file, psb ? 2 : 1, 2);
    integer(file, 0, 6);
    integer(file, 3, 2);
    integer(file, 1, 4);
    integer(file, 2, 4);
    integer(file, depth, 2);
    integer(file, 3, 2);
    integer(file, 0, 4);
    integer(file, 0, 4);
    integer(file, section.size(), psb ? 8 : 4);
    file.insert(file.end(), section.begin(), section.end());
    // 正常合成图与单层像素相同（不计单层 opacity），能继续由主程序打开。
    integer(file, 0, 2);
    for (int c = 0; c < 3; ++c)
        for (int x = 0; x < 2; ++x)
        {
            const int value = c == 0 ? (x == 0 ? 255 : 0) : c == 1 ? (x == 1 ? 255 : 0) : 0;
            if (depth == 8)
                file.push_back(static_cast<uint8_t>(value));
            else if (depth == 16)
                integer(file, value * 257, 2);
            else
                integer(file, std::bit_cast<uint32_t>(value / 255.0f), 4);
        }
    return file;
}
inline void generateToolFixtures(const std::filesystem::path &directory)
{
    std::filesystem::create_directories(directory);
    for (bool psb : {false, true})
        for (int depth : {8, 16, 32})
            for (int compression : {0, 1, 2, 3})
            {
                const auto name =
                    "layer-" + std::to_string(depth) + "-" + std::to_string(compression) + (psb ? ".psb" : ".psd");
                const auto bytes = makeToolPsd(psb, depth, compression);
                const auto path = directory / name;
                if (!std::filesystem::exists(path))
                    ImageExport::writeNew(path, bytes);
            }
    cv::Mat pattern(2, 3, CV_8UC4);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            pattern.at<cv::Vec4b>(y, x) = {static_cast<uint8_t>(x * 80), static_cast<uint8_t>(y * 120),
                                           static_cast<uint8_t>(20 + x * 30 + y * 10),
                                           static_cast<uint8_t>(x == 0 ? 0 : 255)};
    ImageExport::saveNew(directory / L"orientation-alpha.png", pattern, false);
    const auto two = makeToolPsd(false, 8, 1, true);
    if (!std::filesystem::exists(directory / L"two-layers.psd"))
        ImageExport::writeNew(directory / L"two-layers.psd", two);
}
inline void expectImageTools()
{
    struct ScaleView {int64_t zoomCur=11141,zoomTarget=11141;void setZoom(int64_t value){zoomCur=zoomTarget=value;}};
    for(int64_t initial:{int64_t(11141),int64_t(32768),int64_t(65536),int64_t(137625)}){
        ScaleView view{initial,initial};bool transition=false,stable=true;
        for(int trip=0;trip<100;++trip)for(bool immersive:{false,true}){
            PresentationLayout::PreserveDisplayedScale guard(view,transition);
            // 模拟窗口样式改变和SetWindowPos同步产生的多次WM_SIZE，只有非切换时允许fit。
            for(int message=0;message<3;++message){if(!transition)view.setZoom(immersive?100000:20000);stable&=transition&&view.zoomCur==initial;}
            guard.finish();stable&=!transition&&view.zoomCur==initial&&view.zoomTarget==initial;
        }
        passOrFail("普通/沉浸往返100次始终保留A，非100%和人工缩放后均不漂移",stable);
        view.setZoom(initial+1234);{PresentationLayout::PreserveDisplayedScale guard(view,transition);view.zoomTarget=99999;}
        passOrFail("人工缩放成为新的A，切换冻结显示倍率而非未完成动画目标",view.zoomCur==initial+1234&&view.zoomTarget==initial+1234&&!transition);
    }
    {ScaleView view;bool nested=true;{PresentationLayout::PreserveDisplayedScale guard(view,nested);}
        passOrFail("窗口切换异常/早退保护恢复原来的嵌套状态",nested);}
    const auto restored=PresentationLayout::calculateWindowedAtScale(1000,500,0.5,3840,2160);
    const auto rotated=PresentationLayout::calculateWindowedAtScale(1000,500,0.5,3840,2160,true);
    passOrFail("沉浸恢复边框按当前实际缩放，不重算默认或 DPI 倍率",restored.clientWidth==500&&restored.clientHeight==250&&rotated.clientWidth==250&&rotated.clientHeight==500);
    const auto enlarged=PresentationLayout::calculateWindowedAtScale(4000,4000,2.0,1920,1080);
    passOrFail("放大后的边框窗口限制尺寸但不改图片比例",enlarged.clientWidth==1728&&enlarged.clientHeight==972&&enlarged.initialSlideY==3514);
    for (auto rgb : {ColorValue::Rgb{255, 0, 128}, ColorValue::Rgb{0, 128, 255}, ColorValue::Rgb{0, 0, 0},
                     ColorValue::Rgb{255, 255, 255}})
        passOrFail("取色色值 HEX/RGB 不颠倒通道",
                   ColorValue::parse(ColorValue::hex(rgb)) == rgb &&
                       ColorValue::rgb(rgb) == L"RGB(" + std::to_wstring(rgb.r) + L", " + std::to_wstring(rgb.g) +
                                                   L", " + std::to_wstring(rgb.b) + L")");
    passOrFail("取色接受大小写并拒绝无效 HEX",
               ColorValue::parse(L"aa80fF") == ColorValue::Rgb{170, 128, 255} && !ColorValue::parse(L"#FF00") &&
                   !ColorValue::parse(L"#GG0000") && !ColorValue::parse(L"##000000"));
    passOrFail("取色 RGB 输入严格限制 0..255", ColorValue::channel(L"0") == 0 &&
                                                   ColorValue::channel(L"255") == 255 &&
                                                   !ColorValue::channel(L"256") && !ColorValue::channel(L"-1") &&
                                                   !ColorValue::channel(L"a") && !ColorValue::channel(L""));
    passOrFail("屏幕取色支持负坐标并拒绝越界", ColorValue::screenPoint(-1920, -1080, -1920, -1080, 3840, 2160) &&
                                                   !ColorValue::screenPoint(-1921, 0, -1920, -1080, 3840, 2160) &&
                                                   !ColorValue::screenPoint(1920, 0, -1920, -1080, 3840, 2160));
    cv::Mat source(2, 3, CV_8UC4);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            source.at<cv::Vec4b>(y, x) = {static_cast<uint8_t>(x + 3 * y), 20, 40, static_cast<uint8_t>(x * 100)};
    const cv::Mat original = source.clone();
    for (int rotation = 0; rotation < 4; ++rotation)
        for (bool h : {false, true})
            for (bool v : {false, true})
            {
                const auto out = ImageExport::transform(source, rotation, h, v);
                bool equal = out.cols == (rotation % 2 ? source.rows : source.cols) &&
                             out.rows == (rotation % 2 ? source.cols : source.rows);
                for (int y = 0; y < out.rows; ++y)
                    for (int x = 0; x < out.cols; ++x)
                    {
                        auto p = ImageViewTransform::displayToSource(x, y, out.cols, out.rows, rotation, h, v);
                        equal = equal && out.at<cv::Vec4b>(y, x) == source.at<cv::Vec4b>(p.y, p.x);
                    }
                passOrFail("保存副本变换与观看方向一致且不改缓存",
                           equal && cv::norm(source, original, cv::NORM_INF) == 0 && out.data != source.data);
            }
    const auto jpeg = ImageExport::jpegSurface(source);
    passOrFail("JPEG 透明区按白色合成，不丢成黑底",
               jpeg.at<cv::Vec3b>(0, 0) == cv::Vec3b(255, 255, 255) && jpeg.channels() == 3);
    const auto directory =
        std::filesystem::temp_directory_path() /
        (L"YeViewer-Export-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directory(directory);
    const auto path = directory / L"source.png", copy = directory / L"copy.png";
    passOrFail("保存新副本可回读原始分辨率及透明像素",
               ImageExport::saveNew(path, source, false) && ImageExport::saveNew(copy, source, false) &&
                   cv::norm(cv::imread(copy.string(), cv::IMREAD_UNCHANGED), source, cv::NORM_INF) == 0);
    const auto before = std::filesystem::file_size(path);
    passOrFail("保存拒绝源文件和已有副本，内容保持不变",
               !ImageExport::saveNew(path, jpeg, true) && !ImageExport::saveNew(copy, jpeg, true) &&
                   std::filesystem::file_size(path) == before &&
                   cv::norm(cv::imread(path.string(), cv::IMREAD_UNCHANGED), source, cv::NORM_INF) == 0);
    passOrFail("保存失败不产生空文件",
               !ImageExport::saveNew(directory / L"missing" / L"copy.png", source, false) &&
                   !std::filesystem::exists(directory / L"missing"));
    const auto partial = directory / L"partial.png";
    const std::vector<uint8_t> partialData{1, 2, 3};
    const bool failedWrite = ImageExport::writeNewWith(partial, partialData,
                                                       [](HANDLE handle, const void *data, DWORD, DWORD *written) {
                                                           WriteFile(handle, data, 1, written, nullptr);
                                                           return false;
                                                       });
    passOrFail("部分写入失败在独占句柄上清理半成品，已有文件不变",
               !failedWrite && !std::filesystem::exists(partial) && std::filesystem::file_size(path) == before);
    const auto oldBytes=ImageOriginalSave::read(path);std::vector<uint8_t> newBytes;
    cv::imencode(".png",ImageExport::transform(source,1,false,false),newBytes);
    passOrFail("覆盖PNG只接受完整8位静图",ImageOriginalSave::simple8BitRaster(newBytes,true));
    auto unusual=newBytes;unusual[24]=16;
    passOrFail("覆盖PNG拒绝高位深和尾附数据",!ImageOriginalSave::simple8BitRaster(unusual,true)&&!ImageOriginalSave::simple8BitRaster(std::span(newBytes).first(24),true));
    unusual=newBytes;unusual.insert(unusual.begin()+33,{0,0,0,0,'a','c','T','L',0,0,0,0});
    passOrFail("覆盖PNG拒绝APNG，不把多帧写成第一帧",!ImageOriginalSave::simple8BitRaster(unusual,true));
    std::vector<uint8_t> jpegBytes;cv::imencode(".jpg",jpeg,jpegBytes);
    passOrFail("覆盖JPEG接受普通8位单图",ImageOriginalSave::simple8BitRaster(jpegBytes,false));
    unusual=jpegBytes;unusual.push_back(0);
    passOrFail("覆盖JPEG拒绝尾附视频或其他额外内容",!ImageOriginalSave::simple8BitRaster(unusual,false));
    unusual=jpegBytes;unusual.insert(unusual.begin()+2,{255,226,0,6,'M','P','F',0});
    passOrFail("覆盖JPEG拒绝MPO，不静默丢弃多视图",!ImageOriginalSave::simple8BitRaster(unusual,false));
    std::vector<std::filesystem::path> saveFiles;
    const auto replaced=ImageOriginalSave::replace(path,oldBytes,newBytes);saveFiles.push_back(replaced.backup);
    passOrFail("覆盖只提交已校验新图，唯一备份逐字节保留原图",replaced.saved&&ImageOriginalSave::read(path)==newBytes&&ImageOriginalSave::read(replaced.backup)==oldBytes);
    bool called=false;const auto stale=ImageOriginalSave::replaceWith(path,oldBytes,newBytes,[&](const auto&,const auto&,const auto&){called=true;return true;});
    passOrFail("保存期间源文件变更时拒绝提交且不覆盖外部更改",!stale.saved&&!called&&ImageOriginalSave::read(path)==newBytes);
    SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_READONLY);const auto readonly=ImageOriginalSave::replace(path,newBytes,oldBytes);SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL);
    passOrFail("只读原图被保护，不删除或改写原图",!readonly.saved&&ImageOriginalSave::read(path)==newBytes);
    const auto failed=ImageOriginalSave::replaceWith(path,newBytes,oldBytes,[](const auto&,const auto&,const auto&){SetLastError(ERROR_ACCESS_DENIED);return false;});saveFiles.push_back(failed.recovery);
    passOrFail("替换失败保留原图以及新图恢复文件",!failed.saved&&failed.error==ERROR_ACCESS_DENIED&&ImageOriginalSave::read(path)==newBytes&&ImageOriginalSave::read(failed.recovery)==oldBytes);
    const auto moved=ImageOriginalSave::replaceWith(path,newBytes,oldBytes,[](const auto& original,const auto&,const auto& backup){
        if(!MoveFileW(original.c_str(),backup.c_str()))return false;SetLastError(ERROR_UNABLE_TO_MOVE_REPLACEMENT);return false;
    });saveFiles.push_back(moved.backup);saveFiles.push_back(moved.recovery);
    passOrFail("替换中途原图已移至备份时，从备份恢复缺失原路径，仍保留恢复文件",!moved.saved&&ImageOriginalSave::read(path)==newBytes&&ImageOriginalSave::read(moved.backup)==newBytes&&ImageOriginalSave::read(moved.recovery)==oldBytes);
    const auto raced=ImageOriginalSave::replaceWith(path,newBytes,oldBytes,[&](const auto& original,const auto&,const auto& backup){
        if(!MoveFileW(original.c_str(),backup.c_str()))return false;ImageExport::writeNew(original,partialData);SetLastError(ERROR_UNABLE_TO_MOVE_REPLACEMENT);return false;
    });saveFiles.push_back(raced.backup);saveFiles.push_back(raced.recovery);
    passOrFail("失败恢复绝不覆盖原路径后来出现的其他文件",!raced.saved&&ImageOriginalSave::read(path)==partialData&&ImageOriginalSave::read(raced.backup)==newBytes);
    for(const auto& file:saveFiles)if(!file.empty())std::filesystem::remove(file);
    std::filesystem::remove(copy);
    std::filesystem::remove(path);
    std::filesystem::remove(directory);
    {PickerHandoff::Gesture handoff(1000);passOrFail("取色后1999毫秒右键可打开面板",handoff.down(2999)&&handoff.up(3001)&&handoff.chosen());}
    {PickerHandoff::Gesture handoff(1000);passOrFail("取色两秒期满不拦截其他软件右键",!handoff.down(3000)&&!handoff.up(3001)&&!handoff.chosen());}
    {PickerHandoff::Gesture handoff(1000);handoff.cancel();passOrFail("其他点击取消取色后快捷窗口期",!handoff.active(1001)&&!handoff.down(1001));}
    {PickerHandoff::Gesture handoff(1000);passOrFail("没有对应按下的右键松开不可误打开面板",!handoff.up(1100)&&!handoff.chosen());}
    {PickerHandoff::Gesture handoff(1000);handoff.down(1100);passOrFail("按下后允许成对松开且不无限等待",handoff.active(3000)&&!handoff.active(6100));}
    {const auto prompt=TransformSavePrompt::layout(1920,1080,96,{700,0,500,1080});
        passOrFail("竖图保存提示紧凑靠右下且完全避开图像",prompt.panel.width==284&&prompt.panel.x>=1200&&prompt.panel.y>900&&prompt.buttons[2].width<50);}
    {const auto prompt=TransformSavePrompt::layout(1920,1080,96,{500,0,1420,1080});
        passOrFail("右下占图时选择左下空白而不盖图",prompt.panel.x+prompt.panel.width<=500&&prompt.panel.y>900);}
    {const auto prompt=TransformSavePrompt::layout(1920,1080,96,{0,200,1920,880});
        passOrFail("下方无留白时使用顶部留白",prompt.panel.y+prompt.panel.height<=200);}
    for (int dpi : {96, 144, 192, 288})
    {
        for(int width:{3,40,240,500,1920}){
            const auto prompt=TransformSavePrompt::layout(width,1080,dpi);bool bounded=prompt.panel.x>=0&&prompt.panel.x+prompt.panel.width<=width;
            for(const auto& button:prompt.buttons)bounded&=button.width>0&&button.x>=prompt.panel.x&&button.x+button.width<=prompt.panel.x+prompt.panel.width;
            passOrFail("非阻塞保存条在窄窗口及高DPI中不越界",bounded);
            for(int height:{3,40,180,1080}){const auto small=TransformSavePrompt::layout(width,height,dpi,{0,0,width,height});
                bool inside=small.panel.x>=0&&small.panel.y>=0&&small.panel.x+small.panel.width<=width&&small.panel.y+small.panel.height<=height;
                for(const auto& b:small.buttons)inside&=b.width>0&&b.height>0&&b.x>=0&&b.y>=0&&b.x+b.width<=width&&b.y+b.height<=height;
                passOrFail("紧凑保存提示短窗口和高DPI所有热区有界",inside);}
            TransformSavePrompt::Press press;const auto& button=prompt.buttons[1];
            const int x=button.x+button.width/2,y=button.y+button.height/2;
            const bool began=press.begin(prompt,x,y);const bool picked=press.release(prompt,x,y)==1;
            press.begin(prompt,x,y);const bool outside=press.release(prompt,-1,-1)==-1;
            press.begin(prompt,x,y);press.cancel();
            passOrFail("保存条只在原按钮内松手执行；拖出/失焦/捕获丢失不执行或穿透",began&&picked&&outside&&!press.held()&&press.release(prompt,x,y)==-1);
        }
        const auto hint = HintLayout::tooltip(3840, 2160, 3800, 2100, 600 * dpi / 96, 16 * dpi / 96, dpi);
        passOrFail("4K 提示按真实文字宽度/DPI 留足空间",
                   hint.width >= 600 * dpi / 96 + 24 * dpi / 96 && hint.height >= 32 * dpi / 96 && hint.x >= 0 &&
                       hint.x + hint.width <= 3840 && hint.y + hint.height <= 2160);
        const auto close = OverlayLayout::presentationCloseRect(3840, 2160, dpi);
        passOrFail("方形关闭按钮贴住右上角且甩鼠可命中",
                   close.y == 0 && close.x + close.width == 3840 && close.width == close.height &&
                       OverlayLayout::hitTest(3840, 2160, 3839, 0, dpi) == OverlayLayout::Hit::PresentationClose);
    }
    const auto narrow = HintLayout::tooltip(50, 20, 0, 10, 400, 32, 192);
    passOrFail("极小窗口提示布局不会反向夹取越界", narrow.width > 0 && narrow.height > 0 && narrow.x >= 0 &&
                                                       narrow.x + narrow.width <= 50 &&
                                                       narrow.y + narrow.height <= 20);
    passOrFail("分隔线弱化实际 RGB 而非只写 alpha", HintLayout::SEPARATOR_COLOR == 0xFF45474Du);
    for (bool psb : {false, true})
        for (int depth : {8, 16, 32})
            for (int compression : {0, 1, 2, 3})
            {
                const auto bytes = makeToolPsd(psb, depth, compression);
                const auto doc = PsdLayers::read(bytes);
                const auto raster = PsdLayers::preview(doc, 0);
                passOrFail("PSD/PSB 8/16/32 位四压缩单层像素/alpha",
                           doc.valid && doc.layers.size() == 1 && raster.width == 2 && raster.height == 1 &&
                               raster.bgra == std::vector<uint8_t>({0, 0, 255, 255, 0, 255, 0, 128}));
                passOrFail("PSD 图层 Unicode/隐藏/不透明度/负坐标",
                           doc.valid && doc.layers.size() == 1 && doc.layers[0].name == L"测试" &&
                               !doc.layers[0].visible && doc.layers[0].opacity == 128 &&
                               doc.layers[0].left == -2 && doc.layers[0].top == -1);
                passOrFail("PSD 图层内存预算和索引越界拒绝",
                           PsdLayers::preview(doc, 0, 4).bgra.empty() && PsdLayers::preview(doc, 1).bgra.empty());
                passOrFail("PSD 图层截断不能伪成功",
                           !PsdLayers::read(std::span(bytes).first(bytes.size() / 2)).valid);
            }
    // 真正落在 test/ 的最小复现素材，防止只有运行期自建的理想数据。
    for (auto name : {"layer-8-0.psd", "layer-16-1.psb", "layer-32-3.psd"})
    {
        const auto bytes = readFile(std::string("test/image-tools/") + name);
        const auto doc = PsdLayers::read(bytes);
        passOrFail("仓库实际 PSD 图层夹具可读", doc.valid && !PsdLayers::preview(doc, 0).bgra.empty());
    }
    const auto twoBytes = readFile("test/image-tools/two-layers.psd");
    const auto two = PsdLayers::read(twoBytes);
    const auto oneRaster = PsdLayers::preview(two, 0), twoRaster = PsdLayers::preview(two, 1);
    passOrFail("PSD 多图层能按索引切换不同像素，不重复第一层",
               two.valid && two.layers.size() == 2 &&
                   oneRaster.bgra == std::vector<uint8_t>({0, 255, 0, 255, 0, 0, 255, 128}) &&
                   twoRaster.bgra == std::vector<uint8_t>({0, 0, 255, 255, 0, 255, 0, 128}));
}
