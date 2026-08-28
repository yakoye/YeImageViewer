#pragma once

#include "BuildInfo.h"
#include "FileAssociationManager.h"
#include "MatWindow.h"
#include "SettingCommand.h"
#include "SettingLayout.h"
#include "TextDrawer.h"

#include <array>
#include <cctype>
#include <optional>

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
    std::optional<ShortcutConfig::Action> shortcutCapture;

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
        case 2: return SettingLayout::SHORTCUT_CONTENT_HEIGHT;
        default: return SettingLayout::ABOUT_CONTENT_HEIGHT;
        }
    }

    std::array<cv::Rect, 4> associationButtonRects() const {
        std::array<cv::Rect, 4> result{};
        for (int i = 0; i < 4; ++i)
            result[i] = toCvRect(SettingLayout::associationButtonRect(i, associationButtonsY()));
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

    struct ShortcutItem {
        ShortcutConfig::Action action;
        const char* nameZH;
        const char* nameEN;
    };

    static constexpr std::array<ShortcutItem,
        static_cast<std::size_t>(ShortcutConfig::Action::Count)> shortcutItems{
            ShortcutItem{ ShortcutConfig::Action::OpenFile, "打开图片", "Open image" },
            ShortcutItem{ ShortcutConfig::Action::ExportFrames, "导出全部帧", "Export all frames" },
            ShortcutItem{ ShortcutConfig::Action::CopyImage, "复制图片", "Copy image" },
            ShortcutItem{ ShortcutConfig::Action::PrintImage, "打印图片", "Print image" },
            ShortcutItem{ ShortcutConfig::Action::CloseViewer, "关闭看图窗口", "Close viewer" },
            ShortcutItem{ ShortcutConfig::Action::PreviousFrame, "上一帧", "Previous frame" },
            ShortcutItem{ ShortcutConfig::Action::ToggleAnimation, "暂停/继续动图", "Pause/resume animation" },
            ShortcutItem{ ShortcutConfig::Action::NextFrame, "下一帧", "Next frame" },
            ShortcutItem{ ShortcutConfig::Action::CopyImageInfo, "复制图片信息", "Copy image information" },
            ShortcutItem{ ShortcutConfig::Action::ToggleFullscreen, "全屏显示", "Toggle fullscreen" },
            ShortcutItem{ ShortcutConfig::Action::RotateLeft, "向左旋转", "Rotate left" },
            ShortcutItem{ ShortcutConfig::Action::RotateRight, "向右旋转", "Rotate right" },
            ShortcutItem{ ShortcutConfig::Action::PanUp, "向上拖动", "Pan up" },
            ShortcutItem{ ShortcutConfig::Action::PanDown, "向下拖动", "Pan down" },
            ShortcutItem{ ShortcutConfig::Action::PanLeft, "向左拖动", "Pan left" },
            ShortcutItem{ ShortcutConfig::Action::PanRight, "向右拖动", "Pan right" },
            ShortcutItem{ ShortcutConfig::Action::ZoomIn, "放大", "Zoom in" },
            ShortcutItem{ ShortcutConfig::Action::ZoomOut, "缩小", "Zoom out" },
            ShortcutItem{ ShortcutConfig::Action::ZoomFit, "适合窗口", "Fit window" },
            ShortcutItem{ ShortcutConfig::Action::PreviousImage, "上一张", "Previous image" },
            ShortcutItem{ ShortcutConfig::Action::NextImage, "下一张", "Next image" },
            ShortcutItem{ ShortcutConfig::Action::FirstImage, "第一张", "First image" },
            ShortcutItem{ ShortcutConfig::Action::LastImage, "最后一张", "Last image" },
            ShortcutItem{ ShortcutConfig::Action::PlayPause, "播放/暂停", "Play/pause" },
            ShortcutItem{ ShortcutConfig::Action::ToggleImageInfo, "显示图片信息", "Toggle image information" },
            ShortcutItem{ ShortcutConfig::Action::OpenSettings, "打开设置", "Open settings" },
            ShortcutItem{ ShortcutConfig::Action::RenameImage, "重命名图片", "Rename image" },
            ShortcutItem{ ShortcutConfig::Action::OpenShortcuts, "打开快捷键设置", "Open shortcuts" },
            ShortcutItem{ ShortcutConfig::Action::OpenAbout, "打开关于", "Open about" },
            ShortcutItem{ ShortcutConfig::Action::DeleteImage, "删除图片", "Delete image" },
        };

    const char* wheelActionName(ShortcutConfig::WheelAction action, bool chinese) const {
        switch (action) {
        case ShortcutConfig::WheelAction::Zoom: return chinese ? "放大/缩小" : "Zoom";
        case ShortcutConfig::WheelAction::PanVertical: return chinese ? "上下拖动" : "Pan vertically";
        case ShortcutConfig::WheelAction::PanHorizontal: return chinese ? "左右拖动" : "Pan horizontally";
        case ShortcutConfig::WheelAction::SwitchImage: return chinese ? "上一张/下一张" : "Previous/next image";
        case ShortcutConfig::WheelAction::Count: break;
        }
        return "";
    }

    void refreshShortcutTab(cv::Mat& page) {
        const bool chinese = GlobalVar::settingParameter.UI_LANG == 0;
        drawCard(page, toCvRect(SettingLayout::SHORTCUT_CARD));
        textDrawer.putAlignLeft(page, toCvRect(SettingLayout::SHORTCUT_WHEEL_HEADER),
            chinese ? "鼠标滚轮（点击右侧选项可切换）" :
                "MOUSE WHEEL (click an option to change)", GlobalVar::currentTheme.CHECK);
        static constexpr std::array<const char*, 3> wheelZH{ "滚轮", "Ctrl + 滚轮", "Shift + 滚轮" };
        static constexpr std::array<const char*, 3> wheelEN{ "Wheel", "Ctrl + wheel", "Shift + wheel" };
        for (int index = 0; index < 3; ++index) {
            const cv::Rect row = toCvRect(SettingLayout::shortcutWheelRow(index));
            cv::line(page, { row.x, row.y }, { row.x + row.width, row.y },
                jarkUtils::to_cv_scalar(GlobalVar::currentTheme.BG_TAG), 1);
            textDrawer.putAlignLeft(page, { row.x + 8, row.y, 220, row.height },
                chinese ? wheelZH[index] : wheelEN[index], primaryText());
            const cv::Rect keyRect{ row.x + 260, row.y + 5, 280, row.height - 10 };
            fillRoundedRect(page, keyRect, GlobalVar::currentTheme.BG_DEEP, 5);
            textDrawer.putAlignCenter(page, keyRect, wheelActionName(
                ShortcutConfig::getWheelAction(GlobalVar::settingParameter.reserve, index), chinese),
                GlobalVar::currentTheme.CHECK);
        }
        const cv::Rect reset = toCvRect(SettingLayout::SHORTCUT_RESET_BUTTON);
        fillRoundedRect(page, reset, GlobalVar::currentTheme.BG_TAG, 6);
        textDrawer.putAlignCenter(page, reset, chinese ? "恢复默认" : "Restore defaults", primaryText());

        textDrawer.putAlignLeft(page, toCvRect(SettingLayout::SHORTCUT_KEYBOARD_HEADER),
            chinese ? "键盘快捷键（点击右侧按键后重新输入）" :
                "KEYBOARD (click a key, then press a replacement)", GlobalVar::currentTheme.CHECK);
        for (int index = 0; index < static_cast<int>(shortcutItems.size()); ++index) {
            const auto& item = shortcutItems[index];
            const cv::Rect row = toCvRect(SettingLayout::shortcutKeyboardRow(index));
            cv::line(page, { row.x, row.y }, { row.x + row.width, row.y },
                jarkUtils::to_cv_scalar(GlobalVar::currentTheme.BG_TAG), 1);
            textDrawer.putAlignLeft(page, { row.x + 8, row.y, 290, row.height },
                chinese ? item.nameZH : item.nameEN, primaryText());
            const cv::Rect keyRect{ row.x + 312, row.y + 5, 228, row.height - 10 };
            const bool capturing = shortcutCapture && *shortcutCapture == item.action;
            fillRoundedRect(page, keyRect, capturing ?
                GlobalVar::currentTheme.CHECK : GlobalVar::currentTheme.BG_DEEP, 5);
            const std::string keyText = capturing ?
                (chinese ? "请按新快捷键…" : "Press a shortcut...") :
                ShortcutConfig::keyName(ShortcutConfig::getBinding(
                    GlobalVar::settingParameter.reserve, item.action), chinese);
            textDrawer.putAlignCenter(page, keyRect, keyText.c_str(),
                capturing ? 0xFFFFFFFFu : secondaryText());
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
        case 2: refreshShortcutTab(page); break;
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

    void handleShortcutTab(int x, int y) {
        for (int index = 0; index < 3; ++index) {
            const cv::Rect row = toCvRect(SettingLayout::shortcutWheelRow(index));
            if (!isInside(x, y, row))
                continue;
            const auto current = ShortcutConfig::getWheelAction(
                GlobalVar::settingParameter.reserve, index);
            const auto next = static_cast<ShortcutConfig::WheelAction>(
                (static_cast<uint32_t>(current) + 1) %
                static_cast<uint32_t>(ShortcutConfig::WheelAction::Count));
            ShortcutConfig::setWheelAction(GlobalVar::settingParameter.reserve, index, next);
            shortcutCapture.reset();
            isNeedRefreshUI = true;
            return;
        }
        if (isInside(x, y, toCvRect(SettingLayout::SHORTCUT_RESET_BUTTON))) {
            ShortcutConfig::reset(GlobalVar::settingParameter.reserve,
                std::size(GlobalVar::settingParameter.reserve));
            shortcutCapture.reset();
            isNeedRefreshUI = true;
            return;
        }
        for (int index = 0; index < static_cast<int>(shortcutItems.size()); ++index) {
            const cv::Rect row = toCvRect(SettingLayout::shortcutKeyboardRow(index));
            if (!isInside(x, y, row))
                continue;
            shortcutCapture = shortcutItems[index].action;
            isNeedRefreshUI = true;
            return;
        }
        shortcutCapture.reset();
        isNeedRefreshUI = true;
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
        const auto visibleExtensionCount = curTabIdx == 1 ?
            static_cast<int>(filteredExtensions().size()) : 0;
        const auto command = SettingCommand::resolve(curTabIdx, m_x, m_y,
            scrollOffsets[curTabIdx], visibleExtensionCount,
            curTabIdx == 1 ? associationButtonsY() : 0);

        if (command.kind == SettingCommand::Kind::Tab) {
            const int newTab = command.index;
            if (newTab != curTabIdx) {
                if (curTabIdx == 1)
                    finishAssociateTab();
                curTabIdx = newTab;
                associationSearchActive = false;
                shortcutCapture.reset();
                isNeedRefreshUI = true;
            }
            return;
        }

        if (command.kind == SettingCommand::Kind::Scrollbar) {
            setScrollFromTrack(m_y);
            return;
        }

        const int contentX = m_x;
        const int contentY = m_y - tabHeight + scrollOffsets[curTabIdx];
        switch (command.kind) {
        case SettingCommand::Kind::GeneralToggle:
        case SettingCommand::Kind::GeneralRadioOption:
            handleGeneralTab(contentX, contentY);
            break;
        case SettingCommand::Kind::AssociationSearch:
        case SettingCommand::Kind::AssociationExtension:
        case SettingCommand::Kind::AssociationDefaults:
        case SettingCommand::Kind::AssociationAll:
        case SettingCommand::Kind::AssociationNone:
        case SettingCommand::Kind::AssociationApply:
            handleAssociateTab(contentX, contentY);
            break;
        case SettingCommand::Kind::ShortcutWheel:
        case SettingCommand::Kind::ShortcutReset:
        case SettingCommand::Kind::ShortcutBinding:
            handleShortcutTab(contentX, contentY);
            break;
        case SettingCommand::Kind::AboutProject:
        case SettingCommand::Kind::AboutUpstream:
            handleAboutTab(contentX, contentY);
            break;
        default:
            break;
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
        if (curTabIdx == 2 && shortcutCapture) {
            if (key == VK_ESCAPE) {
                shortcutCapture.reset();
                isNeedRefreshUI = true;
                return;
            }
            if (key == VK_BACK) {
                ShortcutConfig::setBinding(GlobalVar::settingParameter.reserve,
                    *shortcutCapture, 0);
                shortcutCapture.reset();
                isNeedRefreshUI = true;
                return;
            }
            if (ShortcutConfig::isModifierKey(static_cast<uint32_t>(key)))
                return;
            uint32_t modifiers = 0;
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
                modifiers |= ShortcutConfig::MODIFIER_CONTROL;
            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
                modifiers |= ShortcutConfig::MODIFIER_SHIFT;
            if ((GetKeyState(VK_MENU) & 0x8000) != 0)
                modifiers |= ShortcutConfig::MODIFIER_ALT;
            ShortcutConfig::setBinding(GlobalVar::settingParameter.reserve,
                *shortcutCapture,
                ShortcutConfig::binding(static_cast<uint16_t>(key), modifiers));
            shortcutCapture.reset();
            isNeedRefreshUI = true;
            return;
        }
        if (key == VK_ESCAPE) {
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        }
        else if (key == VK_TAB) {
            if (curTabIdx == 1)
                finishAssociateTab();
            curTabIdx = (curTabIdx + 1) % 4;
            associationSearchActive = false;
            shortcutCapture.reset();
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

    static void persistSettings() {
        if (GlobalVar::settingPath.empty())
            return;
        memcpy(GlobalVar::settingParameter.header, GlobalVar::settingHeader.data(),
            GlobalVar::settingHeader.length());
        if (FILE* file = _wfopen(GlobalVar::settingPath.c_str(), L"wb")) {
            fwrite(&GlobalVar::settingParameter, 1, sizeof(SettingParameter), file);
            fclose(file);
        }
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
        persistSettings();
        requestExitFlag = false;
        isWorking = false;
        hwnd = nullptr;
    }

    static void requestExit() {
        if (hwnd)
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
};
