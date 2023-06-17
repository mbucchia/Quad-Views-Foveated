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

#include <ProjectionVS.h>
#include <ProjectionPS.h>

namespace openxr_api_layer {

    using namespace log;
    using namespace xr::math;
    using namespace openxr_api_layer::utils;

    // Initialize these vectors with arrays of extensions to block and implicitly request for the instance.
    const std::vector<std::string> blockedExtensions = {XR_VARJO_QUAD_VIEWS_EXTENSION_NAME,
                                                        XR_VARJO_FOVEATED_RENDERING_EXTENSION_NAME};
    const std::vector<std::string> implicitExtensions = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME,
                                                         XR_FB_EYE_TRACKING_SOCIAL_EXTENSION_NAME};

    struct ProjectionConstants {
        DirectX::XMFLOAT4X4 Projection;
    };

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

            // Parse the configuration.
            LoadConfiguration();

            // Game-specific quirks.
            m_needFocusFovCorrectionQuirk = GetApplicationName() == "DCS World";

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
                // Check if the system supports eye tracking.
                XrSystemEyeTrackingPropertiesFB eyeTrackingProperties{XR_TYPE_SYSTEM_EYE_TRACKING_PROPERTIES_FB};
                eyeTrackingProperties.supportsEyeTracking = XR_FALSE;
                XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
                systemProperties.next = &eyeTrackingProperties;
                CHECK_XRCMD(OpenXrApi::xrGetSystemProperties(instance, *systemId, &systemProperties));
                TraceLoggingWrite(g_traceProvider,
                                  "xrGetSystem",
                                  TLArg(systemProperties.systemName, "SystemName"),
                                  TLArg(eyeTrackingProperties.supportsEyeTracking, "SupportsEyeTracking"));

                m_isEyeTrackingAvailable = m_debugSimulateTracking || eyeTrackingProperties.supportsEyeTracking;

                static bool wasSystemLogged = false;
                if (!wasSystemLogged) {
                    Log(fmt::format("Using OpenXR system: {}\n", systemProperties.systemName));
                    Log(fmt::format("Eye tracking is {}\n", m_isEyeTrackingAvailable ? "supported" : "not supported"));
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
                        bool foveatedRenderingActive = m_isEyeTrackingAvailable && m_preferFoveatedRendering;

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
        XrResult xrDestroySession(XrSession session) override {
            TraceLoggingWrite(g_traceProvider, "xrDestroySession", TLXArg(session, "Session"));

            // Wait for deferred frames to finish before teardown.
            if (m_asyncWaitPromise.valid()) {
                TraceLocalActivity(local);

                TraceLoggingWriteStart(local, "AsyncWaitNow");
                m_asyncWaitPromise.wait_for(5s);
                TraceLoggingWriteStop(local, "AsyncWaitNow");

                m_asyncWaitPromise = {};
            }

            const XrResult result = OpenXrApi::xrDestroySession(session);

            if (XR_SUCCEEDED(result)) {
                m_layerContextState.Reset();
                m_linearClampSampler.Reset();
                m_noDepthRasterizer.Reset();
                m_projectionConstants.Reset();
                m_projectionVS.Reset();
                m_projectionPS.Reset();

                m_applicationDevice.Reset();
                m_renderContext.Reset();

                m_gazeSpaces.clear();
                m_swapchains.clear();
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

            if (XR_SUCCEEDED(result)) {
                if (m_isEyeTrackingAvailable) {
                    if (!m_debugSimulateTracking) {
                        XrEyeTrackerCreateInfoFB createInfo{XR_TYPE_EYE_TRACKER_CREATE_INFO_FB};
                        CHECK_XRCMD(OpenXrApi::xrCreateEyeTrackerFB(session, &createInfo, &m_eyeTrackerFB));

                        XrReferenceSpaceCreateInfo spaceCreateInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
                        spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
                        spaceCreateInfo.poseInReferenceSpace = Pose::Identity();
                        CHECK_XRCMD(OpenXrApi::xrCreateReferenceSpace(session, &spaceCreateInfo, &m_viewSpace));
                    }
                }
            }

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
                                bool foveatedRenderingActive = m_isEyeTrackingAvailable && m_preferFoveatedRendering;

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

                                    XrView viewForGazeProjection{};
                                    viewForGazeProjection.pose = m_cachedEyePoses[stereoViewIndex];
                                    viewForGazeProjection.fov = views[stereoViewIndex].fov;
                                    XrVector2f projectedGaze;
                                    if (!isGazeValid ||
                                        !ProjectPoint(viewForGazeProjection, gazeUnitVector, projectedGaze)) {
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

                                // Quirk for DCS World: the application does not pass the correct FOV for the focus
                                // views in xrEndFrame(). We must keep track of the correct values for each frame.
                                if (m_needFocusFovCorrectionQuirk) {
                                    std::unique_lock lock(m_focusFovMutex);

                                    m_focusFovForDisplayTime.insert_or_assign(
                                        viewLocateInfo->displayTime,
                                        std::make_pair(views[xr::QuadView::FocusLeft].fov,
                                                       views[xr::QuadView::FocusRight].fov));
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

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrCreateSwapchain
        XrResult xrCreateSwapchain(XrSession session,
                                   const XrSwapchainCreateInfo* createInfo,
                                   XrSwapchain* swapchain) override {
            if (createInfo->type != XR_TYPE_SWAPCHAIN_CREATE_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(g_traceProvider,
                              "xrCreateSwapchain",
                              TLXArg(session, "Session"),
                              TLArg(createInfo->arraySize, "ArraySize"),
                              TLArg(createInfo->width, "Width"),
                              TLArg(createInfo->height, "Height"),
                              TLArg(createInfo->createFlags, "CreateFlags"),
                              TLArg(createInfo->format, "Format"),
                              TLArg(createInfo->faceCount, "FaceCount"),
                              TLArg(createInfo->mipCount, "MipCount"),
                              TLArg(createInfo->sampleCount, "SampleCount"),
                              TLArg(createInfo->usageFlags, "UsageFlags"));

            const XrResult result = OpenXrApi::xrCreateSwapchain(session, createInfo, swapchain);

            if (XR_SUCCEEDED(result)) {
                TraceLoggingWrite(g_traceProvider, "xrCreateSwapchain", TLXArg(*swapchain, "Swapchain"));

                std::unique_lock lock(m_swapchainsMutex);
                Swapchain newEntry{};
                newEntry.createInfo = *createInfo;
                m_swapchains.insert_or_assign(*swapchain, std::move(newEntry));
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrDestroySwapchain
        XrResult xrDestroySwapchain(XrSwapchain swapchain) override {
            TraceLoggingWrite(g_traceProvider, "xrDestroySwapchain", TLXArg(swapchain, "Swapchain"));

            // In Turbo Mode, make sure there is no pending frame that may potentially hold onto the swapchain.
            {
                std::unique_lock lock(m_frameMutex);

                if (m_asyncWaitPromise.valid()) {
                    TraceLocalActivity(local);

                    TraceLoggingWriteStart(local, "AsyncWaitNow");
                    m_asyncWaitPromise.wait();
                    TraceLoggingWriteStop(local, "AsyncWaitNow");
                }
            }

            const XrResult result = OpenXrApi::xrDestroySwapchain(swapchain);

            if (XR_SUCCEEDED(result)) {
                std::unique_lock lock(m_swapchainsMutex);

                auto it = m_swapchains.find(swapchain);
                Swapchain& entry = it->second;
                for (uint32_t i = 0; i < xr::StereoView::Count; i++) {
                    if (entry.fullFovSwapchain[i] != XR_NULL_HANDLE) {
                        OpenXrApi::xrDestroySwapchain(entry.fullFovSwapchain[i]);
                    }
                }
                m_swapchains.erase(it);
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
                Swapchain& entry = m_swapchains[swapchain];
                entry.acquiredIndex.push_back(*index);
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

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrWaitFrame
        XrResult xrWaitFrame(XrSession session,
                             const XrFrameWaitInfo* frameWaitInfo,
                             XrFrameState* frameState) override {
            TraceLoggingWrite(g_traceProvider, "xrWaitFrame", TLXArg(session, "Session"));

            const auto lastFrameWaitTimestamp = m_lastFrameWaitTimestamp;
            m_lastFrameWaitTimestamp = std::chrono::steady_clock::now();

            XrResult result = XR_ERROR_RUNTIME_FAILURE;
            {
                std::unique_lock lock(m_frameMutex);

                if (m_asyncWaitPromise.valid()) {
                    TraceLoggingWrite(g_traceProvider, "AsyncWaitMode");

                    // In Turbo mode, we accept pipelining of exactly one frame.
                    if (m_asyncWaitPolled) {
                        TraceLocalActivity(local);

                        // On second frame poll, we must wait.
                        TraceLoggingWriteStart(local, "AsyncWaitNow");
                        m_asyncWaitPromise.wait();
                        TraceLoggingWriteStop(local, "AsyncWaitNow");
                    }
                    m_asyncWaitPolled = true;

                    // In Turbo mode, we don't actually wait, we make up a predicted time.
                    {
                        std::unique_lock lock(m_asyncWaitMutex);

                        frameState->predictedDisplayTime =
                            m_asyncWaitCompleted ? m_lastPredictedDisplayTime
                                                 : (m_lastPredictedDisplayTime +
                                                    (m_lastFrameWaitTimestamp - lastFrameWaitTimestamp).count());
                        frameState->predictedDisplayPeriod = m_lastPredictedDisplayPeriod;
                    }
                    frameState->shouldRender = XR_TRUE;

                    result = XR_SUCCESS;

                } else {
                    lock.unlock();
                    result = OpenXrApi::xrWaitFrame(session, frameWaitInfo, frameState);
                    lock.lock();

                    if (XR_SUCCEEDED(result)) {
                        // We must always store those values to properly handle transitions into Turbo Mode.
                        m_lastPredictedDisplayTime = frameState->predictedDisplayTime;
                        m_lastPredictedDisplayPeriod = frameState->predictedDisplayPeriod;
                    }
                }
            }

            if (XR_SUCCEEDED(result)) {
                // Per OpenXR spec, the predicted display must increase monotonically.
                frameState->predictedDisplayTime = std::max(frameState->predictedDisplayTime, m_waitedFrameTime + 1);

                // Record the predicted display time.
                m_waitedFrameTime = frameState->predictedDisplayTime;

                TraceLoggingWrite(g_traceProvider,
                                  "xrWaitFrame",
                                  TLArg(!!frameState->shouldRender, "ShouldRender"),
                                  TLArg(frameState->predictedDisplayTime, "PredictedDisplayTime"),
                                  TLArg(frameState->predictedDisplayPeriod, "PredictedDisplayPeriod"));
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrBeginFrame
        XrResult xrBeginFrame(XrSession session, const XrFrameBeginInfo* frameBeginInfo) override {
            TraceLoggingWrite(g_traceProvider, "xrBeginFrame", TLXArg(session, "Session"));

            XrResult result = XR_ERROR_RUNTIME_FAILURE;
            {
                std::unique_lock lock(m_frameMutex);

                if (m_asyncWaitPromise.valid()) {
                    // In turbo mode, we do nothing here.
                    TraceLoggingWrite(g_traceProvider, "AsyncWaitMode");
                    result = XR_SUCCESS;
                } else {
                    result = OpenXrApi::xrBeginFrame(session, frameBeginInfo);
                }
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
                                // Do our own FOV composition when the platform does not support it.
                                if (viewIndex >= xr::StereoView::Count) {
                                    const uint32_t referenceViewIndex = viewIndex % xr::StereoView::Count;

                                    std::unique_lock lock(m_swapchainsMutex);

                                    const auto it = m_swapchains.find(proj->views[viewIndex].subImage.swapchain);
                                    if (it == m_swapchains.end()) {
                                        return XR_ERROR_HANDLE_INVALID;
                                    }

                                    Swapchain& entry = it->second;

                                    // Allocate a swapchain for the full FOV.
                                    if (entry.fullFovSwapchain[referenceViewIndex] == XR_NULL_HANDLE) {
                                        XrSwapchainCreateInfo createInfo = entry.createInfo;
                                        createInfo.arraySize = 1;
                                        createInfo.width = m_fullFovResolution.width;
                                        createInfo.height = m_fullFovResolution.height;
                                        CHECK_XRCMD(OpenXrApi::xrCreateSwapchain(
                                            session, &createInfo, &entry.fullFovSwapchain[referenceViewIndex]));
                                    }

                                    XrCompositionLayerProjectionView view = proj->views[viewIndex];
                                    if (m_needFocusFovCorrectionQuirk && viewIndex >= xr::StereoView::Count) {
                                        // Quirk for DCS World: the application does not pass the correct FOV for the
                                        // focus views in xrEndFrame(). We must keep track of the correct values for
                                        // each frame.
                                        std::unique_lock lock(m_focusFovMutex);

                                        const auto& cit = m_focusFovForDisplayTime.find(frameEndInfo->displayTime);
                                        if (cit != m_focusFovForDisplayTime.cend()) {
                                            view.fov = viewIndex == xr::QuadView::FocusLeft ? cit->second.first
                                                                                            : cit->second.second;
                                        }
                                    }

                                    // Relocate the view's content to a full FOV texture.
                                    relocateViewContent(view, entry, referenceViewIndex);

                                    // Patch the view to reference the new swapchain at full FOV.
                                    XrCompositionLayerProjectionView& patchedView =
                                        projectionViewAllocator.back()[viewIndex % xr::StereoView::Count];
                                    patchedView.fov = m_cachedEyeFov[referenceViewIndex];
                                    patchedView.subImage.swapchain = entry.fullFovSwapchain[referenceViewIndex];
                                    patchedView.subImage.imageArrayIndex = 0;
                                    patchedView.subImage.imageRect.offset = {0, 0};
                                    patchedView.subImage.imageRect.extent = m_fullFovResolution;
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

                if (m_needFocusFovCorrectionQuirk) {
                    std::unique_lock lock(m_focusFovMutex);

                    // Delete all entries older than 1s.
                    while (!m_focusFovForDisplayTime.empty() &&
                           m_focusFovForDisplayTime.cbegin()->first < frameEndInfo->displayTime - 1'000'000'000) {
                        m_focusFovForDisplayTime.erase(m_focusFovForDisplayTime.begin());
                    }
                    TraceLoggingWrite(g_traceProvider,
                                      "xrEndFrame",
                                      TLArg(m_focusFovForDisplayTime.size(), "FovForDisplayTimeDictionarySize"));
                }
            }

            XrResult result = XR_ERROR_RUNTIME_FAILURE;
            {
                std::unique_lock lock(m_frameMutex);

                if (m_asyncWaitPromise.valid()) {
                    TraceLocalActivity(local);

                    // This is the latest point we must have fully waited a frame before proceeding.
                    //
                    // Note: we should not wait infinitely here, however certain patterns of engine calls may cause us
                    // to attempt a "double xrWaitFrame" when turning on Turbo. Use a timeout to detect that, and
                    // refrain from enqueing a second wait further down. This isn't a pretty solution, but it is simple
                    // and it seems to work effectively (minus the 1s freeze observed in-game).
                    TraceLoggingWriteStart(local, "AsyncWaitNow");
                    const auto ready = m_asyncWaitPromise.wait_for(1s) == std::future_status::ready;
                    TraceLoggingWriteStop(local, "AsyncWaitNow", TLArg(ready, "Ready"));
                    if (ready) {
                        m_asyncWaitPromise = {};
                    }

                    CHECK_XRCMD(OpenXrApi::xrBeginFrame(session, nullptr));
                }

                result = OpenXrApi::xrEndFrame(session, &chainFrameEndInfo);

                if (m_useTurboMode && !m_asyncWaitPromise.valid()) {
                    m_asyncWaitPolled = false;
                    m_asyncWaitCompleted = false;

                    // In Turbo mode, we kick off a wait thread immediately.
                    TraceLoggingWrite(g_traceProvider, "AsyncWaitStart");
                    m_asyncWaitPromise = std::async(std::launch::async, [&, session] {
                        TraceLocalActivity(local);

                        XrFrameState frameState{XR_TYPE_FRAME_STATE};
                        TraceLoggingWriteStart(local, "AsyncWaitFrame");
                        CHECK_XRCMD(OpenXrApi::xrWaitFrame(session, nullptr, &frameState));
                        TraceLoggingWriteStop(local,
                                              "AsyncWaitFrame",
                                              TLArg(frameState.predictedDisplayTime, "PredictedDisplayTime"),
                                              TLArg(frameState.predictedDisplayPeriod, "PredictedDisplayPeriod"));
                        {
                            std::unique_lock lock(m_asyncWaitMutex);

                            m_lastPredictedDisplayTime = frameState.predictedDisplayTime;
                            m_lastPredictedDisplayPeriod = frameState.predictedDisplayPeriod;

                            m_asyncWaitCompleted = true;
                        }
                    });
                }
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrCreateReferenceSpace
        XrResult xrCreateReferenceSpace(XrSession session,
                                        const XrReferenceSpaceCreateInfo* createInfo,
                                        XrSpace* space) override {
            if (createInfo->type != XR_TYPE_REFERENCE_SPACE_CREATE_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(g_traceProvider,
                              "xrCreateReferenceSpace",
                              TLXArg(session, "Session"),
                              TLArg(xr::ToCString(createInfo->referenceSpaceType), "ReferenceSpaceType"),
                              TLArg(xr::ToString(createInfo->poseInReferenceSpace).c_str(), "PoseInReferenceSpace"));

            XrReferenceSpaceCreateInfo chainCreateInfo = *createInfo;

            if (createInfo->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_COMBINED_EYE_VARJO) {
                // Create a dummy space, we will keep track of those handles below.
                chainCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
            }

            const XrResult result = OpenXrApi::xrCreateReferenceSpace(session, &chainCreateInfo, space);

            if (XR_SUCCEEDED(result)) {
                TraceLoggingWrite(g_traceProvider, "xrCreateReferenceSpace", TLXArg(*space, "Space"));

                if (createInfo->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_COMBINED_EYE_VARJO) {
                    std::unique_lock lock(m_spacesMutex);

                    m_gazeSpaces.insert(*space);
                }
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrDestroySpace
        XrResult xrDestroySpace(XrSpace space) override {
            TraceLoggingWrite(g_traceProvider, "xrDestroySpace", TLXArg(space, "Space"));

            const XrResult result = OpenXrApi::xrDestroySpace(space);

            if (XR_SUCCEEDED(result)) {
                std::unique_lock lock(m_spacesMutex);

                m_gazeSpaces.erase(space);
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrLocateSpace
        XrResult xrLocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation* location) override {
            TraceLoggingWrite(g_traceProvider,
                              "xrLocateSpace",
                              TLXArg(space, "Space"),
                              TLXArg(baseSpace, "BaseSpace"),
                              TLArg(time, "Time"));

            std::unique_lock lock(m_spacesMutex);

            XrResult result = XR_ERROR_RUNTIME_FAILURE;
            if (m_gazeSpaces.count(space)) {
                if (location->type != XR_TYPE_SPACE_LOCATION) {
                    return XR_ERROR_VALIDATION_FAILURE;
                }

                if (time <= 0) {
                    return XR_ERROR_TIME_INVALID;
                }

                XrVector3f dummyVector{};
                if (getEyeGaze(time, true, dummyVector)) {
                    location->locationFlags = XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
                } else {
                    location->locationFlags = 0;
                }
                location->pose = Pose::Identity();

                result = XR_SUCCESS;
            } else {
                result = OpenXrApi::xrLocateSpace(space, baseSpace, time, location);
            }

            if (XR_SUCCEEDED(result)) {
                TraceLoggingWrite(g_traceProvider,
                                  "xrLocateSpace",
                                  TLArg(location->locationFlags, "LocationFlags"),
                                  TLArg(xr::ToString(location->pose).c_str(), "Pose"));
            }

            return result;
        }

#if 0
        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrPollEvent
        XrResult xrPollEvent(XrInstance instance, XrEventDataBuffer* eventData) override {
            // TODO, P2: Block/translate visibility mask events.
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrGetVisibilityMaskKHR
        XrResult xrGetVisibilityMaskKHR(XrSession session,
                                        XrViewConfigurationType viewConfigurationType,
                                        uint32_t viewIndex,
                                        XrVisibilityMaskTypeKHR visibilityMaskType,
                                        XrVisibilityMaskKHR* visibilityMask) override {
            // TODO, P2: Translate visibility mask requests.
        }
#endif

      private:
        struct Swapchain {
            std::deque<uint32_t> acquiredIndex;
            uint32_t lastReleasedIndex{0};

            XrSwapchainCreateInfo createInfo{};
            XrSwapchain fullFovSwapchain[xr::StereoView::Count]{XR_NULL_HANDLE, XR_NULL_HANDLE};
            ComPtr<ID3D11Texture2D> flatImage[xr::StereoView::Count];
        };

        bool getEyeGaze(XrTime time, bool getStateOnly, XrVector3f& unitVector) {
            if (!m_isEyeTrackingAvailable) {
                return false;
            }

            if (!m_debugSimulateTracking) {
                XrEyeGazesInfoFB eyeGazeInfo{XR_TYPE_EYE_GAZES_INFO_FB};
                eyeGazeInfo.baseSpace = m_viewSpace;
                eyeGazeInfo.time = time;

                XrEyeGazesFB eyeGaze{XR_TYPE_EYE_GAZES_FB};
                CHECK_XRCMD(OpenXrApi::xrGetEyeGazesFB(m_eyeTrackerFB, &eyeGazeInfo, &eyeGaze));

                if (!(eyeGaze.gaze[0].isValid && eyeGaze.gaze[1].isValid)) {
                    return false;
                }

                if (!(eyeGaze.gaze[0].gazeConfidence > 0.5f && eyeGaze.gaze[1].gazeConfidence > 0.5f)) {
                    return false;
                }

                if (!getStateOnly) {
                    // Average the poses from both eyes.
                    const auto gaze = LoadXrPose(Pose::Slerp(eyeGaze.gaze[0].gazePose, eyeGaze.gaze[1].gazePose, 0.5f));
                    const auto gazeProjectedPoint =
                        DirectX::XMVector3Transform(DirectX::XMVectorSet(0.f, 0.f, 1.f, 1.f), gaze);

                    unitVector.x = gazeProjectedPoint.m128_f32[0];
                    unitVector.y = -gazeProjectedPoint.m128_f32[1];
                    unitVector.z = gazeProjectedPoint.m128_f32[2];
                }
            } else {
                // Use the mouse to simulate eye tracking.
                RECT rect;
                rect.left = 1;
                rect.right = 999;
                rect.top = 1;
                rect.bottom = 999;
                ClipCursor(&rect);

                POINT cursor{};
                GetCursorPos(&cursor);

                XrVector2f point = {(float)cursor.x / 1000.f, (1000.f - cursor.y) / 1000.f};
                unitVector = Normalize({point.x - 0.5f, 0.5f - point.y, -0.35f});
            }

            TraceLoggingWrite(
                g_traceProvider, "xrLocateViews_EyeGaze", TLArg(xr::ToString(unitVector).c_str(), "GazeUnitVector"));

            return true;
        }

        void relocateViewContent(const XrCompositionLayerProjectionView& view,
                                 Swapchain& swapchain,
                                 uint32_t referenceViewIndex) {
            // TODO, P3: Support D3D12.

            // Grab the input texture.
            ID3D11Texture2D* sourceImage;
            {
                uint32_t count;
                CHECK_XRCMD(OpenXrApi::xrEnumerateSwapchainImages(view.subImage.swapchain, 0, &count, nullptr));
                std::vector<XrSwapchainImageD3D11KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
                CHECK_XRCMD(OpenXrApi::xrEnumerateSwapchainImages(
                    view.subImage.swapchain,
                    count,
                    &count,
                    reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())));
                sourceImage = images[swapchain.lastReleasedIndex].texture;
            }

            // Grab the output texture.
            ID3D11Texture2D* destinationImage;
            {
                uint32_t acquiredImageIndex;
                CHECK_XRCMD(OpenXrApi::xrAcquireSwapchainImage(
                    swapchain.fullFovSwapchain[referenceViewIndex], nullptr, &acquiredImageIndex));
                XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                waitInfo.timeout = 10000000000;
                CHECK_XRCMD(OpenXrApi::xrWaitSwapchainImage(swapchain.fullFovSwapchain[referenceViewIndex], &waitInfo));

                uint32_t count;
                CHECK_XRCMD(OpenXrApi::xrEnumerateSwapchainImages(
                    swapchain.fullFovSwapchain[referenceViewIndex], 0, &count, nullptr));
                std::vector<XrSwapchainImageD3D11KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
                CHECK_XRCMD(OpenXrApi::xrEnumerateSwapchainImages(
                    swapchain.fullFovSwapchain[referenceViewIndex],
                    count,
                    &count,
                    reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())));
                destinationImage = images[acquiredImageIndex].texture;
            }

            // Grab a D3D context.
            {
                ComPtr<ID3D11Device> device;
                destinationImage->GetDevice(device.ReleaseAndGetAddressOf());

                if (!m_applicationDevice) {
                    initializeCompositionResources(device.Get());
                }
            }

            // Save the application context state.
            ComPtr<ID3DDeviceContextState> applicationContextState;
            m_renderContext->SwapDeviceContextState(m_layerContextState.Get(),
                                                    applicationContextState.ReleaseAndGetAddressOf());

            // Copy to a flat texture for sampling.
            {
                D3D11_TEXTURE2D_DESC desc{};
                if (swapchain.flatImage[referenceViewIndex]) {
                    swapchain.flatImage[referenceViewIndex]->GetDesc(&desc);
                }
                if (!swapchain.flatImage[referenceViewIndex] || desc.Width != view.subImage.imageRect.extent.width ||
                    desc.Height != view.subImage.imageRect.extent.height) {
                    desc = {};
                    desc.ArraySize = 1;
                    desc.Width = view.subImage.imageRect.extent.width;
                    desc.Height = view.subImage.imageRect.extent.height;
                    desc.Format = (DXGI_FORMAT)swapchain.createInfo.format;
                    desc.MipLevels = 1;
                    desc.SampleDesc.Count = 1;
                    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                    CHECK_HRCMD(m_applicationDevice->CreateTexture2D(
                        &desc, nullptr, swapchain.flatImage[referenceViewIndex].ReleaseAndGetAddressOf()));
                }
            }
            D3D11_BOX box{};
            box.left = view.subImage.imageRect.offset.x;
            box.top = view.subImage.imageRect.offset.y;
            box.right = box.left + view.subImage.imageRect.extent.width;
            box.bottom = box.top + view.subImage.imageRect.extent.height;
            box.back = 1;
            m_renderContext->CopySubresourceRegion(swapchain.flatImage[referenceViewIndex].Get(),
                                                   0,
                                                   0,
                                                   0,
                                                   0,
                                                   sourceImage,
                                                   view.subImage.imageArrayIndex,
                                                   &box);

            // Create ephemeral SRV/RTV.
            ComPtr<ID3D11ShaderResourceView> srv;
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
                desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                desc.Format = (DXGI_FORMAT)swapchain.createInfo.format;
                desc.Texture2D.MipLevels = 1;
                CHECK_HRCMD(m_applicationDevice->CreateShaderResourceView(
                    swapchain.flatImage[referenceViewIndex].Get(), &desc, srv.ReleaseAndGetAddressOf()));
            }

            ComPtr<ID3D11RenderTargetView> rtv;
            {
                D3D11_RENDER_TARGET_VIEW_DESC desc{};
                desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                desc.Format = (DXGI_FORMAT)swapchain.createInfo.format;
                CHECK_HRCMD(
                    m_applicationDevice->CreateRenderTargetView(destinationImage, &desc, rtv.ReleaseAndGetAddressOf()));
            }

            // Clear the destination.
            const float transparent[] = {0, 0, 0, 0};
            m_renderContext->ClearRenderTargetView(rtv.Get(), transparent);

            // Compute the projection.
            ProjectionConstants projection;
            {
                const DirectX::XMMATRIX baseLayerViewProjection =
                    ComposeProjectionMatrix(m_cachedEyeFov[referenceViewIndex], NearFar{0.1f, 20.f});
                const DirectX::XMMATRIX layerViewProjection = ComposeProjectionMatrix(view.fov, NearFar{0.1f, 20.f});

                DirectX::XMStoreFloat4x4(
                    &projection.Projection,
                    DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, baseLayerViewProjection) *
                                               layerViewProjection));
            }
            {
                D3D11_MAPPED_SUBRESOURCE mappedResources;
                CHECK_HRCMD(
                    m_renderContext->Map(m_projectionConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResources));
                memcpy(mappedResources.pData, &projection, sizeof(projection));
                m_renderContext->Unmap(m_projectionConstants.Get(), 0);
            }

            // Dispatch the composition shader.
            m_renderContext->ClearState();
            m_renderContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            m_renderContext->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
            m_renderContext->RSSetState(m_noDepthRasterizer.Get());
            D3D11_VIEWPORT viewport{};
            viewport.Width = (float)m_fullFovResolution.width;
            viewport.Height = (float)m_fullFovResolution.height;
            viewport.MaxDepth = 1.f;
            m_renderContext->RSSetViewports(1, &viewport);
            m_renderContext->VSSetConstantBuffers(0, 1, m_projectionConstants.GetAddressOf());
            m_renderContext->VSSetShader(m_projectionVS.Get(), nullptr, 0);
            m_renderContext->PSSetSamplers(0, 1, m_linearClampSampler.GetAddressOf());
            m_renderContext->PSSetShaderResources(0, 1, srv.GetAddressOf());
            m_renderContext->PSSetShader(m_projectionPS.Get(), nullptr, 0);
            m_renderContext->Draw(3, 0);

            // Restore the application context state.
            m_renderContext->SwapDeviceContextState(applicationContextState.Get(), nullptr);

            CHECK_XRCMD(OpenXrApi::xrReleaseSwapchainImage(swapchain.fullFovSwapchain[referenceViewIndex], nullptr));
        }

        void initializeCompositionResources(ID3D11Device* device) {
            {
                UINT creationFlags = 0;
                if (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) {
                    creationFlags |= D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED;
                }
                const D3D_FEATURE_LEVEL featureLevel = device->GetFeatureLevel();

                CHECK_HRCMD(device->QueryInterface(m_applicationDevice.ReleaseAndGetAddressOf()));

                CHECK_HRCMD(device->QueryInterface(m_applicationDevice.ReleaseAndGetAddressOf()));

                // Create a switchable context state for the API layer.
                CHECK_HRCMD(
                    m_applicationDevice->CreateDeviceContextState(creationFlags,
                                                                  &featureLevel,
                                                                  1,
                                                                  D3D11_SDK_VERSION,
                                                                  __uuidof(ID3D11Device),
                                                                  nullptr,
                                                                  m_layerContextState.ReleaseAndGetAddressOf()));
            }
            {
                ComPtr<ID3D11DeviceContext> context;
                device->GetImmediateContext(context.ReleaseAndGetAddressOf());
                CHECK_HRCMD(context->QueryInterface(m_renderContext.ReleaseAndGetAddressOf()));
            }
            {
                D3D11_SAMPLER_DESC desc;
                ZeroMemory(&desc, sizeof(desc));
                desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
                desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
                desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
                desc.MaxAnisotropy = 1;
                desc.MinLOD = D3D11_MIP_LOD_BIAS_MIN;
                desc.MaxLOD = D3D11_MIP_LOD_BIAS_MAX;
                CHECK_HRCMD(device->CreateSamplerState(&desc, m_linearClampSampler.ReleaseAndGetAddressOf()));
            }
            {
                D3D11_RASTERIZER_DESC desc;
                ZeroMemory(&desc, sizeof(desc));
                desc.FillMode = D3D11_FILL_SOLID;
                desc.CullMode = D3D11_CULL_NONE;
                desc.FrontCounterClockwise = TRUE;
                CHECK_HRCMD(device->CreateRasterizerState(&desc, m_noDepthRasterizer.ReleaseAndGetAddressOf()));
            }
            {
                D3D11_BUFFER_DESC desc{};
                desc.ByteWidth = (UINT)std::max(16ull, sizeof(ProjectionConstants));
                desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                desc.Usage = D3D11_USAGE_DYNAMIC;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                CHECK_HRCMD(device->CreateBuffer(&desc, nullptr, m_projectionConstants.ReleaseAndGetAddressOf()));
            }
            CHECK_HRCMD(device->CreateVertexShader(
                g_ProjectionVS, sizeof(g_ProjectionVS), nullptr, m_projectionVS.ReleaseAndGetAddressOf()));
            CHECK_HRCMD(device->CreatePixelShader(
                g_ProjectionPS, sizeof(g_ProjectionPS), nullptr, m_projectionPS.ReleaseAndGetAddressOf()));
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

            {
                XrViewConfigurationView stereoViews[xr::StereoView::Count]{{XR_TYPE_VIEW_CONFIGURATION_VIEW},
                                                                           {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
                uint32_t count;
                CHECK_XRCMD(OpenXrApi::xrEnumerateViewConfigurationViews(GetXrInstance(),
                                                                         systemId,
                                                                         XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                                         xr::StereoView::Count,
                                                                         &count,
                                                                         stereoViews));
                const float newWidth =
                    m_focusPixelDensity * stereoViews[xr::StereoView::Left].recommendedImageRectWidth;
                const float ratio = (float)stereoViews[xr::StereoView::Left].recommendedImageRectHeight /
                                    stereoViews[xr::StereoView::Left].recommendedImageRectWidth;
                const float newHeight = newWidth * ratio;

                m_fullFovResolution.width =
                    std::min((uint32_t)newWidth, stereoViews[xr::StereoView::Left].maxImageRectWidth);
                m_fullFovResolution.height =
                    std::min((uint32_t)newHeight, stereoViews[xr::StereoView::Left].maxImageRectHeight);
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

            // Wait for the session to be ready.
            {
                while (true) {
                    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
                    const XrResult result = OpenXrApi::xrPollEvent(GetXrInstance(), &event);
                    if (result == XR_SUCCESS) {
                        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                            const XrEventDataSessionStateChanged& sessionEvent =
                                *reinterpret_cast<XrEventDataSessionStateChanged*>(&event);

                            if (sessionEvent.state == XR_SESSION_STATE_READY) {
                                break;
                            }
                        }
                    }
                    CHECK_XRCMD(result);

                    // TODO, P3: Need some sort of timeout.
                    if (result == XR_EVENT_UNAVAILABLE) {
                        std::this_thread::sleep_for(100ms);
                    }
                }
            }

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

            // Purge events for this session.
            // TODO, P3: This might steal legitimate events from the runtime.
            {
                while (true) {
                    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
                    const XrResult result = OpenXrApi::xrPollEvent(GetXrInstance(), &event);
                    if (result == XR_EVENT_UNAVAILABLE) {
                        break;
                    }
                }
            }

            for (uint32_t eye = 0; eye < xr::StereoView::Count; eye++) {
                m_cachedEyeFov[eye] = view[eye].fov;
                m_cachedEyePoses[eye] = view[eye].pose;
            }
        }

        void LoadConfiguration() {
            std::ifstream configFile;

            // Look in %LocalAppData% first, then fallback to your installation folder.
            auto configPath = localAppData / "settings.cfg";
            Log(fmt::format("Trying to locate configuration file at '{}'...\n", configPath.string()));
            configFile.open(configPath);
            if (!configFile.is_open()) {
                Log("Not found\n");
                configPath = dllHome / "settings.cfg";
                Log(fmt::format("Trying to locate configuration file at '{}'...\n", configPath.string()));
                configFile.open(configPath);
            }

            if (configFile.is_open()) {
                unsigned int lineNumber = 0;
                std::string line;
                while (std::getline(configFile, line)) {
                    lineNumber++;
                    ParseConfigurationStatement(line, lineNumber);
                }
                configFile.close();
            } else {
                Log("No configuration was found\n");
            }
        }

        void ParseConfigurationStatement(const std::string& line, unsigned int lineNumber) {
            try {
                const auto offset = line.find('=');
                if (offset != std::string::npos) {
                    const std::string name = line.substr(0, offset);
                    const std::string value = line.substr(offset + 1);

                    bool parsed = false;
                    if (name == "peripheral_multiplier") {
                        m_peripheralPixelDensity = std::stof(value);
                        parsed = true;
                    } else if (name == "focus_multiplier") {
                        m_focusPixelDensity = std::stof(value);
                        parsed = true;
                    } else if (name == "horizontal_fixed_section") {
                        m_horizontalFovSection[0] = std::stof(value);
                        parsed = true;
                    } else if (name == "vertical_fixed_section") {
                        m_verticalFovSection[0] = std::stof(value);
                        parsed = true;
                    } else if (name == "horizontal_focus_section") {
                        m_horizontalFovSection[1] = std::stof(value);
                        parsed = true;
                    } else if (name == "vertical_focus_section") {
                        m_verticalFovSection[1] = std::stof(value);
                        parsed = true;
                    } else if (name == "prefer_foveated_rendering") {
                        m_preferFoveatedRendering = std::stoi(value);
                        parsed = true;
                    } else if (name == "turbo_mode") {
                        m_useTurboMode = std::stoi(value);
                        parsed = true;
                    } else if (name == "debug_simulate_tracking") {
                        m_debugSimulateTracking = std::stoi(value);
                        parsed = true;
                    } else if (name == "debug_focus_view") {
                        m_debugFocusView = std::stoi(value);
                        parsed = true;
                    } else {
                        Log("L%u: Unrecognized option\n", lineNumber);
                    }

                    if (parsed) {
                        Log(fmt::format("  Found option '{}={}'\n", name, value));
                    }
                } else {
                    Log("L%u: Improperly formatted option\n", lineNumber);
                }
            } catch (...) {
                Log("L%u: Parsing error\n", lineNumber);
            }
        }

        bool m_bypassApiLayer{false};
        bool m_useQuadViews{false};
        bool m_requestedFoveatedRendering{false};
        bool m_loggedResolution{false};
        bool m_isFovMutable{false};
        bool m_isEyeTrackingAvailable{false};

        float m_peripheralPixelDensity{0.5f};
        float m_focusPixelDensity{1.f};
        // [0] = non-foveated, [1] = foveated
        float m_horizontalFovSection[2]{0.75f, 0.5f};
        float m_verticalFovSection[2]{0.7f, 0.5f};
        bool m_preferFoveatedRendering{true};
        bool m_useTurboMode{false};

        bool m_needComputeBaseFov{true};
        // [0] = left, [1] = right
        // [2] = left focus non-foveated, [3] = right focus non-foveated,
        // [4] = left focus foveated, [5] = right focus foveated
        XrFovf m_cachedEyeFov[xr::QuadView::Count + 2]{};
        XrPosef m_cachedEyePoses[xr::StereoView::Count]{};
        XrVector2f m_centerOfFov[xr::StereoView::Count]{};

        XrExtent2Di m_fullFovResolution{};

        std::mutex m_swapchainsMutex;
        std::unordered_map<XrSwapchain, Swapchain> m_swapchains;

        std::mutex m_spacesMutex;
        std::set<XrSpace> m_gazeSpaces;

        XrEyeTrackerFB m_eyeTrackerFB{XR_NULL_HANDLE};
        XrSpace m_viewSpace{XR_NULL_HANDLE};

        ComPtr<ID3D11Device5> m_applicationDevice;
        ComPtr<ID3D11DeviceContext4> m_renderContext;
        ComPtr<ID3DDeviceContextState> m_layerContextState;
        ComPtr<ID3D11SamplerState> m_linearClampSampler;
        ComPtr<ID3D11RasterizerState> m_noDepthRasterizer;
        ComPtr<ID3D11Buffer> m_projectionConstants;
        ComPtr<ID3D11VertexShader> m_projectionVS;
        ComPtr<ID3D11PixelShader> m_projectionPS;

        // Turbo mode.
        std::chrono::time_point<std::chrono::steady_clock> m_lastFrameWaitTimestamp{};
        std::mutex m_frameMutex;
        XrTime m_waitedFrameTime;
        std::mutex m_asyncWaitMutex;
        std::future<void> m_asyncWaitPromise;
        XrTime m_lastPredictedDisplayTime{0};
        XrTime m_lastPredictedDisplayPeriod{0};
        bool m_asyncWaitPolled{false};
        bool m_asyncWaitCompleted{false};

        // FOV submission quirk.
        bool m_needFocusFovCorrectionQuirk{false};
        std::mutex m_focusFovMutex;
        std::map<XrTime, std::pair<XrFovf, XrFovf>> m_focusFovForDisplayTime;

        bool m_debugFocusView{false};
        bool m_debugSimulateTracking{false};
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
