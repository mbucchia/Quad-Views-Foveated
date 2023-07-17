# The list of OpenXR functions our layer will override.
override_functions = [
    "xrGetSystem",
    "xrGetSystemProperties",
    "xrEnumerateViewConfigurations",
    "xrGetViewConfigurationProperties",
    "xrEnumerateViewConfigurationViews",
    "xrEnumerateEnvironmentBlendModes",
    "xrCreateReferenceSpace",
    "xrDestroySpace",
    "xrCreateSession",
    "xrDestroySession",
    "xrBeginSession",
    "xrAttachSessionActionSets",
    "xrCreateSwapchain",
    "xrDestroySwapchain",
    "xrAcquireSwapchainImage",
    "xrReleaseSwapchainImage",
    "xrLocateViews",
    "xrLocateSpace",
    "xrWaitFrame",
    "xrBeginFrame",
    "xrEndFrame",
    "xrPollEvent",
    "xrGetVisibilityMaskKHR",
]

# The list of OpenXR functions our layer will use from the runtime.
# Might repeat entries from override_functions above.
requested_functions = [
    "xrGetInstanceProperties",
    "xrEnumerateSwapchainImages",
    "xrWaitSwapchainImage",
    "xrStringToPath",
    "xrCreateActionSet",
    "xrCreateAction",
    "xrSuggestInteractionProfileBindings",
    "xrCreateActionSpace",
    "xrGetActionStatePose",
    "xrSyncActions",
    "xrCreateEyeTrackerFB",
    "xrGetEyeGazesFB",
]

# The list of OpenXR extensions our layer will either override or use.
extensions = ["XR_KHR_visibility_mask", "XR_FB_eye_tracking_social"]
