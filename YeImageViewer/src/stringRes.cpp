#include "jarkUtils.h"

// 界面字符串表最大条目数
constexpr uint32_t STRING_MAX_NUM = 1024;

// 界面字符串表：0 简体中文，1 English，2 繁體中文
// 繁體用台湾习惯用词（设置→設定、文件→檔案、打印→列印、幻灯片→投影片），
// 不是简单的字形转换；设置/帮助/关于页里那些带字的资源切图仍沿用简体那套，
// 那是位图不是字符串，换掉要重做美术资源。
// 条目所在行号减10即为 stringID
// 因为使用索引是硬编码，所以不要随意在中间增减条目，会打乱索引，只能在后面追加条目
std::string_view UIStringTable[STRING_MAX_NUM][3] = {
    {"NULL", "NULL", "NULL"},
    {"设置", "Settings", "設定"},
    {"常规", "General", "一般"},
    {"文件关联", "Association", "檔案關聯"},
    {"快捷键", "Shortcuts", "快速鍵"},
    {"关于", "About", "關於"},
    {"常见格式", "Common Formats", "常見格式"},
    {"选择常用", "Select Common", "選擇常用"},
    {"全选", "Select All", "全選"},
    {"全不选", "Clear All", "全不選"},
    {"立即关联", "Apply", "立即關聯"},  // 10
    {"本软件原生绿色单文件，请把软件放置到合适位置再关联文件格式，若软件位置变化则需重新关联。\n若不再使用本软件，请点击【全不选】再点击【立即关联】即可移除所有关联关系。", "This software is a portable single file.  Please place the software in an appropriate location before associating file formats.\nIf you no longer to use this software,  please click \"Clear All\" and then click \"Apply\" to remove all associations." , "本軟體是原生綠色單一檔案，請先把軟體放到合適位置再關聯檔案格式；軟體位置變動後需要重新關聯。\n若不再使用本軟體，請點選【全不選】再點選【立即關聯】即可移除所有關聯。"},
    {"旋转动画", "Rotate Animation", "旋轉動畫"},
    {"缩放动画", "Zoom Animation", "縮放動畫"},
    {"删除前提示", "Confirm Before Delete", "刪除前提示"},
    {"ICC色彩管理", "ICC Color Management", "ICC色彩管理"},
    {"切换动画模式", "Switch Animation Mode", "切換動畫模式"},
    {"幻灯片顺序", "Slideshow Order", "投影片順序"},
    {"幻灯片间隔(秒)", "Slideshow Interval (seconds)", "投影片間隔(秒)"},
    {"编译时间 UTC+8", "[Build time UTC+8]", "編譯時間 UTC+8"},
    {"切图动画", "SwitchAnim", "切圖動畫"},  // 20
    {"无动画", "Off", "無動畫"},
    {"上下滑动", "Vertical", "上下滑動"},
    {"左右滑动", "Horizontal", "左右滑動"},
    {"主题", "Theme", "主題"},
    {"跟随系统", "System", "跟隨系統"},
    {"浅色", "Light", "淺色"},
    {"深色", "Dark", "深色"},
    {"语言", "Language", "語言"},
    {"打印", "Print", "列印"},
    {"简体中文", "简体中文", "简体中文"},  // 30
    {"English", "English", "English"},
    {"使用Ctrl+O或拖入图像文件打开", "Use Ctrl+O or drag the image file to open.", "使用 Ctrl+O 或拖入影像檔案開啟"},
    {"图像格式不支持", "Image format not supported", "不支援的影像格式"},
    {"YeImageViewer 看图", "YeImageViewer", "YeImageViewer 看圖"},
    {"错误", "Error", "錯誤"},
    {"鼠标右键", "RightClick", "滑鼠右鍵"},
    {"菜单", "Menu", "選單"},
    {"退出程序", "Exit", "結束程式"},
    {"路径", "Path", "路徑"},
    {"大小", "FileSize", "大小"},  // 40
    {"分辨率", "Resolution", "解析度"},
    {"【按 C 键复制图像全部信息】", "[Press C to copy all image information]", "【按 C 鍵複製影像全部資訊】"},
    {"\n正提示词: ", "\nPositive prompt: ", "\n正向提示詞: "},
    {"\n\n反提示词: ", "\n\nNegative prompt: ", "\n\n負向提示詞: "},
    {"\n\n参数: Steps:", "\n\nParameter: Steps:", "\n\n參數: Steps:"},
    {"\n\nAI生图提示词:\n", "\n\nAI-generated image prompt:\n", "\n\nAI 生圖提示詞:\n"},
    {"北纬 N", "North Latitude", "北緯 N"},
    {"南纬 S", "South Latitude", "南緯 S"},
    {"东经 E", "East Longitude", "東經 E"},
    {"西经 W", "West Longitude", "西經 W"},  // 50
    {"子图数量", "Number of subImage", "子圖數量"},
    {"\n\nAI生图提示词 ComfyUI工作流.json\n", "\n\nAI-generated image prompt ComfyUI_workflow.json\n", "\n\nAI 生圖提示詞 ComfyUI工作流程.json\n"},
    {"\n方向: ", "\nExif.Image.Orientation: ", "\n方向: "},
    {"优先1:1显示", "Prefer 1:1 Display", "優先 1:1 顯示"},
    {"Esc关闭图片", "Close Image with Esc", "Esc關閉圖片"},  // 已停用：改为快捷键「关闭图片」，占位保持后面的索引不变
    {"记住最后使用的显示器", "Remember Last Monitor", "記住最後使用的螢幕"},
    {"打开图片方式", "Open Image As", "開啟圖片方式"},
    {"沉浸预览", "Immersive", "沉浸預覽"},
    {"窗口适应图片", "Fit to Image", "視窗適應圖片"},
    {"记住上次大小", "Last Size", "記住上次大小"},  // 60
    {"双击图片", "Double-Click", "按兩下圖片"},
    {"切换全屏", "Toggle Fullscreen", "切換全螢幕"},
    {"最大化/还原", "Maximize/Restore", "最大化/還原"},
    {"无动作", "Do Nothing", "無動作"},
    {"两侧翻页箭头", "Edge Paging Arrows", "兩側翻頁箭頭"},
    {"隐藏", "Hidden", "隱藏"},
    {"显示", "Shown", "顯示"},
    {"拖动图片", "Dragging the Image", "拖曳圖片"},
    {"仅平移图片", "Always Pan", "僅平移圖片"},
    {"未放大时移动窗口", "Move Window", "未放大時移動視窗"},  // 70
    {"正在加载", "Loading", "正在載入"},
    {"原图加载中", "Loading full image", "原圖載入中"},
    {"信息面板直方图", "Info Histogram", "資訊面板直方圖"},
    {"信息面板透明度", "Info Opacity", "資訊面板透明度"},
    {"最透", "Light", "最透"},
    {"较透", "Medium", "較透"},
    {"默认", "Strong", "預設"},
    {"不透明", "Opaque", "不透明"},
    {"实况自动播放", "Live Autoplay", "實況自動播放"},
    {"静音", "Muted", "靜音"},
    {"有声", "With Sound", "有聲"},
    {"实况", "LIVE", "實況"},  // 实况照片左上角的标记
    {"图片适应窗口", "Fit in Window", "圖片適應視窗"},  // 打开方式第四项：图片缩进窗口，一眼看完
    {"下一张", "Next Image", "下一張"},  // 双击动作第四项
    {"全屏信息条", "Fullscreen Info Bar", "全螢幕資訊列"},  // 全屏时左上角显示标题那一行
    {"繁體中文", "繁體中文", "繁體中文"},  // 86  语言单选的第三项
};


std::wstring_view UIStringTableW[STRING_MAX_NUM][3] = {
    {L"NULL", L"NULL", L"NULL"},
    {L"YeImageViewer 看图", L"YeImageViewer", L"YeImageViewer 看圖"},
    {L"文件关联设置成功！", L"Association successful!", L"檔案關聯設定成功！"},
    {L"文件关联设置失败！", L"Association failed!", L"檔案關聯設定失敗！"},
    {L"保存当前帧到图像文件", L"Save the current frame to an image file", L"儲存目前影格到影像檔案"},
    {L"是否要将此动图或实况图视频的全部帧批量保存到png图片文件？\n\n帧数：", L"Batch save each frames of this animated image to PNG image files? \n\nFrames:", L"是否要將此動圖或實況影片的全部影格批次儲存為 PNG 圖片檔？\n\n影格數："},  // 5
    {L"保存每一帧到原图所在文件夹", L"Save each frame to the folder containing the current image", L"儲存每一影格到原圖所在資料夾"},
    {L"确定要将以下文件移至回收站吗？", L"Move the following files to the recycle bin?", L"確定要將以下檔案移至資源回收筒嗎？"},
    {L"删除失败，错误码", L"Deletion failed, error code", L"刪除失敗，錯誤碼"},
    {L"逐帧浏览", L"Frame by frame", L"逐格瀏覽"},
    {L"逆时针旋转90°", L"Rotate 90° counterclockwise", L"逆時針旋轉90°"},  // 10
    {L"顺时针旋转90°", L"Rotate 90° clockwise", L"順時針旋轉90°"},
    {L"旋转180°", L"Rotate 180°", L"旋轉180°"},
    {L"窗口创建失败！", L"Window creation failed!", L"視窗建立失敗！"},
    {L"错误", L"Error", L"錯誤"},
    {L"提示", L"Tips", L"提示"},  // 15
    {L"图像分辨率太大，将缩放到", L"Resolution is too high, it will be scaled down to", L"影像解析度太大，將縮放到"},
    {L"图像为空，无法复制到剪贴板", L"The image is empty and cannot be copied to the clipboard.", L"影像為空，無法複製到剪貼簿"},
    {L"图像通道: ", L"Channel: ", L"影像通道: "},
    {L"不支持的图像格式", L"Unsupported image formats", L"不支援的影像格式"},
    {L"图像格式转换失败", L"Image format conversion failed", L"影像格式轉換失敗"},  // 20
    {L"无法打开剪贴板", L"Unable to open clipboard", L"無法開啟剪貼簿"},
    {L"清空剪贴板失败", L"Clearing clipboard failed", L"清除剪貼簿失敗"},
    {L"保存到图像文件", L"Save to image file", L"儲存為影像檔案"},
    {L"XX", L"XX", L"XX"},
    {L"复制EXIF信息 (&E)", L"Copy &EXIF info", L"複製EXIF資訊 (&E)"},  // 25
    {L"复制文件路径 (&P)", L"Copy file &path", L"複製檔案路徑 (&P)"},
    {L"复制图像数据 (&C)", L"&Copy image data", L"複製影像資料 (&C)"},
    {L"显示EXIF信息 (&I)", L"Show EXIF &info", L"顯示EXIF資訊 (&I)"},
    {L"打开所在位置 (&L)", L"Open file &location", L"開啟所在位置 (&L)"},
    {L"删除到回收站 (&D)", L"Move to recycle bin", L"刪除到資源回收筒 (&D)"},  // 30
    {L"打印 (&P)", L"&Print", L"列印 (&P)"},
    {L"设置 (&S)", L"&Settings", L"設定 (&S)"},
    {L"关于 (&A)", L"&About", L"關於 (&A)"},
    {L"退出 (&X)", L"E&xit", L"結束 (&X)"},
    {L"打开新图像 (&O)", L"&Open new image", L"開啟新影像 (&O)"},  // 35
    {L"文件属性 (&A)", L"File properties", L"檔案內容 (&A)"},
    {L"快捷键 (&K)", L"Shortcuts (&K)", L"快速鍵 (&K)"},
    {L"全屏 (&F)", L"&FullScreen", L"全螢幕 (&F)"},
    {L"设置", L"Settings", L"設定"},
    {L"打印", L"Print", L"列印"},  // 40
    {L"文件关联已完成，但缩略图扩展注册失败。部分格式可能无法在资源管理器中显示 YeImageViewer 缩略图。", L"Association completed, but thumbnail extension registration failed. Some formats may not show YeImageViewer thumbnails in File Explorer.", L"檔案關聯已完成，但縮圖擴充註冊失敗。部分格式可能無法在檔案總管中顯示 YeImageViewer 縮圖。"},
    {L"背景颜色", L"Background", L"背景顏色"},
    {L"透明", L"Transparent", L"透明"},
    {L"白色", L"White", L"白色"},
    {L"黑色", L"Black", L"黑色"},
    {L"毛玻璃", L"Frosted glass", L"毛玻璃"},
    {L"重命名 (&R)", L"&Rename", L"重新命名 (&R)"},
    {L"重命名图片", L"Rename image", L"重新命名圖片"},
    {L"请输入新的文件名：", L"Enter a new file name:", L"請輸入新的檔案名稱："},
    {L"文件名不能为空。", L"The file name cannot be empty.", L"檔案名稱不能為空。"},  // 50
    {L"文件名不能包含以下字符：<>:\"/\\|?*", L"The file name cannot contain: <>:\"/\\|?*", L"檔案名稱不能包含以下字元：<>:\"/\\|?*"},
    {L"文件名不能以空格或句点结尾。", L"The file name cannot end with a space or period.", L"檔案名稱不能以空格或句點結尾。"},
    {L"该文件名是 Windows 保留名称，请使用其他名称。", L"This name is reserved by Windows. Choose another name.", L"此名稱是 Windows 保留名稱，請使用其他名稱。"},
    {L"文件名过长。", L"The file name is too long.", L"檔案名稱過長。"},
    {L"同名文件已存在，请使用其他名称。", L"A file with that name already exists.", L"同名檔案已存在，請使用其他名稱。"},  // 55
    {L"重命名失败，错误码", L"Rename failed, error code", L"重新命名失敗，錯誤碼"},
    {L"编辑图片 (&E)", L"&Edit image", L"編輯圖片 (&E)"},
    {L"在", L"Open in", L"在"},
    {L"选择并设置应用... (&C)", L"&Choose and configure application...", L"選擇並設定應用程式... (&C)"},
    {L"无法启动外部图片编辑器，错误码", L"Unable to start the external image editor, error code", L"無法啟動外部圖片編輯器，錯誤碼"},  // 60
};

// 获取字符串
// 旧版本的设置文件里 UI_LANG 只可能是 0 或 1；万一读到越界值，
// 这里兜底回简体，而不是拿着野索引去读表。
static uint32_t currentLanguage() {
    const uint32_t language = GlobalVar::settingParameter.UI_LANG;
    return language < UI_LANG_COUNT ? language : 0;
}

const char* const getUIString(const uint32_t stringidx) {
    if (stringidx >= STRING_MAX_NUM)
        return "NULL";
    return UIStringTable[stringidx][currentLanguage()].data();
}

const wchar_t* const getUIStringW(const uint32_t stringidx) {
    if (stringidx >= STRING_MAX_NUM)
        return L"NULL";
    return UIStringTableW[stringidx][currentLanguage()].data();
}
