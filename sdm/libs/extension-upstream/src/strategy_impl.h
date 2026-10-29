/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <private/strategy_interface.h>

#include <set>
#include <vector>

namespace sdm {

class StrategyImpl : public StrategyInterface {
  public:
    StrategyImpl(int32_t display_id, DisplayType type, const HWResourceInfo& hw_res_info,
                 const HWPanelInfo& hw_panel_info, const HWMixerAttributes& mixer_attributes,
                 const HWDisplayAttributes& display_attributes,
                 const DisplayConfigVariableInfo& fb_config);

    DisplayError Start(DispLayerStack* disp_layer_stack, uint32_t* max_attempts,
                       StrategyConstraints* constraints) override;
    DisplayError GetNextStrategy() override;
    DisplayError Stop() override;
    DisplayError SetDrawMethod(const DisplayDrawMethod& draw_method) override;
    DisplayError Reconfigure(const HWPanelInfo& hw_panel_info, const HWResourceInfo& hw_res_info,
                             const HWDisplayAttributes& display_attributes,
                             const HWMixerAttributes& mixer_attributes,
                             const DisplayConfigVariableInfo& fb_config) override;
    DisplayError SetCompositionState(LayerComposition composition_type, bool enable) override;
    DisplayError Purge() override;
    DisplayError SetIdleTimeoutMs(uint32_t active_ms, uint32_t inactive_ms) override;
    DisplayError SetColorModesInfo(const std::vector<PrimariesTransfer>& colormodes_cs) override;
    DisplayError SetBlendSpace(const PrimariesTransfer& blend_space) override;
    void SetDisplayLayerStack(DispLayerStack* disp_layer_stack) override;

  private:
    struct Attempt {
        uint32_t gpu_start;
        uint32_t gpu_end;
    };

    void UpdatePlaneCaps();
    void AddSdeAttempts(uint32_t max_layers);
    bool IsSdeCapable(const Layer& layer) const;
    bool IsColorSupported(const LayerBuffer& buffer) const;
    bool IsGeometrySupported(const Layer& layer) const;
    void AddHwLayer(uint32_t index);
    LayerRect MapDstToMixer(const LayerRect& dst) const;
    void MapToMixer(Layer* layer) const;

    int32_t display_id_;
    HWResourceInfo hw_res_info_;
    HWPanelInfo hw_panel_info_;
    HWMixerAttributes mixer_attributes_;
    HWDisplayAttributes display_attributes_;
    DisplayConfigVariableInfo fb_config_;
    PrimariesTransfer blend_space_ = {};
    DispLayerStack* disp_layer_stack_ = nullptr;
    std::set<LayerBufferFormat> plane_formats_;
    uint32_t plane_count_ = 0;
    bool gpu_disabled_ = false;
    bool sde_disabled_ = false;
    std::vector<Attempt> attempts_;
    size_t next_attempt_ = 0;
};

}  // namespace sdm
