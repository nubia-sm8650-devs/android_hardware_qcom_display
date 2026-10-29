/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "strategy_impl.h"

#include <utils/constants.h>
#include <utils/debug.h>
#include <utils/formats.h>
#include <utils/rect.h>

#include <algorithm>
#include <bitset>
#include <tuple>
#include <utility>

#include "plane_limits.h"

#define __CLASS__ "StrategyImpl"

namespace sdm {

namespace {

constexpr size_t kMaxShortestAttempts = 2;
constexpr size_t kMaxMixedAttempts = 3;

bool IsSdrPrimaries(ColorPrimaries primaries) {
    return primaries == ColorPrimaries_BT709_5 || primaries == ColorPrimaries_BT601_6_625 ||
           primaries == ColorPrimaries_BT601_6_525;
}

bool IsSdrTransfer(GammaTransfer transfer) {
    return transfer == Transfer_sRGB || transfer == Transfer_SMPTE_170M ||
           transfer == Transfer_Gamma2_2;
}

}  // namespace

StrategyImpl::StrategyImpl(int32_t display_id, DisplayType type, const HWResourceInfo& hw_res_info,
                           const HWPanelInfo& hw_panel_info,
                           const HWMixerAttributes& mixer_attributes,
                           const HWDisplayAttributes& display_attributes,
                           const DisplayConfigVariableInfo& fb_config)
    : display_id_(display_id),
      hw_res_info_(hw_res_info),
      hw_panel_info_(hw_panel_info),
      mixer_attributes_(mixer_attributes),
      display_attributes_(display_attributes),
      fb_config_(fb_config) {
    UpdatePlaneCaps();
}

void StrategyImpl::UpdatePlaneCaps() {
    plane_formats_.clear();
    for (HWSubBlockType sub_block : {kHWVIGPipe, kHWRGBPipe, kHWDMAPipe}) {
        auto it = hw_res_info_.supported_formats_map.find(sub_block);
        if (it != hw_res_info_.supported_formats_map.end()) {
            plane_formats_.insert(it->second.begin(), it->second.end());
        }
    }

    plane_count_ = 0;
    for (const HWPipeCaps& pipe : hw_res_info_.hw_pipes) {
        if (pipe.type == kPipeTypeVIG || pipe.type == kPipeTypeRGB || pipe.type == kPipeTypeDMA) {
            plane_count_++;
        }
    }
}

DisplayError StrategyImpl::Start(DispLayerStack* disp_layer_stack, uint32_t* max_attempts,
                                 StrategyConstraints* constraints) {
    disp_layer_stack_ = disp_layer_stack;
    attempts_.clear();
    next_attempt_ = 0;

    bool skip_validate = CanSkipValidate(*constraints);

    const HWLayersInfo& info = disp_layer_stack->info;
    bool gpu_target = info.gpu_target_index >= 0 && !gpu_disabled_;

    if (!sde_disabled_ && !constraints->gpu_fallback_mode && !constraints->idle_timeout) {
        AddSdeAttempts(constraints->max_layers, gpu_target);
    }

    if (gpu_target) {
        attempts_.push_back({0, info.app_layer_count});
    }

    if (attempts_.empty()) {
        DLOGW("No composition to try on display %d", display_id_);
        return kErrorNotSupported;
    }

    *max_attempts = UINT32(attempts_.size());

    return skip_validate ? kErrorNone : kErrorNeedsValidate;
}

bool StrategyImpl::CanSkipValidate(const StrategyConstraints& constraints) const {
    const LayerStack& stack = *disp_layer_stack_->stack;
    const HWLayersInfo& info = disp_layer_stack_->info;

    if (force_validate_ || !full_sde_validated_ || constraints.gpu_fallback_mode ||
        constraints.idle_timeout || constraints.safe_mode) {
        return false;
    }

    if (stack.needs_validate || stack.solid_fill_enabled || stack.tonemapper_active ||
        stack.flags.geometry_changed || stack.flags.single_buffered_layer_present) {
        return false;
    }

    if (info.app_layer_count != validated_layer_count_ ||
        info.hw_layers.size() != validated_layer_count_) {
        return false;
    }

    for (uint32_t i = 0; i < info.app_layer_count; i++) {
        const Layer& layer = *stack.layers.at(i);
        std::bitset<kLayerUpdateMax> updates = layer.update_mask;

        // Damage only matters to partial update and to the GPU batch choice
        updates.reset(kSurfaceDamage);
        updates.reset(kSurfaceInvalidate);
        if (layer.geometry_changes || updates.any() || layer.composition != kCompositionSDE) {
            return false;
        }
    }

    return true;
}

void StrategyImpl::AddSdeAttempts(uint32_t max_layers, bool gpu_target) {
    const std::vector<Layer*>& layers = disp_layer_stack_->stack->layers;
    uint32_t count = disp_layer_stack_->info.app_layer_count;
    uint32_t budget = std::min({max_layers, plane_count_, UINT32(kMaxSDELayers)});
    std::vector<uint32_t> gpu_sum(count + 1, 0);
    std::vector<uint32_t> vig_sum(count + 1, 0);
    std::vector<uint32_t> updating_sum(count + 1, 0);

    for (uint32_t i = 0; i < count; i++) {
        const Layer& layer = *layers.at(i);
        bool capable = IsSdeCapable(layer);
        gpu_sum[i + 1] = gpu_sum[i] + !capable;
        vig_sum[i + 1] = vig_sum[i] + (capable && NeedsVig(layer));
        updating_sum[i + 1] = updating_sum[i] + layer.flags.updating;
    }

    bool full_sde = !gpu_sum[count] && count <= budget && vig_sum[count] <= kVigPipes;
    if (full_sde) {
        attempts_.push_back({0, 0});
    }

    if (!gpu_target || budget < 2) {
        return;
    }

    const Layer& target = *layers.at(UINT32(disp_layer_stack_->info.gpu_target_index));
    uint32_t vig_left = kVigPipes - NeedsVig(target);
    std::vector<Window> windows;
    for (uint32_t length = 1; length < count; length++) {
        if (count - length + 1 > budget) {
            continue;
        }

        for (uint32_t start = 0; start + length <= count; start++) {
            uint32_t end = start + length;
            uint32_t vig = vig_sum[end] - vig_sum[start];

            if (gpu_sum[end] - gpu_sum[start] != gpu_sum[count] || vig_sum[count] - vig > vig_left) {
                continue;
            }

            windows.push_back({start, length, vig, updating_sum[end] - updating_sum[start]});
        }
    }

    if (windows.empty()) {
        return;
    }

    auto by_updating = [](const Window& a, const Window& b) {
        return std::tie(a.length, a.updating, a.start) < std::tie(b.length, b.updating, b.start);
    };
    // A stack within the pipe counts can still fail on a VIG pipe split or on bandwidth, so prefer
    // batches that take VIG layers off the planes
    auto by_vig = [](const Window& a, const Window& b) {
        return std::tie(a.length, b.vig, a.updating, a.start) <
               std::tie(b.length, a.vig, b.updating, b.start);
    };

    size_t mixed = 0;
    if (!full_sde) {
        uint32_t shortest = std::min_element(windows.begin(), windows.end(), by_updating)->length;
        auto shortest_end = std::partition(windows.begin(), windows.end(),
                                           [&](const Window& w) { return w.length == shortest; });

        std::sort(windows.begin(), shortest_end, by_updating);
        for (auto it = windows.begin(); it != shortest_end && mixed < kMaxShortestAttempts; it++) {
            attempts_.push_back({it->start, it->start + it->length});
            mixed++;
        }

        windows.erase(windows.begin(), shortest_end);
    }

    std::sort(windows.begin(), windows.end(), by_vig);
    for (auto it = windows.begin(); it != windows.end() && mixed < kMaxMixedAttempts; it++) {
        attempts_.push_back({it->start, it->start + it->length});
        mixed++;
    }
}

bool StrategyImpl::IsSdeCapable(const Layer& layer) const {
    const LayerBuffer& buffer = layer.input_buffer;

    if (layer.flags.skip || layer.flags.solid_fill || layer.flags.color_transform ||
        layer.flags.is_demura || layer.flags.is_noise || layer.flags.is_cwb) {
        return false;
    }

    if (buffer.planes[0].fd < 0 || !plane_formats_.count(buffer.format)) {
        return false;
    }

    if (buffer.flags.secure || buffer.flags.secure_display || buffer.flags.secure_camera ||
        buffer.flags.interlace || buffer.flags.hdr) {
        return false;
    }

    if (layer.blending == kBlendingSkip || layer.transform.rotation != 0.0f ||
        layer.layer_brightness != 1.0f) {
        return false;
    }

    return IsColorSupported(buffer) && IsGeometrySupported(layer);
}

bool StrategyImpl::IsColorSupported(const LayerBuffer& buffer) const {
    const ColorMetaData& color = buffer.color_metadata;

    if (color.range == Range_Extended) {
        return false;
    }

    // HWDeviceDRM::SelectCscType() derives the YUV matrix from the primaries, and picks the Dolby
    // Vision profile 5 matrix that upstream msm lacks when extended metadata is present
    if (!IsRgbFormat(buffer.format) &&
        (!IsSdrPrimaries(color.colorPrimaries) ||
         (color.range != Range_Limited && color.range != Range_Full) ||
         (buffer.extended_content_metadata && buffer.extended_content_metadata->size))) {
        return false;
    }

    if (blend_space_.primaries == ColorPrimaries_BT709_5 &&
        blend_space_.transfer == Transfer_sRGB) {
        return IsSdrPrimaries(color.colorPrimaries) && IsSdrTransfer(color.transfer);
    }

    return color.colorPrimaries == blend_space_.primaries &&
           color.transfer == blend_space_.transfer;
}

bool StrategyImpl::IsGeometrySupported(const Layer& layer) const {
    LayerRect mixer(0.0f, 0.0f, FLOAT(mixer_attributes_.width), FLOAT(mixer_attributes_.height));
    LayerRect dst = MapDstToMixer(layer.dst_rect);
    float scale_x = 1.0f;
    float scale_y = 1.0f;

    if (!IsValid(Intersection(dst, mixer))) {
        return false;
    }

    if (GetScaleFactor(layer.src_rect, dst, false, &scale_x, &scale_y) != kErrorNone) {
        return false;
    }

    return IsScaleSupported(scale_x, scale_y);
}

bool StrategyImpl::NeedsVig(const Layer& layer) const {
    LayerRect dst = MapDstToMixer(layer.dst_rect);

    return !IsRgbFormat(layer.input_buffer.format) ||
           INT(layer.src_rect.right - layer.src_rect.left) != INT(dst.right - dst.left) ||
           INT(layer.src_rect.bottom - layer.src_rect.top) != INT(dst.bottom - dst.top);
}

DisplayError StrategyImpl::GetNextStrategy() {
    if (next_attempt_ >= attempts_.size()) {
        return kErrorNotSupported;
    }

    const Attempt& attempt = attempts_[next_attempt_++];
    HWLayersInfo& info = disp_layer_stack_->info;
    std::vector<Layer*>& layers = disp_layer_stack_->stack->layers;

    force_validate_ = false;
    full_sde_validated_ = attempt.gpu_start == attempt.gpu_end;
    validated_layer_count_ = info.app_layer_count;

    info.index.clear();
    info.roi_index.clear();
    info.hw_layers.clear();

    for (uint32_t i = 0; i < info.app_layer_count; i++) {
        Layer* layer = layers.at(i);
        bool gpu = i >= attempt.gpu_start && i < attempt.gpu_end;

        layer->request.flags.request_flags = 0;
        layer->composition = gpu ? kCompositionGPU : kCompositionSDE;

        if (!gpu) {
            AddHwLayer(i);
        } else if (i == attempt.gpu_start) {
            AddHwLayer(UINT32(info.gpu_target_index));
        }
    }

    return kErrorNone;
}

void StrategyImpl::AddHwLayer(uint32_t index) {
    HWLayersInfo& info = disp_layer_stack_->info;
    Layer layer = *disp_layer_stack_->stack->layers.at(index);

    MapToMixer(&layer);

    info.index.push_back(index);
    info.roi_index.push_back(0);
    info.hw_layers.push_back(layer);
}

LayerRect StrategyImpl::MapDstToMixer(const LayerRect& dst) const {
    LayerRect fb(0.0f, 0.0f, FLOAT(fb_config_.x_pixels), FLOAT(fb_config_.y_pixels));
    LayerRect mixer(0.0f, 0.0f, FLOAT(mixer_attributes_.width), FLOAT(mixer_attributes_.height));
    const HWPanelOrientation& orientation = hw_panel_info_.panel_orientation;
    LayerRect out = dst;

    if (orientation.flip_horizontal || orientation.flip_vertical) {
        LayerTransform panel;
        panel.flip_horizontal = orientation.flip_horizontal;
        panel.flip_vertical = orientation.flip_vertical;
        TransformHV(fb, dst, panel, &out);
    }

    MapRect(fb, mixer, out, &out);

    return out;
}

void StrategyImpl::MapToMixer(Layer* layer) const {
    const HWPanelOrientation& orientation = hw_panel_info_.panel_orientation;

    layer->dst_rect = MapDstToMixer(layer->dst_rect);
    layer->transform.flip_horizontal ^= orientation.flip_horizontal;
    layer->transform.flip_vertical ^= orientation.flip_vertical;
}

DisplayError StrategyImpl::Stop() {
    return kErrorNone;
}

DisplayError StrategyImpl::SetDrawMethod(const DisplayDrawMethod& draw_method) {
    return kErrorNotSupported;
}

DisplayError StrategyImpl::Reconfigure(const HWPanelInfo& hw_panel_info,
                                       const HWResourceInfo& hw_res_info,
                                       const HWDisplayAttributes& display_attributes,
                                       const HWMixerAttributes& mixer_attributes,
                                       const DisplayConfigVariableInfo& fb_config) {
    force_validate_ = true;
    hw_panel_info_ = hw_panel_info;
    hw_res_info_ = hw_res_info;
    display_attributes_ = display_attributes;
    mixer_attributes_ = mixer_attributes;
    fb_config_ = fb_config;
    UpdatePlaneCaps();

    return kErrorNone;
}

DisplayError StrategyImpl::SetCompositionState(LayerComposition composition_type, bool enable) {
    force_validate_ = true;
    DLOGI("Composition type %d %s on display %d", composition_type,
          enable ? "enabled" : "disabled", display_id_);

    if (composition_type == kCompositionGPU) {
        gpu_disabled_ = !enable;
    } else if (composition_type == kCompositionSDE) {
        sde_disabled_ = !enable;
    }

    return kErrorNone;
}

DisplayError StrategyImpl::Purge() {
    force_validate_ = true;
    return kErrorNone;
}

DisplayError StrategyImpl::SetIdleTimeoutMs(uint32_t active_ms, uint32_t inactive_ms) {
    return kErrorNotSupported;
}

DisplayError StrategyImpl::SetColorModesInfo(const std::vector<PrimariesTransfer>& colormodes_cs) {
    return kErrorNone;
}

DisplayError StrategyImpl::SetBlendSpace(const PrimariesTransfer& blend_space) {
    force_validate_ = true;
    blend_space_ = blend_space;
    return kErrorNone;
}

void StrategyImpl::SetDisplayLayerStack(DispLayerStack* disp_layer_stack) {
    disp_layer_stack_ = disp_layer_stack;
}

}  // namespace sdm
