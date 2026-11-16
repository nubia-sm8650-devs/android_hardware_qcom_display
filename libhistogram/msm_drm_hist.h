/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#ifndef HISTOGRAM_MSM_DRM_HIST_H_
#define HISTOGRAM_MSM_DRM_HIST_H_

#include <linux/types.h>

#define HIST_V_SIZE 256

struct drm_msm_hist {
  __u64 flags;
  __u32 data[HIST_V_SIZE];
};

#endif  // HISTOGRAM_MSM_DRM_HIST_H_
