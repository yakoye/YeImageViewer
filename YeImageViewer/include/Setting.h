#pragma once

#include "BuildInfo.h"
#include "FileAssociationManager.h"
#include "MatWindow.h"
#include "SettingLayout.h"
#include "TextDrawer.h"

#include <array>
#include <cctype>

// TODO: 为 YeImageViewer 增加独立的更新检查。

extern std::wstring_view appVersion;
extern std::wstring_view RepositoryLink;

struct generalTabCheckBox {
    cv::Rect rect{};
    int stringID = 0;
    bool* valuePtr = nullptr;
};

struct generalTabRadio {
    cv::Rect rect{};
    std::vector<int> stringIDs;
    uint32_t* valuePtr = nullptr;
};

class Setting : public MatWindow {
private:
    static constexpr int winWidth = SettingLayout::CANVAS_WIDTH;
    static constexpr int winHeight = SettingLayout::CANVAS_HEIGHT;
    static constexpr int tabHeight = SettingLayout::TAB_HEIGHT;
    static constexpr int tabWidth = SettingLayout::TAB_WIDTH;
    static inline const wchar_t* windowsClassName = L"YeImageViewerSettingWnd";
    static inline constexpr std::wstring_view upstreamRepository =
        L"https://github.com/jark006/JarkViewer";

    static inline std::vector<string> allSupportExt;
    static inline std::set<string> checkedExt;
    static inline std::vector<generalTabCheckBox> generalTabCheckBoxList;
    static inline std::vector<generalTabRadio> generalTabRadioList;

    TextDrawer textDrawer;
    cv::Mat winCanvas;
    std::array<int, 4> scrollOffsets{};
    std::string associationFilter;
    bool associationSearchActive = false;

    uint32_t primaryText() const {
        return GlobalVar::isCurrentUIDarkMode ?
            GlobalVar::currentTheme.FG : GlobalVar::currentTheme.FG_DEEP;
    }

    uint32_t secondaryText() const {
        return GlobalVar::currentTheme.FG;
    }

    static cv::Rect toCvRect(const SettingLayout::Rect& rect) {
        return { rect.x, rect.y, rect.width, rect.height };
    }

    static void fillRoundedRect(cv::Mat& canvas, const cv::Rect& rect,
        uint32_t color, int radius = 10) {
        const cv::Scalar scalar = jarkUtils::to_cv_scalar(color);
        radius = std::clamp(radius, 0, std::min(rect.width, rect.height) / 2);
        if (radius == 0) {
            cv::rectangle(canvas, rect, scalar, -1);
            return;
        }
        cv::rectangle(canvas,
            { rect.x + radius, rect.y, rect.width - radius * 2, rect.height }, scalar, -1);
        cv::rectangle(canvas,
            { rect.x, rect.y + radius, rect.width, rect.height - radius * 2 }, scalar, -1);
        cv::circle(canvas, { rect.x + radius, rect.y + radius }, radius, scalar, -1);
        cv::circle(canvas, { rect.x + rect.width - radius - 1, rect.y + radius }, radius, scalar, -1);
        cv::circle(canvas, { rect.x + radius, rect.y + rect.height - radius - 1 }, radius, scalar, -1);
        cv::circle(canvas,
            { rect.x + rect.width - radius - 1, rect.y + rect.height - radius - 1 }, radius, scalar, -1);
    }

    void drawCard(cv::Mat& canvas, const cv::Rect& rect) {
        fillRoundedRect(canvas, rect, GlobalVar::currentTheme.BG, 10);
        cv::rectangle(canvas, rect, jarkUtils::to_cv_scalar(GlobalVar::currentTheme.BG_TAG), 1);
    }

    void drawSectionTitle(cv::Mat& canvas, const cv::Rect& card, const char* title) {
        textDrawer.putAlignLeft(canvas,
            { card.x + 18, card.y + 10, 180, 28 }, title, GlobalVar::currentTheme.CHECK);
        cv::line(canvas, { card.x + 150, card.y + 25 },
            { card.x + card.width - 18, card.y + 25 },
            jarkUtils::to_cv_scalar(GlobalVar::currentTheme.BG_TAG), 1);
    }

    void Init(int tabIdx = 0) {
        textDrawer.setSize(SettingLayout::FONT_SIZE);
        winCanvas = cv::Mat(winHeight, winWidth, CV_8UC4,
            jarkUtils::to_cv_scalar(GlobalVar::currentTheme.BG_DEEP));
        curTabIdx = std::clamp(tabIdx, 0, 3);

        if (generalTabCheckBoxList.empty()) {
            generalTabCheckBoxList = {
                { toCvRect(SettingLayout::GENERAL_CHECK_BOXES[0]), 12, &GlobalVar::settingParameter.isAllowRotateAnimation },
                { toCvRect(SettingLayout::GENERAL_CHECK_BOXES[1]), 13, &GlobalVar::settingParameter.isAllowZoomAnimation },
                { toCvRect(SettingLayout::GENERAL_CHECK_BOXES[2]), 14, &GlobalVar::settingParameter.isNoteBeforeDelete },
                { toCvRect(SettingLayout::GENERAL_CHECK_BOXES[3]), 15, &GlobalVar::settingParameter.enableColorManagement },
                { toCvRect(SettingLayout::GENERAL_CHECK_BOXES[4]), 54, &GlobalVar::settingParameter.isOneToOnePreferred },
                { toCvRect(SettingLayout::GENERAL_CHECK_BOXES[5]), 55, &GlobalVar::settingParameter.escapeClosesImage },
                { toCvRect(SettingLayout::GENERAL_CHECK_BOXES[6]), 56, &GlobalVar::settingParameter.rememberLastMonitor },
            };
            generalTabRadioList = {
                { toCvRect(SettingLayout::GENERAL_RADIOS[0]), {20, 21, 22, 23}, &GlobalVar::settingParameter.switchImageAnimationMode },
                { toCvRect(SettingLayout::GENERAL_RADIOS[1]), {24, 25, 26, 27}, &GlobalVar::settingParameter.UI_Mode },
                { toCvRect(SettingLayout::GENERAL_RADIOS[2]), {28, 30, 31}, &GlobalVar::settingParameter.UI_LANG },
                { toCvRect(SettingLayout::GENERAL_RADIOS[3]), {36, 37, 38}, &GlobalVar::settingParameter.rightClickAction },
            };
        }

        if (allSupportExt.empty()) {
            std::set<wstring> allSupportExtW;
            allSupportExtW.insert(ImageDatabase::supportExt.begin(), ImageDatabase::supportExt.end());
            allSupportExtW.insert(ImageDatabase::supportRaw.begin(), ImageDatabase::supportRaw.end());
            for (const auto& ext : allSupportExtW)
                allSupportExt.emplace_back(jarkUtils::wstringToUtf8(ext));
        }

        const auto checkedExtVec = jarkUtils::splitString(
            GlobalVar::settingParameter.extCheckedListStr, ",");
        checkedExt.clear();
        for (const auto& ext : checkedExtVec) {
            if (!ext.empty())
                checkedExt.insert(ext);
        }
    }

    std::vector<std::string_view> filteredExtensions() const {
        std::vector<std::string_view> result;
        for (const auto& ext : allSupportExt) {
            if (associationFilter.empty() || ext.find(associationFilter) != string::npos)
                result.emplace_back(ext);
        }
        return result;
    }

    int associationGridRows() const {
        const int count = static_cast<int>(filteredExtensions().size());
        return std::max(1, (count + SettingLayout::ASSOCIATION_GRID_COLUMNS - 1) /
            SettingLayout::ASSOCIATION_GRID_COLUMNS);
    }

    int associationDescriptionY() const {
        return SettingLayout::ASSOCIATION_GRID_Y + associationGridRows() *
            (SettingLayout::ASSOCIATION_TAG_HEIGHT + SettingLayout::ASSOCIATION_TAG_GAP_Y) + 12;
    }

    int associationButtonsY() const {
        return associationDescriptionY() + 76;
    }

    int associationContentHeight() const {
        return associationButtonsY() + SettingLayout::ASSOCIATION_BUTTON_HEIGHT + 20;
    }

    int contentHeightForTab(int tab) const {
        switch (tab) {
        case 0: return SettingLayout::GENERAL_CONTENT_HEIGHT;
        case 1: return associationContentHeight();
        case 2: return SettingLayout::HELP_CONTENT_HEIGHT;
        default: return SettingLayout::ABOUT_CONTENT_HEIGHT;
        }
    }

    std::array<cv::Rect, 4> associationButtonRects() const {
        constexpr int gap = 8;
        constexpr int width = (SettingLayout::CARD_WIDTH - gap * 3) / 4;
        std::array<cv::Rect, 4> result{};
        for (int i = 0; i < 4; ++i) {
            result[i] = {
                SettingLayout::PAGE_PADDING + i * (width + gap), associationButtonsY(),
                width, SettingLayout::ASSOCIATION_BUTTON_HEIGHT };
        }
        return result;
    }

    void drawToggle(cv::Mat& canvas, const generalTabCheckBox& toggle) {
        const cv::Rect track{ toggle.rect.x, toggle.rect.y + 6, 40, 20 };
        fillRoundedRect(canvas, track,
            *toggle.valuePtr ? GlobalVar::currentTheme.CHECK : GlobalVar::currentTheme.BG_TAG, 10);
        const int knobX = *toggle.valuePtr ? track.x + 22 : track.x + 2;
        cv::circle(canvas, { knobX + 8, track.y + 10 }, 8,
            jarkUtils::to_cv_scalar(*toggle.valuePtr ? 0xFFFFFFFFu : GlobalVar::currentTheme.FG), -1);
        textDrawer.putAlignLeft(canvas,
            { toggle.rect.x + 52, toggle.rect.y, toggle.rect.width - 52, toggle.rect.height },
            getUIString(toggle.stringID),
            *toggle.valuePtr ? primaryText() : secondaryText());
    }

    void drawSegment(cv::Mat& canvas, const generalTabRadio& radio) {
        const int selected = std::min<int>(*radio.valuePtr,
            static_cast<int>(radio.stringIDs.size()) - 2);
        constexpr int labelWidth = 138;
        const cv::Rect segments{ radio.rect.x + labelWidth, radio.rect.y + 5,
            radio.rect.width - labelWidth, radio.rect.height - 10 };
        textDrawer.putAlignLeft(canvas,
            { radio.rect.x, radio.rect.y, labelWidth - 10, radio.rect.height },
            getUIString(radio.stringIDs.front()), secondaryText());
        fillRoundedRect(canvas, segments, GlobalVar::currentTheme.BG_DEEP, 7);
        cv::rectangle(canvas, segments,
            jarkUtils::to_cv_scalar(GlobalVar::currentTheme.BG_TAG), 1);
        const int optionCount = static_cast<int>(radio.stringIDs.size()) - 1;
        const int itemWidth = segments.width / optionCount;
        const cv::Rect selectedRect{ segments.x + selected * itemWidth + 2,
            segments.y + 2, itemWidth - 4, segments.height - 4 };
        fillRoundedRect(canvas, selectedRect, GlobalVar::currentTheme.CHECK, 5);
        for (int index = 0; index < optionCount; ++index) {
            textDrawer.putAlignCenter(canvas,
                { segments.x + index * itemWidth, segments.y, itemWidth, segments.height },
                getUIString(radio.stringIDs[index + 1]),
                index == selected ? 0xFFFFFFFFu : primaryText());
        }
    }

    void refreshGeneralTab(cv::Mat& page) {
        const bool chinese = GlobalVar::settingParameter.UI_LANG == 0;
        drawCard(page, toCvRect(SettingLayout::GENERAL_BEHAVIOR_CARD));
        drawSectionTitle(page, toCvRect(SettingLayout::GENERAL_BEHAVIOR_CARD),
            chinese ? "行为" : "BEHAVIOR");
        for (const auto& item : generalTabCheckBoxList)
            drawToggle(page, item);

        drawCard(page, toCvRect(SettingLayout::GENERAL_DISPLAY_CARD));
        drawSectionTitle(page, toCvRect(SettingLayout::GENERAL_DISPLAY_CARD),
            chinese ? "显示与交互" : "DISPLAY & INPUT");
        for (const auto& radio : generalTabRadioList)
            drawSegment(page, radio);
    }

    void refreshAssociateTab(cv::Mat& page) {
        const bool chinese = GlobalVar::settingParameter.UI_LANG == 0;
        const cv::Rect search = toCvRect(SettingLayout::ASSOCIATION_SEARCH);
        drawCard(page, search);
        cv::circle(page, { search.x + 20, search.y + search.height / 2 - 2 }, 6,
            jarkUtils::to_cv_scalar(secondaryText()), 2);
        cv::line(page, { search.x + 25, search.y + search.height / 2 + 3 },
            { search.x + 31, search.y + search.height / 2 + 9 },
            jarkUtils::to_cv_scalar(secondaryText()), 2);
        const std::string searchText = associationFilter.empty() ?
            (associationSearchActive ? "|" : (chinese ? "搜索格式..." : "Search formats...")) :
            associationFilter + (associationSearchActive ? "|" : "");
        textDrawer.putAlignLeft(page,
            { search.x + 42, search.y, search.width - 170, search.height },
            searchText.c_str(), associationFilter.empty() ?
            secondaryText() : primaryText());
        const std::string countText = std::format("{} / {}", checkedExt.size(), allSupportExt.size());
        textDrawer.putAlignCenter(page,
            { search.x + search.width - 120, search.y, 100, search.height },
            countText.c_str(), secondaryText());

        const auto visible = filteredExtensions();
        for (int index = 0; index < static_cast<int>(visible.size()); ++index) {
            const int column = index % SettingLayout::ASSOCIATION_GRID_COLUMNS;
            const int row = index / SettingLayout::ASSOCIATION_GRID_COLUMNS;
            const cv::Rect rect{
                SettingLayout::ASSOCIATION_GRID_X + column *
                    (SettingLayout::ASSOCIATION_TAG_WIDTH + SettingLayout::ASSOCIATION_TAG_GAP_X),
                SettingLayout::ASSOCIATION_GRID_Y + row *
                    (SettingLayout::ASSOCIATION_TAG_HEIGHT + SettingLayout::ASSOCIATION_TAG_GAP_Y),
                SettingLayout::ASSOCIATION_TAG_WIDTH,
                SettingLayout::ASSOCIATION_TAG_HEIGHT };
            const bool active = checkedExt.contains(std::string(visible[index]));
            fillRoundedRect(page, rect,
                active ? GlobalVar::currentTheme.BG_TAG : GlobalVar::currentTheme.BG, 5);
            cv::rectangle(page, rect,
                jarkUtils::to_cv_scalar(active ? GlobalVar::currentTheme.CHECK : GlobalVar::currentTheme.BG_TAG), 1);
            textDrawer.putAlignCenter(page, rect, visible[index].data(),
                active ? primaryText() : secondaryText());
        }

        textDrawer.putAlignLeft(page,
            { 24, associationDescriptionY(), winWidth - 48, 62 }, getUIString(11),
            secondaryText());
        const auto buttons = associationButtonRects();
        constexpr std::array<int, 4> textIDs{ 7, 8, 9, 10 };
        for (int index = 0; index < 4; ++index) {
            fillRoundedRect(page, buttons[index],
                index == 3 ? GlobalVar::currentTheme.CHECK : GlobalVar::currentTheme.BG_TAG, 7);
            textDrawer.putAlignCenter(page, buttons[index], getUIString(textIDs[index]),
                index == 3 ? 0xFFFFFFFFu : primaryText());
        }
    }

    struct HelpItem {
        const char* actionZH;
        const char* actionEN;
        const char* descriptionZH;
        const char* descriptionEN;
        const char* keysZH;
        const char* keysEN;
    };

    void refreshHelpTab(cv::Mat& page) {
        static constexpr std::array<HelpItem, 12> items{
            HelpItem{ "切换图片", "Switch", "上一张或下一张", "Previous or next image", "Ctrl+滚轮  左右键", "Ctrl+wheel  arrows" },
            HelpItem{ "缩放图片", "Zoom", "放大或缩小", "Zoom image", "滚轮  上下键", "Wheel  Up/Down" },
            HelpItem{ "旋转图片", "Rotate", "向左或向右旋转", "Rotate left or right", "Q  E", "Q  E" },
            HelpItem{ "平移图片", "Pan", "拖动查看区域", "Move viewport", "Shift+滚轮  拖动", "Shift+wheel  drag" },
            HelpItem{ "全屏显示", "Fullscreen", "进入或退出全屏", "Toggle fullscreen", "F  F11  双击", "F  F11  double click" },
            HelpItem{ "退出沉浸", "Leave immersive", "恢复普通窗口", "Restore framed window", "Esc  点击背景", "Esc  click background" },
            HelpItem{ "图像信息", "Image info", "显示精简信息卡", "Toggle information card", "Tab  I  中键", "Tab  I  middle click" },
            HelpItem{ "复制图像", "Copy image", "复制到剪贴板", "Copy to clipboard", "Ctrl+C", "Ctrl+C" },
            HelpItem{ "打印图像", "Print", "打印当前图片", "Print current image", "Ctrl+P", "Ctrl+P" },
            HelpItem{ "分解动图", "Split animation", "导出所有帧", "Export all frames", "Ctrl+S", "Ctrl+S" },
            HelpItem{ "逐帧浏览", "Browse frames", "向后、暂停、向前", "Back, pause, forward", "J  K  L", "J  K  L" },
            HelpItem{ "播放暂停", "Play or pause", "切换动画播放", "Toggle animation", "空格键", "Space" },
        };
        static constexpr std::array<const char*, 4> groupsZH{
            "图片浏览", "界面控制", "编辑与操作", "动画播放" };
        static constexpr std::array<const char*, 4> groupsEN{
            "IMAGE BROWSING", "INTERFACE", "EDIT & ACTIONS", "ANIMATION" };
        const bool chinese = GlobalVar::settingParameter.UI_LANG == 0;

        drawCard(page, { 20, 20, 580, SettingLayout::HELP_CONTENT_HEIGHT - 40 });
        const cv::Rect header = toCvRect(SettingLayout::HELP_HEADER);
        cv::rectangle(page, header, jarkUtils::to_cv_scalar(GlobalVar::currentTheme.BG_TAG), -1);
            textDrawer.putAlignLeft(page, { 36, header.y, 100, header.height },
            chinese ? "功能" : "ACTION", secondaryText());
        textDrawer.putAlignLeft(page, { 154, header.y, 210, header.height },
            chinese ? "说明" : "DESCRIPTION", secondaryText());
        textDrawer.putAlignLeft(page, { 374, header.y, 210, header.height },
            chinese ? "快捷键" : "SHORTCUTS", secondaryText());

        constexpr std::array<int, 4> groupStarts{ 0, 4, 7, 10 };
        for (int group = 0; group < 4; ++group) {
            const cv::Rect groupRect = toCvRect(SettingLayout::HELP_GROUP_HEADERS[group]);
            cv::rectangle(page, groupRect,
                jarkUtils::to_cv_scalar(GlobalVar::currentTheme.BG_DEEP), -1);
            cv::circle(page, { groupRect.x + 16, groupRect.y + groupRect.height / 2 }, 3,
                jarkUtils::to_cv_scalar(GlobalVar::currentTheme.CHECK), -1);
            textDrawer.putAlignLeft(page,
                { groupRect.x + 28, groupRect.y, groupRect.width - 30, groupRect.height },
                chinese ? groupsZH[group] : groupsEN[group], GlobalVar::currentTheme.CHECK);
        }

        for (int index = 0; index < static_cast<int>(items.size()); ++index) {
            const cv::Rect row = toCvRect(SettingLayout::HELP_ITEMS[index]);
            cv::line(page, { row.x, row.y }, { row.x + row.width, row.y },
                jarkUtils::to_cv_scalar(GlobalVar::currentTheme.BG_TAG), 1);
            textDrawer.putAlignLeft(page, { row.x + 16, row.y, 112, row.height },
                chinese ? items[index].actionZH : items[index].actionEN,
                primaryText());
            textDrawer.putAlignLeft(page, { row.x + 134, row.y, 214, row.height },
                chinese ? items[index].descriptionZH : items[index].descriptionEN,
                secondaryText());
            const cv::Rect keyRect{ row.x + 354, row.y + 8, 208, row.height - 16 };
            fillRoundedRect(page, keyRect, GlobalVar::currentTheme.BG_DEEP, 5);
            textDrawer.putAlignCenter(page, keyRect,
                chinese ? items[index].keysZH : items[index].keysEN,
                secondaryText());
        }
    }

    void refreshAboutTab(cv::Mat& page) {
        const bool chinese = GlobalVar::settingParameter.UI_LANG == 0;
        const cv::Rect hero = toCvRect(SettingLayout::ABOUT_HERO_CARD);
        drawCard(page, hero);
        const cv::Rect icon{ hero.x + hero.width / 2 - 32, hero.y + 34, 64, 64 };
        fillRoundedRect(page, icon, GlobalVar::currentTheme.CHECK, 14);
        textDrawer.putAlignCenter(page, icon, "Y", 0xFFFFFFFFu);
        textDrawer.putAlignCenter(page, { hero.x + 40, hero.y + 112, hero.width - 80, 42 },
            "YeImageViewer", primaryText());
        const cv::Rect versionRect{ hero.x + hero.width / 2 - 72, hero.y + 160, 144, 34 };
        fillRoundedRect(page, versionRect, GlobalVar::currentTheme.BG_TAG, 17);
        textDrawer.putAlignCenter(page, versionRect,
            jarkUtils::wstringToUtf8(appVersion).c_str(), GlobalVar::currentTheme.CHECK);
        textDrawer.putAlignCenter(page, { hero.x + 30, hero.y + 208, hero.width - 60, 36 },
            chinese ? "基于 JarkViewer 开发  ·  GNU GPL v3" :
                "Based on JarkViewer  ·  GNU GPL v3",
            secondaryText());
        textDrawer.putAlignCenter(page, { hero.x + 30, hero.y + 254, hero.width - 60, 34 },
            chinese ? "作者  yakoye" : "Author  yakoye", secondaryText());

        cv::line(page, { hero.x + 30, hero.y + 300 },
            { hero.x + hero.width - 30, hero.y + 300 },
            jarkUtils::to_cv_scalar(GlobalVar::currentTheme.BG_TAG), 1);
        const auto buildTimeText = std::format("{}: {}", getUIString(19),
            jarkUtils::COMPILE_DATE_TIME);
        const auto commitText = std::format("Commit ID: {}", BuildInfo::GIT_COMMIT_ID);
        textDrawer.putAlignCenter(page,
            { hero.x + 30, hero.y + 310, hero.width - 60, 36 },
            buildTimeText.c_str(), secondaryText());
        textDrawer.putAlignCenter(page,
            { hero.x + 30, hero.y + 350, hero.width - 60, 36 },
            commitText.c_str(), secondaryText());

        const auto projectButton = toCvRect(SettingLayout::ABOUT_PROJECT_BUTTON);
        const auto upstreamButton = toCvRect(SettingLayout::ABOUT_UPSTREAM_BUTTON);
        fillRoundedRect(page, projectButton, GlobalVar::currentTheme.CHECK, 8);
        fillRoundedRect(page, upstreamButton, GlobalVar::currentTheme.BG_TAG, 8);
        textDrawer.putAlignCenter(page, projectButton,
            chinese ? "访问本项目" : "Open this project", 0xFFFFFFFFu);
        textDrawer.putAlignCenter(page, upstreamButton,
            chinese ? "访问上游项目" : "Open upstream", primaryText());
    }

    void drawTabs() {
        cv::rectangle(winCanvas, { 0, 0, winWidth, tabHeight },
            jarkUtils::to_cv_scalar(GlobalVar::currentTheme.BG), -1);
        cv::line(winCanvas, { 0, tabHeight - 1 }, { winWidth, tabHeight - 1 },
            jarkUtils::to_cv_scalar(GlobalVar::currentTheme.BG_TAG), 1);
        for (int index = 0; index < 4; ++index) {
            textDrawer.putAlignCenter(winCanvas,
                { index * tabWidth, 0, tabWidth, tabHeight }, getUIString(2 + index),
                index == curTabIdx ? primaryText() : secondaryText());
        }
        cv::rectangle(winCanvas,
            { curTabIdx * tabWidth + 18, tabHeight - 3, tabWidth - 36, 3 },
            jarkUtils::to_cv_scalar(GlobalVar::currentTheme.CHECK), -1);
    }

    void drawScrollbar(int contentHeight) {
        const int thumbHeight = SettingLayout::scrollbarThumbHeight(contentHeight);
        if (thumbHeight == 0)
            return;
        const int x = winWidth - SettingLayout::SCROLLBAR_RIGHT_MARGIN -
            SettingLayout::SCROLLBAR_WIDTH;
        const cv::Rect track{ x, tabHeight + 6, SettingLayout::SCROLLBAR_WIDTH,
            SettingLayout::CONTENT_VIEW_HEIGHT - 12 };
        fillRoundedRect(winCanvas, track, GlobalVar::currentTheme.BG_TAG, 2);
        const cv::Rect thumb{ x, SettingLayout::scrollbarThumbY(
            contentHeight, scrollOffsets[curTabIdx]), SettingLayout::SCROLLBAR_WIDTH, thumbHeight };
        fillRoundedRect(winCanvas, thumb, GlobalVar::currentTheme.CHECK, 2);
    }

    void drawingUI() override {
        textDrawer.setSize(SettingLayout::FONT_SIZE);
        const int tab = std::clamp(static_cast<int>(curTabIdx), 0, 3);
        const int contentHeight = contentHeightForTab(tab);
        scrollOffsets[tab] = SettingLayout::clampScrollOffset(contentHeight, scrollOffsets[tab]);
        cv::Mat page(contentHeight, winWidth, CV_8UC4,
            jarkUtils::to_cv_scalar(GlobalVar::currentTheme.BG_DEEP));
        switch (tab) {
        case 0: refreshGeneralTab(page); break;
        case 1: refreshAssociateTab(page); break;
        case 2: refreshHelpTab(page); break;
        default: refreshAboutTab(page); break;
        }

        winCanvas.setTo(jarkUtils::to_cv_scalar(GlobalVar::currentTheme.BG_DEEP));
        drawTabs();
        const int visibleHeight = std::min(SettingLayout::CONTENT_VIEW_HEIGHT,
            contentHeight - scrollOffsets[tab]);
        page(cv::Rect{ 0, scrollOffsets[tab], winWidth, visibleHeight }).copyTo(
            winCanvas(cv::Rect{ 0, tabHeight, winWidth, visibleHeight }));
        drawScrollbar(contentHeight);
    }

    bool isInside(int x, int y, const cv::Rect& rect) const {
        return rect.x <= x && x < rect.x + rect.width &&
            rect.y <= y && y < rect.y + rect.height;
    }

    void updateWindowAttribute() {
        if (!hwnd)
            return;
        BOOL themeMode = GlobalVar::isCurrentUIDarkMode;
        DwmSetWindowAttribute(hwnd, DWMWINDOWATTRIBUTE::DWMWA_USE_IMMERSIVE_DARK_MODE,
            &themeMode, sizeof(BOOL));
    }

    void handleGeneralTab(int x, int y) {
        for (auto& toggle : generalTabCheckBoxList) {
            if (!isInside(x, y, toggle.rect))
                continue;
            *toggle.valuePtr = !*toggle.valuePtr;
            if (toggle.valuePtr == &GlobalVar::settingParameter.enableColorManagement)
                GlobalVar::isNeedReloadImageCache = true;
            isNeedRefreshUI = true;
            return;
        }

        constexpr int labelWidth = 138;
        for (auto& radio : generalTabRadioList) {
            const cv::Rect segments{ radio.rect.x + labelWidth, radio.rect.y + 5,
                radio.rect.width - labelWidth, radio.rect.height - 10 };
            if (!isInside(x, y, segments))
                continue;
            const int optionCount = static_cast<int>(radio.stringIDs.size()) - 1;
            const int itemWidth = segments.width / optionCount;
            const int selected = std::clamp((x - segments.x) / itemWidth, 0, optionCount - 1);
            *radio.valuePtr = selected;
            if (radio.stringIDs.front() == 24) {
                GlobalVar::isCurrentUIDarkMode = GlobalVar::settingParameter.UI_Mode == 0 ?
                    GlobalVar::isSystemDarkMode : GlobalVar::settingParameter.UI_Mode == 2;
                GlobalVar::currentTheme = GlobalVar::isCurrentUIDarkMode ? deepTheme : lightTheme;
                updateWindowAttribute();
                GlobalVar::isNeedUpdateTheme = true;
            }
            isNeedRefreshUI = true;
            return;
        }
    }

    template<typename T>
    void toggle(std::set<T>& values, const T& value) {
        if (!values.insert(value).second)
            values.erase(value);
    }

    FileAssociationResult SetupFileAssociations(const std::vector<std::wstring>& extChecked,
        const std::vector<std::wstring>& extUnchecked) {
        FileAssociationManager manager;
        return manager.ManageFileAssociations(extChecked, extUnchecked);
    }

    void handleAssociateTab(int x, int y) {
        const cv::Rect search = toCvRect(SettingLayout::ASSOCIATION_SEARCH);
        if (isInside(x, y, search)) {
            associationSearchActive = true;
            isNeedRefreshUI = true;
            return;
        }
        associationSearchActive = false;

        const auto visible = filteredExtensions();
        for (int index = 0; index < static_cast<int>(visible.size()); ++index) {
            const int column = index % SettingLayout::ASSOCIATION_GRID_COLUMNS;
            const int row = index / SettingLayout::ASSOCIATION_GRID_COLUMNS;
            const cv::Rect rect{
                SettingLayout::ASSOCIATION_GRID_X + column *
                    (SettingLayout::ASSOCIATION_TAG_WIDTH + SettingLayout::ASSOCIATION_TAG_GAP_X),
                SettingLayout::ASSOCIATION_GRID_Y + row *
                    (SettingLayout::ASSOCIATION_TAG_HEIGHT + SettingLayout::ASSOCIATION_TAG_GAP_Y),
                SettingLayout::ASSOCIATION_TAG_WIDTH,
                SettingLayout::ASSOCIATION_TAG_HEIGHT };
            if (isInside(x, y, rect)) {
                toggle(checkedExt, std::string(visible[index]));
                isNeedRefreshUI = true;
                return;
            }
        }

        const auto buttons = associationButtonRects();
        if (isInside(x, y, buttons[0])) {
            const auto defaults = jarkUtils::splitString(SettingParameter::defaultExtList, ",");
            checkedExt.clear();
            for (const auto& ext : defaults) {
                if (!ext.empty())
                    checkedExt.insert(ext);
            }
            isNeedRefreshUI = true;
        }
        else if (isInside(x, y, buttons[1])) {
            checkedExt.insert(allSupportExt.begin(), allSupportExt.end());
            isNeedRefreshUI = true;
        }
        else if (isInside(x, y, buttons[2])) {
            checkedExt.clear();
            isNeedRefreshUI = true;
        }
        else if (isInside(x, y, buttons[3])) {
            std::vector<std::wstring> checkedExtW, uncheckedExtW;
            for (const auto& ext : allSupportExt) {
                (checkedExt.contains(ext) ? checkedExtW : uncheckedExtW).emplace_back(
                    jarkUtils::utf8ToWstring(ext));
            }
            const auto result = SetupFileAssociations(checkedExtW, uncheckedExtW);
            if (!result.associationSucceeded)
                MessageBoxW(nullptr, getUIStringW(3), getUIStringW(1), MB_OK | MB_ICONERROR);
            else if (result.thumbnailOperationFailed)
                MessageBoxW(nullptr, getUIStringW(41), getUIStringW(1), MB_OK | MB_ICONWARNING);
            else
                MessageBoxW(nullptr, getUIStringW(2), getUIStringW(1), MB_OK | MB_ICONINFORMATION);
        }
    }

    void finishAssociateTab() {
        std::string checkedList;
        for (const auto& ext : checkedExt) {
            if (!checkedList.empty())
                checkedList += ',';
            checkedList += ext;
        }
        const std::size_t capacity = sizeof(GlobalVar::settingParameter.extCheckedListStr);
        memset(GlobalVar::settingParameter.extCheckedListStr, 0, capacity);
        if (!checkedList.empty()) {
            memcpy(GlobalVar::settingParameter.extCheckedListStr, checkedList.data(),
                std::min(checkedList.size(), capacity - 1));
        }
    }

    void handleAboutTab(int x, int y) {
        if (isInside(x, y, toCvRect(SettingLayout::ABOUT_PROJECT_BUTTON)))
            jarkUtils::openUrl(RepositoryLink.data());
        else if (isInside(x, y, toCvRect(SettingLayout::ABOUT_UPSTREAM_BUTTON)))
            jarkUtils::openUrl(upstreamRepository.data());
    }

    void setScrollFromTrack(int y) {
        const int contentHeight = contentHeightForTab(curTabIdx);
        const int maximum = SettingLayout::maxScrollOffset(contentHeight);
        if (maximum == 0)
            return;
        const int relative = std::clamp(y - tabHeight, 0, SettingLayout::CONTENT_VIEW_HEIGHT);
        scrollOffsets[curTabIdx] = maximum * relative / SettingLayout::CONTENT_VIEW_HEIGHT;
        isNeedRefreshUI = true;
    }

    void onPaint(HDC hdc) override {
        if (!winCanvas.empty())
            blitMat(hdc, winCanvas);
    }

    void onLButtonUp() override {
        if (m_y < tabHeight) {
            const int newTab = std::clamp(m_x / tabWidth, 0, 3);
            if (newTab != curTabIdx) {
                if (curTabIdx == 1)
                    finishAssociateTab();
                curTabIdx = newTab;
                associationSearchActive = false;
                isNeedRefreshUI = true;
            }
            return;
        }

        if (m_x >= winWidth - 16) {
            setScrollFromTrack(m_y);
            return;
        }

        const int contentX = m_x;
        const int contentY = m_y - tabHeight + scrollOffsets[curTabIdx];
        switch (curTabIdx) {
        case 0: handleGeneralTab(contentX, contentY); break;
        case 1: handleAssociateTab(contentX, contentY); break;
        case 3: handleAboutTab(contentX, contentY); break;
        }
    }

    void onMouseWheel(int delta) override {
        const int contentHeight = contentHeightForTab(curTabIdx);
        if (SettingLayout::maxScrollOffset(contentHeight) == 0)
            return;
        scrollOffsets[curTabIdx] = SettingLayout::clampScrollOffset(contentHeight,
            scrollOffsets[curTabIdx] - (delta > 0 ? 72 : -72));
        isNeedRefreshUI = true;
    }

    void onKeyDown(WPARAM key) override {
        if (key == VK_ESCAPE) {
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        }
        else if (key == VK_TAB) {
            if (curTabIdx == 1)
                finishAssociateTab();
            curTabIdx = (curTabIdx + 1) % 4;
            associationSearchActive = false;
            isNeedRefreshUI = true;
        }
    }

    void onChar(WPARAM character) override {
        if (curTabIdx != 1 || !associationSearchActive)
            return;
        if (character == 8) {
            if (!associationFilter.empty())
                associationFilter.pop_back();
        }
        else if (character < 128 && (std::isalnum(static_cast<unsigned char>(character)) ||
            character == '.')) {
            associationFilter.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(character))));
        }
        else {
            return;
        }
        scrollOffsets[1] = 0;
        isNeedRefreshUI = true;
    }

    void onRButtonUp() override {
        if (GlobalVar::settingParameter.rightClickAction == 1)
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    }

    void windowsMainLoop() {
        if (!createWindow(winWidth, winHeight, windowsClassName, getUIStringW(39)))
            return;
        hwnd = m_hwnd;
        runMessageLoop();
        if (curTabIdx == 1)
            finishAssociateTab();
    }

public:
    static inline volatile bool isWorking = false;
    static inline volatile HWND hwnd = nullptr;
    static inline volatile int curTabIdx = 0;

    explicit Setting(int tabIdx = 0) {
        requestExitFlag = false;
        isWorking = true;
        Init(tabIdx);
        windowsMainLoop();
        requestExitFlag = false;
        isWorking = false;
        hwnd = nullptr;
    }

    static void requestExit() {
        if (hwnd)
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
};
