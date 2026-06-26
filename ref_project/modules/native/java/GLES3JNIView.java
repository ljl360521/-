package com.example.imgui;

import android.content.Context;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.InputDevice;
import android.view.SurfaceHolder;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class GLES3JNIView extends GLSurfaceView implements GLSurfaceView.Renderer {
    private static final String TAG = "GLES3JNIView";
    public static boolean interceptVolumeKeys = false;
    private static boolean mSkipScreenshot = true;
    private static boolean mSecure = true;
    private static GLES3JNIView sInstance = null;


    public static byte[] fontData;

    // ========================================================================
    // 命名内部类 - 避免匿名类/lambda 导致 D8 崩溃
    // ========================================================================

    private static class SecuritySurfaceCallback implements SurfaceHolder.Callback {
        private final GLES3JNIView view;

        SecuritySurfaceCallback(GLES3JNIView view) {
            this.view = view;
        }

        @Override
        public void surfaceCreated(SurfaceHolder holder) {
            view.post(new ApplySecurityRunnable(view));
        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
            view.post(new ApplySecurityRunnable(view));
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
        }
    }

    private static class ApplySecurityRunnable implements Runnable {
        private final GLES3JNIView view;

        ApplySecurityRunnable(GLES3JNIView view) {
            this.view = view;
        }

        @Override
        public void run() {
            view.applySurfaceSecurity();
        }
    }

    // ========================================================================
    // 构造函数
    // ========================================================================

    public GLES3JNIView(Context context) {
        super(context);

        setEGLConfigChooser(8, 8, 8, 8, 16, 0);
        setEGLContextClientVersion(3);
        setRenderer(this);
        sInstance = this;

        setFocusable(true);
        setFocusableInTouchMode(true);

        // 监听 surface 生命周期 - 使用命名类
        getHolder().addCallback(new SecuritySurfaceCallback(this));
    }

    // ========================================================================
    // Renderer 回调
    // ========================================================================

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        GLES20.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        init(getHolder().getSurface());

        // Surface 创建后应用安全模式 - 使用命名 Runnable
        post(new ApplySecurityRunnable(this));
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        GLES20.glViewport(0, 0, width, height);
        resize(width, height);

        // 每次 surface 变化（包括从后台恢复）重新应用安全模式
        post(new ApplySecurityRunnable(this));
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT | GLES20.GL_DEPTH_BUFFER_BIT);
        step();
    }

    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        imgui_Shutdown();
    }

    public static void setInterceptVolumeKeys(boolean intercept) {
        interceptVolumeKeys = intercept;
    }

    // ========================================================================
    // Surface 安全模式
    // ========================================================================

    public void applySurfaceSecurity() {
        if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.Q) {
            return;
        }
        try {
            android.view.SurfaceControl sc = getSurfaceControl();
            if (sc == null || !sc.isValid()) return;

            android.view.SurfaceControl.Transaction transaction =
                    new android.view.SurfaceControl.Transaction();

            // setSkipScreenshot (API 34+)
            callTransactionMethod(transaction, "setSkipScreenshot",
                    new Class[]{android.view.SurfaceControl.class, boolean.class},
                    sc, mSkipScreenshot);
            transaction.apply();
            transaction.close();

            Log.i(TAG, "Surface security applied: skipScreenshot=" + mSkipScreenshot + " secure=" + mSecure);
        } catch (Exception e) {
            Log.w(TAG, "Failed to apply surface security", e);
        }
    }
    public static void setSurfaceSecurity(boolean skipScreenshot, boolean secure) {
        mSkipScreenshot = skipScreenshot;
        mSecure = secure;
        // 立即生效
        if (sInstance != null) {
            sInstance.post(new ApplySecurityRunnable(sInstance));
        }
    }

    private void callTransactionMethod(android.view.SurfaceControl.Transaction transaction,
                                       String methodName, Class<?>[] paramTypes, Object... args) {
        try {
            java.lang.reflect.Method method = transaction.getClass().getMethod(methodName, paramTypes);
            method.invoke(transaction, args);
        } catch (Exception e) {
            Log.d(TAG, methodName + " not available on this API level");
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

    // ========================================================================
    // 键盘事件处理 - 支持蓝牙键盘、OTG键盘、虚拟键盘
    // ========================================================================

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        int source = event.getSource();
        int deviceId = event.getDeviceId();

        Log.d(TAG, "onKeyDown: keyCode=" + keyCode
            + " scanCode=" + event.getScanCode()
            + " source=0x" + Integer.toHexString(source)
            + " deviceId=" + deviceId
            + " device=" + (event.getDevice() != null ? event.getDevice().getName() : "unknown"));

        nativeOnKeyEvent(
            0,
            keyCode,
            event.getScanCode(),
            event.getMetaState(),
            deviceId,
            source
        );

        int unicodeChar = event.getUnicodeChar(event.getMetaState());
        if (unicodeChar > 0) {
            nativeOnCharInput(unicodeChar);
        }

        if (isSystemKey(keyCode)) {
            return super.onKeyDown(keyCode, event);
        }

        return true;
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        int source = event.getSource();
        int deviceId = event.getDeviceId();

        Log.d(TAG, "onKeyUp: keyCode=" + keyCode
            + " source=0x" + Integer.toHexString(source));

        nativeOnKeyEvent(
            1,
            keyCode,
            event.getScanCode(),
            event.getMetaState(),
            deviceId,
            source
        );

        if (isSystemKey(keyCode)) {
            return super.onKeyUp(keyCode, event);
        }

        return true;
    }

    // ========================================================================
    // 鼠标事件处理 - 支持蓝牙鼠标、OTG鼠标
    // ========================================================================

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        int source = event.getSource();

        if ((source & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE
            || (source & InputDevice.SOURCE_MOUSE_RELATIVE) == InputDevice.SOURCE_MOUSE_RELATIVE) {

            int action = event.getActionMasked();
            float x = event.getX();
            float y = event.getY();
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

    // ========================================================================
    // 辅助方法
    // ========================================================================

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

    // ========================================================================
    // Native 方法声明
    // ========================================================================

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

    public static native void nativeOnMouseEvent(
        int eventType,
        float x,
        float y,
        int button,
        float scrollX,
        float scrollY,
        int deviceId,
        int source
    );
}