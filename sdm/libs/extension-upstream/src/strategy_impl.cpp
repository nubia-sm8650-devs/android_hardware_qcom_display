/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "strategy_impl.h"

#include <utils/constants.h>
#include <utils/debug.h>
#include <utils/rect.h>

#define __CLASS__ "StrategyImpl"

namespace sdm {

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
      fb_config_(fb_config) {}

DisplayError StrategyImpl::Start(DispLayerStack* disp_layer_stack, uint32_t* max_attempts,
                                 StrategyConstraints* constraints) {
    disp_layer_stack_ = disp_layer_stack;
    attempts_.clear();
    next_attempt_ = 0;

    const HWLayersInfo& info = disp_layer_stack->info;
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

void StrategyImpl::MapToMixer(Layer* layer) const {
    LayerRect fb(0.0f, 0.0f, FLOAT(fb_config_.x_pixels), FLOAT(fb_config_.y_pixels));
    LayerRect mixer(0.0f, 0.0f, FLOAT(mixer_attributes_.width), FLOAT(mixer_attributes_.height));
    const HWPanelOrientation& orientation = hw_panel_info_.panel_orientation;

    if (orientation.flip_horizontal || orientation.flip_vertical) {
        LayerTransform panel;
        panel.flip_horizontal = orientation.flip_horizontal;
        panel.flip_vertical = orientation.flip_vertical;

        TransformHV(fb, layer->dst_rect, panel, &layer->dst_rect);
        layer->transform.flip_horizontal ^= orientation.flip_horizontal;
        layer->transform.flip_vertical ^= orientation.flip_vertical;
    }

    MapRect(fb, mixer, layer->dst_rect, &layer->dst_rect);
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

    return kErrorNone;
}

DisplayError StrategyImpl::SetCompositionState(LayerComposition composition_type, bool enable) {
    DLOGI("Composition type %d %s on display %d", composition_type,
          enable ? "enabled" : "disabled", display_id_);

    if (composition_type == kCompositionGPU) {
        gpu_disabled_ = !enable;
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
