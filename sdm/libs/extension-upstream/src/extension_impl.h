/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <private/extension_interface.h>

namespace sdm {

class ExtensionImpl : public ExtensionInterface {
  public:
    ~ExtensionImpl() override = default;

    DisplayError CreatePartialUpdate(int32_t display_id, DisplayType type,
                                     const HWResourceInfo& hw_resource_info,
                                     const HWPanelInfo& hw_panel_info,
                                     const HWMixerAttributes& mixer_attributes,
                                     const HWDisplayAttributes& display_attributes,
                                     const DisplayConfigVariableInfo& fb_config,
                                     PartialUpdateInterface** interface) override;
    DisplayError DestroyPartialUpdate(PartialUpdateInterface* interface) override;

    DisplayError CreateStrategyExtn(int32_t display_id, DisplayType type,
                                    BufferAllocator* buffer_allocator,
                                    const HWResourceInfo& hw_resource_info,
                                    const HWPanelInfo& hw_panel_info,
                                    const HWMixerAttributes& mixer_attributes,
                                    const HWDisplayAttributes& display_attributes,
                                    const DisplayConfigVariableInfo& fb_config,
                                    StrategyInterface** interface) override;
    DisplayError DestroyStrategyExtn(StrategyInterface* interface) override;

    DisplayError CreateResourceExtn(const HWResourceInfo& hw_resource_info,
                                    BufferAllocator* buffer_allocator,
                                    ResourceInterface** interface) override;
    DisplayError DestroyResourceExtn(ResourceInterface* interface) override;

    DisplayError CreateDppsControlExtn(DppsControlInterface** dpps_control_interface,
                                       SocketHandler* socket_handler) override;
    DisplayError DestroyDppsControlExtn(DppsControlInterface* interface) override;

#ifdef PROFILE_COVERAGE_DATA
    DisplayError DumpCodeCoverage() override;
#endif

    DisplayError CreateCapabilitiesExtn(const HWResourceInfo& hw_res_info,
                                        CapabilitiesInterface** interface) override;
    DisplayError DestroyCapabilitiesExtn(CapabilitiesInterface* interface) override;

    DisplayError CreateCwbManagerExtn(CwbCallback* callback,
                                      CwbManagerInterface** interface) override;
    DisplayError DestroyCwbManagerExtn(CwbManagerInterface* interface) override;
};

}  // namespace sdm
