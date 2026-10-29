/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

namespace sdm {

// MAX_DOWNSCALE_RATIO and MAX_UPSCALE_RATIO of drivers/gpu/drm/msm/disp/dpu1/dpu_plane.c
constexpr float kMaxDownscale = 4.0f;
constexpr float kMaxUpscale = 20.0f;

// SSPP_VIG0-3 of catalog/dpu_10_0_sm8650.h. dpu_rm_reserve_sspp() gives YUV and scaled planes
// only VIG pipes.
constexpr uint32_t kVigPipes = 4;

inline bool IsScaleSupported(float scale_x, float scale_y) {
    return scale_x <= kMaxDownscale && scale_y <= kMaxDownscale &&
           scale_x * kMaxUpscale >= 1.0f && scale_y * kMaxUpscale >= 1.0f;
}

}  // namespace sdm
