.class Lcom/mxp/Helper$2;
.super Ljava/lang/Object;
.source "Helper.java"

# interfaces
.implements Ljava/lang/Runnable;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lcom/mxp/Helper;->showSoftKeyboard(Z)V
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x0
    name = null
.end annotation


# direct methods
.method constructor <init>()V
    .registers 1

    .line 150
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


# virtual methods
.method public run()V
    .registers 4

    .line 153
    invoke-static {}, Lcom/mxp/Helper;->-$$Nest$sfgetkeyRepeatHandler()Landroid/os/Handler;

    move-result-object v0

    if-nez v0, :cond_7

    return-void

    .line 154
    :cond_7
    const/16 v0, 0x43

    invoke-static {v0}, Lcom/mxp/Helper;->nativeKeyEvent(I)V

    .line 155
    invoke-static {}, Lcom/mxp/Helper;->-$$Nest$sfgetkeyRepeatHandler()Landroid/os/Handler;

    move-result-object v0

    const-wide/16 v1, 0x32

    invoke-virtual {v0, p0, v1, v2}, Landroid/os/Handler;->postDelayed(Ljava/lang/Runnable;J)Z

    .line 156
    return-void
.end method
