// Hand tracking for Vapor64 VR (XR_EXT_hand_tracking).
// See hand_tracking.h and quest-xr/HAND_TRACKING.md.

#include "hand_tracking.h"

#include <android/log.h>
#include <cmath>
#include <cstring>

#define TAG "QuestXrHand"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace questxr {
namespace {

// Tuning. All distances in meters; see HAND_TRACKING.md for why these values.
constexpr float kPinchPress = 0.025f;  // thumb tip to fingertip: press below this
constexpr float kPinchRelease = 0.035f;  // release above this (hysteresis)
constexpr float kFistDistance = 0.055f;  // mean fingertip-to-palm distance for a fist
constexpr float kStickRadius = 0.08f;  // palm displacement for full stick deflection
constexpr float kStickDeadzone = 0.15f;  // fraction of full deflection ignored
constexpr float kStickSmoothing = 0.5f;  // per-frame lerp towards the target stick

// Joint indices for the four pinch pairs (thumb tip to each fingertip).
constexpr int kTipJoints[4] = {
    XR_HAND_JOINT_INDEX_TIP_EXT,
    XR_HAND_JOINT_MIDDLE_TIP_EXT,
    XR_HAND_JOINT_RING_TIP_EXT,
    XR_HAND_JOINT_LITTLE_TIP_EXT,
};
constexpr int kThumbTip = XR_HAND_JOINT_THUMB_TIP_EXT;
constexpr int kPalm = XR_HAND_JOINT_PALM_EXT;

XrVector3f add(const XrVector3f& a, const XrVector3f& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
XrVector3f sub(const XrVector3f& a, const XrVector3f& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
XrVector3f scale(const XrVector3f& v, float f) { return {v.x * f, v.y * f, v.z * f}; }
float dot(const XrVector3f& a, const XrVector3f& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float length(const XrVector3f& v) { return std::sqrt(dot(v, v)); }

XrVector3f rotateVector(const XrQuaternionf& q, const XrVector3f& v) {
    // v' = q * v * q^-1, expanded for a unit quaternion
    const XrVector3f u = {q.x, q.y, q.z};
    const XrVector3f cross1 = {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    const XrVector3f cross2 = {u.y * cross1.z - u.z * cross1.y, u.z * cross1.x - u.x * cross1.z,
                               u.x * cross1.y - u.y * cross1.x};
    return add(add(v, scale(cross1, 2.0f * q.w)), scale(cross2, 2.0f));
}

template <typename Fn>
bool getProc(XrInstance instance, const char* name, Fn& fn) {
    PFN_xrVoidFunction proc = nullptr;
    if (XR_FAILED(xrGetInstanceProcAddr(instance, name, &proc)) || proc == nullptr) {
        LOGE("xrGetInstanceProcAddr(%s) failed", name);
        return false;
    }
    fn = reinterpret_cast<Fn>(proc);
    return true;
}

}  // namespace

bool HandTracker::jointValid(const XrHandJointLocationEXT& joint) {
    const XrSpaceLocationFlags required =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    return (joint.locationFlags & required) == required;
}

float HandTracker::distance(const XrVector3f& a, const XrVector3f& b) {
    return length(sub(a, b));
}

bool HandTracker::pinch(float distance, bool wasPressed) {
    // Hysteresis: press close in, release further out, so a held pinch does not flutter.
    return wasPressed ? distance < kPinchRelease : distance < kPinchPress;
}

bool HandTracker::init(XrInstance instance, XrSession session, XrSpace localSpace, XrSpace viewSpace) {
    mSession = session;
    mLocalSpace = localSpace;
    mViewSpace = viewSpace;

    if (!getProc(instance, "xrCreateHandTrackerEXT", mFns.create) ||
        !getProc(instance, "xrDestroyHandTrackerEXT", mFns.destroy) ||
        !getProc(instance, "xrLocateHandJointsEXT", mFns.locate)) {
        return false;
    }

    const XrHandEXT hands[2] = {XR_HAND_LEFT_EXT, XR_HAND_RIGHT_EXT};
    for (int i = 0; i < 2; ++i) {
        XrHandTrackerCreateInfoEXT info{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
        info.hand = hands[i];
        info.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
        if (XR_FAILED(mFns.create(session, &info, &mHands[i].tracker))) {
            LOGE("xrCreateHandTrackerEXT(hand %d) failed", i);
            destroy();
            return false;
        }
    }
    mAvailable = true;
    LOGI("Hand tracking ready");
    return true;
}

void HandTracker::destroy() {
    for (HandState& hand : mHands) {
        if (hand.tracker != XR_NULL_HANDLE && mFns.destroy != nullptr) {
            mFns.destroy(hand.tracker);
            hand.tracker = XR_NULL_HANDLE;
        }
        hand.wasActive = false;
        hand.hasAnchor = false;
        hand.stick = {};
        for (bool& pinchState : hand.pinchState) {
            pinchState = false;
        }
    }
    mAvailable = false;
    mSession = XR_NULL_HANDLE;
    mLocalSpace = XR_NULL_HANDLE;
    mViewSpace = XR_NULL_HANDLE;
}

bool HandTracker::locateJoints(int hand, XrTime time, XrHandJointLocationEXT* joints, bool& active) {
    active = false;
    XrHandJointLocateInfoEXT locateInfo{XR_TYPE_HAND_JOINT_LOCATE_INFO_EXT};
    locateInfo.baseSpace = mLocalSpace;
    locateInfo.time = time;

    XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
    locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
    locations.jointLocations = joints;
    if (XR_FAILED(mFns.locate(mHands[hand].tracker, &locateInfo, &locations))) {
        return false;
    }
    if (!locations.isActive) {
        return false;
    }
    // The palm and the thumb tip drive everything else; without them there is no input.
    if (!jointValid(joints[kPalm]) || !jointValid(joints[kThumbTip])) {
        return false;
    }
    active = true;
    return true;
}

void HandTracker::update(XrTime time) {
    if (!mAvailable || !mEnabled) {
        for (int hand = 0; hand < 2; ++hand) {
            mInput[hand] = HandInput{};
            mHands[hand].wasActive = false;
            mHands[hand].hasAnchor = false;
        }
        return;
    }

    // Head orientation for the stick basis. Falls back to facing -Z (local space)
    // when the head pose is unavailable, so the stick still works.
    XrVector3f headForward = {0.0f, 0.0f, -1.0f};
    XrVector3f headRight = {1.0f, 0.0f, 0.0f};
    XrSpaceLocation headLocation{XR_TYPE_SPACE_LOCATION};
    if (mViewSpace != XR_NULL_HANDLE &&
        XR_SUCCEEDED(xrLocateSpace(mViewSpace, mLocalSpace, time, &headLocation)) &&
        (headLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
        headForward = rotateVector(headLocation.pose.orientation, {0.0f, 0.0f, -1.0f});
        headForward.y = 0.0f;
        const float forwardLength = length(headForward);
        if (forwardLength > 0.01f) {
            headForward = scale(headForward, 1.0f / forwardLength);
            headRight = {-headForward.z, 0.0f, headForward.x};  // forward x up
        }
    }

    XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT];
    for (int hand = 0; hand < 2; ++hand) {
        HandState& state = mHands[hand];
        HandInput& out = mInput[hand];
        out = HandInput{};

        bool active = false;
        if (state.tracker == XR_NULL_HANDLE || !locateJoints(hand, time, joints, active) || !active) {
            // Hand left tracking: drop the anchor and release everything, so no
            // button can stick down.
            state.wasActive = false;
            state.hasAnchor = false;
            state.stick = {};
            for (bool& pinchState : state.pinchState) {
                pinchState = false;
            }
            continue;
        }

        const XrVector3f palm = joints[kPalm].pose.position;
        const XrVector3f thumbTip = joints[kThumbTip].pose.position;

        // Anchor the stick on the first tracked frame; it resets every time the
        // hand leaves tracking, so the stick never drifts away from the hand.
        if (!state.wasActive || !state.hasAnchor) {
            state.anchor = palm;
            state.hasAnchor = true;
            state.stick = {};
        }
        state.wasActive = true;

        // Stick: palm displacement from the anchor, head-relative, deadzoned.
        const XrVector3f displacement = sub(palm, state.anchor);
        XrVector2f target = {dot(displacement, headRight) / kStickRadius,
                             dot(displacement, headForward) / kStickRadius};
        const float magnitude = std::sqrt(target.x * target.x + target.y * target.y);
        if (magnitude < kStickDeadzone) {
            target = {0.0f, 0.0f};
        } else {
            const float scaled = std::min((magnitude - kStickDeadzone) / (1.0f - kStickDeadzone), 1.0f);
            target = {target.x / magnitude * scaled, target.y / magnitude * scaled};
        }
        state.stick.x += (target.x - state.stick.x) * kStickSmoothing;
        state.stick.y += (target.y - state.stick.y) * kStickSmoothing;

        // Pinches: thumb tip to each fingertip, with hysteresis.
        bool pinches[4] = {};
        for (int finger = 0; finger < 4; ++finger) {
            if (!jointValid(joints[kTipJoints[finger]])) {
                pinches[finger] = state.pinchState[finger];
                continue;
            }
            pinches[finger] = pinch(distance(thumbTip, joints[kTipJoints[finger]].pose.position),
                                   state.pinchState[finger]);
            state.pinchState[finger] = pinches[finger];
        }

        // Fist: all four fingertips curled to the palm.
        float tipDistance = 0.0f;
        int validTips = 0;
        for (int finger = 0; finger < 4; ++finger) {
            if (jointValid(joints[kTipJoints[finger]])) {
                tipDistance += distance(palm, joints[kTipJoints[finger]].pose.position);
                ++validTips;
            }
        }
        const bool fist = validTips == 4 && tipDistance / 4.0f < kFistDistance;

        out.active = true;
        out.stick = state.stick;
        out.indexPinch = pinches[0];
        out.middlePinch = pinches[1];
        out.ringPinch = pinches[2];
        out.littlePinch = pinches[3];
        out.fist = fist;
    }
}

}  // namespace questxr
