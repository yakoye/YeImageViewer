#pragma once

#include "jarkUtils.h"
#include "SettingLayout.h"
#include "ViewerOptions.h"

#include <vssym32.h>

#include <string>
#include <vector>

// 设置页「常规」标签改用 Win32 原生控件。自绘画布在高 DPI 下只能把逻辑尺寸的位图
// 拉伸上去，文字必然发虚；原生控件由系统按物理像素渲染，天生清晰，还白得键盘导航
// 和无障碍支持。控件都挂在一个可滚动的子面板上，滚动时整体平移面板即可。
namespace SettingGeneralPanel {

inline constexpr int FIRST_CONTROL_ID = 3000;

// 逻辑布局单位，创建时统一按 DPI 放大。
inline constexpr int MARGIN = 18;
inline constexpr int GROUP_GAP = 14;
inline constexpr int ROW_HEIGHT = 30;
inline constexpr int ROW_GAP = 6;
inline constexpr int GROUP_PAD_TOP = 26;
inline constexpr int GROUP_PAD_BOTTOM = 14;
inline constexpr int LABEL_WIDTH = 150;
inline constexpr int RADIO_WIDTH = 128;

struct ToggleSpec {
    int stringId;
    bool* value;
};

struct ChoiceSpec {
    int labelStringId;
    std::vector<int> optionStringIds;
    uint32_t* value;
};

inline int scaled(int value, int dpi) {
    return MulDiv(value, dpi > 0 ? dpi : USER_DEFAULT_SCREEN_DPI, USER_DEFAULT_SCREEN_DPI);
}

// 启用视觉样式后勾选框与单选钮自己绘制文字，不再经过 WM_CTLCOLORSTATIC。必须先让
// 进程声明允许深色，comctl32 才会改用深色主题取色；否则未选中项会按浅色主题画成
// 深灰，在深色背景上几乎看不见。这个导出只有序号没有名字。
inline void allowDarkModeForProcess() {
    static const bool applied = []() {
        HMODULE module = GetModuleHandleW(L"uxtheme.dll");
        if (!module)
            module = LoadLibraryW(L"uxtheme.dll");
        if (!module)
            return false;
        using SetPreferredAppModeFn = int(WINAPI*)(int);
        const auto setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(
            GetProcAddress(module, MAKEINTRESOURCEA(135)));
        if (!setPreferredAppMode)
            return false;
        setPreferredAppMode(1); // AllowDark：跟随各窗口自己的 SetWindowTheme
        return true;
    }();
    (void)applied;
}

class Panel {
    enum class Kind { Toggle, Radio, Label };

public:
    bool isCreated() const { return m_panel != nullptr; }
    HWND handle() const { return m_panel; }
    int contentHeight() const { return m_contentHeight; }

    void destroy() {
        if (m_panel) {
            DestroyWindow(m_panel);
            m_panel = nullptr;
        }
        m_controls.clear();
        m_toggles.clear();
        m_choices.clear();
        if (m_font) {
            DeleteObject(m_font);
            m_font = nullptr;
        }
        if (m_background) {
            DeleteObject(m_background);
            m_background = nullptr;
        }
    }

    ~Panel() { destroy(); }

    bool create(HWND parent, int dpi, std::vector<ToggleSpec> toggles,
        std::vector<ChoiceSpec> choices) {
        destroy();
        if (!parent)
            return false;
        allowDarkModeForProcess();
        m_dpi = dpi > 0 ? dpi : USER_DEFAULT_SCREEN_DPI;
        m_toggles = std::move(toggles);
        m_choices = std::move(choices);

        registerPanelClass();
        m_panel = CreateWindowExW(0, panelClassName(), L"",
            WS_CHILD | WS_CLIPCHILDREN, 0, 0, 10, 10, parent, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (!m_panel)
            return false;
        SetWindowLongPtrW(m_panel, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        createFont();
        buildControls();
        applyTheme();
        return true;
    }

    // 面板整体平移实现滚动，控件相对面板的位置不变。
    void place(int x, int y, int width, int scrollOffset) {
        if (!m_panel)
            return;
        SetWindowPos(m_panel, nullptr, x, y - scrollOffset, width, m_contentHeight,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void setVisible(bool visible) {
        if (m_panel)
            ShowWindow(m_panel, visible ? SW_SHOW : SW_HIDE);
    }

    // 主题或语言变化后重建外观与文案。
    void applyTheme() {
        if (!m_panel)
            return;
        const bool dark = GlobalVar::isCurrentUIDarkMode;
        if (m_background)
            DeleteObject(m_background);
        m_background = CreateSolidBrush(toColorRef(
            dark ? GlobalVar::currentTheme.BG_DEEP : GlobalVar::currentTheme.BG_DEEP));
        // 勾选框与单选钮自身的绘制走系统深色主题；文字颜色仍要靠 WM_CTLCOLORSTATIC。
        const wchar_t* theme = dark ? L"DarkMode_Explorer" : nullptr;
        SetWindowTheme(m_panel, theme, nullptr);
        for (HWND control : m_controls)
            SetWindowTheme(control, theme, nullptr);
        InvalidateRect(m_panel, nullptr, TRUE);
    }

    void refreshTexts() {
        std::size_t index = 0;
        for (const auto& toggle : m_toggles) {
            if (index < m_controls.size())
                SetWindowTextW(m_controls[index],
                    jarkUtils::utf8ToWstring(getUIString(toggle.stringId)).c_str());
            ++index;
        }
        for (const auto& choice : m_choices) {
            if (index < m_controls.size())
                SetWindowTextW(m_controls[index],
                    jarkUtils::utf8ToWstring(getUIString(choice.labelStringId)).c_str());
            ++index;
            for (std::size_t option = 1; option < choice.optionStringIds.size(); ++option) {
                if (index < m_controls.size())
                    SetWindowTextW(m_controls[index], jarkUtils::utf8ToWstring(
                        getUIString(choice.optionStringIds[option])).c_str());
                ++index;
            }
        }
    }

    void reloadFromSettings() {
        std::size_t index = 0;
        for (const auto& toggle : m_toggles) {
            if (index < m_checkedCache.size())
                m_checkedCache[index] = *toggle.value;
            ++index;
        }
        for (const auto& choice : m_choices) {
            ++index; // 标签
            const uint32_t selected = *choice.value;
            for (std::size_t option = 1; option < choice.optionStringIds.size(); ++option) {
                if (index < m_checkedCache.size())
                    m_checkedCache[index] = (option - 1) == selected;
                ++index;
            }
        }
        for (HWND control : m_controls)
            InvalidateRect(control, nullptr, TRUE);
    }

    // 返回 true 表示这条通知属于本面板且已消费。
    bool handleCommand(WPARAM wParam, bool& needsThemeRefresh, bool& needsCacheReload) {
        const int id = LOWORD(wParam);
        if (HIWORD(wParam) != BN_CLICKED)
            return false;
        const int offset = id - FIRST_CONTROL_ID;
        if (offset < 0 || offset >= static_cast<int>(m_controls.size()))
            return false;

        std::size_t index = 0;
        for (const auto& toggle : m_toggles) {
            if (static_cast<int>(index) == offset) {
                // owner-draw 按钮不会自己翻转选中态，这里手动取反。
                *toggle.value = !*toggle.value;
                if (toggle.value == &GlobalVar::settingParameter.enableColorManagement)
                    needsCacheReload = true;
                reloadFromSettings();
                return true;
            }
            ++index;
        }
        for (const auto& choice : m_choices) {
            ++index; // 标签本身不可点
            for (std::size_t option = 1; option < choice.optionStringIds.size(); ++option) {
                if (static_cast<int>(index) == offset) {
                    *choice.value = static_cast<uint32_t>(option - 1);
                    if (choice.labelStringId == 24) { // 主题
                        GlobalVar::isCurrentUIDarkMode =
                            GlobalVar::settingParameter.UI_Mode == 0 ?
                            GlobalVar::isSystemDarkMode :
                            GlobalVar::settingParameter.UI_Mode == 2;
                        GlobalVar::currentTheme =
                            GlobalVar::isCurrentUIDarkMode ? deepTheme : lightTheme;
                        GlobalVar::isNeedUpdateTheme = true;
                        needsThemeRefresh = true;
                    }
                    else if (choice.labelStringId == 28) { // 语言
                        needsThemeRefresh = true;
                    }
                    reloadFromSettings();
                    return true;
                }
                ++index;
            }
        }
        return false;
    }

    // 深色下勾选框与单选钮的文字不吃 SetWindowTheme，必须在这里染色。
    bool handleCtlColor(HDC hdc, HWND control, LRESULT& result) {
        if (!m_panel)
            return false;
        if (control != m_panel && GetParent(control) != m_panel)
            return false;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, toColorRef(GlobalVar::isCurrentUIDarkMode ?
            GlobalVar::currentTheme.FG : GlobalVar::currentTheme.FG_DEEP));
        SetBkColor(hdc, toColorRef(GlobalVar::currentTheme.BG_DEEP));
        result = reinterpret_cast<LRESULT>(m_background);
        return m_background != nullptr;
    }

private:
    static COLORREF toColorRef(uint32_t bgra) {
        // 主题色是 0xAARRGGBB 排布，GDI 需要 0x00BBGGRR。
        const uint32_t red = (bgra >> 16) & 0xFF;
        const uint32_t green = (bgra >> 8) & 0xFF;
        const uint32_t blue = bgra & 0xFF;
        return RGB(red, green, blue);
    }

    static const wchar_t* panelClassName() { return L"YeImageViewerSettingPanel"; }

    static void registerPanelClass() {
        static bool registered = false;
        if (registered)
            return;
        WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = panelProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = panelClassName();
        RegisterClassExW(&wc);
        registered = true;
    }

    // 面板自身只负责把子控件的染色请求转交给宿主窗口，其余交给默认处理。
    static LRESULT CALLBACK panelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN) {
            auto* self = reinterpret_cast<Panel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            LRESULT result = 0;
            if (self && self->handleCtlColor(reinterpret_cast<HDC>(wParam),
                reinterpret_cast<HWND>(lParam), result))
                return result;
        }
        if (msg == WM_COMMAND) {
            return SendMessageW(GetParent(hwnd), WM_COMMAND, wParam, lParam);
        }
        if (msg == WM_DRAWITEM) {
            auto* self = reinterpret_cast<Panel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (self && item && self->drawButton(*item))
                return TRUE;
        }
        if (msg == WM_ERASEBKGND) {
            auto* self = reinterpret_cast<Panel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self && self->m_background) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                FillRect(reinterpret_cast<HDC>(wParam), &rc, self->m_background);
                return 1;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void createFont() {
        NONCLIENTMETRICSW metrics{ sizeof(NONCLIENTMETRICSW) };
        if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
            &metrics, 0, static_cast<UINT>(m_dpi))) {
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
        }
        m_font = CreateFontIndirectW(&metrics.lfMessageFont);
    }

    HWND addControl(const wchar_t* className, const std::wstring& text,
        DWORD style, int x, int y, int width, int height, Kind kind) {
        const int id = FIRST_CONTROL_ID + static_cast<int>(m_controls.size());
        HWND control = CreateWindowExW(0, className, text.c_str(),
            WS_CHILD | WS_VISIBLE | style,
            scaled(x, m_dpi), scaled(y, m_dpi), scaled(width, m_dpi), scaled(height, m_dpi),
            m_panel, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
        if (control) {
            if (m_font)
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
            m_controls.push_back(control);
            m_kinds.push_back(kind);
            m_checkedCache.push_back(false);
        }
        return control;
    }

public:
    // 勾选框与单选钮改为 owner-draw。启用视觉样式后它们自己画文字，取的是浅色主题的
    // 颜色，深色背景上几乎看不见，而 SetWindowTheme 对 BUTTON 的文字不起作用。这里
    // 方框仍交给系统主题绘制以保住现代外观，只把文字按当前主题重新着色。
    bool drawButton(const DRAWITEMSTRUCT& item) {
        const int offset = static_cast<int>(item.CtlID) - FIRST_CONTROL_ID;
        if (offset < 0 || offset >= static_cast<int>(m_kinds.size()))
            return false;
        const Kind kind = m_kinds[offset];
        if (kind == Kind::Label)
            return false;

        HDC hdc = item.hDC;
        RECT rc = item.rcItem;
        if (m_background)
            FillRect(hdc, &rc, m_background);

        const bool checked = offset < static_cast<int>(m_checkedCache.size()) &&
            m_checkedCache[offset];
        const bool isRadio = kind == Kind::Radio;
        const int glyph = scaled(13, m_dpi);
        RECT box{ rc.left, rc.top + (rc.bottom - rc.top - glyph) / 2,
            rc.left + glyph, rc.top + (rc.bottom - rc.top + glyph) / 2 };

        const bool dark = GlobalVar::isCurrentUIDarkMode;
        HTHEME theme = OpenThemeData(item.hwndItem,
            dark ? L"DarkMode_Explorer::Button" : L"Button");
        if (theme) {
            const int part = isRadio ? BP_RADIOBUTTON : BP_CHECKBOX;
            int state = isRadio ?
                (checked ? RBS_CHECKEDNORMAL : RBS_UNCHECKEDNORMAL) :
                (checked ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL);
            if (item.itemState & ODS_DISABLED) {
                state = isRadio ?
                    (checked ? RBS_CHECKEDDISABLED : RBS_UNCHECKEDDISABLED) :
                    (checked ? CBS_CHECKEDDISABLED : CBS_UNCHECKEDDISABLED);
            }
            DrawThemeBackground(theme, hdc, part, state, &box, nullptr);
            CloseThemeData(theme);
        }
        else {
            UINT flags = DFCS_BUTTONCHECK;
            if (isRadio)
                flags = DFCS_BUTTONRADIO;
            if (checked)
                flags |= DFCS_CHECKED;
            DrawFrameControl(hdc, &box, DFC_BUTTON, flags);
        }

        wchar_t text[256]{};
        GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
        RECT textRect{ box.right + scaled(7, m_dpi), rc.top, rc.right, rc.bottom };
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, toColorRef(dark ?
            GlobalVar::currentTheme.FG : GlobalVar::currentTheme.FG_DEEP));
        HGDIOBJ previousFont = m_font ? SelectObject(hdc, m_font) : nullptr;
        DrawTextW(hdc, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (previousFont)
            SelectObject(hdc, previousFont);

        if (item.itemState & ODS_FOCUS) {
            RECT focus{ textRect.left - scaled(2, m_dpi), rc.top, rc.right, rc.bottom };
            DrawFocusRect(hdc, &focus);
        }
        return true;
    }

private:

    void buildControls() {
        const int columnWidth = (SettingLayout::CANVAS_WIDTH - MARGIN * 2) / 2;
        int y = GROUP_PAD_TOP;

        // 开关两列排布，和原来的自绘版式保持一致。
        for (std::size_t index = 0; index < m_toggles.size(); ++index) {
            const int column = static_cast<int>(index % 2);
            const int row = static_cast<int>(index / 2);
            addControl(L"BUTTON",
                jarkUtils::utf8ToWstring(getUIString(m_toggles[index].stringId)),
                BS_OWNERDRAW | WS_TABSTOP,
                MARGIN + column * columnWidth,
                y + row * (ROW_HEIGHT + ROW_GAP),
                columnWidth - 8, ROW_HEIGHT, Kind::Toggle);
        }
        if (!m_toggles.empty()) {
            const int rows = static_cast<int>((m_toggles.size() + 1) / 2);
            y += rows * (ROW_HEIGHT + ROW_GAP) + GROUP_GAP;
        }

        for (const auto& choice : m_choices) {
            addControl(L"STATIC",
                jarkUtils::utf8ToWstring(getUIString(choice.labelStringId)),
                SS_LEFT | SS_CENTERIMAGE, MARGIN, y, LABEL_WIDTH, ROW_HEIGHT, Kind::Label);
            int x = MARGIN + LABEL_WIDTH;
            for (std::size_t option = 1; option < choice.optionStringIds.size(); ++option) {
                DWORD style = BS_OWNERDRAW | WS_TABSTOP;
                if (option == 1)
                    style |= WS_GROUP; // 每组第一个起新组，方向键才不会跨组跑
                addControl(L"BUTTON", jarkUtils::utf8ToWstring(
                    getUIString(choice.optionStringIds[option])),
                    style, x, y, RADIO_WIDTH, ROW_HEIGHT, Kind::Radio);
                x += RADIO_WIDTH;
            }
            y += ROW_HEIGHT + ROW_GAP;
        }

        // 面板正好占满外部编辑器卡片之前的区域：那部分仍是自绘的，两者按同一个
        // scrollOffset 滚动，高度对不上就会互相遮挡或错位。
        m_contentHeight = scaled(
            std::max(y + GROUP_PAD_BOTTOM, SettingLayout::GENERAL_EDITOR_CARD_Y), m_dpi);
        reloadFromSettings();
    }

    HWND m_panel = nullptr;
    HFONT m_font = nullptr;
    HBRUSH m_background = nullptr;
    int m_dpi = USER_DEFAULT_SCREEN_DPI;
    int m_contentHeight = 0;
    std::vector<HWND> m_controls;
    std::vector<Kind> m_kinds;
    // BS_OWNERDRAW 与 BS_AUTOCHECKBOX 的样式位冲突，按钮不再自动切换选中态，
    // 绘制时也读不到可靠的 BM_GETCHECK，所以状态由这里统一缓存。
    std::vector<bool> m_checkedCache;
    std::vector<ToggleSpec> m_toggles;
    std::vector<ChoiceSpec> m_choices;
};

}
