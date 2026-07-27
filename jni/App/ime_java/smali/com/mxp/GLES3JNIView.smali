.class public Lcom/mxp/GLES3JNIView;
.super Landroid/opengl/GLSurfaceView;
.source "GLES3JNIView.java"

# interfaces
.implements Landroid/opengl/GLSurfaceView$Renderer;


# static fields
.field public static fontData:[B


# direct methods
.method public constructor <init>(Landroid/content/Context;)V
    .registers 9

    .line 14
    invoke-direct {p0, p1}, Landroid/opengl/GLSurfaceView;-><init>(Landroid/content/Context;)V

    .line 15
    const/16 v5, 0x10

    const/4 v6, 0x0

    const/16 v1, 0x8

    const/16 v2, 0x8

    const/16 v3, 0x8

    const/16 v4, 0x8

    move-object v0, p0

    invoke-virtual/range {v0 .. v6}, Lcom/mxp/GLES3JNIView;->setEGLConfigChooser(IIIIII)V

    .line 16
    invoke-virtual {p0}, Lcom/mxp/GLES3JNIView;->getHolder()Landroid/view/SurfaceHolder;

    move-result-object p1

    const/4 v0, 0x1

    invoke-interface {p1, v0}, Landroid/view/SurfaceHolder;->setFormat(I)V

    .line 17
    const/4 p1, 0x3

    invoke-virtual {p0, p1}, Lcom/mxp/GLES3JNIView;->setEGLContextClientVersion(I)V

    .line 18
    invoke-virtual {p0, p0}, Lcom/mxp/GLES3JNIView;->setRenderer(Landroid/opengl/GLSurfaceView$Renderer;)V

    .line 19
    invoke-virtual {p0, v0}, Lcom/mxp/GLES3JNIView;->setRenderMode(I)V

    .line 20
    return-void
.end method

.method public static native MotionEventClick(ZFF)V
.end method

.method public static native getWindowRect()Ljava/lang/String;
.end method

.method public static native imgui_Shutdown()V
.end method

.method public static native init()V
.end method

.method public static native nativeAddChar(I)V
.end method

.method public static native nativeKeyEvent(I)V
.end method

.method public static native resize(II)V
.end method

.method public static native step()V
.end method


# virtual methods
.method protected onDetachedFromWindow()V
    .registers 1

    .line 39
    invoke-super {p0}, Landroid/opengl/GLSurfaceView;->onDetachedFromWindow()V

    .line 40
    invoke-static {}, Lcom/mxp/GLES3JNIView;->imgui_Shutdown()V

    .line 41
    return-void
.end method

.method public onDrawFrame(Ljavax/microedition/khronos/opengles/GL10;)V
    .registers 2

    .line 24
    invoke-static {}, Lcom/mxp/GLES3JNIView;->step()V

    .line 25
    return-void
.end method

.method public onSurfaceChanged(Ljavax/microedition/khronos/opengles/GL10;II)V
    .registers 4

    .line 29
    invoke-static {p2, p3}, Lcom/mxp/GLES3JNIView;->resize(II)V

    .line 30
    return-void
.end method

.method public onSurfaceCreated(Ljavax/microedition/khronos/opengles/GL10;Ljavax/microedition/khronos/egl/EGLConfig;)V
    .registers 3

    .line 34
    invoke-static {}, Lcom/mxp/GLES3JNIView;->init()V

    .line 35
    return-void
.end method
