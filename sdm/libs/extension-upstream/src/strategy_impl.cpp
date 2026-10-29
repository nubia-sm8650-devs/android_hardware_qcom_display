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

#include "plane_limits.h"

#define __CLASS__ "StrategyImpl"

namespace sdm {

namespace {

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

    const HWLayersInfo& info = disp_layer_stack->info;
    if (!sde_disabled_ && !constraints->gpu_fallback_mode && !constraints->idle_timeout) {
        AddSdeAttempts(constraints->max_layers);
    }

    if (info.gpu_target_index >= 0 && !gpu_disabled_) {
        attempts_.push_back({0, info.app_layer_count});
    }

    if (attempts_.empty()) {
        DLOGW("No composition to try on display %d", display_id_);
        return kErrorNotSupported;
    }

    *max_attempts = UINT32(attempts_.size());

    return kErrorNeedsValidate;
}

void StrategyImpl::AddSdeAttempts(uint32_t max_layers) {
    const std::vector<Layer*>& layers = disp_layer_stack_->stack->layers;
    uint32_t count = disp_layer_stack_->info.app_layer_count;
    uint32_t budget = std::min({max_layers, plane_count_, UINT32(kMaxSDELayers)});

    for (uint32_t i = 0; i < count; i++) {
        if (!IsSdeCapable(*layers.at(i))) {
            return;
        }
    }

    if (count <= budget) {
        attempts_.push_back({0, 0});
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

    // _dpu_plane_get_csc() converts every YUV format with BT.601 limited range coefficients
    if (!IsRgbFormat(buffer.format) &&
        ((color.matrixCoefficients != MatrixCoEff_BT601_6_625 &&
          color.matrixCoefficients != MatrixCoEff_BT601_6_525) ||
         color.range != Range_Limited)) {
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

DisplayError StrategyImpl::GetNextStrategy() {
    if (next_attempt_ >= attempts_.size()) {
        return kErrorNotSupported;
    }

    const Attempt& attempt = attempts_[next_attempt_++];
    HWLayersInfo& info = disp_layer_stack_->info;
    std::vector<Layer*>& layers = disp_layer_stack_->stack->layers;

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
    hw_panel_info_ = hw_panel_info;
    hw_res_info_ = hw_res_info;
    display_attributes_ = display_attributes;
    mixer_attributes_ = mixer_attributes;
    fb_config_ = fb_config;
    UpdatePlaneCaps();

    return kErrorNone;
}

DisplayError StrategyImpl::SetCompositionState(LayerComposition composition_type, bool enable) {
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
    return kErrorNone;
}

DisplayError StrategyImpl::SetIdleTimeoutMs(uint32_t active_ms, uint32_t inactive_ms) {
    return kErrorNotSupported;
}

DisplayError StrategyImpl::SetColorModesInfo(const std::vector<PrimariesTransfer>& colormodes_cs) {
    return kErrorNone;
}

DisplayError StrategyImpl::SetBlendSpace(const PrimariesTransfer& blend_space) {
    blend_space_ = blend_space;
    return kErrorNone;
}

void StrategyImpl::SetDisplayLayerStack(DispLayerStack* disp_layer_stack) {
    disp_layer_stack_ = disp_layer_stack;
}

}  // namespace sdm
