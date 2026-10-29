/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <private/resource_interface.h>

#include <map>
#include <string>
#include <vector>

namespace sdm {

class ResourceImpl : public ResourceInterface {
  public:
    explicit ResourceImpl(const HWResourceInfo& hw_res_info);
    ~ResourceImpl() override;

    DisplayError Init();

    DisplayError RegisterDisplay(int32_t display_id, DisplayType type,
                                 const HWDisplayAttributes& display_attributes,
                                 const HWPanelInfo& hw_panel_info,
                                 const HWMixerAttributes& mixer_attributes,
                                 const Resolution& fb_resolution, Handle* display_ctx) override;
    DisplayError UnregisterDisplay(Handle display_ctx) override;
    DisplayError ReconfigureDisplay(Handle display_ctx,
                                    const HWDisplayAttributes& display_attributes,
                                    const HWPanelInfo& hw_panel_info,
                                    const HWMixerAttributes& mixer_attributes,
                                    const Resolution& fb_resolution) override;
    DisplayError Start(Handle display_ctx, LayerStack* layer_stack) override;
    DisplayError Precheck(Handle display_ctx, DispLayerStack* disp_layer_stack,
                          LayerFeedback* feedback) override;
    DisplayError Stop(Handle display_ctx, DispLayerStack* disp_layer_stack) override;
    DisplayError SetDrawMethod(Handle display_ctx, const DisplayDrawMethod& draw_method) override;
    DisplayError Prepare(Handle display_ctx, DispLayerStack* disp_layer_stack,
                         LayerFeedback* feedback) override;
    DisplayError PostPrepare(Handle display_ctx, DispLayerStack* disp_layer_stack) override;
    DisplayError Commit(Handle display_ctx, DispLayerStack* disp_layer_stack) override;
    DisplayError PostCommit(Handle display_ctx, DispLayerStack* disp_layer_stack) override;
    void Purge(Handle display_ctx) override;
    DisplayError SetMaxMixerStages(Handle display_ctx, uint32_t max_mixer_stages) override;
    DisplayError ValidateScaling(const LayerRect& crop, const LayerRect& dst, bool rotate90,
                                 BufferLayout layout, bool use_rotator_downscale) override;
    DisplayError ValidateAndSetCursorPosition(Handle display_ctx,
                                              DispLayerStack* disp_layer_stack, int x, int y,
                                              DisplayConfigVariableInfo* fb_config) override;
    DisplayError SetMaxBandwidthMode(HWBwModes mode) override;
    DisplayError GetScaleLutConfig(HWScaleLutInfo* lut_info) override;
    DisplayError SetDetailEnhancerData(Handle display_ctx,
                                       const DisplayDetailEnhancerData& de_data) override;
    DisplayError UpdateSyncHandle(Handle display_ctx, const SyncPoints& sync_points) override;
    DisplayError Perform(int cmd, ...) override;
    bool IsRotatorSupportedFormat(LayerBufferFormat format) override;
    DisplayError FreeDemuraFetchResources(const int32_t& display_id) override;
    DisplayError GetDemuraFetchResourceCount(
            std::map<uint32_t, uint8_t>* fetch_resource_cnt) override;
    DisplayError ReserveDemuraFetchResources(const int32_t& display_id,
                                             const int8_t& preferred_rect) override;
    DisplayError GetDemuraFetchResources(Handle display_ctx, FetchResourceList* frl) override;
    DisplayError SetMaxSDEClk(Handle display_ctx, uint32_t clk) override;
    DisplayError ForceToneMapConfigure(Handle display_ctx,
                                       DispLayerStack* disp_layer_stack) override;
    bool ToneMapQueryRequested(Handle display_ctx) override;
    DisplayError PreCommit(Handle display_ctx) override;
    bool HandleCwbTeardown(Handle display_ctx) override;
    DisplayError RequestVirtualDisplayId(int32_t* vdisp_id) override;
    DisplayError AllocateVirtualDisplayId(int32_t* vdisp_id) override;
    DisplayError DeallocateVirtualDisplayId(int32_t vdisp_id) override;
    void HandleSkipValidate(Handle display_ctx) override;
    std::string Dump(Handle display_ctx) override;
    uint32_t GetMixerCount() override;
    DisplayError SetBlendSpace(Handle display_ctx, const PrimariesTransfer& blend_space) override;
    void HandleTUITransition(Handle display_ctx, bool tui_active) override;
    void GetDSConfig(Handle display_ctx, DestScaleInfoMap* dest_scale_info_map) override;
    bool IsDisplayHWAvailable() override;

  private:
    struct DisplayContext {
        int32_t display_id = -1;
        HWMixerAttributes mixer_attributes = {};
        std::vector<uint32_t> prepared_planes;
        std::vector<uint32_t> committed_planes;
    };

    bool IsPlaneFree(const DisplayContext* ctx, uint32_t plane_id) const;
    DisplayError ConfigPipe(const DisplayContext* ctx, const Layer& layer, HWPipeInfo* pipe) const;

    HWResourceInfo hw_res_info_;
    std::vector<uint32_t> planes_;
    std::vector<DisplayContext*> displays_;
};

}  // namespace sdm
