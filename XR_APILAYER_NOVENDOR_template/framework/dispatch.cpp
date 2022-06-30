// MIT License
//
// Copyright(c) 2021-2022 Matthieu Bucchianeri
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pch.h"

#include <layer.h>

#include "dispatch.h"
#include "log.h"

#ifndef LAYER_NAMESPACE
#error Must define LAYER_NAMESPACE
#endif

using namespace LAYER_NAMESPACE::log;

namespace LAYER_NAMESPACE {

    // Entry point for creating the layer.
    XrResult xrCreateApiLayerInstance(const XrInstanceCreateInfo* const instanceCreateInfo,
                                      const struct XrApiLayerCreateInfo* const apiLayerInfo,
                                      XrInstance* const instance) {
        TraceLoggingWrite(g_traceProvider, "xrCreateApiLayerInstance");
        DebugLog("--> xrCreateApiLayerInstance\n");

        if (!apiLayerInfo || apiLayerInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO ||
            apiLayerInfo->structVersion != XR_API_LAYER_CREATE_INFO_STRUCT_VERSION ||
            apiLayerInfo->structSize != sizeof(XrApiLayerCreateInfo) || !apiLayerInfo->nextInfo ||
            apiLayerInfo->nextInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO ||
            apiLayerInfo->nextInfo->structVersion != XR_API_LAYER_NEXT_INFO_STRUCT_VERSION ||
            apiLayerInfo->nextInfo->structSize != sizeof(XrApiLayerNextInfo) ||
            apiLayerInfo->nextInfo->layerName != LayerName || !apiLayerInfo->nextInfo->nextGetInstanceProcAddr ||
            !apiLayerInfo->nextInfo->nextCreateApiLayerInstance) {
            ErrorLog("xrCreateApiLayerInstance validation failed\n");
            return XR_ERROR_INITIALIZATION_FAILED;
        }

        // Dump the other layers.
        {
            auto info = apiLayerInfo->nextInfo;
            while (info) {
                TraceLoggingWrite(g_traceProvider, "xrCreateApiLayerInstance", TLArg(info->layerName, "LayerName"));
                Log("Using layer: %s\n", info->layerName);
                info = info->next;
            }
        }

        // The list of extensions to remove or implicitly add.
        std::vector<std::string> blockedExtensions;
        std::vector<std::string> implicitExtensions;

        // Dump the requested extensions.
        XrInstanceCreateInfo chainInstanceCreateInfo = *instanceCreateInfo;
        std::vector<const char*> newEnabledExtensionNames;
        for (uint32_t i = 0; i < chainInstanceCreateInfo.enabledExtensionCount; i++) {
            const std::string_view ext(chainInstanceCreateInfo.enabledExtensionNames[i]);
            TraceLoggingWrite(g_traceProvider, "xrCreateApiLayerInstance", TLArg(ext.data(), "ExtensionName"));

            if (std::find(blockedExtensions.cbegin(), blockedExtensions.cend(), ext) == blockedExtensions.cend()) {
                Log("Requested extension: %s\n", ext.data());
                newEnabledExtensionNames.push_back(ext.data());
            } else {
                Log("Blocking extension: %s\n", ext.data());
            }
        }
        for (const auto& ext : implicitExtensions) {
            Log("Requesting extension: %s\n", ext.c_str());
            newEnabledExtensionNames.push_back(ext.c_str());
        }
        chainInstanceCreateInfo.enabledExtensionNames = newEnabledExtensionNames.data();
        chainInstanceCreateInfo.enabledExtensionCount = (uint32_t)newEnabledExtensionNames.size();

        // Call the chain to create the instance.
        XrApiLayerCreateInfo chainApiLayerInfo = *apiLayerInfo;
        chainApiLayerInfo.nextInfo = apiLayerInfo->nextInfo->next;
        XrResult result =
            apiLayerInfo->nextInfo->nextCreateApiLayerInstance(&chainInstanceCreateInfo, &chainApiLayerInfo, instance);
        if (result == XR_SUCCESS) {
            // Create our layer.
            LAYER_NAMESPACE::GetInstance()->SetGetInstanceProcAddr(apiLayerInfo->nextInfo->nextGetInstanceProcAddr,
                                                                   *instance);

            // Forward the xrCreateInstance() call to the layer.
            try {
                result = LAYER_NAMESPACE::GetInstance()->xrCreateInstance(instanceCreateInfo);
            } catch (std::runtime_error exc) {
                TraceLoggingWrite(g_traceProvider, "xrCreateInstance_Error", TLArg(exc.what(), "Error"));
                ErrorLog("xrCreateInstance: %s\n", exc.what());
                result = XR_ERROR_RUNTIME_FAILURE;
            }

            // Cleanup attempt before returning an error.
            if (XR_FAILED(result)) {
                PFN_xrDestroyInstance xrDestroyInstance = nullptr;
                if (XR_SUCCEEDED(apiLayerInfo->nextInfo->nextGetInstanceProcAddr(
                        *instance, "xrDestroyInstance", reinterpret_cast<PFN_xrVoidFunction*>(&xrDestroyInstance)))) {
                    xrDestroyInstance(*instance);
                }
            }
        }

        TraceLoggingWrite(g_traceProvider, "xrCreateApiLayerInstance_Result", TLArg(xr::ToCString(result), "Result"));
        if (XR_FAILED(result)) {
            ErrorLog("xrCreateApiLayerInstance failed with %s\n", xr::ToCString(result));
        }

        DebugLog("<-- xrCreateApiLayerInstance %d\n", result);

        return result;
    }

    // Handle cleanup of the layer's singleton.
    XrResult xrDestroyInstance(XrInstance instance) {
        TraceLoggingWrite(g_traceProvider, "xrDestroyInstance");

        XrResult result;
        try {
            result = LAYER_NAMESPACE::GetInstance()->xrDestroyInstance(instance);
            if (XR_SUCCEEDED(result)) {
                LAYER_NAMESPACE::ResetInstance();
            }
        } catch (std::runtime_error exc) {
            TraceLoggingWrite(g_traceProvider, "xrDestroyInstance_Error", TLArg(exc.what(), "Error"));
            ErrorLog("xrDestroyInstance: %s\n", exc.what());
            result = XR_ERROR_RUNTIME_FAILURE;
        }

        TraceLoggingWrite(g_traceProvider, "xrDestroyInstance_Result", TLArg(xr::ToCString(result), "Result"));
        if (XR_FAILED(result)) {
            ErrorLog("xrDestroyInstance failed with %s\n", xr::ToCString(result));
        }

        return result;
    }

    // Forward the xrGetInstanceProcAddr() call to the dispatcher.
    XrResult xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
        TraceLoggingWrite(g_traceProvider, "xrGetInstanceProcAddr");

        XrResult result;
        try {
            result = LAYER_NAMESPACE::GetInstance()->xrGetInstanceProcAddr(instance, name, function);
        } catch (std::runtime_error exc) {
            TraceLoggingWrite(g_traceProvider, "xrGetInstanceProcAddr_Error", TLArg(exc.what(), "Error"));
            ErrorLog("xrGetInstanceProcAddr: %s\n", exc.what());
            result = XR_ERROR_RUNTIME_FAILURE;
        }

        TraceLoggingWrite(g_traceProvider, "xrGetInstanceProcAddr_Result", TLArg(xr::ToCString(result), "Result"));

        return result;
    }

} // namespace LAYER_NAMESPACE
