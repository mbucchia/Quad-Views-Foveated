// MIT License
//
// Copyright(c) 2022-2023 Matthieu Bucchianeri
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
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

#include "log.h"
#include "inputs.h"

namespace xr {

    using namespace openxr_api_layer::utils::inputs;

    static inline std::string ToString(MotionControllerButton button) {
        switch (button) {
        case MotionControllerButton::Select:
            return "Select";
        case MotionControllerButton::Menu:
            return "Menu";
        case MotionControllerButton::Squeeze:
            return "Squeeze";
        case MotionControllerButton::ThumbstickClick:
            return "ThumbstickClick";
        };

        return "";
    }

} // namespace xr

namespace {

    using namespace openxr_api_layer::log;
    using namespace openxr_api_layer::utils::general;
    using namespace openxr_api_layer::utils::inputs;
    using namespace xr::math;

    struct FrameworkActions {
        XrActionSet actionSet{XR_NULL_HANDLE};
        XrAction aimAction{XR_NULL_HANDLE};
        XrAction selectAction{XR_NULL_HANDLE};
        XrAction menuAction{XR_NULL_HANDLE};
        XrAction squeezeAction{XR_NULL_HANDLE};
        XrAction thumbstickClickAction{XR_NULL_HANDLE};
        XrAction thumbstickPositionAction{XR_NULL_HANDLE};
        XrAction hapticAction{XR_NULL_HANDLE};
    };

    struct ForwardDispatch {
        PFN_xrWaitFrame xrWaitFrame{nullptr};
        PFN_xrBeginFrame xrBeginFrame{nullptr};
        PFN_xrAttachSessionActionSets xrAttachSessionActionSets{nullptr};
        PFN_xrSyncActions xrSyncActions{nullptr};
    };

    struct InputFramework : IInputFramework {
        InputFramework(const XrInstanceCreateInfo& instanceInfo,
                       XrInstance instance,
                       PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr_,
                       const XrSessionCreateInfo& sessionInfo,
                       XrSession session,
                       const FrameworkActions& frameworkActions,
                       PFN_xrSuggestInteractionProfileBindings xrSuggestInteractionProfileBindings_,
                       const ForwardDispatch& forwardDispatch,
                       InputMethod methods)
            : m_instance(instance), xrGetInstanceProcAddr(xrGetInstanceProcAddr_), m_session(session),
              m_frameworkActions(frameworkActions),
              xrSuggestInteractionProfileBindings(xrSuggestInteractionProfileBindings_),
              m_forwardDispatch(forwardDispatch) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(
                local, "InputFramework_Create", TLXArg(session, "Session"), TLArg((int)methods, "InputMethods"));

            CHECK_XRCMD(
                xrGetInstanceProcAddr(instance, "xrPollEvent", reinterpret_cast<PFN_xrVoidFunction*>(&xrPollEvent)));
            CHECK_XRCMD(xrGetInstanceProcAddr(
                instance, "xrLocateSpace", reinterpret_cast<PFN_xrVoidFunction*>(&xrLocateSpace)));
            CHECK_XRCMD(xrGetInstanceProcAddr(
                instance, "xrGetActionStateBoolean", reinterpret_cast<PFN_xrVoidFunction*>(&xrGetActionStateBoolean)));
            CHECK_XRCMD(xrGetInstanceProcAddr(instance,
                                              "xrGetActionStateVector2f",
                                              reinterpret_cast<PFN_xrVoidFunction*>(&xrGetActionStateVector2f)));
            CHECK_XRCMD(xrGetInstanceProcAddr(
                instance, "xrApplyHapticFeedback", reinterpret_cast<PFN_xrVoidFunction*>(&xrApplyHapticFeedback)));
            CHECK_XRCMD(xrGetInstanceProcAddr(
                instance, "xrStringToPath", reinterpret_cast<PFN_xrVoidFunction*>(&xrStringToPath)));

            CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/left", &m_sidePath[Hands::Left]));
            CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/right", &m_sidePath[Hands::Right]));

            // Create the necessary action spaces for motion controller tracking.
            if (m_frameworkActions.aimAction != XR_NULL_HANDLE) {
                PFN_xrCreateActionSpace xrCreateActionSpace;
                CHECK_XRCMD(xrGetInstanceProcAddr(
                    instance, "xrCreateActionSpace", reinterpret_cast<PFN_xrVoidFunction*>(&xrCreateActionSpace)));

                XrActionSpaceCreateInfo actionSpaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
                actionSpaceInfo.action = m_frameworkActions.aimAction;
                actionSpaceInfo.poseInActionSpace = Pose::Identity();
                actionSpaceInfo.subactionPath = m_sidePath[Hands::Left];
                CHECK_XRCMD(xrCreateActionSpace(m_session, &actionSpaceInfo, &m_aimActionSpace[Hands::Left]));
                actionSpaceInfo.subactionPath = m_sidePath[Hands::Right];
                CHECK_XRCMD(xrCreateActionSpace(m_session, &actionSpaceInfo, &m_aimActionSpace[Hands::Right]));
            }

            TraceLoggingWriteStop(local, "InputFramework_Create", TLPArg(this, "InputFramework"));
        }

        ~InputFramework() override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "InputFramework_Destroy", TLXArg(m_session, "Session"));

            PFN_xrDestroySpace xrDestroySpace;
            if (XR_SUCCEEDED(xrGetInstanceProcAddr(
                    m_instance, "xrDestroySpace", reinterpret_cast<PFN_xrVoidFunction*>(&xrDestroySpace)))) {
                for (uint32_t side = 0; side < Hands::Count; side++) {
                    if (m_aimActionSpace[side] != XR_NULL_HANDLE) {
                        xrDestroySpace(m_aimActionSpace[side]);
                    }
                }
            }

            TraceLoggingWriteStop(local, "InputFramework_Destroy");
        }

        XrSession getSessionHandle() const override {
            return m_session;
        }

        void setSessionData(std::unique_ptr<IInputSessionData> sessionData) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local,
                                   "InputFramework_SetSessionData",
                                   TLXArg(m_session, "Session"),
                                   TLPArg(sessionData.get(), "SessionData"));

            m_sessionData = std::move(sessionData);

            TraceLoggingWriteStop(local, "InputFramework_SetSessionData");
        }

        IInputSessionData* getSessionDataPtr() const override {
            return m_sessionData.get();
        }

        void blockApplicationInput(bool blocked) override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(
                local, "InputFramework_BlockApplicationInput", TLXArg(m_session, "Session"), TLArg(blocked, "Blocked"));

            m_blockApplicationInputs = blocked;

            TraceLoggingWriteStop(local, "InputFramework_BlockApplicationInput");
        }

        XrSpaceLocationFlags locateMotionController(uint32_t side, XrSpace baseSpace, XrPosef& pose) const {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(
                local, "InputFramework_LocateMotionController", TLXArg(m_session, "Session"), TLArg(side, "Side"));

            if (side >= Hands::Count) {
                throw std::runtime_error("Invalid hand");
            }

            if (m_aimActionSpace[side] == XR_NULL_HANDLE) {
                throw std::runtime_error(
                    "Motion controller tracking is not available (did you specify MotionControllerSpatial methods?)");
            }

            // Prevent error before the first frame.
            XrSpaceLocationFlags locationFlags = 0;
            if (m_currentFrameTime) {
                XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
                CHECK_XRCMD(xrLocateSpace(m_aimActionSpace[side], baseSpace, m_currentFrameTime, &location));
                if (Pose::IsPoseValid(location.locationFlags)) {
                    pose = location.pose;
                } else {
                    pose = Pose::Identity();
                }

                locationFlags = location.locationFlags;
            }

            TraceLoggingWriteStop(
                local, "InputFramework_LocateMotionController", TLArg(locationFlags, "LocationFlags"));

            return locationFlags;
        }

        XrSpace getMotionControllerSpace(uint32_t side) const {
            if (side >= Hands::Count) {
                throw std::runtime_error("Invalid hand");
            }

            return m_aimActionSpace[side];
        }

        bool getMotionControllerButtonState(uint32_t side, MotionControllerButton button) const {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local,
                                   "InputFramework_GetMotionControllerButtonState",
                                   TLXArg(m_session, "Session"),
                                   TLArg(side, "Side"),
                                   TLArg(xr::ToString(button).c_str(), "Button"));

            if (side >= Hands::Count) {
                throw std::runtime_error("Invalid hand");
            }

            XrAction action;
            switch (button) {
            case MotionControllerButton::Select:
                action = m_frameworkActions.selectAction;
                break;
            case MotionControllerButton::Menu:
                action = m_frameworkActions.menuAction;
                break;
            case MotionControllerButton::Squeeze:
                action = m_frameworkActions.squeezeAction;
                break;
            case MotionControllerButton::ThumbstickClick:
                action = m_frameworkActions.thumbstickClickAction;
                break;
            default:
                throw std::runtime_error("Invalid button");
            }

            if (action == XR_NULL_HANDLE) {
                throw std::runtime_error("Motion controller buttons are not available (did you specify the "
                                         "MotionControllerButtons input method?)");
            }

            XrActionStateGetInfo actionInfo{XR_TYPE_ACTION_STATE_GET_INFO};
            actionInfo.action = action;
            actionInfo.subactionPath = m_sidePath[side];

            XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
            CHECK_XRCMD(xrGetActionStateBoolean(m_session, &actionInfo, &state));

            TraceLoggingWriteStop(local,
                                  "InputFramework_GetMotionControllerButtonState",
                                  TLArg(state.isActive, "IsActive"),
                                  TLArg(state.currentState, "State"));

            return state.isActive && state.currentState;
        }

        XrVector2f getMotionControllerThumbstickState(uint32_t side) const {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local,
                                   "InputFramework_GetMotionControllerThumbstickState",
                                   TLXArg(m_session, "Session"),
                                   TLArg(side, "Side"));

            if (side >= Hands::Count) {
                throw std::runtime_error("Invalid hand");
            }

            if (m_frameworkActions.thumbstickPositionAction == XR_NULL_HANDLE) {
                throw std::runtime_error("Motion controller buttons are not available (did you specify the "
                                         "MotionControllerButtons input method?)");
            }

            XrActionStateGetInfo actionInfo{XR_TYPE_ACTION_STATE_GET_INFO};
            actionInfo.action = m_frameworkActions.thumbstickPositionAction;
            actionInfo.subactionPath = m_sidePath[side];

            XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
            CHECK_XRCMD(xrGetActionStateVector2f(m_session, &actionInfo, &state));

            TraceLoggingWriteStart(
                local,
                "InputFramework_GetMotionControllerThumbstickState",
                TLArg(state.isActive, "IsActive"),
                TLArg(fmt::format("x:{}, y:{}", state.currentState.x, state.currentState.y).c_str(), "State"));

            if (state.isActive) {
                return state.currentState;
            }
            return {0, 0};
        }

        void pulseMotionControllerHaptics(uint32_t side, float strength) const {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local,
                                   "InputFramework_PulseMotionControllerHaptics",
                                   TLXArg(m_session, "Session"),
                                   TLArg(side, "Side"),
                                   TLArg(strength, "Strength"));

            if (side >= Hands::Count) {
                throw std::runtime_error("Invalid hand");
            }

            if (m_frameworkActions.hapticAction == XR_NULL_HANDLE) {
                throw std::runtime_error("Motion controller haptics is not available (did you specify the "
                                         "MotionControllerHaptics input method?)");
            }

            XrHapticActionInfo hapticInfo{XR_TYPE_HAPTIC_ACTION_INFO};
            hapticInfo.action = m_frameworkActions.hapticAction;
            hapticInfo.subactionPath = m_sidePath[side];

            XrHapticVibration hapticVibration{XR_TYPE_HAPTIC_VIBRATION};
            hapticVibration.amplitude = std::clamp(strength, FLT_EPSILON, 1.f);

            // Let the runtime decide what is best.
            hapticVibration.duration = XR_MIN_HAPTIC_DURATION;
            hapticVibration.frequency = XR_FREQUENCY_UNSPECIFIED;

            CHECK_XRCMD(
                xrApplyHapticFeedback(m_session, &hapticInfo, reinterpret_cast<XrHapticBaseHeader*>(&hapticVibration)));

            TraceLoggingWriteStop(local, "InputFramework_PulseMotionControllerHaptics");
        }

        void updateNeedPollEvent(bool needPollEvent) {
            m_needPollEvent = needPollEvent;
        }

        XrResult xrWaitFrame_subst(XrSession session, const XrFrameWaitInfo* frameWaitInfo, XrFrameState* frameState) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "InputFramework_WaitFrame", TLXArg(session, "Session"));

            const XrResult result = m_forwardDispatch.xrWaitFrame(session, frameWaitInfo, frameState);
            if (XR_SUCCEEDED(result)) {
                std::unique_lock lock(m_frameMutex);

                m_waitedFrameTime.push_back(frameState->predictedDisplayTime);
            }

            TraceLoggingWriteStop(local,
                                  "InputFramework_WaitFrame",
                                  TLArg(xr::ToCString(result), "Result"),
                                  TLArg(frameState->predictedDisplayTime, "PredictedDisplayTime"));

            return result;
        }

        XrResult xrBeginFrame_subst(XrSession session, const XrFrameBeginInfo* frameBeginInfo) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "InputFramework_BeginFrame", TLXArg(session, "Session"));

            const XrResult result = m_forwardDispatch.xrBeginFrame(session, frameBeginInfo);
            if (XR_SUCCEEDED(result)) {
                std::unique_lock lock(m_frameMutex);

                // If the application doesn't use motion controller at all, we need to attach our actionset ourselves...
                if (m_frameworkActions.actionSet != XR_NULL_HANDLE && !m_wasActionSetsAttached) {
                    TraceLoggingWriteTagged(local, "InputFramework_BeginFrame_SetupFrameworkActionSet");

                    // Make sure our bindings are complete. We only submit suggestions for the interaction profiles in
                    // the core spec, and hope runtimes do the right thing for implicit remapping.
                    const char* coreInteractionProfiles[] = {"/interaction_profiles/khr/simple_controller",
                                                             "/interaction_profiles/htc/vive_controller",
                                                             "/interaction_profiles/microsoft/motion_controller",
                                                             "/interaction_profiles/oculus/touch_controller",
                                                             "/interaction_profiles/valve/index_controller"};
                    for (const auto& interationProfile : coreInteractionProfiles) {
                        XrInteractionProfileSuggestedBinding bindings{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
                        CHECK_XRCMD(xrStringToPath(m_instance, interationProfile, &bindings.interactionProfile));
                        const XrResult suggestResult = xrSuggestInteractionProfileBindings(m_instance, &bindings);
                        if (XR_FAILED(suggestResult)) {
                            TraceLoggingWriteTagged(local,
                                                    "InputFramework_BeginFrame_SuggestInteractionProfileBindings_Error",
                                                    TLArg(xr::ToString(suggestResult).c_str(), "Result"));
                            ErrorLog(fmt::format("Could not suggest framework's bindings: {}\n",
                                                 xr::ToCString(suggestResult)));
                        }
                    }

                    XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
                    const XrResult attachResult = xrAttachSessionActionSets_subst(session, &attachInfo);
                    if (XR_SUCCEEDED(attachResult)) {
                        // We will also need to do our own synchronization of actions.
                        m_needSyncActions = true;
                    } else {
                        TraceLoggingWriteTagged(local,
                                                "InputFramework_BeginFrame_AttachSessionActionSets_Error",
                                                TLArg(xr::ToString(attachResult).c_str(), "Result"));
                        ErrorLog(fmt::format("Could not attach framework's actionset for session: {}\n",
                                             xr::ToCString(attachResult)));
                    }
                }

                // ...and to synchronize actions ourselves.
                if (m_needSyncActions) {
                    // If the application does not poll for events, we need to do it ourselves to avoid the session
                    // remaining stuck in the non-focused state (which will make xrSyncActions() fail).
                    if (m_needPollEvent) {
                        TraceLoggingWriteTagged(local, "InputFramework_BeginFrame_PollEvent");

                        while (true) {
                            XrEventDataBuffer buf{XR_TYPE_EVENT_DATA_BUFFER};
                            if (xrPollEvent(m_instance, &buf) != XR_SUCCESS) {
                                break;
                            }
                        }
                    }

                    TraceLoggingWriteTagged(local, "InputFramework_BeginFrame_SyncFrameworkActions");
                    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
                    CHECK_XRCMD(xrSyncActions_subst(session, &syncInfo));
                }

                // We keep track of the current frame time in order to query the tracking information for that frame.
                m_currentFrameTime = m_waitedFrameTime.front();
                m_waitedFrameTime.pop_front();
            }

            TraceLoggingWriteStop(local, "InputFramework_BeginFrame", TLArg(xr::ToCString(result), "Result"));

            return result;
        }

        XrResult xrAttachSessionActionSets_subst(XrSession session, const XrSessionActionSetsAttachInfo* attachInfo) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "InputFramework_AttachSessionActionSets", TLXArg(session, "Session"));

            if (attachInfo->type != XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            XrSessionActionSetsAttachInfo chainAttachInfo = *attachInfo;

            // Inject our actionset.
            std::vector<XrActionSet> actionSets(chainAttachInfo.actionSets,
                                                chainAttachInfo.actionSets + chainAttachInfo.countActionSets);
            if (m_frameworkActions.actionSet != XR_NULL_HANDLE) {
                actionSets.push_back(m_frameworkActions.actionSet);
            }
            chainAttachInfo.actionSets = actionSets.data();
            chainAttachInfo.countActionSets = static_cast<uint32_t>(actionSets.size());

            const XrResult result = m_forwardDispatch.xrAttachSessionActionSets(session, &chainAttachInfo);
            if (XR_SUCCEEDED(result)) {
                m_wasActionSetsAttached = true;
            }

            TraceLoggingWriteStop(
                local, "InputFramework_AttachSessionActionSets", TLArg(xr::ToCString(result), "Result"));

            return result;
        }

        XrResult xrSyncActions_subst(XrSession session, const XrActionsSyncInfo* syncInfo) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "InputFramework_SyncActions", TLXArg(session, "Session"));

            if (syncInfo->type != XR_TYPE_ACTIONS_SYNC_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            XrActionsSyncInfo chainSyncInfo = *syncInfo;

            // Sync our actionset and block out app action sets when requested
            std::vector<XrActiveActionSet> activeActionSets;
            if (!m_blockApplicationInputs) {
                activeActionSets.assign(chainSyncInfo.activeActionSets,
                                        chainSyncInfo.activeActionSets + chainSyncInfo.countActiveActionSets);
            }
            if (m_frameworkActions.actionSet != XR_NULL_HANDLE) {
                activeActionSets.push_back({m_frameworkActions.actionSet, XR_NULL_PATH});
            }
            chainSyncInfo.activeActionSets = activeActionSets.data();
            chainSyncInfo.countActiveActionSets = static_cast<uint32_t>(activeActionSets.size());

            const XrResult result = m_forwardDispatch.xrSyncActions(session, &chainSyncInfo);

            TraceLoggingWriteStop(local, "InputFramework_SyncActions", TLArg(xr::ToCString(result), "Result"));

            return result;
        }

        const XrInstance m_instance;
        const PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr;
        const XrSession m_session;
        const FrameworkActions m_frameworkActions;
        const PFN_xrSuggestInteractionProfileBindings xrSuggestInteractionProfileBindings;
        const ForwardDispatch& m_forwardDispatch;

        std::unique_ptr<IInputSessionData> m_sessionData;

        bool m_blockApplicationInputs{false};
        XrPath m_sidePath[Hands::Count]{{XR_NULL_PATH}, {XR_NULL_PATH}};
        XrSpace m_aimActionSpace[Hands::Count]{{XR_NULL_HANDLE}, {XR_NULL_HANDLE}};
        bool m_wasActionSetsAttached{false};
        bool m_needSyncActions{false};
        bool m_needPollEvent;

        std::mutex m_frameMutex;
        std::deque<XrTime> m_waitedFrameTime;
        XrTime m_currentFrameTime{0};

        PFN_xrPollEvent xrPollEvent{nullptr};
        PFN_xrLocateSpace xrLocateSpace{nullptr};
        PFN_xrGetActionStateBoolean xrGetActionStateBoolean{nullptr};
        PFN_xrGetActionStateVector2f xrGetActionStateVector2f{nullptr};
        PFN_xrApplyHapticFeedback xrApplyHapticFeedback{nullptr};
        PFN_xrStringToPath xrStringToPath{nullptr};
    };

    struct InputFrameworkFactory : IInputFrameworkFactory {
        InputFrameworkFactory(const XrInstanceCreateInfo& instanceInfo,
                              XrInstance instance,
                              PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr_,
                              InputMethod methods)
            : m_instanceInfo(instanceInfo), m_instance(instance), xrGetInstanceProcAddr(xrGetInstanceProcAddr_),
              m_methods(methods) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "InputFrameworkFactory_Create", TLArg((int)methods, "InputMethods"));

            {
                std::unique_lock lock(factoryMutex);
                if (factory) {
                    throw std::runtime_error("There can only be one InputFramework factory");
                }
                factory = this;
            }

            // Deep-copy the instance extensions strings.
            m_instanceExtensions.reserve(m_instanceInfo.enabledExtensionCount);
            for (uint32_t i = 0; i < m_instanceInfo.enabledExtensionCount; i++) {
                m_instanceExtensions.push_back(m_instanceInfo.enabledExtensionNames[i]);
                m_instanceExtensionsArray.push_back(m_instanceExtensions.back().c_str());
            }
            m_instanceInfo.enabledExtensionNames = m_instanceExtensionsArray.data();

            CHECK_XRCMD(xrGetInstanceProcAddr(
                instance, "xrStringToPath", reinterpret_cast<PFN_xrVoidFunction*>(&xrStringToPath)));
            CHECK_XRCMD(xrGetInstanceProcAddr(
                instance, "xrPathToString", reinterpret_cast<PFN_xrVoidFunction*>(&xrPathToString)));

            // When using motion controllers, create the necessary actions tied to the instance.
            if ((methods & InputMethod::MotionControllerSpatial) == InputMethod::MotionControllerSpatial ||
                (methods & InputMethod::MotionControllerButtons) == InputMethod::MotionControllerButtons ||
                (methods & InputMethod::MotionControllerHaptics) == InputMethod::MotionControllerHaptics) {
                PFN_xrCreateActionSet xrCreateActionSet;
                CHECK_XRCMD(xrGetInstanceProcAddr(
                    instance, "xrCreateActionSet", reinterpret_cast<PFN_xrVoidFunction*>(&xrCreateActionSet)));
                PFN_xrCreateAction xrCreateAction;
                CHECK_XRCMD(xrGetInstanceProcAddr(
                    instance, "xrCreateAction", reinterpret_cast<PFN_xrVoidFunction*>(&xrCreateAction)));

                XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
                strcpy(actionSetInfo.actionSetName, "input_framework");
                strcpy(actionSetInfo.localizedActionSetName, "Input Framework");
                // TODO: Need to investigate whether this can work in all applications.
                actionSetInfo.priority = 0;
                CHECK_XRCMD(xrCreateActionSet(m_instance, &actionSetInfo, &m_frameworkActions.actionSet));

                XrPath subactionPaths[Hands::Count];
                CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/left", &subactionPaths[Hands::Left]));
                CHECK_XRCMD(xrStringToPath(m_instance, "/user/hand/right", &subactionPaths[Hands::Right]));

                if ((methods & InputMethod::MotionControllerSpatial) == InputMethod::MotionControllerSpatial) {
                    XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
                    strcpy(actionInfo.actionName, "aim");
                    actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
                    strcpy(actionInfo.localizedActionName, "Aim");
                    actionInfo.subactionPaths = subactionPaths;
                    actionInfo.countSubactionPaths = static_cast<uint32_t>(std::size(subactionPaths));
                    CHECK_XRCMD(
                        xrCreateAction(m_frameworkActions.actionSet, &actionInfo, &m_frameworkActions.aimAction));
                }

                if ((methods & InputMethod::MotionControllerButtons) == InputMethod::MotionControllerButtons) {
                    {
                        XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
                        strcpy(actionInfo.actionName, "select");
                        actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
                        strcpy(actionInfo.localizedActionName, "Select");
                        actionInfo.subactionPaths = subactionPaths;
                        actionInfo.countSubactionPaths = static_cast<uint32_t>(std::size(subactionPaths));
                        CHECK_XRCMD(xrCreateAction(
                            m_frameworkActions.actionSet, &actionInfo, &m_frameworkActions.selectAction));
                    }
                    {
                        XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
                        strcpy(actionInfo.actionName, "menu");
                        actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
                        strcpy(actionInfo.localizedActionName, "Menu");
                        actionInfo.subactionPaths = subactionPaths;
                        actionInfo.countSubactionPaths = static_cast<uint32_t>(std::size(subactionPaths));
                        CHECK_XRCMD(
                            xrCreateAction(m_frameworkActions.actionSet, &actionInfo, &m_frameworkActions.menuAction));
                    }
                    {
                        XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
                        strcpy(actionInfo.actionName, "squeeze");
                        actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
                        strcpy(actionInfo.localizedActionName, "Squeeze");
                        actionInfo.subactionPaths = subactionPaths;
                        actionInfo.countSubactionPaths = static_cast<uint32_t>(std::size(subactionPaths));
                        CHECK_XRCMD(xrCreateAction(
                            m_frameworkActions.actionSet, &actionInfo, &m_frameworkActions.squeezeAction));
                    }
                    {
                        XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
                        strcpy(actionInfo.actionName, "thumbstick_click");
                        actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
                        strcpy(actionInfo.localizedActionName, "Thumbstick Click");
                        actionInfo.subactionPaths = subactionPaths;
                        actionInfo.countSubactionPaths = static_cast<uint32_t>(std::size(subactionPaths));
                        CHECK_XRCMD(xrCreateAction(
                            m_frameworkActions.actionSet, &actionInfo, &m_frameworkActions.thumbstickClickAction));
                    }
                    {
                        XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
                        strcpy(actionInfo.actionName, "thumbstick_position");
                        actionInfo.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
                        strcpy(actionInfo.localizedActionName, "Thumbstick Position");
                        actionInfo.subactionPaths = subactionPaths;
                        actionInfo.countSubactionPaths = static_cast<uint32_t>(std::size(subactionPaths));
                        CHECK_XRCMD(xrCreateAction(
                            m_frameworkActions.actionSet, &actionInfo, &m_frameworkActions.thumbstickPositionAction));
                    }
                }

                if ((methods & InputMethod::MotionControllerHaptics) == InputMethod::MotionControllerHaptics) {
                    XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
                    strcpy(actionInfo.actionName, "vibration");
                    actionInfo.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
                    strcpy(actionInfo.localizedActionName, "Vibration");
                    actionInfo.subactionPaths = subactionPaths;
                    actionInfo.countSubactionPaths = static_cast<uint32_t>(std::size(subactionPaths));
                    CHECK_XRCMD(
                        xrCreateAction(m_frameworkActions.actionSet, &actionInfo, &m_frameworkActions.hapticAction));
                }
            }

            // xrCreateSession(), xrDestroySession() and xrSuggestInteractionProfileBindings() function pointers are
            // chained.

            TraceLoggingWriteStop(local, "InputFrameworkFactory_Create", TLPArg(this, "InputFrameworkFactory"));
        }

        ~InputFrameworkFactory() override {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "InputFrameworkFactory_Destroy");

            PFN_xrDestroyAction xrDestroyAction;
            if (XR_SUCCEEDED(xrGetInstanceProcAddr(
                    m_instance, "xrDestroyAction", reinterpret_cast<PFN_xrVoidFunction*>(&xrDestroyAction)))) {
                if (m_frameworkActions.aimAction != XR_NULL_HANDLE) {
                    xrDestroyAction(m_frameworkActions.aimAction);
                }
                if (m_frameworkActions.selectAction != XR_NULL_HANDLE) {
                    xrDestroyAction(m_frameworkActions.selectAction);
                }
                if (m_frameworkActions.menuAction != XR_NULL_HANDLE) {
                    xrDestroyAction(m_frameworkActions.menuAction);
                }
                if (m_frameworkActions.squeezeAction != XR_NULL_HANDLE) {
                    xrDestroyAction(m_frameworkActions.squeezeAction);
                }
                if (m_frameworkActions.thumbstickClickAction != XR_NULL_HANDLE) {
                    xrDestroyAction(m_frameworkActions.thumbstickClickAction);
                }
                if (m_frameworkActions.thumbstickPositionAction != XR_NULL_HANDLE) {
                    xrDestroyAction(m_frameworkActions.thumbstickPositionAction);
                }
                if (m_frameworkActions.hapticAction != XR_NULL_HANDLE) {
                    xrDestroyAction(m_frameworkActions.hapticAction);
                }
            }

            if (m_frameworkActions.actionSet != XR_NULL_HANDLE) {
                PFN_xrDestroyActionSet xrDestroyActionSet;
                if (XR_SUCCEEDED(xrGetInstanceProcAddr(m_instance,
                                                       "xrDestroyActionSet",
                                                       reinterpret_cast<PFN_xrVoidFunction*>(&xrDestroyActionSet)))) {
                    xrDestroyActionSet(m_frameworkActions.actionSet);
                }
            }

            {
                std::unique_lock lock(factoryMutex);

                factory = nullptr;
            }

            TraceLoggingWriteStop(local, "InputFrameworkFactory_Destroy");
        }

        void xrGetInstanceProcAddr_post(XrInstance instance, const char* name, PFN_xrVoidFunction* function) override {
            const std::string_view functionName(name);
            if (functionName == "xrCreateSession") {
                xrCreateSession = reinterpret_cast<PFN_xrCreateSession>(*function);
                *function = reinterpret_cast<PFN_xrVoidFunction>(hookCreateSession);
            } else if (functionName == "xrDestroySession") {
                xrDestroySession = reinterpret_cast<PFN_xrDestroySession>(*function);
                *function = reinterpret_cast<PFN_xrVoidFunction>(hookDestroySession);
            } else if (functionName == "xrPollEvent") {
                xrPollEvent = reinterpret_cast<PFN_xrPollEvent>(*function);
                *function = reinterpret_cast<PFN_xrVoidFunction>(hookPollEvent);
            } else if (functionName == "xrSuggestInteractionProfileBindings") {
                xrSuggestInteractionProfileBindings =
                    reinterpret_cast<PFN_xrSuggestInteractionProfileBindings>(*function);
                *function = reinterpret_cast<PFN_xrVoidFunction>(hookSuggestInteractionProfileBindings);
            } else if (functionName == "xrWaitFrame") {
                m_forwardDispatch.xrWaitFrame = reinterpret_cast<PFN_xrWaitFrame>(*function);
                *function = reinterpret_cast<PFN_xrVoidFunction>(hookWaitFrame);
            } else if (functionName == "xrBeginFrame") {
                m_forwardDispatch.xrBeginFrame = reinterpret_cast<PFN_xrBeginFrame>(*function);
                *function = reinterpret_cast<PFN_xrVoidFunction>(hookBeginFrame);
            } else if (functionName == "xrAttachSessionActionSets") {
                m_forwardDispatch.xrAttachSessionActionSets =
                    reinterpret_cast<PFN_xrAttachSessionActionSets>(*function);
                *function = reinterpret_cast<PFN_xrVoidFunction>(hookAttachSessionActionSets);
            } else if (functionName == "xrSyncActions") {
                m_forwardDispatch.xrSyncActions = reinterpret_cast<PFN_xrSyncActions>(*function);
                *function = reinterpret_cast<PFN_xrVoidFunction>(hookSyncActions);
            }
        }

        XrResult xrPollEvent_subst(XrInstance instance, XrEventDataBuffer* eventData) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "InputFrameworkFactory_xrPollEvent");

            const XrResult result = xrPollEvent(instance, eventData);
            if (XR_SUCCEEDED(result)) {
                m_needPollEvent = false;
            }

            TraceLoggingWriteStop(local, "InputFrameworkFactory_xrPollEvent", TLArg(xr::ToCString(result), "Result"));

            return result;
        }

        IInputFramework* getInputFramework(XrSession session) override {
            std::unique_lock lock(m_sessionsMutex);

            auto it = m_sessions.find(session);
            if (it == m_sessions.end()) {
                throw std::runtime_error("No session found");
            }

            return it->second.get();
        }

        XrResult xrCreateSession_subst(XrInstance instance, const XrSessionCreateInfo* createInfo, XrSession* session) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "InputFrameworkFactory_CreateSession");

            const XrResult result = xrCreateSession(instance, createInfo, session);
            if (XR_SUCCEEDED(result)) {
                std::unique_lock lock(m_sessionsMutex);

                m_sessions.insert_or_assign(
                    *session,
                    std::move(std::make_unique<InputFramework>(m_instanceInfo,
                                                               m_instance,
                                                               xrGetInstanceProcAddr,
                                                               *createInfo,
                                                               *session,
                                                               m_frameworkActions,
                                                               hookSuggestInteractionProfileBindings,
                                                               m_forwardDispatch,
                                                               m_methods)));
            }

            TraceLoggingWriteStop(local,
                                  "InputFrameworkFactory_CreateSession",
                                  TLArg(xr::ToCString(result), "Result"),
                                  TLXArg(*session, "Session"));

            return result;
        }

        XrResult xrDestroySession_subst(XrSession session) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "InputFrameworkFactory_DestroySession", TLXArg(session, "Session"));

            {
                std::unique_lock lock(m_sessionsMutex);

                auto it = m_sessions.find(session);
                if (it != m_sessions.end()) {
                    m_sessions.erase(it);
                }
            }
            const XrResult result = xrDestroySession(session);

            TraceLoggingWriteStop(
                local, "InputFrameworkFactory_DestroySession", TLArg(xr::ToCString(result), "Result"));

            return result;
        }

        XrResult xrSuggestInteractionProfileBindings_subst(
            XrInstance instance, const XrInteractionProfileSuggestedBinding* suggestedBindings) {
            TraceLocalActivity(local);
            TraceLoggingWriteStart(local, "InputFrameworkFactory_SuggestInteractionProfileBindings");

            if (suggestedBindings->type != XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            XrInteractionProfileSuggestedBinding chainSuggestedBindings = *suggestedBindings;

            // Inject our bindings into the relevant interaction profiles.
            std::vector<XrActionSuggestedBinding> updatedBindings(chainSuggestedBindings.suggestedBindings,
                                                                  chainSuggestedBindings.suggestedBindings +
                                                                      chainSuggestedBindings.countSuggestedBindings);
            const std::string& interationProfile = getXrPath(suggestedBindings->interactionProfile);
            TraceLoggingWriteTagged(local,
                                    "InputFrameworkFactory_SuggestInteractionProfileBindings",
                                    TLArg(interationProfile.c_str(), "InteractionProfile"));
            static struct InteractionProfileCapabilities {
                const char* interactionProfile;
                bool hasAimPose{false};
                bool hasHaptic{false};
                std::string selectPath;
                std::string menuPath;
                std::string squeezePath;
                std::string thumbstickPath;
            } interactionProfileTable[] = {
                // clang-format off
                //                                                       Aim?   Hapt?  Select.           Menu.              Squeeze.          Thumbstick.
                {"/interaction_profiles/khr/simple_controller",          true,  true,  "/input/select",  "/input/menu",     "",               ""},
                {"/interaction_profiles/htc/vive_controller",            true,  true,  "/input/trigger", "/input/menu",     "/input/squeeze", "/input/trackpad"},
                {"/interaction_profiles/microsoft/motion_controller",    true,  true,  "/input/trigger", "/input/menu",     "/input/squeeze", "/input/thumbstick"},
                {"/interaction_profiles/oculus/touch_controller",        true,  true,  "/input/trigger", "left/input/menu", "/input/squeeze", "/input/thumbstick"},
                {"/interaction_profiles/valve/index_controller",         true,  true,  "/input/trigger", "/input/a",        "/input/squeeze", "/input/thumbstick"},
                {"/interaction_profiles/hp/mixed_reality_controller",    true,  true,  "/input/trigger", "/input/menu",     "/input/squeeze", "/input/thumbstick"},
                {"/interaction_profiles/bytedance/pico_neo3_controller", true,  true,  "/input/trigger", "/input/menu",     "/input/squeeze", "/input/thumbstick"},
                {"/interaction_profiles/bytedance/pico4_controller",     true,  true,  "/input/trigger", "left/input/menu", "/input/squeeze", "/input/thumbstick"},
                {"/interaction_profiles/facebook/touch_controller_pro",  true,  true,  "/input/trigger", "left/input/menu", "/input/squeeze", "/input/thumbstick"},
                {"/interaction_profiles/htc/vive_cosmos_controller",     true,  true,  "/input/trigger", "left/input/menu", "/input/squeeze", "/input/thumbstick"},
                {"/interaction_profiles/htc/vive_focus3_controller",     true,  true,  "/input/trigger", "left/input/menu", "/input/squeeze", "/input/thumbstick"},
                {"/interaction_profiles/microsoft/hand_interaction",     true,  false, "/input/select",   "",               "/input/squeeze", ""},
                {""} // Catch-all
                // clang-format on
            };

            struct InteractionProfileCapabilities capabilities;
            for (uint32_t i = 0; i < std::size(interactionProfileTable); i++) {
                if (interationProfile == interactionProfileTable[i].interactionProfile) {
                    capabilities = interactionProfileTable[i];
                    break;
                }
            }

            const auto injectLeftRightBinding = [&](XrAction action, const std::string& path) {
                if (action == XR_NULL_HANDLE) {
                    return;
                }

                XrPath leftActionPath = XR_NULL_PATH;
                XrPath rightActionPath = XR_NULL_PATH;
                if (startsWith(path, "left/")) {
                    CHECK_XRCMD(
                        xrStringToPath(m_instance, ("/user/hand/left" + path.substr(4)).c_str(), &leftActionPath));
                } else if (startsWith(path, "right/")) {
                    CHECK_XRCMD(
                        xrStringToPath(m_instance, ("/user/hand/right" + path.substr(5)).c_str(), &rightActionPath));
                } else {
                    CHECK_XRCMD(xrStringToPath(m_instance, ("/user/hand/left" + path).c_str(), &leftActionPath));
                    CHECK_XRCMD(xrStringToPath(m_instance, ("/user/hand/right" + path).c_str(), &rightActionPath));
                }
                if (leftActionPath != XR_NULL_PATH) {
                    TraceLoggingWriteTagged(local,
                                            "InputFrameworkFactory_SuggestInteractionProfileBindings_Inject",
                                            TLArg("Left", "Side"),
                                            TLArg(path.c_str(), "ActionPath"));
                    updatedBindings.push_back({action, leftActionPath});
                }
                if (rightActionPath != XR_NULL_PATH) {
                    TraceLoggingWriteTagged(local,
                                            "InputFrameworkFactory_SuggestInteractionProfileBindings_Inject",
                                            TLArg("Right", "Side"),
                                            TLArg(path.c_str(), "ActionPath"));
                    updatedBindings.push_back({action, rightActionPath});
                }
            };

            // Choose bindings based on the capabilities of the interaction profile.
            if (capabilities.hasAimPose) {
                injectLeftRightBinding(m_frameworkActions.aimAction, "/input/aim/pose");
            }
            if (!capabilities.selectPath.empty()) {
                injectLeftRightBinding(m_frameworkActions.selectAction, capabilities.selectPath);
            }
            if (!capabilities.menuPath.empty()) {
                injectLeftRightBinding(m_frameworkActions.menuAction, capabilities.menuPath);
            }
            if (!capabilities.squeezePath.empty()) {
                injectLeftRightBinding(m_frameworkActions.squeezeAction, capabilities.squeezePath);
            }
            if (!capabilities.thumbstickPath.empty()) {
                injectLeftRightBinding(m_frameworkActions.thumbstickClickAction,
                                       capabilities.thumbstickPath + "/click");
                injectLeftRightBinding(m_frameworkActions.thumbstickPositionAction, capabilities.thumbstickPath);
            }
            if (capabilities.hasHaptic) {
                injectLeftRightBinding(m_frameworkActions.hapticAction, "/output/haptic");
            }

            chainSuggestedBindings.suggestedBindings = updatedBindings.data();
            chainSuggestedBindings.countSuggestedBindings = static_cast<uint32_t>(updatedBindings.size());

            const XrResult result = xrSuggestInteractionProfileBindings(instance, &chainSuggestedBindings);

            TraceLoggingWriteStop(local,
                                  "InputFrameworkFactory_SuggestInteractionProfileBindings",
                                  TLArg(xr::ToCString(result), "Result"));

            return result;
        }

        XrResult xrWaitFrame_subst(XrSession session, const XrFrameWaitInfo* frameWaitInfo, XrFrameState* frameState) {
            InputFramework* inputFramework = static_cast<InputFramework*>(getInputFramework(session));
            inputFramework->updateNeedPollEvent(m_needPollEvent);
            return inputFramework->xrWaitFrame_subst(session, frameWaitInfo, frameState);
        }

        XrResult xrBeginFrame_subst(XrSession session, const XrFrameBeginInfo* frameBeginInfo) {
            return static_cast<InputFramework*>(getInputFramework(session))
                ->xrBeginFrame_subst(session, frameBeginInfo);
        }

        XrResult xrAttachSessionActionSets_subst(XrSession session, const XrSessionActionSetsAttachInfo* attachInfo) {
            return static_cast<InputFramework*>(getInputFramework(session))
                ->xrAttachSessionActionSets_subst(session, attachInfo);
        }

        XrResult xrSyncActions_subst(XrSession session, const XrActionsSyncInfo* syncInfo) {
            return static_cast<InputFramework*>(getInputFramework(session))->xrSyncActions_subst(session, syncInfo);
        }

        const std::string getXrPath(XrPath path) {
            char buf[XR_MAX_PATH_LENGTH];
            uint32_t count;
            CHECK_XRCMD(xrPathToString(m_instance, path, sizeof(buf), &count, buf));
            std::string str;
            str.assign(buf, count - 1);
            return str;
        }

        const XrInstance m_instance;
        const PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr;
        const InputMethod m_methods;
        XrInstanceCreateInfo m_instanceInfo;
        std::vector<std::string> m_instanceExtensions;
        std::vector<const char*> m_instanceExtensionsArray;

        std::mutex m_sessionsMutex;
        std::unordered_map<XrSession, std::unique_ptr<InputFramework>> m_sessions;

        FrameworkActions m_frameworkActions;

        PFN_xrCreateSession xrCreateSession{nullptr};
        PFN_xrDestroySession xrDestroySession{nullptr};
        PFN_xrPollEvent xrPollEvent{nullptr};
        PFN_xrSuggestInteractionProfileBindings xrSuggestInteractionProfileBindings{nullptr};
        PFN_xrStringToPath xrStringToPath{nullptr};
        PFN_xrPathToString xrPathToString{nullptr};
        ForwardDispatch m_forwardDispatch;
        bool m_needPollEvent{true};

        static inline std::mutex factoryMutex;
        static inline InputFrameworkFactory* factory{nullptr};

        static XrResult hookCreateSession(XrInstance instance,
                                          const XrSessionCreateInfo* createInfo,
                                          XrSession* session) {
            return factory->xrCreateSession_subst(instance, createInfo, session);
        }

        static XrResult hookDestroySession(XrSession session) {
            return factory->xrDestroySession_subst(session);
        }

        static XrResult hookPollEvent(XrInstance instance, XrEventDataBuffer* eventData) {
            return factory->xrPollEvent_subst(instance, eventData);
        }

        static XrResult hookSuggestInteractionProfileBindings(
            XrInstance instance, const XrInteractionProfileSuggestedBinding* suggestedBindings) {
            return factory->xrSuggestInteractionProfileBindings_subst(instance, suggestedBindings);
        }

        static XrResult hookWaitFrame(XrSession session,
                                      const XrFrameWaitInfo* frameWaitInfo,
                                      XrFrameState* frameState) {
            return factory->xrWaitFrame_subst(session, frameWaitInfo, frameState);
        }

        static XrResult hookBeginFrame(XrSession session, const XrFrameBeginInfo* frameBeginInfo) {
            return factory->xrBeginFrame_subst(session, frameBeginInfo);
        }

        static XrResult hookAttachSessionActionSets(XrSession session,
                                                    const XrSessionActionSetsAttachInfo* attachInfo) {
            return factory->xrAttachSessionActionSets_subst(session, attachInfo);
        }

        static XrResult hookSyncActions(XrSession session, const XrActionsSyncInfo* syncInfo) {
            return factory->xrSyncActions_subst(session, syncInfo);
        }
    };

} // namespace

namespace openxr_api_layer::utils::inputs {
    std::shared_ptr<IInputFrameworkFactory> createInputFrameworkFactory(const XrInstanceCreateInfo& instanceInfo,
                                                                        XrInstance instance,
                                                                        PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr,
                                                                        InputMethod methods) {
        return std::make_shared<InputFrameworkFactory>(instanceInfo, instance, xrGetInstanceProcAddr, methods);
    }

} // namespace openxr_api_layer::utils::inputs
