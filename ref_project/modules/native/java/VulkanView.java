package com.example.imgui;

import android.content.Context;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.InputDevice;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

public class VulkanView extends SurfaceView implements SurfaceHolder.Callback {
    private static final String TAG = "VulkanView";

    public static boolean interceptVolumeKeys = false;
    private boolean mSkipScreenshot = true;
    private boolean mSecure = true;

    public static byte[] fontData;

    private RenderThread mRenderThread;
    private volatile boolean mSurfaceReady = false;

    private static class RenderThread extends Thread {
        private final VulkanView view;
        private volatile boolean running = true;
        private volatile boolean paused  = false;
        private final Object pauseLock = new Object();

        RenderThread(VulkanView view) {
            super("VulkanRenderThread");
            this.view = view;
        }

        void requestStop() {
            running = false;
            synchronized (pauseLock) {
                paused = false;
                pauseLock.notifyAll();
            }
        }

        void onPause() {
            synchronized (pauseLock) {
                paused = true;
            }
        }

        void onResume() {
            synchronized (pauseLock) {
                paused = false;
                pauseLock.notifyAll();
            }
        }

        @Override
        public void run() {
            Log.i(TAG, "RenderThread started");
            while (running) {
                synchronized (pauseLock) {
                    while (paused && running) {
                        try { pauseLock.wait(); } catch (InterruptedException ignored) {}
                    }
                }
                if (!running) break;

                if (view.mSurfaceReady) {
                    step();
                }

                try { Thread.sleep(1); } catch (InterruptedException ignored) {}
            }
            Log.i(TAG, "RenderThread stopped");
        }
    }

    private static class ApplySecurityRunnable implements Runnable {
        private final VulkanView view;
        ApplySecurityRunnable(VulkanView view) { this.view = view; }
        @Override public void run() { view.applySurfaceSecurity(); }
    }

    public VulkanView(Context context) {
        super(context);

        setZOrderOnTop(true);
        getHolder().setFormat(android.graphics.PixelFormat.TRANSLUCENT);

        getHolder().addCallback(this);
        setFocusable(true);
        setFocusableInTouchMode(true);

        sInstance = this;
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        Log.i(TAG, "surfaceCreated");
        Surface surface = holder.getSurface();

        init(surface);
        mSurfaceReady = true;

        if (mRenderThread == null || !mRenderThread.isAlive()) {
            mRenderThread = new RenderThread(this);
            mRenderThread.start();
        } else {
      
            mRenderThread.onResume();
        }

        post(new ApplySecurityRunnable(this));
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.i(TAG, "surfaceChanged: " + width + "x" + height);

        resize(width, height);
        post(new ApplySecurityRunnable(this));
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.i(TAG, "surfaceDestroyed");

        mSurfaceReady = false;

        if (mRenderThread != null) {
            mRenderThread.requestStop();
            try { mRenderThread.join(3000); } catch (InterruptedException ignored) {}
            mRenderThread = null;
        }

        imgui_Shutdown();

        Log.i(TAG, "surfaceDestroyed complete");
    }

    public void onPause() {
        if (mRenderThread != null) mRenderThread.onPause();
    }

    public void onResume() {
        if (mRenderThread != null) mRenderThread.onResume();
    }

    @Override
    protected void onDetachedFromWindow() {
        Log.i(TAG, "onDetachedFromWindow");
        mSurfaceReady = false;

        if (mRenderThread != null) {
            mRenderThread.requestStop();
            try { mRenderThread.join(3000); } catch (InterruptedException ignored) {}
            mRenderThread = null;
        }

        imgui_Shutdown();

        super.onDetachedFromWindow();
    }

    public static void setInterceptVolumeKeys(boolean intercept) {
        interceptVolumeKeys = intercept;
    }

    public void applySurfaceSecurity() {
        if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.Q) return;
        try {
            android.view.SurfaceControl sc = getSurfaceControl();
            if (sc == null || !sc.isValid()) return;

            android.view.SurfaceControl.Transaction txn =
                    new android.view.SurfaceControl.Transaction();

            callTransactionMethod(txn, "setSkipScreenshot",
                    new Class[]{android.view.SurfaceControl.class, boolean.class},
                    sc, mSkipScreenshot);

            txn.apply();
            txn.close();
            Log.i(TAG, "Surface security applied: skip=" + mSkipScreenshot + " secure=" + mSecure);
        } catch (Exception e) {
            Log.w(TAG, "Failed to apply surface security", e);
        }
    }

    private void callTransactionMethod(android.view.SurfaceControl.Transaction txn,
                                       String name, Class<?>[] types, Object... args) {
        try {
            java.lang.reflect.Method m = txn.getClass().getMethod(name, types);
            m.invoke(txn, args);
        } catch (Exception e) {
            Log.d(TAG, name + " not available on this API level");
        }
    }

    public void setSkipScreenshot(boolean skip) {
        mSkipScreenshot = skip;
        post(new ApplySecurityRunnable(this));
    }

    public void setSecure(boolean secure) {
        mSecure = secure;
        post(new ApplySecurityRunnable(this));
    }

    private static VulkanView sInstance;

    public static void setSurfaceSecurity(boolean skipScreenshot, boolean secure) {
        if (sInstance != null) {
            sInstance.setSkipScreenshot(skipScreenshot);
            sInstance.setSecure(secure);
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        int source = event.getSource();
        int deviceId = event.getDeviceId();

        Log.d(TAG, "onKeyDown: keyCode=" + keyCode
                + " scanCode=" + event.getScanCode()
                + " source=0x" + Integer.toHexString(source)
                + " deviceId=" + deviceId
                + " device=" + (event.getDevice() != null ? event.getDevice().getName() : "unknown"));

        nativeOnKeyEvent(0, keyCode, event.getScanCode(),
                event.getMetaState(), deviceId, source);

        int unicodeChar = event.getUnicodeChar(event.getMetaState());
        if (unicodeChar > 0) nativeOnCharInput(unicodeChar);

        if (isSystemKey(keyCode)) return super.onKeyDown(keyCode, event);
        return true;
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        Log.d(TAG, "onKeyUp: keyCode=" + keyCode
                + " source=0x" + Integer.toHexString(event.getSource()));

        nativeOnKeyEvent(1, keyCode, event.getScanCode(),
                event.getMetaState(), event.getDeviceId(), event.getSource());

        if (isSystemKey(keyCode)) return super.onKeyUp(keyCode, event);
        return true;
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        int source = event.getSource();
        if ((source & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE
                || (source & InputDevice.SOURCE_MOUSE_RELATIVE) == InputDevice.SOURCE_MOUSE_RELATIVE) {
            int action = event.getActionMasked();
            float x = event.getX(), y = event.getY();
            int deviceId = event.getDeviceId();

            switch (action) {
                case MotionEvent.ACTION_HOVER_MOVE:
                    nativeOnMouseEvent(0, x, y, 0, 0, 0, deviceId, source);
                    return true;
                case MotionEvent.ACTION_SCROLL:
                    float scrollX = event.getAxisValue(MotionEvent.AXIS_HSCROLL);
                    float scrollY = event.getAxisValue(MotionEvent.AXIS_VSCROLL);
                    nativeOnMouseEvent(3, x, y, 0, scrollX, scrollY, deviceId, source);
                    return true;
            }
        }
        return super.onGenericMotionEvent(event);
    }

    private boolean isSystemKey(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_VOLUME_UP:
            case KeyEvent.KEYCODE_VOLUME_DOWN:
            case KeyEvent.KEYCODE_VOLUME_MUTE:
                return !interceptVolumeKeys;
            case KeyEvent.KEYCODE_POWER:
            case KeyEvent.KEYCODE_CAMERA:
            case KeyEvent.KEYCODE_CALL:
            case KeyEvent.KEYCODE_ENDCALL:
            case KeyEvent.KEYCODE_HEADSETHOOK:
                return true;
            default:
                return false;
        }
    }

    public static boolean isExternalDevice(int source) {
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M) {
            return (source & InputDevice.SOURCE_BLUETOOTH_STYLUS) != 0
                    || (source & InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD
                    || (source & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE
                    || (source & InputDevice.SOURCE_JOYSTICK) != 0
                    || (source & InputDevice.SOURCE_GAMEPAD) != 0;
        } else {
            return (source & InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD
                    || (source & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE
                    || (source & InputDevice.SOURCE_JOYSTICK) != 0
                    || (source & InputDevice.SOURCE_GAMEPAD) != 0;
        }
    }

    public static native boolean isImGuiComponentTouched(float x, float y);
    public static native void init(Object surface);
    public static native void resize(int width, int height);
    public static native void step();
    public static native void imgui_Shutdown();

    public static native void MotionEventClick(boolean down, float posX, float posY);
    public static native void nativeOnTouchEvent(int action, int pointerIndex,
            int[] pointerIds, float[] pointerXs, float[] pointerYs,
            float[] pointerPressures, int pointerCount);
    public static native void nativeOnKeyEvent(int action, int keyCode, int scanCode,
            int metaState, int deviceId, int source);
    public static native void nativeOnCharInput(int unicodeChar);
    public static native void nativeOnMouseEvent(int eventType, float x, float y,
            int button, float scrollX, float scrollY, int deviceId, int source);
}