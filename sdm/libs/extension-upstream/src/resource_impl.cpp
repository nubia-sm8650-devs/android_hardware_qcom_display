/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "resource_impl.h"

#include <utils/constants.h>
#include <utils/debug.h>
#include <utils/rect.h>

#include <algorithm>
#include <new>
#include <utility>

#include "plane_limits.h"

#define __CLASS__ "ResourceImpl"

namespace sdm {

namespace {

bool Contains(const std::vector<uint32_t>& planes, uint32_t plane_id) {
    return std::find(planes.begin(), planes.end(), plane_id) != planes.end();
}

bool ClipToMixer(const LayerRect& mixer, const LayerTransform& transform, LayerRect* src,
                 LayerRect* dst) {
    if (!IsValid(*src) || !IsValid(*dst)) {
        return false;
    }

    float src_width = src->right - src->left;
    float src_height = src->bottom - src->top;
    float dst_width = dst->right - dst->left;
    float dst_height = dst->bottom - dst->top;

    float cut_left = std::max(0.0f, mixer.left - dst->left) / dst_width;
    float cut_right = std::max(0.0f, dst->right - mixer.right) / dst_width;
    float cut_top = std::max(0.0f, mixer.top - dst->top) / dst_height;
    float cut_bottom = std::max(0.0f, dst->bottom - mixer.bottom) / dst_height;

    if (transform.flip_horizontal) {
        std::swap(cut_left, cut_right);
    }
    if (transform.flip_vertical) {
        std::swap(cut_top, cut_bottom);
    }

    *dst = Intersection(*dst, mixer);
    src->left += src_width * cut_left;
    src->right -= src_width * cut_right;
    src->top += src_height * cut_top;
    src->bottom -= src_height * cut_bottom;

    Normalize(1, 1, src);
    Normalize(1, 1, dst);

    return IsValid(*src) && IsValid(*dst);
}

}  // namespace

ResourceImpl::ResourceImpl(const HWResourceInfo& hw_res_info) : hw_res_info_(hw_res_info) {}

ResourceImpl::~ResourceImpl() {
    for (DisplayContext* ctx : displays_) {
        delete ctx;
    }
}

DisplayError ResourceImpl::Init() {
    for (const HWPipeCaps& pipe : hw_res_info_.hw_pipes) {
        switch (pipe.type) {
            case kPipeTypeVIG:
            case kPipeTypeRGB:
            case kPipeTypeDMA:
                planes_.push_back(pipe.id);
                break;
            default:
                break;
        }
    }

    if (planes_.empty()) {
        DLOGE("No planes to compose on");
        return kErrorResources;
    }

    DLOGI("Composing on %zu planes", planes_.size());

    return kErrorNone;
}

DisplayError ResourceImpl::RegisterDisplay(int32_t display_id, DisplayType type,
                                           const HWDisplayAttributes& display_attributes,
                                           const HWPanelInfo& hw_panel_info,
                                           const HWMixerAttributes& mixer_attributes,
                                           const Resolution& fb_resolution, Handle* display_ctx) {
    if (type != kBuiltIn && type != kPluggable) {
        DLOGW("Display %d of type %d is not supported", display_id, type);
        return kErrorParameters;
    }

    DisplayContext* ctx = new (std::nothrow) DisplayContext();
    if (!ctx) {
        return kErrorMemory;
    }

    ctx->display_id = display_id;
    ctx->mixer_attributes = mixer_attributes;
    displays_.push_back(ctx);

    *display_ctx = ctx;

    return kErrorNone;
}

DisplayError ResourceImpl::UnregisterDisplay(Handle display_ctx) {
    DisplayContext* ctx = reinterpret_cast<DisplayContext*>(display_ctx);

    displays_.erase(std::remove(displays_.begin(), displays_.end(), ctx), displays_.end());
    delete ctx;

    return kErrorNone;
}

DisplayError ResourceImpl::ReconfigureDisplay(Handle display_ctx,
                                              const HWDisplayAttributes& display_attributes,
                                              const HWPanelInfo& hw_panel_info,
                                              const HWMixerAttributes& mixer_attributes,
                                              const Resolution& fb_resolution) {
    DisplayContext* ctx = reinterpret_cast<DisplayContext*>(display_ctx);

    ctx->mixer_attributes = mixer_attributes;

    return kErrorNone;
}

DisplayError ResourceImpl::Start(Handle display_ctx, LayerStack* layer_stack) {
    return kErrorNone;
}

DisplayError ResourceImpl::Precheck(Handle display_ctx, DispLayerStack* disp_layer_stack,
                                    LayerFeedback* feedback) {
    return kErrorNone;
}

DisplayError ResourceImpl::Stop(Handle display_ctx, DispLayerStack* disp_layer_stack) {
    return kErrorNone;
}

DisplayError ResourceImpl::SetDrawMethod(Handle display_ctx,
                                         const DisplayDrawMethod& draw_method) {
    return kErrorNone;
}

bool ResourceImpl::IsPlaneFree(const DisplayContext* ctx, uint32_t plane_id) const {
    for (const DisplayContext* other : displays_) {
        if (other == ctx) {
            continue;
        }

        if (Contains(other->prepared_planes, plane_id) ||
            Contains(other->committed_planes, plane_id)) {
            return false;
        }
    }

    return true;
}

DisplayError ResourceImpl::ConfigPipe(const DisplayContext* ctx, const Layer& layer,
                                      HWPipeInfo* pipe) const {
    LayerRect mixer(0.0f, 0.0f, FLOAT(ctx->mixer_attributes.width),
                    FLOAT(ctx->mixer_attributes.height));
    LayerRect src = layer.src_rect;
    LayerRect dst = layer.dst_rect;

    if (!ClipToMixer(mixer, layer.transform, &src, &dst)) {
        Log(kTagResources, "Unusable src_rect", layer.src_rect);
        Log(kTagResources, "Unusable dst_rect", layer.dst_rect);
        return kErrorNotSupported;
    }

    pipe->src_roi = src;
    pipe->dst_roi = dst;
    pipe->format = layer.input_buffer.format;
    pipe->transform = layer.transform;
    pipe->valid = true;

    return kErrorNone;
}

DisplayError ResourceImpl::Prepare(Handle display_ctx, DispLayerStack* disp_layer_stack,
                                   LayerFeedback* feedback) {
    DisplayContext* ctx = reinterpret_cast<DisplayContext*>(display_ctx);
    HWLayersInfo& info = disp_layer_stack->info;
    size_t count = info.hw_layers.size();

    *feedback = LayerFeedback(0);
    ctx->prepared_planes.clear();

    if (count > kMaxSDELayers) {
        return kErrorResources;
    }

    std::vector<uint32_t> planes = ctx->committed_planes;
    for (uint32_t plane_id : planes_) {
        if (!Contains(planes, plane_id) && IsPlaneFree(ctx, plane_id)) {
            planes.push_back(plane_id);
        }
    }

    if (count > planes.size()) {
        DLOGV_IF(kTagResources, "%zu layers for %zu planes on display %d", count, planes.size(),
                 ctx->display_id);
        return kErrorResources;
    }

    for (size_t i = 0; i < count; i++) {
        HWLayerConfig& config = info.config[i];
        config = {};

        DisplayError error = ConfigPipe(ctx, info.hw_layers.at(i), &config.left_pipe);
        if (error != kErrorNone) {
            ctx->prepared_planes.clear();
            return error;
        }

        config.left_pipe.pipe_id = planes[i];
        config.left_pipe.z_order = UINT32(i);
        ctx->prepared_planes.push_back(planes[i]);
    }

    return kErrorNone;
}

DisplayError ResourceImpl::PostPrepare(Handle display_ctx, DispLayerStack* disp_layer_stack) {
    return kErrorNone;
}

DisplayError ResourceImpl::Commit(Handle display_ctx, DispLayerStack* disp_layer_stack) {
    return kErrorNone;
}

DisplayError ResourceImpl::PostCommit(Handle display_ctx, DispLayerStack* disp_layer_stack) {
    DisplayContext* ctx = reinterpret_cast<DisplayContext*>(display_ctx);

    ctx->committed_planes = ctx->prepared_planes;

    LayerStack* stack = disp_layer_stack->stack;
    const HWLayersInfo& info = disp_layer_stack->info;
    if (!stack) {
        return kErrorNone;
    }

    stack->retire_fence = info.retire_fence;

    // A layer that left the planes with this commit stays in use until the commit is on screen
    std::shared_ptr<Fence> release_fence;
    if (!info.hw_layers.empty()) {
        release_fence = info.hw_layers.front().input_buffer.release_fence;
    }

    for (Layer* layer : stack->layers) {
        layer->input_buffer.release_fence = release_fence;
    }

    return kErrorNone;
}

void ResourceImpl::Purge(Handle display_ctx) {
    DisplayContext* ctx = reinterpret_cast<DisplayContext*>(display_ctx);

    ctx->prepared_planes.clear();
    ctx->committed_planes.clear();
}

DisplayError ResourceImpl::SetMaxMixerStages(Handle display_ctx, uint32_t max_mixer_stages) {
    return kErrorNone;
}

DisplayError ResourceImpl::ValidateScaling(const LayerRect& crop, const LayerRect& dst,
                                           bool rotate90, BufferLayout layout,
                                           bool use_rotator_downscale) {
    float scale_x = 1.0f;
    float scale_y = 1.0f;

    DisplayError error = GetScaleFactor(crop, dst, rotate90, &scale_x, &scale_y);
    if (error != kErrorNone) {
        return error;
    }

    return IsScaleSupported(scale_x, scale_y) ? kErrorNone : kErrorNotSupported;
}

DisplayError ResourceImpl::ValidateAndSetCursorPosition(Handle display_ctx,
                                                        DispLayerStack* disp_layer_stack, int x,
                                                        int y,
                                                        DisplayConfigVariableInfo* fb_config) {
    return kErrorNotSupported;
}

DisplayError ResourceImpl::SetMaxBandwidthMode(HWBwModes mode) {
    return kErrorNotSupported;
}

DisplayError ResourceImpl::GetScaleLutConfig(HWScaleLutInfo* lut_info) {
    return kErrorNone;
}

DisplayError ResourceImpl::SetDetailEnhancerData(Handle display_ctx,
                                                 const DisplayDetailEnhancerData& de_data) {
    return kErrorNotSupported;
}

DisplayError ResourceImpl::UpdateSyncHandle(Handle display_ctx, const SyncPoints& sync_points) {
    return kErrorNotSupported;
}

DisplayError ResourceImpl::Perform(int cmd, ...) {
    return kErrorNone;
}

bool ResourceImpl::IsRotatorSupportedFormat(LayerBufferFormat format) {
    return false;
}

DisplayError ResourceImpl::FreeDemuraFetchResources(const int32_t& display_id) {
    return kErrorNone;
}

DisplayError ResourceImpl::GetDemuraFetchResourceCount(
        std::map<uint32_t, uint8_t>* fetch_resource_cnt) {
    return kErrorNone;
}

DisplayError ResourceImpl::ReserveDemuraFetchResources(const int32_t& display_id,
                                                       const int8_t& preferred_rect) {
    return kErrorNone;
}

DisplayError ResourceImpl::GetDemuraFetchResources(Handle display_ctx, FetchResourceList* frl) {
    return kErrorNone;
}

DisplayError ResourceImpl::SetMaxSDEClk(Handle display_ctx, uint32_t clk) {
    return kErrorNotSupported;
}

DisplayError ResourceImpl::ForceToneMapConfigure(Handle display_ctx,
                                                 DispLayerStack* disp_layer_stack) {
    return kErrorNotSupported;
}

bool ResourceImpl::ToneMapQueryRequested(Handle display_ctx) {
    return false;
}

DisplayError ResourceImpl::PreCommit(Handle display_ctx) {
    return kErrorNone;
}

bool ResourceImpl::HandleCwbTeardown(Handle display_ctx) {
    return false;
}

DisplayError ResourceImpl::RequestVirtualDisplayId(int32_t* vdisp_id) {
    return kErrorResources;
}

DisplayError ResourceImpl::AllocateVirtualDisplayId(int32_t* vdisp_id) {
    return kErrorResources;
}

DisplayError ResourceImpl::DeallocateVirtualDisplayId(int32_t vdisp_id) {
    return kErrorResources;
}

void ResourceImpl::HandleSkipValidate(Handle display_ctx) {}

std::string ResourceImpl::Dump(Handle display_ctx) {
    DisplayContext* ctx = reinterpret_cast<DisplayContext*>(display_ctx);
    std::string planes;

    for (uint32_t plane_id : ctx->committed_planes) {
        planes += " " + std::to_string(plane_id);
    }

    return "Planes:" + planes + "\n";
}

uint32_t ResourceImpl::GetMixerCount() {
    return 0;
}

DisplayError ResourceImpl::SetBlendSpace(Handle display_ctx,
                                         const PrimariesTransfer& blend_space) {
    return kErrorNone;
}

void ResourceImpl::HandleTUITransition(Handle display_ctx, bool tui_active) {}

void ResourceImpl::GetDSConfig(Handle display_ctx, DestScaleInfoMap* dest_scale_info_map) {}

bool ResourceImpl::IsDisplayHWAvailable() {
    return true;
}

}  // namespace sdm
