#pragma once

#include <vector>
#include <string>
#include <string_view>
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
