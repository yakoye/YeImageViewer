#pragma once
#include "ImageDatabase.h"
#include "ImageExport.h"
#include "ImageOriginalSave.h"
#include "PsdCompositeDecoder.h"
#include <array>

namespace ImageExportJob
{
inline bool pngProfile(std::vector<uint8_t>& png,std::span<const uint8_t> profile){
    if(png.size()<33||profile.empty())return false;
    uLongf size=compressBound(uLong(profile.size()));std::vector<uint8_t> packed(size);
    if(compress2(packed.data(),&size,profile.data(),uLong(profile.size()),Z_BEST_SPEED)!=Z_OK)return false;packed.resize(size);
    std::vector<uint8_t> chunk;const auto be=[&](uint32_t n){for(int i=3;i>=0;--i)chunk.push_back(uint8_t(n>>(i*8)));};
    be(uint32_t(packed.size()+6));for(char c:std::string_view("iCCP"))chunk.push_back(uint8_t(c));
    for(uint8_t c:std::array<uint8_t,6>{'s','R','G','B',0,0})chunk.push_back(c);chunk.insert(chunk.end(),packed.begin(),packed.end());
    be(uint32_t(crc32(0,chunk.data()+4,uInt(chunk.size()-4))));png.insert(png.begin()+33,chunk.begin(),chunk.end());return true;
}
inline bool canReplace(const std::wstring& source){
    auto ext=std::filesystem::path(source).extension().wstring();for(auto& c:ext)c=wchar_t(towlower(c));
    return ext==L".png"||ext==L".jpg"||ext==L".jpeg";
}
inline ImageOriginalSave::Result saveOriginal(const std::wstring& source,int rotation,bool horizontal,bool vertical)noexcept{
    ImageOriginalSave::Result failure;failure.error=ERROR_INVALID_DATA;
    try{
        if(!canReplace(source))return failure;
        const auto original=ImageOriginalSave::read(source);if(original.size()<32)return failure;
        const bool png=original[0]==137&&original[1]=='P'&&original[2]=='N'&&original[3]=='G';
        if(!ImageOriginalSave::simple8BitRaster(original,png)){failure.error=ERROR_NOT_SUPPORTED;return failure;}
        auto ext=std::filesystem::path(source).extension().wstring();for(auto& c:ext)c=wchar_t(towlower(c));
        if(png!=(ext==L".png"))return failure;
        ImageDatabase database;auto asset=database.decodeForExport(source);
        if(asset.primaryFrame.empty()||database.isErrorTipsFrame(asset.primaryFrame)||!asset.frames.empty()||
            (asset.format!=ImageFormat::None&&asset.format!=ImageFormat::Still))return failure;
        const auto pixels=ImageExport::transform(asset.primaryFrame,rotation,horizontal,vertical);std::vector<uint8_t> encoded;
        if(!cv::imencode(png?".png":".jpg",png?pixels:ImageExport::jpegSurface(pixels),encoded))return failure;
        const auto icc=PsdCompositeDecoder::srgbProfile();
        // Exiv2新建PNG的ICC名称为空会产生libpng "bad keyword"。先提供有效命名的iCCP，
        // 再读入元数据，避免生成能显示但色彩配置被其他软件忽略的文件。
        if(png&&!pngProfile(encoded,icc))return failure;
        // 只修改内存中的新文件；保留摄影信息，清除旧缩略图，并统一方向、尺寸和sRGB标记。
        auto from=Exiv2::ImageFactory::open(original.data(),original.size());auto to=Exiv2::ImageFactory::open(encoded.data(),encoded.size());
        from->readMetadata();to->readMetadata();
        for(const auto& entry:from->xmpData()){const auto key=entry.key();if(key.find("MotionPhoto")!=std::string::npos||key.find("MicroVideo")!=std::string::npos||key.find("Container:")!=std::string::npos){failure.error=ERROR_NOT_SUPPORTED;return failure;}}
        auto exif=from->exifData();Exiv2::ExifThumb(exif).erase();
        exif["Exif.Image.Orientation"]=uint16_t(1);exif["Exif.Photo.PixelXDimension"]=uint32_t(pixels.cols);exif["Exif.Photo.PixelYDimension"]=uint32_t(pixels.rows);exif["Exif.Photo.ColorSpace"]=uint16_t(1);
        exif["Exif.Image.ImageWidth"]=uint32_t(pixels.cols);exif["Exif.Image.ImageLength"]=uint32_t(pixels.rows);
        auto xmp=from->xmpData();xmp["Xmp.tiff.Orientation"]="1";xmp["Xmp.exif.PixelXDimension"]=std::to_string(pixels.cols);xmp["Xmp.exif.PixelYDimension"]=std::to_string(pixels.rows);
        xmp["Xmp.tiff.ImageWidth"]=std::to_string(pixels.cols);xmp["Xmp.tiff.ImageLength"]=std::to_string(pixels.rows);
        to->setExifData(exif);to->setXmpData(xmp);to->setIptcData(from->iptcData());
        to->setIccProfile(Exiv2::DataBuf(icc.data(),icc.size()));to->writeMetadata();
        auto& io=to->io();const auto size=io.size();if(!size||size>(256u<<20))return failure;
        const auto* bytes=io.mmap();if(!bytes)return failure;encoded.assign(bytes,bytes+size);io.munmap();
        const auto verified=cv::imdecode(encoded,cv::IMREAD_UNCHANGED);
        if(verified.empty()||verified.cols!=pixels.cols||verified.rows!=pixels.rows)return failure;
        if(png&&(verified.type()!=pixels.type()||cv::norm(verified,pixels,cv::NORM_INF)!=0))return failure;
        return ImageOriginalSave::replace(source,original,encoded);
    }catch(...){return failure;}
}

inline bool save(const std::wstring &source, const std::filesystem::path &target, int rotation, bool horizontal,
                 bool vertical, bool jpeg, int frame = 0) noexcept
{
    try
    {
        ImageDatabase database;
        auto asset = database.decodeForExport(source);
        cv::Mat pixels = asset.frames.empty()
                             ? asset.primaryFrame
                             : asset.frames[std::clamp(frame, 0, static_cast<int>(asset.frames.size()) - 1)];
        return !pixels.empty() && !database.isErrorTipsFrame(pixels) &&
               ImageExport::saveNew(target, ImageExport::transform(pixels, rotation, horizontal, vertical), jpeg);
    }
    catch (...)
    {
        return false;
    }
}

// 生产编码/元数据/替换一体回归。仅在本次创建的GUID子目录内生成和修改最小夹具。
inline bool originalSaveSelfTest(const std::filesystem::path& output){
    std::filesystem::create_directories(output);const auto root=output/(L"original-save-"+ImageOriginalSave::token());
    if(!std::filesystem::create_directory(root))return false;std::ofstream log(root/"result.txt");int passed=0,failed=0;
    const auto check=[&](bool okay,const char* name){okay?++passed:++failed;log<<(okay?"PASS ":"FAIL ")<<name<<"\n";};
    cv::Mat pattern(23,37,CV_8UC4);for(int y=0;y<pattern.rows;++y)for(int x=0;x<pattern.cols;++x)pattern.at<cv::Vec4b>(y,x)={uint8_t(x*5),uint8_t(y*9),uint8_t((x+y)*3),uint8_t(80+(x+y)%176)};
    for(bool jpeg:{false,true})for(int r=0;r<4;++r)for(bool h:{false,true})for(bool v:{false,true}){
        const auto path=root/(std::to_wstring(jpeg)+L"-"+std::to_wstring(r)+std::to_wstring(h)+std::to_wstring(v)+(jpeg?L".jpg":L".png"));
        check(ImageExport::saveNew(path,pattern,jpeg),"create fixture");
        {auto metadata=Exiv2::ImageFactory::open(path.string());metadata->readMetadata();metadata->exifData()["Exif.Image.Artist"]="Ye regression";metadata->exifData()["Exif.Image.Orientation"]=uint16_t(1);metadata->writeMetadata();}
        const auto original=ImageOriginalSave::read(path);ImageDatabase database;const auto asset=database.decodeForExport(path.wstring());
        const auto expected=ImageExport::transform(asset.primaryFrame,r,h,v);const auto result=saveOriginal(path.wstring(),r,h,v);
        check(result.saved&&ImageOriginalSave::read(result.backup)==original,"commit retains exact original backup");
        const auto bytes=ImageOriginalSave::read(path);const auto saved=cv::imdecode(bytes,cv::IMREAD_UNCHANGED);
        check(!saved.empty()&&saved.cols==expected.cols&&saved.rows==expected.rows,"saved orientation dimensions");
        if(!jpeg)check(saved.type()==expected.type()&&cv::norm(saved,expected,cv::NORM_INF)==0,"PNG transformed pixels lossless");
        auto metadata=Exiv2::ImageFactory::open(bytes.data(),bytes.size());metadata->readMetadata();
        auto& exif=metadata->exifData();
        check(exif["Exif.Image.Orientation"].toInt64()==1&&exif["Exif.Image.Artist"].toString()=="Ye regression"&&
            exif["Exif.Photo.PixelXDimension"].toInt64()==expected.cols&&exif["Exif.Photo.PixelYDimension"].toInt64()==expected.rows&&metadata->iccProfileDefined(),"metadata retained, orientation/dimensions/sRGB updated");
    }
    cv::Mat high(2,3,CV_16UC3,cv::Scalar(1234,2345,3456));std::vector<uint8_t> encoded;cv::imencode(".png",high,encoded);
    const auto highPath=root/L"high.png";ImageExport::writeNew(highPath,encoded);check(!saveOriginal(highPath.wstring(),1,false,false).saved&&ImageOriginalSave::read(highPath)==encoded,"16-bit refuses destructive downconversion");
    const auto invalidPath=root/L"invalid.jpg";ImageExport::writeNew(invalidPath,encoded);check(!saveOriginal(invalidPath.wstring(),1,false,false).saved&&ImageOriginalSave::read(invalidPath)==encoded,"mismatched extension rejected without write");
    check(!saveOriginal((root/L"missing.png").wstring(),1,false,false).saved,"missing source fails safely");
    log<<"passed="<<passed<<" failed="<<failed<<"\n";return failed==0;
}
} // namespace ImageExportJob
