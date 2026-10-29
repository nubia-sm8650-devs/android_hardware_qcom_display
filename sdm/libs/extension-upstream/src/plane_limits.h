/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

namespace sdm {

// MAX_DOWNSCALE_RATIO and MAX_UPSCALE_RATIO of drivers/gpu/drm/msm/disp/dpu1/dpu_plane.c
constexpr float kMaxDownscale = 4.0f;
constexpr float kMaxUpscale = 20.0f;

inline bool IsScaleSupported(float scale_x, float scale_y) {
    return scale_x <= kMaxDownscale && scale_y <= kMaxDownscale &&
           scale_x * kMaxUpscale >= 1.0f && scale_y * kMaxUpscale >= 1.0f;
}

}  // namespace sdm
