/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "extension_impl.h"

#include <new>

#include "resource_impl.h"
#include "strategy_impl.h"

namespace sdm {

DisplayError ExtensionImpl::CreatePartialUpdate(int32_t display_id, DisplayType type,
                                                const HWResourceInfo& hw_resource_info,
                                                const HWPanelInfo& hw_panel_info,
                                                const HWMixerAttributes& mixer_attributes,
                                                const HWDisplayAttributes& display_attributes,
                                                const DisplayConfigVariableInfo& fb_config,
                                                PartialUpdateInterface** interface) {
    *interface = nullptr;
    return kErrorNotSupported;
}

DisplayError ExtensionImpl::DestroyPartialUpdate(PartialUpdateInterface* interface) {
    return kErrorNone;
}

DisplayError ExtensionImpl::CreateStrategyExtn(int32_t display_id, DisplayType type,
                                               BufferAllocator* buffer_allocator,
                                               const HWResourceInfo& hw_resource_info,
                                               const HWPanelInfo& hw_panel_info,
                                               const HWMixerAttributes& mixer_attributes,
                                               const HWDisplayAttributes& display_attributes,
                                               const DisplayConfigVariableInfo& fb_config,
                                               StrategyInterface** interface) {
    *interface = new (std::nothrow)
            StrategyImpl(display_id, type, hw_resource_info, hw_panel_info, mixer_attributes,
                         display_attributes, fb_config);
    return *interface ? kErrorNone : kErrorMemory;
}

DisplayError ExtensionImpl::DestroyStrategyExtn(StrategyInterface* interface) {
    delete interface;
    return kErrorNone;
}

DisplayError ExtensionImpl::CreateResourceExtn(const HWResourceInfo& hw_resource_info,
                                               BufferAllocator* buffer_allocator,
                                               ResourceInterface** interface) {
    *interface = nullptr;

    ResourceImpl* resource = new (std::nothrow) ResourceImpl(hw_resource_info);
    if (!resource) {
        return kErrorMemory;
    }

    DisplayError error = resource->Init();
    if (error != kErrorNone) {
        delete resource;
        return error;
    }

    *interface = resource;
    return kErrorNone;
}

DisplayError ExtensionImpl::DestroyResourceExtn(ResourceInterface* interface) {
    delete interface;
    return kErrorNone;
}

DisplayError ExtensionImpl::CreateDppsControlExtn(DppsControlInterface** dpps_control_interface,
                                                  SocketHandler* socket_handler) {
    *dpps_control_interface = nullptr;
    return kErrorNotSupported;
}

DisplayError ExtensionImpl::DestroyDppsControlExtn(DppsControlInterface* interface) {
    return kErrorNone;
}

#ifdef PROFILE_COVERAGE_DATA
DisplayError ExtensionImpl::DumpCodeCoverage() {
    return kErrorNone;
}
#endif

DisplayError ExtensionImpl::CreateCapabilitiesExtn(const HWResourceInfo& hw_res_info,
                                                   CapabilitiesInterface** interface) {
    *interface = nullptr;
    return kErrorNotSupported;
}

DisplayError ExtensionImpl::DestroyCapabilitiesExtn(CapabilitiesInterface* interface) {
    return kErrorNone;
}

DisplayError ExtensionImpl::CreateCwbManagerExtn(CwbCallback* callback,
                                                 CwbManagerInterface** interface) {
    *interface = nullptr;
    return kErrorNotSupported;
}

DisplayError ExtensionImpl::DestroyCwbManagerExtn(CwbManagerInterface* interface) {
    return kErrorNone;
}

}  // namespace sdm

extern "C" sdm::DisplayError CreateExtensionInterface(uint16_t version,
                                                      sdm::ExtensionInterface** interface) {
    if (!interface) {
        return sdm::kErrorParameters;
    }

    if (version != EXTENSION_VERSION_TAG) {
        return sdm::kErrorVersion;
    }

    *interface = new (std::nothrow) sdm::ExtensionImpl();
    return *interface ? sdm::kErrorNone : sdm::kErrorMemory;
}

extern "C" sdm::DisplayError DestroyExtensionInterface(sdm::ExtensionInterface* interface) {
    delete static_cast<sdm::ExtensionImpl*>(interface);
    return sdm::kErrorNone;
}
