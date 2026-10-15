#pragma once
namespace RawDecodePolicy {
inline constexpr bool valid(int status, int errors) { return status == 0 && errors == 0; }
}
