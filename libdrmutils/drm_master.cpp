/*
* Copyright (c) 2017 - 2018, 2021 The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of The Linux Foundation nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
// Intentionally included after xf86 headers so that they in-turn include libdrm version of drm.h
// that doesn't use keyword "virtual" for a variable name. Not doing so leads to the kernel version
// of drm.h being included causing compilation to fail
#include <drm/msm_drm.h>
#include <display/drm/sde_drm.h>
#include <algorithm>
#include <iterator>
#include <chrono>
#include <thread>

#include "drm_master.h"

#define __CLASS__ "DRMMaster"

#ifndef DRM_IOCTL_MODE_CLOSEFB
struct drm_mode_closefb {
  __u32 fb_id;
  __u32 pad;
};
#define DRM_IOCTL_MODE_CLOSEFB DRM_IOWR(0xD0, struct drm_mode_closefb)
#endif

using std::mutex;
using std::lock_guard;
using std::begin;
using std::copy;
using std::end;
using std::fill;

namespace drm_utils {

DRMMaster *DRMMaster::s_instance = nullptr;
mutex DRMMaster::s_lock;

int DRMMaster::GetInstance(DRMMaster **master) {
  lock_guard<mutex> obj(s_lock);

  if (!s_instance) {
    s_instance = new DRMMaster();
    if (s_instance->Init() < 0) {
      delete s_instance;
      s_instance = nullptr;
      return -ENODEV;
    }
  }

  *master = s_instance;
  return 0;
}

void DRMMaster::DestroyInstance() {
  lock_guard<mutex> obj(s_lock);
  delete s_instance;
  s_instance = nullptr;
}

#ifdef TARGET_USES_UPSTREAM_MSM
static const char *const kDriverNames[] = {"msm-kms", "msm"};
#else
static const char *const kDriverNames[] = {"msm_drm"};
#endif

static bool HasModesetResources(int fd) {
  drmModeRes *res = drmModeGetResources(fd);
  bool ret = res && res->count_crtcs > 0;
  drmModeFreeResources(res);
  return ret;
}

static bool SupportsCloseFb(int fd) {
  struct drm_mode_closefb closefb = {};
  return drmIoctl(fd, DRM_IOCTL_MODE_CLOSEFB, &closefb) && errno == ENOENT;
}

int DRMMaster::Init() {
  for (uint8_t retry = 0; retry <= MAX_RETRY; retry++) {
    for (const char *name : kDriverNames) {
      int fd = drmOpen(name, nullptr);
      if (fd < 0) {
        continue;
      }

      // The upstream msm driver can expose a GPU only device under the same name
      if (kUpstreamMsm && !HasModesetResources(fd)) {
        drmClose(fd);
        continue;
      }

      dev_fd_ = fd;
      driver_name_ = name;
      closefb_supported_ = kUpstreamMsm && SupportsCloseFb(fd);
      DLOGI("Opened DRM device of driver %s", name);
      return 0;
    }

    DRM_LOGW("drmOpen failed for all display drivers, retry %d", retry);
    if (retry < MAX_RETRY) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  return -ENODEV;
}

int DRMMaster::OpenDevice() const {
  return drmOpen(driver_name_.c_str(), nullptr);
}

DRMMaster::~DRMMaster() {
  drmClose(dev_fd_);
  dev_fd_ = -1;
}

int DRMMaster::CreateFbId(const DRMBuffer &drm_buffer, uint32_t *fb_id) {
  lock_guard<mutex> obj(s_lock);
  uint32_t gem_handle = 0;
  int ret = drmPrimeFDToHandle(dev_fd_, drm_buffer.fd, &gem_handle);
  if (ret) {
    DRM_LOGE("drmPrimeFDToHandle failed with error %d", ret);
    return ret;
  }

  struct drm_mode_fb_cmd2 cmd2 {};
  cmd2.width = drm_buffer.width;
  cmd2.height = drm_buffer.height;
  cmd2.pixel_format = drm_buffer.drm_format;
  cmd2.flags = DRM_MODE_FB_MODIFIERS;
  fill(begin(cmd2.handles), begin(cmd2.handles) + drm_buffer.num_planes, gem_handle);
  copy(begin(drm_buffer.stride), end(drm_buffer.stride), begin(cmd2.pitches));
  copy(begin(drm_buffer.offset), end(drm_buffer.offset), begin(cmd2.offsets));
  fill(begin(cmd2.modifier), begin(cmd2.modifier) + drm_buffer.num_planes,
       drm_buffer.drm_format_modifier);

  if ((ret = drmIoctl(dev_fd_, DRM_IOCTL_MODE_ADDFB2, &cmd2))) {
    DRM_LOGE("DRM_IOCTL_MODE_ADDFB2 failed with error %d", ret);
  } else {
    *fb_id = cmd2.fb_id;
  }

  struct drm_gem_close gem_close = {};
  gem_close.handle = gem_handle;
  int ret1 = drmIoctl(dev_fd_, DRM_IOCTL_GEM_CLOSE, &gem_close);
  if (ret1) {
    DRM_LOGE("drmIoctl::DRM_IOCTL_GEM_CLOSE failed with error %d", ret1);
    return ret1;
  }

  return ret;
}

int DRMMaster::RemoveFbId(uint32_t fb_id) {
  lock_guard<mutex> obj(s_lock);
  int ret = 0;
  if (kUpstreamMsm) {
    if (closefb_supported_) {
      // CLOSEFB drops the reference without disabling the planes scanning out of the FB
      struct drm_mode_closefb closefb = {};
      closefb.fb_id = fb_id;
      ret = drmIoctl(dev_fd_, DRM_IOCTL_MODE_CLOSEFB, &closefb);
    } else {
      ret = drmModeRmFB(dev_fd_, fb_id);
    }
    if (ret) {
      DRM_LOGE("Closing fb_id %d failed with error %d", fb_id, errno);
    }
    return ret;
  }
#ifdef DRM_IOCTL_MSM_RMFB2
  ret = drmIoctl(dev_fd_, DRM_IOCTL_MSM_RMFB2, &fb_id);
  if (ret) {
    DRM_LOGE("drmIoctl::DRM_IOCTL_MSM_RMFB2 failed for fb_id %d with error %d", fb_id, errno);
  }
#else
  DRM_LOGE("drmModeRmFB is no longer used. DRM_IOCTL_MSM_RMFB2 not found");
#endif
  return ret;
}

bool DRMMaster::IsRmFbRefCounted() {
  if (kUpstreamMsm) {
    return closefb_supported_;
  }
#ifdef DRM_IOCTL_MSM_RMFB2
  return true;
#endif
  return false;
}

}  // namespace drm_utils
