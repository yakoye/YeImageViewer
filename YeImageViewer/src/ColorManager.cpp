#include "ColorManager.h"

#include <ppl.h>
#include <algorithm>
#include "exifParse.h"
#include "lcms2.h"

#include <fstream>

namespace {

class CmsProfile {
public:
    explicit CmsProfile(cmsHPROFILE profile = nullptr) : profile(profile) {}

    ~CmsProfile() {
        if (profile)
            cmsCloseProfile(profile);
    }

    CmsProfile(const CmsProfile&) = delete;
    CmsProfile& operator=(const CmsProfile&) = delete;

    void reset(cmsHPROFILE newProfile = nullptr) {
        if (profile)
            cmsCloseProfile(profile);
        profile = newProfile;
    }

    operator cmsHPROFILE() const {
        return profile;
    }

private:
    cmsHPROFILE profile = nullptr;
};

class CmsTransform {
public:
    explicit CmsTransform(cmsHTRANSFORM transform = nullptr) : transform(transform) {}

    ~CmsTransform() {
        if (transform)
            cmsDeleteTransform(transform);
    }

    CmsTransform(const CmsTransform&) = delete;
    CmsTransform& operator=(const CmsTransform&) = delete;

    operator cmsHTRANSFORM() const {
        return transform;
    }

private:
    cmsHTRANSFORM transform = nullptr;
};

bool isInternalTipsImage(const cv::Mat& mat) {
    if (mat.empty() || mat.type() != CV_8UC4)
        return false;

    const bool isTipsSize = (mat.cols == 800 && mat.rows == 600) || (mat.cols == 600 && mat.rows == 800);
    if (!isTipsSize)
        return false;

    const uint32_t firstPixel = *reinterpret_cast<const uint32_t*>(mat.ptr());
    return firstPixel == deepTheme.BG || firstPixel == lightTheme.BG;
}

std::vector<uint8_t> readFileBytes(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file)
        return {};

    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size <= 0)
        return {};

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file)
        return {};

    return bytes;
}

}

void ColorManager::setWindow(HWND hwnd) {
    this->hwnd = hwnd;
}

std::vector<uint8_t> ColorManager::readEmbeddedIccProfile(std::wstring_view path, std::span<const uint8_t> buf) {
    if (buf.empty())
        return {};

    try {
        auto image = Exiv2::ImageFactory::open(buf.data(), buf.size());
        if (!image)
            return {};

        image->readMetadata();
        if (!image->iccProfileDefined())
            return {};

        const auto& profile = image->iccProfile();
        if (profile.empty())
            return {};

        return { profile.c_data(), profile.c_data() + profile.size() };
    }
    catch ([[maybe_unused]] const Exiv2::Error& e) {
        JARK_LOG("Read ICC failed: {} [{}]", jarkUtils::wstringToUtf8(path), e.what());
    }
    catch ([[maybe_unused]] const std::exception& e) {
        JARK_LOG("Read ICC failed: {} [{}]", jarkUtils::wstringToUtf8(path), e.what());
    }

    return {};
}

std::vector<uint8_t> ColorManager::readMonitorIccProfile() const {
    if (!hwnd)
        return {};

    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor)
        return {};

    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(MONITORINFOEXW);
    if (!GetMonitorInfoW(monitor, &monitorInfo))
        return {};

    HDC hdc = CreateDCW(L"DISPLAY", monitorInfo.szDevice, nullptr, nullptr);
    if (!hdc)
        return {};

    DWORD pathLen = MAX_PATH;
    std::wstring profilePath(pathLen, L'\0');
    bool ok = GetICMProfileW(hdc, &pathLen, profilePath.data());
    if (!ok && pathLen > MAX_PATH) {
        profilePath.assign(pathLen, L'\0');
        ok = GetICMProfileW(hdc, &pathLen, profilePath.data());
    }
    DeleteDC(hdc);

    if (!ok)
        return {};

    profilePath.resize(pathLen);
    if (!profilePath.empty() && profilePath.back() == L'\0')
        profilePath.pop_back();

    return readFileBytes(profilePath);
}

std::vector<uint8_t>& ColorManager::readMonitorIccProfileCached() {
    static std::mutex mutex;
    static std::vector<uint8_t> cachedProfile;

    std::lock_guard<std::mutex> lock(mutex);
    if (cachedProfile.empty()) {
        cachedProfile = readMonitorIccProfile();
    }
    return cachedProfile;
}

bool ColorManager::applyToMat(cv::Mat& mat, const std::vector<uint8_t>& sourceIcc, const std::vector<uint8_t>& monitorIcc) {
    if (mat.empty() || isInternalTipsImage(mat))
        return false;

    const uint32_t pixelType = mat.type() == CV_8UC3 ? TYPE_BGR_8 :
        mat.type() == CV_8UC4 ? TYPE_BGRA_8 : 0;
    if (pixelType == 0)
        return false;

    // 源和目标是同一个色彩空间时，变换是恒等的，跑一遍只是把每个像素原样写回去。
    // 绝大多数图是这种情况：文件里没嵌 profile（当 sRGB 处理），显示器也没设 profile
    // （当 sRGB 处理）。一亿像素白跑一趟要两三百毫秒，直接省掉。
    if (sourceIcc.empty() && monitorIcc.empty())
        return false;
    if (!sourceIcc.empty() && sourceIcc == monitorIcc)
        return false;

    CmsProfile sourceProfile(sourceIcc.empty() ?
        cmsCreate_sRGBProfile() :
        cmsOpenProfileFromMem(sourceIcc.data(), static_cast<cmsUInt32Number>(sourceIcc.size())));
    if (!sourceProfile)
        sourceProfile.reset(cmsCreate_sRGBProfile());
    if (!sourceProfile)
        return false;

    CmsProfile outputProfile(monitorIcc.empty() ?
        cmsCreate_sRGBProfile() :
        cmsOpenProfileFromMem(monitorIcc.data(), static_cast<cmsUInt32Number>(monitorIcc.size())));
    if (!outputProfile)
        outputProfile.reset(cmsCreate_sRGBProfile());
    if (!outputProfile)
        return false;

    CmsTransform transform(cmsCreateTransform(
        sourceProfile,
        pixelType,
        outputProfile,
        pixelType,
        INTENT_PERCEPTUAL,
        mat.channels() == 4 ? cmsFLAGS_COPY_ALPHA : 0));
    if (!transform)
        return false;

    // 一亿像素的图单线程做变换要两三百毫秒，而这一步在「用户已经等完解码」之后，
    // 直接顶在出图时间上。lcms2 的 transform 句柄可以被多线程共用（只要没人去改它），
    // 各线程处理互不重叠的行，结果与单线程逐行调用完全一致。
    //
    // 小图不值得分线程：调度开销比变换本身还大，所以设个像素数门槛。
    constexpr std::size_t PARALLEL_PIXEL_THRESHOLD = 2u << 20;   // 约 200 万像素
    const std::size_t totalPixels = mat.total();

    if (totalPixels < PARALLEL_PIXEL_THRESHOLD) {
        if (mat.isContinuous()) {
            cmsDoTransform(transform, mat.ptr(), mat.ptr(),
                static_cast<cmsUInt32Number>(totalPixels));
        }
        else {
            for (int y = 0; y < mat.rows; ++y)
                cmsDoTransform(transform, mat.ptr(y), mat.ptr(y),
                    static_cast<cmsUInt32Number>(mat.cols));
        }
        return true;
    }

    // 按行分块：一行一次调用太碎（一亿像素的图有上万行），按块调用开销可以忽略。
    const int rowsPerBlock = std::max(1, mat.rows / 64);
    const int blockCount = (mat.rows + rowsPerBlock - 1) / rowsPerBlock;
    const cmsHTRANSFORM rawTransform = transform;   // CmsTransform 有到句柄的隐式转换
    concurrency::parallel_for(0, blockCount, [&](int block) {
        const int firstRow = block * rowsPerBlock;
        const int lastRow = std::min(firstRow + rowsPerBlock, mat.rows);
        for (int y = firstRow; y < lastRow; ++y) {
            cmsDoTransform(rawTransform, mat.ptr(y), mat.ptr(y),
                static_cast<cmsUInt32Number>(mat.cols));
        }
    });
    return true;
}

void ColorManager::applyToImageAsset(ImageAsset& imageAsset) {
    if (!GlobalVar::settingParameter.enableColorManagement)
        return;

    std::vector<uint8_t>& monitorIcc = readMonitorIccProfileCached();
    applyToMat(imageAsset.primaryFrame, imageAsset.iccProfile, monitorIcc);
}
