.class Lcom/mxp/Helper$3;
.super Ljava/lang/Object;
.source "Helper.java"

# interfaces
.implements Ljava/lang/Runnable;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lcom/mxp/Helper;->hideScreen(Z)V
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x0
    name = null
.end annotation


# direct methods
.method constructor <init>()V
    .registers 1

    .line 239
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


# virtual methods
.method public run()V
    .registers 4

    .line 242
    invoke-static {}, Lcom/mxp/Helper;->-$$Nest$sfgetisSecureActive()Z

    move-result v0

    if-nez v0, :cond_7

    return-void

    .line 243
    :cond_7
    sget-object v0, Lcom/mxp/Helper;->activity:Landroid/app/Activity;

    invoke-virtual {v0}, Landroid/app/Activity;->getWindow()Landroid/view/Window;

    move-result-object v0

    invoke-virtual {v0}, Landroid/view/Window;->getAttributes()Landroid/view/WindowManager$LayoutParams;

    move-result-object v0

    .line 244
    iget v0, v0, Landroid/view/WindowManager$LayoutParams;->flags:I

    and-int/lit16 v0, v0, 0x2000

    if-nez v0, :cond_1b

    .line 245
    const/4 v0, 0x1

    invoke-static {v0}, Lcom/mxp/Helper;->hideScreen(Z)V

    .line 247
    :cond_1b
    invoke-static {}, Lcom/mxp/Helper;->-$$Nest$sfgetsecureWatcherHandler()Landroid/os/Handler;

    move-result-object v0

    const-wide/16 v1, 0x12c

    invoke-virtual {v0, p0, v1, v2}, Landroid/os/Handler;->postDelayed(Ljava/lang/Runnable;J)Z

    .line 248
    return-void
.end method
