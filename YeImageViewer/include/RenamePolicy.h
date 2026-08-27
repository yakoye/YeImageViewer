#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef small
#undef small
#endif

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace RenamePolicy {

enum class ValidationError {
    None,
    Empty,
    InvalidCharacter,
    TrailingDotOrSpace,
    ReservedName,
    TooLong,
};

enum class OperationError {
    None,
    NoChange,
    InvalidName,
    AlreadyExists,
    SystemError,
};

struct OperationResult {
    OperationError error = OperationError::None;
    ValidationError validation = ValidationError::None;
    std::filesystem::path target;
    DWORD systemError = ERROR_SUCCESS;
};

inline std::wstring trim(std::wstring_view value) {
    const auto first = std::find_if_not(value.begin(), value.end(),
        [](wchar_t character) { return std::iswspace(character) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(),
        [](wchar_t character) { return std::iswspace(character) != 0; }).base();
    if (first >= last)
        return {};
    return std::wstring(first, last);
}

inline bool isReservedDeviceName(std::wstring_view filenameStem) {
    const auto dot = filenameStem.find(L'.');
    std::wstring base(filenameStem.substr(0, dot));
    std::transform(base.begin(), base.end(), base.begin(),
        [](wchar_t character) { return static_cast<wchar_t>(std::towupper(character)); });

    if (base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL")
        return true;
    if (base.size() == 4 && (base.starts_with(L"COM") || base.starts_with(L"LPT")) &&
        base[3] >= L'1' && base[3] <= L'9')
        return true;
    return false;
}

inline ValidationError validate(std::wstring_view filenameStem, std::wstring_view extension = {}) {
    if (filenameStem.empty())
        return ValidationError::Empty;

    for (const wchar_t character : filenameStem) {
        if (character < 32 || std::wstring_view(L"<>:\"/\\|?*").contains(character))
            return ValidationError::InvalidCharacter;
    }
    if (filenameStem.ends_with(L' ') || filenameStem.ends_with(L'.'))
        return ValidationError::TrailingDotOrSpace;
    if (isReservedDeviceName(filenameStem))
        return ValidationError::ReservedName;
    if (filenameStem.size() + extension.size() > 255)
        return ValidationError::TooLong;
    return ValidationError::None;
}

inline std::filesystem::path buildTargetPath(
    const std::filesystem::path& currentPath, std::wstring_view filenameStem) {
    return currentPath.parent_path() /
        (std::wstring(filenameStem) + currentPath.extension().wstring());
}

inline OperationResult renameFile(
    const std::filesystem::path& source, std::wstring_view requestedStem) {
    const auto filenameStem = trim(requestedStem);
    const auto validation = validate(filenameStem, source.extension().wstring());
    if (validation != ValidationError::None)
        return { OperationError::InvalidName, validation };

    const auto target = buildTargetPath(source, filenameStem);
    const auto sourceText = source.wstring();
    const auto targetText = target.wstring();
    if (sourceText == targetText)
        return { OperationError::NoChange, ValidationError::None, target };

    std::error_code sourceError;
    if (!std::filesystem::is_regular_file(source, sourceError)) {
        return { OperationError::SystemError, ValidationError::None, target,
            sourceError ? static_cast<DWORD>(sourceError.value()) : ERROR_FILE_NOT_FOUND };
    }

    const bool samePathIgnoringCase = CompareStringOrdinal(
        sourceText.c_str(), static_cast<int>(sourceText.size()),
        targetText.c_str(), static_cast<int>(targetText.size()), TRUE) == CSTR_EQUAL;
    std::error_code existsError;
    if (std::filesystem::exists(target, existsError) && !samePathIgnoringCase)
        return { OperationError::AlreadyExists, ValidationError::None, target, ERROR_FILE_EXISTS };

    if (!MoveFileExW(sourceText.c_str(), targetText.c_str(), MOVEFILE_WRITE_THROUGH))
        return { OperationError::SystemError, ValidationError::None, target, GetLastError() };
    return { OperationError::None, ValidationError::None, target };
}

} // namespace RenamePolicy
