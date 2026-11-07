// Hand tracking for Vapor64 VR (XR_EXT_hand_tracking).
//
// Turns raw hand joints into the same per-hand input the Touch controllers
// produce, so the Java side needs no changes: quest_xr.cpp overlays these
// gestures onto its InputState and the existing onXrInput call carries them.
//
// Design:
//  * One XrHandTrackerEXT per hand, located every frame at the predicted
//    display time, in the LOCAL reference space.
//  * The analog stick comes from the palm's displacement from an anchor taken
//    when the hand first becomes tracked, measured head-relative so turning
//    around keeps the mapping. Deadzone 0.15, full deflection at 8 cm.
//  * Buttons come from pinches (thumb tip to fingertip) with press/release
//    hysteresis so a pinch does not flutter. A fist is all four fingertips
//    curled to the palm.
//  * Controllers always win: quest_xr.cpp only applies a hand's gestures
//    while that hand's controller produced no input that frame.
//
// Uncompiled draft: written against the OpenXR 1.1 headers, not yet built.
// Needs a machine with the Android SDK + NDK to verify.

#pragma once

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace questxr {

// Raw per-hand gestures for one frame. quest_xr.cpp maps these onto the
// Touch-controller InputState (see HAND_TRACKING.md for the mapping table).
struct HandInput {
    bool active = false;  // joints valid this frame
    XrVector2f stick = {};  // palm displacement from anchor, -1..1 per axis
    bool indexPinch = false;  // thumb tip to index fingertip
    bool middlePinch = false;  // thumb tip to middle fingertip
    bool ringPinch = false;  // thumb tip to ring fingertip
    bool littlePinch = false;  // thumb tip to little fingertip
    bool fist = false;  // all four fingertips curled to the palm
};

class HandTracker {
public:
    // Loads the XR_EXT_hand_tracking functions and creates one tracker per
    // hand. Returns false when the extension is unavailable; the caller then
    // keeps controllers-only input. Safe to call destroy() after a false return.
    bool init(XrInstance instance, XrSession session, XrSpace localSpace, XrSpace viewSpace);
    void destroy();

    bool available() const { return mAvailable; }
    void setEnabled(bool enabled) { mEnabled = enabled; }
    bool enabled() const { return mEnabled; }

    // Locate joints at `time` and derive per-hand input. Call once per frame.
    void update(XrTime time);

    // hand: 0 = left, 1 = right (matches quest_xr.cpp's Hand enum).
    const HandInput& input(int hand) const { return mInput[hand]; }

private:
    struct Fns {
        PFN_xrCreateHandTrackerEXT create = nullptr;
        PFN_xrDestroyHandTrackerEXT destroy = nullptr;
        PFN_xrLocateHandJointsEXT locate = nullptr;
    };

    struct HandState {
        XrHandTrackerEXT tracker = XR_NULL_HANDLE;
        bool wasActive = false;
        bool hasAnchor = false;
        XrVector3f anchor = {};
        XrVector2f stick = {};
        bool pinchState[4] = {};  // index, middle, ring, little
    };

    bool locateJoints(int hand, XrTime time, XrHandJointLocationEXT* joints, bool& active);
    static bool jointValid(const XrHandJointLocationEXT& joint);
    static float distance(const XrVector3f& a, const XrVector3f& b);
    static bool pinch(float distance, bool wasPressed);

    Fns mFns;
    XrSession mSession = XR_NULL_HANDLE;
    XrSpace mLocalSpace = XR_NULL_HANDLE;
    XrSpace mViewSpace = XR_NULL_HANDLE;
    HandState mHands[2];
    HandInput mInput[2];
    bool mAvailable = false;
    bool mEnabled = true;
};

}  // namespace questxr
