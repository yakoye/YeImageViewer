#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <algorithm>
#include <cmath>
#include <tinyxml2.h>

// 因lunaSVG不支持<switch>标签，需预处理SVG
class SVGPreprocessor {
public:
    std::string preprocessSVG(const char* svgContentPtr, size_t nBytes, const std::string& language = "en") {
        cv::tinyxml2::XMLDocument doc;
        if (doc.Parse(svgContentPtr, nBytes) != cv::tinyxml2::XML_SUCCESS) {
            return {};
        }

        processSwitchElements(doc.RootElement(), language);
        processUnsupportedCss(doc.RootElement());

        cv::tinyxml2::XMLPrinter printer;
        doc.Print(&printer);
        return printer.CStr();
    }

private:
    struct DrawioTextLine {
        std::string text;
        bool bold = false;
        bool ruleAfter = false;
    };

    struct DrawioTextStyle {
        double fontSize = 12.0;
        bool centered = false;
        bool hasBackground = false;
    };

    static std::string trim(std::string value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    static std::string replaceLightDark(std::string value) {
        constexpr std::string_view functionName = "light-dark(";
        size_t functionStart = 0;
        while ((functionStart = value.find(functionName, functionStart)) != std::string::npos) {
            const size_t argumentsStart = functionStart + functionName.size();
            size_t comma = std::string::npos;
            size_t functionEnd = std::string::npos;
            int depth = 1;

            for (size_t i = argumentsStart; i < value.size(); ++i) {
                if (value[i] == '(') {
                    ++depth;
                }
                else if (value[i] == ')') {
                    if (--depth == 0) {
                        functionEnd = i;
                        break;
                    }
                }
                else if (value[i] == ',' && depth == 1 && comma == std::string::npos) {
                    comma = i;
                }
            }

            if (comma == std::string::npos || functionEnd == std::string::npos) {
                break;
            }

            const auto lightColor = trim(value.substr(argumentsStart, comma - argumentsStart));
            value.replace(functionStart, functionEnd - functionStart + 1, lightColor);
            functionStart += lightColor.size();
        }
        return value;
    }

    void processUnsupportedCss(cv::tinyxml2::XMLElement* element) {
        if (!element) return;

        if (const char* style = element->Attribute("style")) {
            auto compatibleStyle = replaceLightDark(style);
            if (compatibleStyle != style) {
                element->SetAttribute("style", compatibleStyle.c_str());
            }
        }

        for (auto child = element->FirstChildElement(); child; child = child->NextSiblingElement()) {
            processUnsupportedCss(child);
        }
    }

    void processSwitchElements(cv::tinyxml2::XMLElement* element, const std::string& language) {
        if (!element) return;

        // 先处理所有子元素中的switch（深度优先）
        std::vector<cv::tinyxml2::XMLElement*> children;
        for (auto child = element->FirstChildElement(); child; child = child->NextSiblingElement()) {
            children.push_back(child);
        }

        for (auto child : children) {
            processSwitchElements(child, language);
        }

        // 处理当前元素如果是 switch
        if (std::string(element->Name()) == "switch") {
            processSwitchElement(element, language);
        }
    }

    void processSwitchElement(cv::tinyxml2::XMLElement* switchElement, const std::string& language) {
        if (auto drawioText = createDrawioTextFallback(switchElement)) {
            auto parent = switchElement->Parent();
            parent->InsertAfterChild(switchElement, drawioText);
            parent->DeleteChild(switchElement);
            return;
        }

        cv::tinyxml2::XMLElement* selectedChild = nullptr;

        // 按顺序检查子元素
        for (auto child = switchElement->FirstChildElement(); child; child = child->NextSiblingElement()) {
            if (shouldSelectElement(child, language)) {
                selectedChild = child;
                break;
            }
        }

        if (selectedChild) {
            auto parent = switchElement->Parent();
            auto doc = switchElement->GetDocument();

            // 手动克隆选中的元素
            auto clonedElement = cloneElement(selectedChild, doc);

            // 替换 switch 元素
            parent->InsertAfterChild(switchElement, clonedElement);
            parent->DeleteChild(switchElement);
        }
        else {
            // 如果没有匹配的元素，删除整个switch
            auto parent = switchElement->Parent();
            parent->DeleteChild(switchElement);
        }
    }

    static double readCssNumber(std::string_view style, std::string_view property, double fallback) {
        const size_t propertyPos = style.find(property);
        if (propertyPos == std::string_view::npos) return fallback;

        const size_t valueStart = propertyPos + property.size();
        try {
            return std::stod(std::string(style.substr(valueStart)));
        }
        catch (...) {
            return fallback;
        }
    }

    void inspectDrawioStyle(cv::tinyxml2::XMLElement* element, DrawioTextStyle& style) {
        if (!element) return;

        if (const char* rawStyle = element->Attribute("style")) {
            const std::string_view css(rawStyle);
            style.fontSize = std::max(style.fontSize, readCssNumber(css, "font-size:", style.fontSize));
            style.centered = style.centered ||
                css.find("text-align: center") != std::string_view::npos ||
                css.find("justify-content: unsafe center") != std::string_view::npos;
            style.hasBackground = style.hasBackground ||
                css.find("background-color:") != std::string_view::npos;
        }

        for (auto child = element->FirstChildElement(); child; child = child->NextSiblingElement()) {
            inspectDrawioStyle(child, style);
        }
    }

    void collectDrawioText(
        cv::tinyxml2::XMLNode* node,
        bool inheritedBold,
        std::vector<DrawioTextLine>& lines) {
        if (!node) return;
        if (lines.empty()) lines.emplace_back();

        for (auto child = node->FirstChild(); child; child = child->NextSibling()) {
            if (auto text = child->ToText()) {
                lines.back().text += text->Value();
                lines.back().bold = lines.back().bold || inheritedBold;
                continue;
            }

            auto element = child->ToElement();
            if (!element) continue;
            const std::string_view name(element->Name());
            if (name == "br") {
                lines.emplace_back();
            }
            else if (name == "hr") {
                lines.back().ruleAfter = true;
                lines.emplace_back();
            }
            else {
                collectDrawioText(element, inheritedBold || name == "b" || name == "strong", lines);
            }
        }
    }

    cv::tinyxml2::XMLElement* createDrawioTextFallback(cv::tinyxml2::XMLElement* switchElement) {
        auto foreignObject = switchElement->FirstChildElement("foreignObject");
        if (!foreignObject) return nullptr;

        cv::tinyxml2::XMLElement* fallbackImage = nullptr;
        for (auto child = switchElement->FirstChildElement(); child; child = child->NextSiblingElement()) {
            if (std::string_view(child->Name()) == "image") {
                fallbackImage = child;
                break;
            }
        }
        if (!fallbackImage) return nullptr;

        double x = 0.0;
        double y = 0.0;
        double width = 0.0;
        double height = 0.0;
        if (fallbackImage->QueryDoubleAttribute("x", &x) != cv::tinyxml2::XML_SUCCESS ||
            fallbackImage->QueryDoubleAttribute("y", &y) != cv::tinyxml2::XML_SUCCESS ||
            fallbackImage->QueryDoubleAttribute("width", &width) != cv::tinyxml2::XML_SUCCESS ||
            fallbackImage->QueryDoubleAttribute("height", &height) != cv::tinyxml2::XML_SUCCESS ||
            width <= 0.0 || height <= 0.0) {
            return nullptr;
        }

        std::vector<DrawioTextLine> lines;
        collectDrawioText(foreignObject, false, lines);
        for (auto& line : lines) line.text = trim(std::move(line.text));
        std::erase_if(lines, [](const DrawioTextLine& line) { return line.text.empty(); });
        if (lines.empty()) return nullptr;

        DrawioTextStyle style;
        inspectDrawioStyle(foreignObject, style);
        const double lineHeight = style.fontSize * 1.2;
        const double ruleSpacing = std::ranges::count_if(lines, [](const DrawioTextLine& line) {
            return line.ruleAfter;
        }) * 4.0;
        const double blockHeight = lines.size() * lineHeight + ruleSpacing;
        double baseline = y + std::max(0.0, (height - blockHeight) / 2.0) + style.fontSize;
        const double textX = style.centered ? x + width / 2.0 : x;

        auto doc = switchElement->GetDocument();
        auto group = doc->NewElement("g");
        group->SetAttribute("data-yeimageviewer", "drawio-text");

        if (style.hasBackground) {
            auto background = doc->NewElement("rect");
            background->SetAttribute("x", x);
            background->SetAttribute("y", y);
            background->SetAttribute("width", width);
            background->SetAttribute("height", height);
            background->SetAttribute("fill", "#ffffff");
            group->InsertEndChild(background);
        }

        for (const auto& line : lines) {
            auto text = doc->NewElement("text");
            text->SetAttribute("x", textX);
            text->SetAttribute("y", baseline);
            text->SetAttribute("fill", "#000000");
            text->SetAttribute("font-size", style.fontSize);
            if (style.centered) text->SetAttribute("text-anchor", "middle");
            if (line.bold) text->SetAttribute("font-weight", "bold");
            text->SetText(line.text.c_str());
            group->InsertEndChild(text);

            if (line.ruleAfter) {
                auto rule = doc->NewElement("line");
                rule->SetAttribute("x1", x);
                rule->SetAttribute("x2", x + width);
                rule->SetAttribute("y1", baseline + 3.0);
                rule->SetAttribute("y2", baseline + 3.0);
                rule->SetAttribute("stroke", "#b3b3b3");
                rule->SetAttribute("stroke-width", 1.0);
                group->InsertEndChild(rule);
                baseline += 4.0;
            }
            baseline += lineHeight;
        }
        return group;
    }

    // 手动实现元素克隆
    cv::tinyxml2::XMLElement* cloneElement(cv::tinyxml2::XMLElement* source, cv::tinyxml2::XMLDocument* doc) {
        auto cloned = doc->NewElement(source->Name());

        // 复制属性
        for (auto attr = source->FirstAttribute(); attr; attr = attr->Next()) {
            cloned->SetAttribute(attr->Name(), attr->Value());
        }

        // 复制文本内容
        if (source->GetText()) {
            cloned->SetText(source->GetText());
        }

        // 递归复制子元素
        for (auto child = source->FirstChildElement(); child; child = child->NextSiblingElement()) {
            auto clonedChild = cloneElement(child, doc);
            cloned->InsertEndChild(clonedChild);
        }

        return cloned;
    }

    bool shouldSelectElement(cv::tinyxml2::XMLElement* element, const std::string& language) {
        // lunaSVG does not implement foreignObject. draw.io exports an image
        // fallback after each foreignObject, so continue to that fallback.
        if (std::string_view(element->Name()) == "foreignObject") {
            return false;
        }

        // 检查 systemLanguage 属性
        const char* systemLang = element->Attribute("systemLanguage");
        if (systemLang) {
            std::string lang(systemLang);
            // 支持语言列表（空格分隔）
            return lang.find(language) != std::string::npos ||
                lang.find(language.substr(0, 2)) != std::string::npos;
        }

        // 检查 requiredFeatures 属性
        const char* requiredFeatures = element->Attribute("requiredFeatures");
        if (requiredFeatures) {
            // Unknown required features must not be claimed as supported.
            return false;
        }

        // 检查 requiredExtensions 属性
        const char* requiredExtensions = element->Attribute("requiredExtensions");
        if (requiredExtensions) {
            // lunaSVG通常不支持扩展，返回false
            return false;
        }

        // 没有条件属性的元素总是被选中
        return true;
    }
};
