/*
 * Decompiled with CFR 0.152.
 * 
 * Could not load the following classes:
 *  android.content.Context
 *  android.opengl.GLSurfaceView
 *  android.opengl.GLSurfaceView$Renderer
 *  javax.microedition.khronos.egl.EGLConfig
 *  javax.microedition.khronos.opengles.GL10
 */
package com.mxp;

import android.content.Context;
import android.opengl.GLSurfaceView;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class GLES3JNIView
extends GLSurfaceView
implements GLSurfaceView.Renderer {
    public static byte[] fontData;

    public GLES3JNIView(Context context) {
        super(context);
        this.setEGLConfigChooser(8, 8, 8, 8, 16, 0);
        this.getHolder().setFormat(1);
        this.setEGLContextClientVersion(3);
        this.setRenderer(this);
        this.setRenderMode(1);
    }

    public static native void MotionEventClick(boolean var0, float var1, float var2);

    public static native String getWindowRect();

    public static native void imgui_Shutdown();

    public static native void init();

    public static native void nativeAddChar(int var0);

    public static native void nativeKeyEvent(int var0);

    public static native void resize(int var0, int var1);

    public static native void step();

    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        GLES3JNIView.imgui_Shutdown();
    }

    public void onDrawFrame(GL10 gL10) {
        GLES3JNIView.step();
    }

    public void onSurfaceChanged(GL10 gL10, int n, int n2) {
        GLES3JNIView.resize(n, n2);
    }

    public void onSurfaceCreated(GL10 gL10, EGLConfig eGLConfig) {
        GLES3JNIView.init();
    }
}

