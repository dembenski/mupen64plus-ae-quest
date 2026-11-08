package paulscode.android.mupen64plusae.game.xr;

import android.app.Activity;
import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.SurfaceTexture;
import android.os.Build;
import android.util.Log;
import android.view.Surface;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;

/**
 * Thin wrapper around the native OpenXR session (quest-xr module).
 * <p>
 * The session shows quad layers for the game, the VR menu and a 2D N64 controller, and a 3D N64
 * controller in the projection layer, optionally over passthrough.
 * Each quad gets a Surface backed by a SurfaceTexture; the native frame thread copies whatever
 * is drawn into it into an OpenXR swapchain.
 */
public class QuestXr
{
    private static final String TAG = "QuestXr";

    // Button bits, must match quest_xr.cpp
    public static final int BTN_A = 1;
    public static final int BTN_B = 1 << 1;
    public static final int BTN_X = 1 << 2;
    public static final int BTN_Y = 1 << 3;
    public static final int BTN_MENU = 1 << 4;
    public static final int BTN_LSTICK = 1 << 5;
    public static final int BTN_RSTICK = 1 << 6;

    // XrSessionState values
    public static final int STATE_IDLE = 1;
    public static final int STATE_READY = 2;
    public static final int STATE_SYNCHRONIZED = 3;
    public static final int STATE_VISIBLE = 4;
    public static final int STATE_FOCUSED = 5;
    public static final int STATE_STOPPING = 6;
    public static final int STATE_LOSS_PENDING = 7;
    public static final int STATE_EXITING = 8;

    // Quad ids, must match quest_xr.cpp
    public static final int QUAD_GAME = 0;
    public static final int QUAD_MENU = 1;
    public static final int QUAD_CONTROLLER = 2;
    public static final int QUAD_DOCK = 3;
    private static final int QUAD_COUNT = 4;

    // Quad attach modes, must match quest_xr.cpp
    /** Position and yaw in the tracking space */
    public static final int ATTACH_WORLD = 0;
    /** Position and yaw relative to the game quad */
    public static final int ATTACH_GAME = 1;
    /** Between the controllers, raised by the y offset, facing the head */
    public static final int ATTACH_HANDS = 2;

    /** Callbacks are invoked on the XR frame thread. */
    public interface Listener
    {
        void onXrInput(float leftX, float leftY, float rightX, float rightY,
                       float leftTrigger, float rightTrigger, float leftGrip, float rightGrip, int buttons);

        void onXrSessionState(int state);

        /** The game screen was grabbed and released at a new pose. */
        void onXrScreenMoved(float x, float y, float z, float yaw, float width);

        /** The virtual controller was grabbed and released at a new pose. */
        void onXrControllerMoved(float x, float y, float z, float yaw);

        /**
        * A controller is aiming at a pointable quad. u and v are 0..1 from its top left corner;
        * quad is -1 with u and v at -1 when nothing is hit.
        */
        void onXrPointer(int quad, float u, float v, boolean pressed);
    }

    private static boolean sLibraryLoaded = false;

    static
    {
        try {
            System.loadLibrary("questxr");
            sLibraryLoaded = true;
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to load native library", e);
        }
    }

    public static boolean isLibraryLoaded()
    {
        return sLibraryLoaded;
    }

    public static boolean isPassthroughSupported()
    {
        return sLibraryLoaded && nativeIsPassthroughSupported();
    }

    /** Fill in the given Surface with the quad's contents from Java drawing code. */
    public static void create(Activity activity, Listener listener,
                              int gameWidth, int gameHeight, int menuWidth, int menuHeight,
                              int controllerWidth, int controllerHeight, int dockWidth, int dockHeight)
    {
        if (sLibraryLoaded) {
            nativeCreate(activity, listener, gameWidth, gameHeight, menuWidth, menuHeight,
                    controllerWidth, controllerHeight, dockWidth, dockHeight);
        }
    }

    /** Set the game's frame rate: the runtime's display rate or half of it. */
    public static void setDisplayRefreshRate(float rate)
    {
        if (sLibraryLoaded) {
            nativeSetDisplayRefreshRate(rate);
        }
    }

    public static void start()
    {
        if (sLibraryLoaded) {
            nativeStart();
        }
    }

    public static void setDisplayRate(float rate)
    {
        if (sLibraryLoaded) {
            nativeSetDisplayRate(rate);
        }
    }

    public static void requestExit()
    {
        if (sLibraryLoaded) {
            nativeRequestExit();
        }
    }

    public static void setImmersiveGame(boolean immersive)
    {
        if (sLibraryLoaded) {
            nativeSetImmersiveGame(immersive);
        }
    }

    public static void setHandTrackingEnabled(boolean enabled)
    {
        if (sLibraryLoaded) {
            nativeSetHandTrackingEnabled(enabled);
        }
    }

    public static void setController3dVisible(boolean visible)
    {
        if (sLibraryLoaded) {
            nativeSetController3dVisible(visible);
        }
    }

    /** The 3D controller is the placed virtual controller, not the between-hands one. */
    public static void setControllerVirtual(boolean virtualController)
    {
        if (sLibraryLoaded) {
            nativeSetControllerVirtual(virtualController);
        }
    }

    /** Place the virtual controller at a world pose: position in meters, yaw in radians. */
    public static void setVirtualControllerPose(float x, float y, float z, float yaw)
    {
        if (sLibraryLoaded) {
            nativeSetVirtualControllerPose(x, y, z, yaw);
        }
    }

    /** The placed virtual controller's pose as [x, y, z, yaw]. */
    public static float[] getVirtualControllerPose()
    {
        if (sLibraryLoaded) {
            final float[] pose = nativeGetVirtualControllerPose();
            if (pose != null && pose.length == 4) {
                return pose;
            }
        }
        return new float[] {0.0f, -0.1f, -1.0f, 0.0f};
    }

    /** While enabled, holding a grip moves the placed virtual controller with that hand. */
    public static void setControllerGrabEnabled(boolean enabled)
    {
        if (sLibraryLoaded) {
            nativeSetControllerGrabEnabled(enabled);
        }
    }

    /**
     * While placing the controller, its finger-press path is off: pinches, not finger
     * zones, finish placement, so a thumb resting on the A button while gripping to
     * move it cannot finish placement by accident.
     */
    public static void setControllerPlacing(boolean placing)
    {
        if (sLibraryLoaded) {
            nativeSetControllerPlacing(placing);
        }
    }

    /** Move the game screen straight ahead of the current head direction; reported via onXrScreenMoved. */
    public static void recenterScreen(float distance, float width)
    {
        if (sLibraryLoaded) {
            nativeRecenterScreen(distance, width);
        }
    }

    public static void setPassthrough(boolean enabled)
    {
        if (sLibraryLoaded) {
            nativeSetPassthrough(enabled);
        }
    }

    public static void setGameQuad(float x, float y, float z, float yaw, float width)
    {
        if (sLibraryLoaded) {
            nativeSetGameQuad(x, y, z, yaw, width);
        }
    }

    public static void setMenuQuad(float x, float y, float z, float yaw, float width)
    {
        if (sLibraryLoaded) {
            nativeSetMenuQuad(x, y, z, yaw, width);
        }
    }

    public static void setControllerQuad(float x, float y, float z, float yaw, float width)
    {
        if (sLibraryLoaded) {
            nativeSetControllerQuad(x, y, z, yaw, width);
        }
    }

    public static void setDockQuad(float x, float y, float z, float yaw, float width)
    {
        if (sLibraryLoaded) {
            nativeSetDockQuad(x, y, z, yaw, width);
        }
    }

    public static void setPointerQuad(int quad, int attachMode, float x, float y, float z, float yaw,
                                      float width, float height)
    {
        if (sLibraryLoaded) {
            nativeSetPointerQuad(quad, attachMode, x, y, z, yaw, width, height);
        }
    }

    /** The game quad, the one everything else is placed relative to. */
    public static void setN64State(int buttons, float axisX, float axisY)
    {
        if (sLibraryLoaded) {
            nativeSetN64State(buttons, axisX, axisY);
        }
    }

    public static void setGrabEnabled(boolean enabled)
    {
        if (sLibraryLoaded) {
            nativeSetGrabEnabled(enabled);
        }
    }

    public static void setStickResizeEnabled(boolean enabled)
    {
        if (sLibraryLoaded) {
            nativeSetStickResizeEnabled(enabled);
        }
    }

    public static void loadControllerModel(AssetManager assets)
    {
        if (!sLibraryLoaded) {
            return;
        }
        try (InputStream stream = assets.open("quest/n64_controller.bin")) {
            ByteArrayOutputStream buffer = new ByteArrayOutputStream();
            byte[] chunk = new byte[4096];
            int read;
            while ((read = stream.read(chunk)) != -1) {
                buffer.write(chunk, 0, read);
            }
            final Bitmap texture = loadControllerTexture(assets);
            if (texture == null) {
                Log.e(TAG, "Missing controller texture");
                return;
            }
            if (!nativeSetControllerModel(ByteBuffer.wrap(buffer.toByteArray()), texture)) {
                Log.e(TAG, "Failed to load controller model");
            }
        } catch (IOException e) {
            Log.e(TAG, "Failed to load controller model", e);
        }
    }

    private static Bitmap loadControllerTexture(AssetManager assets)
    {
        try (InputStream stream = assets.open("quest/n64_controller.png")) {
            return BitmapFactory.decodeStream(stream);
        } catch (IOException e) {
            Log.e(TAG, "Failed to load controller texture", e);
            return null;
        }
    }

    private static native boolean nativeIsPassthroughSupported();

    private static native boolean nativeCreate(Activity activity, Listener listener, int gameWidth, int gameHeight,
                                               int menuWidth, int menuHeight, int controllerWidth,
                                               int controllerHeight, int dockWidth, int dockHeight);

    private static native void nativeSetDisplayRefreshRate(float rate);

    private static native void nativeStart();

    private static native void nativeSetDisplayRate(float rate);

    private static native void nativeRequestExit();

    private static native void nativeSetImmersiveGame(boolean immersive);

    private static native void nativeSetHandTrackingEnabled(boolean enabled);

    private static native void nativeSetControllerModel(ByteBuffer mesh, Bitmap texture);

    private static native void nativeSetN64State(int buttons, float axisX, float axisY);

    private static native void nativeSetController3dVisible(boolean visible);
    private static native void nativeSetControllerVirtual(boolean virtualController);
    private static native void nativeSetVirtualControllerPose(float x, float y, float z, float yaw);
    private static native float[] nativeGetVirtualControllerPose();
    private static native void nativeSetControllerGrabEnabled(boolean enabled);
    private static native void nativeSetControllerPlacing(boolean placing);
    private static native void nativeRecenterScreen(float distance, float width);

    private static native boolean nativeSetPassthrough(boolean enabled);

    private static native void nativeSetGameQuad(float x, float y, float z, float yaw, float width);

    private static native void nativeSetMenuQuad(float x, float y, float z, float yaw, float width);

    private static native void nativeSetControllerQuad(float x, float y, float z, float yaw, float width);

    private static native void nativeSetDockQuad(float x, float y, float z, float yaw, float width);

    private static native void nativeSetPointerQuad(int quad, int attachMode, float x, float y, float z, float yaw,
                                                    float width, float height);

    private static native void nativeSetGrabEnabled(boolean enabled);

    private static native void nativeSetStickResizeEnabled(boolean enabled);
}
