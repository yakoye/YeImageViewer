#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool decode_lepton(
    const uint8_t* lepFileBuf,
    size_t lepFileSize,
    uint8_t** jpegData,
    int* jpegSize);

void free_lepton_buffer(uint8_t* jpegData, int jpegSize);

#ifdef __cplusplus
}
#endif
