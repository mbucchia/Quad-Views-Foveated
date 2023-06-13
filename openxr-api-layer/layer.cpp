// MIT License
//
// Copyright(c) 2022-2023 Matthieu Bucchianeri
//
// Based on https://github.com/mbucchia/OpenXR-Layer-Template.
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

#include "layer.h"
#include <log.h>
#include <util.h>
#include <utils/graphics.h>

#include "views.h"

namespace openxr_api_layer {

    using namespace log;
    using namespace xr::math;
    using namespace openxr_api_layer::utils;

    // Initialize these vectors with arrays of extensions to block and implicitly request for the instance.
    const std::vector<std::string> blockedExtensions = {XR_VARJO_QUAD_VIEWS_EXTENSION_NAME,
                                                        XR_VARJO_FOVEATED_RENDERING_EXTENSION_NAME};
    const std::vector<std::string> implicitExtensions = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};

    // This class implements our API layer.
    class OpenXrLayer : public openxr_api_layer::OpenXrApi {
      public:
        OpenXrLayer() = default;
        ~OpenXrLayer() = default;

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrGetInstanceProcAddr
        XrResult xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) override {
            TraceLoggingWrite(g_traceProvider,
                              "xrGetInstanceProcAddr",
                              TLXArg(instance, "Instance"),
                              TLArg(name, "Name"),
                              TLArg(m_bypassApiLayer, "Bypass"));

            XrResult result = m_bypassApiLayer ? m_xrGetInstanceProcAddr(instance, name, function)
                                               : OpenXrApi::xrGetInstanceProcAddr(instance, name, function);

            TraceLoggingWrite(g_traceProvider, "xrGetInstanceProcAddr", TLPArg(*function, "Function"));

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrCreateInstance
        XrResult xrCreateInstance(const XrInstanceCreateInfo* createInfo) override {
            if (createInfo->type != XR_TYPE_INSTANCE_CREATE_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            // Needed to resolve the requested function pointers.
            OpenXrApi::xrCreateInstance(createInfo);

            // Dump the application name, OpenXR runtime information and other useful things for debugging.
            TraceLoggingWrite(g_traceProvider,
                              "xrCreateInstance",
                              TLArg(xr::ToString(createInfo->applicationInfo.apiVersion).c_str(), "ApiVersion"),
                              TLArg(createInfo->applicationInfo.applicationName, "ApplicationName"),
                              TLArg(createInfo->applicationInfo.applicationVersion, "ApplicationVersion"),
                              TLArg(createInfo->applicationInfo.engineName, "EngineName"),
                              TLArg(createInfo->applicationInfo.engineVersion, "EngineVersion"),
                              TLArg(createInfo->createFlags, "CreateFlags"));
            Log(fmt::format("Application: {}\n", createInfo->applicationInfo.applicationName));

            for (uint32_t i = 0; i < createInfo->enabledApiLayerCount; i++) {
                TraceLoggingWrite(
                    g_traceProvider, "xrCreateInstance", TLArg(createInfo->enabledApiLayerNames[i], "ApiLayerName"));
            }

            // Bypass the extension unless the app might request quad views.
            m_bypassApiLayer = true;
            for (uint32_t i = 0; i < createInfo->enabledExtensionCount; i++) {
                const std::string_view ext(createInfo->enabledExtensionNames[i]);
                TraceLoggingWrite(g_traceProvider, "xrCreateInstance", TLArg(ext.data(), "ExtensionName"));
                if (ext == XR_VARJO_QUAD_VIEWS_EXTENSION_NAME) {
                    m_bypassApiLayer = false;
                } else if (ext == XR_VARJO_FOVEATED_RENDERING_EXTENSION_NAME) {
                    m_requestedFoveatedRendering = true;
                }
            }

            if (m_bypassApiLayer) {
                Log(fmt::format("{} layer will be bypassed\n", LayerName));
                return XR_SUCCESS;
            }

            XrInstanceProperties instanceProperties = {XR_TYPE_INSTANCE_PROPERTIES};
            CHECK_XRCMD(OpenXrApi::xrGetInstanceProperties(GetXrInstance(), &instanceProperties));
            const auto runtimeName = fmt::format("{} {}.{}.{}",
                                                 instanceProperties.runtimeName,
                                                 XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
                                                 XR_VERSION_MINOR(instanceProperties.runtimeVersion),
                                                 XR_VERSION_PATCH(instanceProperties.runtimeVersion));
            TraceLoggingWrite(g_traceProvider, "xrCreateInstance", TLArg(runtimeName.c_str(), "RuntimeName"));
            Log(fmt::format("Using OpenXR runtime: {}\n", runtimeName));

            return XR_SUCCESS;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrGetSystem
        XrResult xrGetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId) override {
            if (getInfo->type != XR_TYPE_SYSTEM_GET_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(g_traceProvider,
                              "xrGetSystem",
                              TLXArg(instance, "Instance"),
                              TLArg(xr::ToCString(getInfo->formFactor), "FormFactor"));

            const XrResult result = OpenXrApi::xrGetSystem(instance, getInfo, systemId);

            if (XR_SUCCEEDED(result) && getInfo->formFactor == XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) {
                static bool wasSystemLogged = false;
                if (!wasSystemLogged) {
                    XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
                    CHECK_XRCMD(OpenXrApi::xrGetSystemProperties(instance, *systemId, &systemProperties));
                    TraceLoggingWrite(g_traceProvider, "xrGetSystem", TLArg(systemProperties.systemName, "SystemName"));
                    Log(fmt::format("Using OpenXR system: {}\n", systemProperties.systemName));
                    wasSystemLogged = true;
                }
            }

            TraceLoggingWrite(g_traceProvider, "xrGetSystem", TLArg((int)*systemId, "SystemId"));

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrGetSystemProperties
        XrResult xrGetSystemProperties(XrInstance instance,
                                       XrSystemId systemId,
                                       XrSystemProperties* properties) override {
            TraceLoggingWrite(g_traceProvider,
                              "xrGetSystemProperties",
                              TLXArg(instance, "Instance"),
                              TLArg((int)systemId, "SystemId"));

            const XrResult result = OpenXrApi::xrGetSystemProperties(instance, systemId, properties);

            if (XR_SUCCEEDED(result)) {
                if (m_requestedFoveatedRendering) {
                    XrSystemFoveatedRenderingPropertiesVARJO* foveatedProperties =
                        reinterpret_cast<XrSystemFoveatedRenderingPropertiesVARJO*>(properties->next);
                    while (foveatedProperties) {
                        if (foveatedProperties->type == XR_TYPE_SYSTEM_FOVEATED_RENDERING_PROPERTIES_VARJO) {
                            foveatedProperties->supportsFoveatedRendering =
                                m_isEyeTrackingAvailable ? XR_TRUE : XR_FALSE;
                            break;
                        }
                        foveatedProperties =
                            reinterpret_cast<XrSystemFoveatedRenderingPropertiesVARJO*>(foveatedProperties->next);
                    }
                }
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrEnumerateViewConfigurations
        XrResult xrEnumerateViewConfigurations(XrInstance instance,
                                               XrSystemId systemId,
                                               uint32_t viewConfigurationTypeCapacityInput,
                                               uint32_t* viewConfigurationTypeCountOutput,
                                               XrViewConfigurationType* viewConfigurationTypes) override {
            TraceLoggingWrite(g_traceProvider,
                              "xrEnumerateViewConfigurations",
                              TLXArg(instance, "Instance"),
                              TLArg((int)systemId, "SystemId"),
                              TLArg(viewConfigurationTypeCapacityInput, "ViewConfigurationTypeCapacityInput"));

            XrResult result = XR_ERROR_RUNTIME_FAILURE;
            if (viewConfigurationTypeCapacityInput) {
                result = OpenXrApi::xrEnumerateViewConfigurations(instance,
                                                                  systemId,
                                                                  viewConfigurationTypeCapacityInput - 1,
                                                                  viewConfigurationTypeCountOutput,
                                                                  viewConfigurationTypes + 1);
                if (XR_SUCCEEDED(result)) {
                    // Prepend (since we prefer quad views).
                    viewConfigurationTypes[0] = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO;
                    (*viewConfigurationTypeCountOutput)++;
                }
            } else {
                result = OpenXrApi::xrEnumerateViewConfigurations(
                    instance, systemId, 0, viewConfigurationTypeCountOutput, nullptr);
                if (XR_SUCCEEDED(result)) {
                    (*viewConfigurationTypeCountOutput)++;
                }
            }

            if (XR_SUCCEEDED(result)) {
                TraceLoggingWrite(g_traceProvider,
                                  "xrEnumerateViewConfigurations",
                                  TLArg(*viewConfigurationTypeCountOutput, "ViewConfigurationTypeCountOutput"));

                if (viewConfigurationTypeCapacityInput && viewConfigurationTypes) {
                    for (uint32_t i = 0; i < *viewConfigurationTypeCountOutput; i++) {
                        TraceLoggingWrite(g_traceProvider,
                                          "xrEnumerateViewConfigurations",
                                          TLArg(xr::ToCString(viewConfigurationTypes[i]), "ViewConfigurationType"));
                    }
                }
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrEnumerateViewConfigurationViews
        XrResult xrEnumerateViewConfigurationViews(XrInstance instance,
                                                   XrSystemId systemId,
                                                   XrViewConfigurationType viewConfigurationType,
                                                   uint32_t viewCapacityInput,
                                                   uint32_t* viewCountOutput,
                                                   XrViewConfigurationView* views) override {
            TraceLoggingWrite(g_traceProvider,
                              "xrEnumerateViewConfigurationViews",
                              TLXArg(instance, "Instance"),
                              TLArg((int)systemId, "SystemId"),
                              TLArg(viewCapacityInput, "ViewCapacityInput"),
                              TLArg(xr::ToCString(viewConfigurationType), "ViewConfigurationType"));

            XrResult result = XR_ERROR_RUNTIME_FAILURE;
            if (viewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO) {
                if (viewCapacityInput) {
                    XrViewConfigurationView stereoViews[xr::StereoView::Count]{{XR_TYPE_VIEW_CONFIGURATION_VIEW},
                                                                               {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
                    if (viewCapacityInput >= xr::QuadView::Count) {
                        result = OpenXrApi::xrEnumerateViewConfigurationViews(instance,
                                                                              systemId,
                                                                              XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                                              xr::StereoView::Count,
                                                                              viewCountOutput,
                                                                              stereoViews);
                    } else {
                        result = XR_ERROR_SIZE_INSUFFICIENT;
                    }

                    if (XR_SUCCEEDED(result)) {
                        *viewCountOutput = xr::QuadView::Count;

                        for (uint32_t i = 0; i < *viewCountOutput; i++) {
                            if (views[i].type != XR_TYPE_VIEW_CONFIGURATION_VIEW) {
                                return XR_ERROR_VALIDATION_FAILURE;
                            }
                        }

                        // Make sure we have the prerequisite data to compute the resolutions we need.
                        populateFovTables(systemId);

                        // Override default to specify whether foveated rendering is desired when the application does
                        // not specify.
                        bool foveatedRenderingActive = m_preferFoveatedRendering;

                        // When foveated rendering extension is active, look whether the application is requesting it
                        // for the views. The spec is a little questionable and calls for each view to have the flag
                        // specified. Here we check that at least one view has the flag on.
                        if (m_requestedFoveatedRendering) {
                            for (uint32_t i = 0; i < *viewCountOutput; i++) {
                                const XrFoveatedViewConfigurationViewVARJO* foveatedViewConfiguration =
                                    reinterpret_cast<const XrFoveatedViewConfigurationViewVARJO*>(views[i].next);
                                while (foveatedViewConfiguration) {
                                    if (foveatedViewConfiguration->type ==
                                        XR_TYPE_FOVEATED_VIEW_CONFIGURATION_VIEW_VARJO) {
                                        foveatedRenderingActive = foveatedRenderingActive ||
                                                                  foveatedViewConfiguration->foveatedRenderingActive;
                                        break;
                                    }
                                    foveatedViewConfiguration =
                                        reinterpret_cast<const XrFoveatedViewConfigurationViewVARJO*>(
                                            foveatedViewConfiguration->next);
                                }
                            }

                            TraceLoggingWrite(g_traceProvider,
                                              "xrEnumerateViewConfigurationViews",
                                              TLArg(foveatedRenderingActive, "FoveatedRenderingActive"));
                        }

                        const float basePixelDensity = stereoViews[xr::StereoView::Left].recommendedImageRectWidth /
                                                       (-m_cachedEyeFov[xr::StereoView::Left].angleLeft +
                                                        m_cachedEyeFov[xr::StereoView::Left].angleRight);

                        // When using quad views, we use 2 peripheral views with lower pixel densities, and 2 focus
                        // views with higher pixel densities.
                        for (uint32_t i = 0; i < *viewCountOutput; i++) {
                            uint32_t referenceFovIndex = i;
                            float pixelDensityMultiplier = m_peripheralPixelDensity;
                            if (i >= xr::StereoView::Count) {
                                pixelDensityMultiplier = m_focusPixelDensity;
                                if (foveatedRenderingActive) {
                                    referenceFovIndex = i + 2;
                                }
                            }

                            const float horizontalFov = (-m_cachedEyeFov[referenceFovIndex].angleLeft +
                                                         m_cachedEyeFov[referenceFovIndex].angleRight);
                            const float newWidth = basePixelDensity * pixelDensityMultiplier * horizontalFov;
                            const float ratio =
                                (float)stereoViews[i % xr::StereoView::Count].recommendedImageRectHeight /
                                stereoViews[i % xr::StereoView::Count].recommendedImageRectWidth;
                            const float newHeight = newWidth * ratio;

                            views[i] = stereoViews[i % xr::StereoView::Count];
                            views[i].recommendedImageRectWidth =
                                std::min((uint32_t)newWidth, views[i].maxImageRectWidth);
                            views[i].recommendedImageRectHeight =
                                std::min((uint32_t)newHeight, views[i].maxImageRectHeight);
                        }

                        if (!m_loggedResolution) {
                            Log("Recommended peripheral resolution: %ux%u (%.3fx density)\n",
                                views[xr::StereoView::Left].recommendedImageRectWidth,
                                views[xr::StereoView::Left].recommendedImageRectHeight,
                                m_peripheralPixelDensity);
                            Log("Recommended focus resolution: %ux%u (%.3fx density)\n",
                                views[xr::QuadView::FocusLeft].recommendedImageRectWidth,
                                views[xr::QuadView::FocusLeft].recommendedImageRectHeight,
                                m_focusPixelDensity);
                            m_loggedResolution = true;
                        }
                    }
                } else {
                    result = OpenXrApi::xrEnumerateViewConfigurationViews(
                        instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, viewCountOutput, nullptr);
                    if (XR_SUCCEEDED(result)) {
                        *viewCountOutput = xr::QuadView::Count;
                    }
                }
            } else {
                result = OpenXrApi::xrEnumerateViewConfigurationViews(
                    instance, systemId, viewConfigurationType, viewCapacityInput, viewCountOutput, views);
            }

            if (XR_SUCCEEDED(result)) {
                if (viewCapacityInput && views) {
                    for (uint32_t i = 0; i < *viewCountOutput; i++) {
                        TraceLoggingWrite(
                            g_traceProvider,
                            "xrEnumerateViewConfigurationViews",
                            TLArg(i, "ViewIndex"),
                            TLArg(views[i].maxImageRectWidth, "MaxImageRectWidth"),
                            TLArg(views[i].maxImageRectHeight, "MaxImageRectHeight"),
                            TLArg(views[i].maxSwapchainSampleCount, "MaxSwapchainSampleCount"),
                            TLArg(views[i].recommendedImageRectWidth, "RecommendedImageRectWidth"),
                            TLArg(views[i].recommendedImageRectHeight, "RecommendedImageRectHeight"),
                            TLArg(views[i].recommendedSwapchainSampleCount, "RecommendedSwapchainSampleCount"));
                    }
                }
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrEnumerateEnvironmentBlendModes
        XrResult xrEnumerateEnvironmentBlendModes(XrInstance instance,
                                                  XrSystemId systemId,
                                                  XrViewConfigurationType viewConfigurationType,
                                                  uint32_t environmentBlendModeCapacityInput,
                                                  uint32_t* environmentBlendModeCountOutput,
                                                  XrEnvironmentBlendMode* environmentBlendModes) override {
            TraceLoggingWrite(g_traceProvider,
                              "xrEnumerateEnvironmentBlendModes",
                              TLXArg(instance, "Instance"),
                              TLArg((int)systemId, "SystemId"),
                              TLArg(xr::ToCString(viewConfigurationType), "ViewConfigurationType"),
                              TLArg(environmentBlendModeCapacityInput, "EnvironmentBlendModeCapacityInput"));

            // We will implement quad views on top of stereo.
            if (viewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO) {
                viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            }

            const XrResult result = OpenXrApi::xrEnumerateEnvironmentBlendModes(instance,
                                                                                systemId,
                                                                                viewConfigurationType,
                                                                                environmentBlendModeCapacityInput,
                                                                                environmentBlendModeCountOutput,
                                                                                environmentBlendModes);

            if (XR_SUCCEEDED(result)) {
                TraceLoggingWrite(g_traceProvider,
                                  "xrEnumerateEnvironmentBlendModes",
                                  TLArg(*environmentBlendModeCountOutput, "EnvironmentBlendModeCountOutput"));

                if (environmentBlendModeCapacityInput && environmentBlendModes) {
                    for (uint32_t i = 0; i < *environmentBlendModeCountOutput; i++) {
                        TraceLoggingWrite(g_traceProvider,
                                          "xrEnumerateEnvironmentBlendModes",
                                          TLArg(xr::ToCString(environmentBlendModes[i]), "EnvironmentBlendMode"));
                    }
                }
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrGetViewConfigurationProperties
        XrResult xrGetViewConfigurationProperties(XrInstance instance,
                                                  XrSystemId systemId,
                                                  XrViewConfigurationType viewConfigurationType,
                                                  XrViewConfigurationProperties* configurationProperties) override {
            TraceLoggingWrite(g_traceProvider,
                              "xrGetViewConfigurationProperties",
                              TLXArg(instance, "Instance"),
                              TLArg((int)systemId, "SystemId"),
                              TLArg(xr::ToCString(viewConfigurationType), "ViewConfigurationType"));

            // We will implement quad views on top of stereo.
            const XrViewConfigurationType originalViewConfigurationType = viewConfigurationType;
            if (viewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO) {
                viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            }

            const XrResult result = OpenXrApi::xrGetViewConfigurationProperties(
                instance, systemId, viewConfigurationType, configurationProperties);

            if (XR_SUCCEEDED(result)) {
                if (originalViewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO) {
                    configurationProperties->viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                }

                TraceLoggingWrite(
                    g_traceProvider,
                    "xrGetViewConfigurationProperties",
                    TLArg(xr::ToCString(configurationProperties->viewConfigurationType), "ViewConfigurationType"),
                    TLArg(!!configurationProperties->fovMutable, "FovMutable"));
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrCreateSession
        XrResult xrCreateSession(XrInstance instance,
                                 const XrSessionCreateInfo* createInfo,
                                 XrSession* session) override {
            if (createInfo->type != XR_TYPE_SESSION_CREATE_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(g_traceProvider,
                              "xrCreateSession",
                              TLXArg(instance, "Instance"),
                              TLArg((int)createInfo->systemId, "SystemId"),
                              TLArg(createInfo->createFlags, "CreateFlags"));

            const XrResult result = OpenXrApi::xrCreateSession(instance, createInfo, session);

            if (XR_SUCCEEDED(result)) {
                TraceLoggingWrite(g_traceProvider, "xrCreateSession", TLXArg(*session, "Session"));

                XrViewConfigurationProperties properties{XR_TYPE_VIEW_CONFIGURATION_PROPERTIES};
                CHECK_XRCMD(OpenXrApi::xrGetViewConfigurationProperties(
                    instance, createInfo->systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, &properties));
                m_isFovMutable = properties.fovMutable;
                Log(fmt::format("fovMutable is {}\n", m_isFovMutable ? "supported" : "not supported"));
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrBeginSession
        XrResult xrBeginSession(XrSession session, const XrSessionBeginInfo* beginInfo) override {
            if (beginInfo->type != XR_TYPE_SESSION_BEGIN_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(
                g_traceProvider,
                "xrBeginSession",
                TLXArg(session, "Session"),
                TLArg(xr::ToCString(beginInfo->primaryViewConfigurationType), "PrimaryViewConfigurationType"));

            // We will implement quad views on top of stereo.
            XrSessionBeginInfo chainBeginInfo = *beginInfo;
            if (beginInfo->primaryViewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO) {
                m_useQuadViews = true;
                chainBeginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            }

            const XrResult result = OpenXrApi::xrBeginSession(session, &chainBeginInfo);

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrLocateViews
        XrResult xrLocateViews(XrSession session,
                               const XrViewLocateInfo* viewLocateInfo,
                               XrViewState* viewState,
                               uint32_t viewCapacityInput,
                               uint32_t* viewCountOutput,
                               XrView* views) override {
            if (viewLocateInfo->type != XR_TYPE_VIEW_LOCATE_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(g_traceProvider,
                              "xrLocateViews",
                              TLXArg(session, "Session"),
                              TLArg(xr::ToCString(viewLocateInfo->viewConfigurationType), "ViewConfigurationType"),
                              TLArg(viewLocateInfo->displayTime, "DisplayTime"),
                              TLXArg(viewLocateInfo->space, "Space"),
                              TLArg(viewCapacityInput, "ViewCapacityInput"));

            XrResult result = XR_ERROR_RUNTIME_FAILURE;
            if (viewLocateInfo->viewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO) {
                if (m_useQuadViews) {
                    XrViewLocateInfo chainViewLocateInfo = *viewLocateInfo;
                    chainViewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

                    if (viewCapacityInput) {
                        if (viewCapacityInput >= xr::QuadView::Count) {
                            result = OpenXrApi::xrLocateViews(session,
                                                              &chainViewLocateInfo,
                                                              viewState,
                                                              xr::StereoView::Count,
                                                              viewCountOutput,
                                                              views);
                        } else {
                            result = XR_ERROR_SIZE_INSUFFICIENT;
                        }

                        if (XR_SUCCEEDED(result)) {
                            *viewCountOutput = xr::QuadView::Count;

                            for (uint32_t i = 0; i < *viewCountOutput; i++) {
                                if (views[i].type != XR_TYPE_VIEW) {
                                    return XR_ERROR_VALIDATION_FAILURE;
                                }
                            }

                            if (viewState->viewStateFlags &
                                (XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
                                // Override default to specify whether foveated rendering is desired when the
                                // application does not specify.
                                bool foveatedRenderingActive = m_preferFoveatedRendering;

                                if (m_requestedFoveatedRendering) {
                                    const XrViewLocateFoveatedRenderingVARJO* foveatedLocate =
                                        reinterpret_cast<const XrViewLocateFoveatedRenderingVARJO*>(
                                            viewLocateInfo->next);
                                    while (foveatedLocate) {
                                        if (foveatedLocate->type == XR_TYPE_VIEW_LOCATE_FOVEATED_RENDERING_VARJO) {
                                            foveatedRenderingActive = foveatedLocate->foveatedRenderingActive;
                                            break;
                                        }
                                        foveatedLocate = reinterpret_cast<const XrViewLocateFoveatedRenderingVARJO*>(
                                            foveatedLocate->next);
                                    }

                                    TraceLoggingWrite(g_traceProvider,
                                                      "xrLocateViews",
                                                      TLArg(foveatedRenderingActive, "FoveatedRenderingActive"));
                                }

                                // Query the eye tracker if needed.
                                bool isGazeValid = false;
                                XrVector3f gazeUnitVector{};
                                if (foveatedRenderingActive) {
                                    isGazeValid = getEyeGaze(
                                        viewLocateInfo->displayTime, false /* getStateOnly */, gazeUnitVector);
                                }

                                // Set up the focus view.
                                for (uint32_t i = xr::StereoView::Count; i < *viewCountOutput; i++) {
                                    const uint32_t stereoViewIndex = i - xr::StereoView::Count;

                                    views[i].pose = views[stereoViewIndex].pose;
                                    XrVector2f projectedGaze;
                                    if (!isGazeValid ||
                                        !ProjectPoint(views[stereoViewIndex], gazeUnitVector, projectedGaze)) {
                                        views[i].fov = m_cachedEyeFov[i];

                                    } else {
                                        // Shift FOV according to the eye gaze.
                                        // We also widen the FOV when near the edges of the headset to make sure there's
                                        // enough overlap between the two eyes.
                                        const float MaxWidenAngle = 0.122173f; /* rad */
                                        constexpr float Deadzone = 0.15f;
                                        const XrVector2f centerOfFov{(projectedGaze.x + 1.f) / 2.f,
                                                                     (1.f - projectedGaze.y) / 2.f};
                                        const XrVector2f v = centerOfFov - m_centerOfFov[stereoViewIndex];
                                        const float distanceFromCenter = std::sqrt(v.x * v.x + v.y * v.y);
                                        const float widenHalfAngle =
                                            std::clamp(distanceFromCenter - Deadzone, 0.f, 0.5f) * MaxWidenAngle;
                                        XrFovf globalFov = m_cachedEyeFov[i % xr::StereoView::Count];
                                        std::tie(views[i].fov.angleLeft, views[i].fov.angleRight) =
                                            Fov::Lerp(std::make_pair(globalFov.angleLeft, globalFov.angleRight),
                                                      std::make_pair(m_cachedEyeFov[i + 2].angleLeft - widenHalfAngle,
                                                                     m_cachedEyeFov[i + 2].angleRight + widenHalfAngle),
                                                      centerOfFov.x);
                                        std::tie(views[i].fov.angleDown, views[i].fov.angleUp) =
                                            Fov::Lerp(std::make_pair(globalFov.angleDown, globalFov.angleUp),
                                                      std::make_pair(m_cachedEyeFov[i + 2].angleDown - widenHalfAngle,
                                                                     m_cachedEyeFov[i + 2].angleUp + widenHalfAngle),
                                                      centerOfFov.y);
                                    }
                                }
                            }
                        }
                    } else {
                        result = OpenXrApi::xrLocateViews(
                            session, &chainViewLocateInfo, viewState, 0, viewCountOutput, nullptr);
                        if (XR_SUCCEEDED(result)) {
                            *viewCountOutput = xr::QuadView::Count;
                        }
                    }
                } else {
                    result = XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
                }
            } else {
                result = OpenXrApi::xrLocateViews(
                    session, viewLocateInfo, viewState, viewCapacityInput, viewCountOutput, views);
            }

            if (XR_SUCCEEDED(result)) {
                TraceLoggingWrite(g_traceProvider, "xrLocateViews", TLArg(*viewCountOutput, "ViewCountOutput"));

                if (viewCapacityInput && views) {
                    for (uint32_t i = 0; i < *viewCountOutput; i++) {
                        TraceLoggingWrite(g_traceProvider,
                                          "xrLocateViews",
                                          TLArg(i, "ViewIndex"),
                                          TLArg(xr::ToString(views[i].pose).c_str(), "Pose"),
                                          TLArg(xr::ToString(views[i].fov).c_str(), "Fov"));
                    }
                }
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrAcquireSwapchainImage
        XrResult xrAcquireSwapchainImage(XrSwapchain swapchain,
                                         const XrSwapchainImageAcquireInfo* acquireInfo,
                                         uint32_t* index) override {
            TraceLoggingWrite(g_traceProvider, "xrAcquireSwapchainImage", TLXArg(swapchain, "Swapchain"));

            const XrResult result = OpenXrApi::xrAcquireSwapchainImage(swapchain, acquireInfo, index);

            if (XR_SUCCEEDED(result)) {
                TraceLoggingWrite(g_traceProvider, "xrAcquireSwapchainImage", TLArg(*index, "Index"));

                std::unique_lock lock(m_swapchainsMutex);
                const auto it = m_swapchains.find(swapchain);
                if (it != m_swapchains.end()) {
                    it->second.acquiredIndex.push_back(*index);
                } else {
                    Swapchain newEntry;
                    newEntry.acquiredIndex.push_back(*index);
                    m_swapchains.insert_or_assign(swapchain, std::move(newEntry));
                }
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrReleaseSwapchainImage
        XrResult xrReleaseSwapchainImage(XrSwapchain swapchain,
                                         const XrSwapchainImageReleaseInfo* releaseInfo) override {
            TraceLoggingWrite(g_traceProvider, "xrReleaseSwapchainImage", TLXArg(swapchain, "Swapchain"));

            const XrResult result = OpenXrApi::xrReleaseSwapchainImage(swapchain, releaseInfo);

            if (XR_SUCCEEDED(result)) {
                std::unique_lock lock(m_swapchainsMutex);

                Swapchain& entry = m_swapchains[swapchain];
                entry.lastReleasedIndex = entry.acquiredIndex.front();
                entry.acquiredIndex.pop_front();
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrEndFrame
        XrResult xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) override {
            if (frameEndInfo->type != XR_TYPE_FRAME_END_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(g_traceProvider,
                              "xrEndFrame",
                              TLXArg(session, "Session"),
                              TLArg(frameEndInfo->displayTime, "DisplayTime"),
                              TLArg(xr::ToCString(frameEndInfo->environmentBlendMode), "EnvironmentBlendMode"));

            // We will allocate structures to pass to the real xrEndFrame().
            std::vector<XrCompositionLayerProjection> projectionAllocator;
            std::vector<std::array<XrCompositionLayerProjectionView, xr::StereoView::Count>> projectionViewAllocator;
            std::vector<const XrCompositionLayerBaseHeader*> layers;

            // Ensure pointers remain stable. We may push up to 2 layers for each layer.
            projectionAllocator.reserve(frameEndInfo->layerCount * 2);
            projectionViewAllocator.reserve(frameEndInfo->layerCount * 2);

            XrFrameEndInfo chainFrameEndInfo = *frameEndInfo;

            if (m_useQuadViews) {
                for (uint32_t i = 0; i < frameEndInfo->layerCount; i++) {
                    if (!frameEndInfo->layers[i]) {
                        return XR_ERROR_LAYER_INVALID;
                    }

                    if (frameEndInfo->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
                        const XrCompositionLayerProjection* proj =
                            reinterpret_cast<const XrCompositionLayerProjection*>(frameEndInfo->layers[i]);

                        TraceLoggingWrite(g_traceProvider,
                                          "xrEndFrame_Layer",
                                          TLArg(xr::ToCString(proj->type), "Type"),
                                          TLArg(proj->layerFlags, "Flags"),
                                          TLXArg(proj->space, "Space"));

                        if (proj->viewCount != xr::QuadView::Count) {
                            return XR_ERROR_VALIDATION_FAILURE;
                        }

                        // Quad views uses 2 stereo layers.
                        for (uint32_t viewIndex = 0; viewIndex < proj->viewCount; viewIndex++) {
                            if (!(viewIndex % xr::StereoView::Count)) {
                                // Start a new set of views.
                                projectionViewAllocator.push_back({proj->views[viewIndex], proj->views[viewIndex + 1]});
                            }

                            TraceLoggingWrite(
                                g_traceProvider,
                                "xrEndFrame_View",
                                TLArg(viewIndex, "ViewIndex"),
                                TLXArg(proj->views[viewIndex].subImage.swapchain, "Swapchain"),
                                TLArg(proj->views[viewIndex].subImage.imageArrayIndex, "ImageArrayIndex"),
                                TLArg(xr::ToString(proj->views[viewIndex].subImage.imageRect).c_str(), "ImageRect"),
                                TLArg(xr::ToString(proj->views[viewIndex].pose).c_str(), "Pose"),
                                TLArg(xr::ToString(proj->views[viewIndex].fov).c_str(), "Fov"));

                            if (!m_isFovMutable) {
                                // TODO, P1: Workaround fovMutable.
                                if (viewIndex >= xr::StereoView::Count) {
                                    std::unique_lock lock(m_swapchainsMutex);

                                    const auto it = m_swapchains.find(proj->views[viewIndex].subImage.swapchain);
                                    if (it == m_swapchains.end()) {
                                        return XR_ERROR_HANDLE_INVALID;
                                    }
                                }

                                // TODO, P3: Handling depth submission (uncommon).
                            }
                        }

                        if (!m_debugFocusView) {
                            projectionAllocator.push_back(*proj);
                            projectionAllocator.back().views =
                                projectionViewAllocator.at(projectionViewAllocator.size() - 2).data();
                            projectionAllocator.back().viewCount = xr::StereoView::Count;
                            layers.push_back(
                                reinterpret_cast<XrCompositionLayerBaseHeader*>(&projectionAllocator.back()));
                        }
                        projectionAllocator.push_back(*proj);
                        projectionAllocator.back().layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                        projectionAllocator.back().views =
                            projectionViewAllocator.at(projectionViewAllocator.size() - 1).data();
                        projectionAllocator.back().viewCount = xr::StereoView::Count;
                        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&projectionAllocator.back()));

                    } else {
                        TraceLoggingWrite(g_traceProvider,
                                          "xrEndFrame_Layer",
                                          TLArg(xr::ToCString(frameEndInfo->layers[i]->type), "Type"));
                        layers.push_back(frameEndInfo->layers[i]);
                    }
                }

                chainFrameEndInfo.layers = layers.data();
                chainFrameEndInfo.layerCount = (uint32_t)layers.size();
            }

            const XrResult result = OpenXrApi::xrEndFrame(session, &chainFrameEndInfo);

            return result;
        }

#if 0
        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrCreateReferenceSpace
        XrResult xrCreateReferenceSpace(XrSession session,
                                        const XrReferenceSpaceCreateInfo* createInfo,
                                        XrSpace* space) override {
            // TODO, P2
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrLocateSpace
        XrResult xrLocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation* location) override {
            // TODO, P2
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrPollEvent
        XrResult xrPollEvent(XrInstance instance, XrEventDataBuffer* eventData) override {
            // TODO, P2
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrGetVisibilityMaskKHR
        XrResult xrGetVisibilityMaskKHR(XrSession session,
                                        XrViewConfigurationType viewConfigurationType,
                                        uint32_t viewIndex,
                                        XrVisibilityMaskTypeKHR visibilityMaskType,
                                        XrVisibilityMaskKHR* visibilityMask) override {
            // TODO, P2
        }
#endif

      private:
        struct Swapchain {
            std::deque<uint32_t> acquiredIndex;
            uint32_t lastReleasedIndex{0};
        };

        bool getEyeGaze(XrTime time, bool getStateOnly, XrVector3f& unitVector) const {
            if (!m_isEyeTrackingAvailable) {
                return false;
            }

            // TODO, P1: Add support for querying the eye tracker.
            XrVector2f point{0.5f, 0.5f};
            {
                // Use the mouse to simulate eye tracking.
                RECT rect;
                rect.left = 1;
                rect.right = 999;
                rect.top = 1;
                rect.bottom = 999;
                ClipCursor(&rect);

                POINT pt{};
                GetCursorPos(&pt);

                point = {(float)pt.x / 1000.f, (1000.f - pt.y) / 1000.f};
            }

            unitVector = Normalize({point.x - 0.5f, 0.5f - point.y, -0.35f});

            return true;
        }

        void populateFovTables(XrSystemId systemId) {
            if (!m_needComputeBaseFov) {
                return;
            }

            cacheStereoView(systemId);

            XrView view[xr::StereoView::Count]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
            XrVector2f projectedGaze[xr::StereoView::Count]{{}, {}};
            for (uint32_t eye = 0; eye < xr::StereoView::Count; eye++) {
                view[eye].fov = m_cachedEyeFov[eye];
                view[eye].pose = m_cachedEyePoses[eye];

                // Calculate the "resting" gaze position.
                ProjectPoint(view[eye], {0.f, 0.f, -1.f}, projectedGaze[eye]);
                m_centerOfFov[eye].x = (projectedGaze[eye].x + 1.f) / 2.f;
                m_centerOfFov[eye].y = (1.f - projectedGaze[eye].y) / 2.f;
            }

            for (uint32_t foveated = 0; foveated <= 1; foveated++) {
                for (uint32_t eye = 0; eye < xr::StereoView::Count; eye++) {
                    const uint32_t viewIndex = 2 + (foveated * 2) + eye;

                    // Apply the FOV multiplier.
                    std::tie(m_cachedEyeFov[viewIndex].angleLeft, m_cachedEyeFov[viewIndex].angleRight) =
                        Fov::Scale(std::make_pair(m_cachedEyeFov[eye].angleLeft, m_cachedEyeFov[eye].angleRight),
                                   m_horizontalFovSection[foveated]);
                    std::tie(m_cachedEyeFov[viewIndex].angleDown, m_cachedEyeFov[viewIndex].angleUp) =
                        Fov::Scale(std::make_pair(m_cachedEyeFov[eye].angleDown, m_cachedEyeFov[eye].angleUp),
                                   m_verticalFovSection[foveated]);

                    // Adjust for (fixed) gaze.
                    std::tie(m_cachedEyeFov[viewIndex].angleLeft, m_cachedEyeFov[viewIndex].angleRight) = Fov::Lerp(
                        std::make_pair(m_cachedEyeFov[eye].angleLeft, m_cachedEyeFov[eye].angleRight),
                        std::make_pair(m_cachedEyeFov[viewIndex].angleLeft, m_cachedEyeFov[viewIndex].angleRight),
                        m_centerOfFov[eye].x);
                    std::tie(m_cachedEyeFov[viewIndex].angleDown, m_cachedEyeFov[viewIndex].angleUp) = Fov::Lerp(
                        std::make_pair(m_cachedEyeFov[eye].angleDown, m_cachedEyeFov[eye].angleUp),
                        std::make_pair(m_cachedEyeFov[viewIndex].angleDown, m_cachedEyeFov[viewIndex].angleUp),
                        m_centerOfFov[eye].y);
                }
            }

            m_needComputeBaseFov = false;
        }

        void cacheStereoView(XrSystemId systemId) {
            // Create an ephemeral session to query the information we need.
            XrGraphicsRequirementsD3D11KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
            CHECK_XRCMD(OpenXrApi::xrGetD3D11GraphicsRequirementsKHR(GetXrInstance(), systemId, &graphicsRequirements));

            std::shared_ptr<graphics::IGraphicsDevice> graphicsDevice =
                graphics::internal::createD3D11CompositionDevice(graphicsRequirements.adapterLuid);

            XrGraphicsBindingD3D11KHR graphicsBindings{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
            graphicsBindings.device = graphicsDevice->getNativeDevice<graphics::D3D11>();

            XrSessionCreateInfo sessionCreateInfo{XR_TYPE_SESSION_CREATE_INFO};
            sessionCreateInfo.systemId = systemId;
            sessionCreateInfo.next = &graphicsBindings;

            XrSession session;
            CHECK_XRCMD(OpenXrApi::xrCreateSession(GetXrInstance(), &sessionCreateInfo, &session));

            XrReferenceSpaceCreateInfo spaceCreateInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
            spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
            spaceCreateInfo.poseInReferenceSpace = Pose::Identity();

            XrSpace viewSpace;
            CHECK_XRCMD(OpenXrApi::xrCreateReferenceSpace(session, &spaceCreateInfo, &viewSpace));

            XrSessionBeginInfo beginSessionInfo{XR_TYPE_SESSION_BEGIN_INFO};
            beginSessionInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            CHECK_XRCMD(OpenXrApi::xrBeginSession(session, &beginSessionInfo));

            XrViewLocateInfo viewLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
            viewLocateInfo.space = viewSpace;
            viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

            XrView view[xr::StereoView::Count]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
            while (true) {
                XrFrameState frameState{XR_TYPE_FRAME_STATE};
                CHECK_XRCMD(OpenXrApi::xrWaitFrame(session, nullptr, &frameState));
                CHECK_XRCMD(OpenXrApi::xrBeginFrame(session, nullptr));

                viewLocateInfo.displayTime = frameState.predictedDisplayTime;

                XrViewState viewState{XR_TYPE_VIEW_STATE};
                uint32_t count;
                CHECK_XRCMD(OpenXrApi::xrLocateViews(session, &viewLocateInfo, &viewState, 2, &count, view));
                if (viewState.viewStateFlags &
                    (XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
                    break;
                }
            }

            CHECK_XRCMD(OpenXrApi::xrDestroySession(session));

            // TODO, P2: Do we need to purge events?

            for (uint32_t eye = 0; eye < xr::StereoView::Count; eye++) {
                m_cachedEyeFov[eye] = view[eye].fov;
                m_cachedEyePoses[eye] = view[eye].pose;
            }
        }

        bool m_bypassApiLayer{false};
        bool m_useQuadViews{false};
        bool m_requestedFoveatedRendering{false};
        bool m_loggedResolution{false};
        bool m_isFovMutable{false};
        bool m_isEyeTrackingAvailable{true};

        // TODO, P2: Make this tunable.
        float m_peripheralPixelDensity{0.5f};
        float m_focusPixelDensity{1.f};
        // [0] = non-foveated, [1] = foveated
        float m_horizontalFovSection[2]{0.75f, 0.5f};
        float m_verticalFovSection[2]{0.7f, 0.5f};
        bool m_preferFoveatedRendering{true};

        bool m_needComputeBaseFov{true};
        // [0] = left, [1] = right
        // [2] = left focus non-foveated, [3] = right focus non-foveated,
        // [4] = left focus foveated, [5] = right focus foveated
        XrFovf m_cachedEyeFov[xr::QuadView::Count + 2]{};
        XrPosef m_cachedEyePoses[xr::StereoView::Count]{};
        XrVector2f m_centerOfFov[xr::StereoView::Count]{};

        std::mutex m_swapchainsMutex;
        std::unordered_map<XrSwapchain, Swapchain> m_swapchains;

        bool m_debugFocusView{false};
    };

    // This method is required by the framework to instantiate your OpenXrApi implementation.
    OpenXrApi* GetInstance() {
        if (!g_instance) {
            g_instance = std::make_unique<OpenXrLayer>();
        }
        return g_instance.get();
    }

} // namespace openxr_api_layer

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        TraceLoggingRegister(openxr_api_layer::log::g_traceProvider);
        break;

    case DLL_PROCESS_DETACH:
        TraceLoggingUnregister(openxr_api_layer::log::g_traceProvider);
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}
