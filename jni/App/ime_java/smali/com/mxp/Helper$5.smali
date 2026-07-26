.class Lcom/mxp/Helper$5;
.super Ljava/lang/Object;
.source "Helper.java"

# interfaces
.implements Ljava/lang/Runnable;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lcom/mxp/Helper;->startCountdownAndExit()V
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x0
    name = null
.end annotation


# instance fields
.field final synthetic val$countdown:[I

.field final synthetic val$handler:Landroid/os/Handler;


# direct methods
.method constructor <init>([ILandroid/os/Handler;)V
    .registers 3
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "()V"
        }
    .end annotation

    .line 335
    iput-object p1, p0, Lcom/mxp/Helper$5;->val$countdown:[I

    iput-object p2, p0, Lcom/mxp/Helper$5;->val$handler:Landroid/os/Handler;

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


# virtual methods
.method public run()V
    .registers 6

    .line 338
    iget-object v0, p0, Lcom/mxp/Helper$5;->val$countdown:[I

    const/4 v1, 0x0

    aget v0, v0, v1

    if-lez v0, :cond_3d

    .line 339
    sget-object v0, Lcom/mxp/Helper;->activity:Landroid/app/Activity;

    iget-object v2, p0, Lcom/mxp/Helper$5;->val$countdown:[I

    aget v2, v2, v1

    new-instance v3, Ljava/lang/StringBuilder;

    invoke-direct {v3}, Ljava/lang/StringBuilder;-><init>()V

    const-string v4, "VPN detected! Please turn off VPN (App will close in "

    invoke-virtual {v3, v4}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v3

    invoke-virtual {v3, v2}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;

    move-result-object v2

    const-string v3, ")"

    invoke-virtual {v2, v3}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v2

    invoke-virtual {v2}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v2

    invoke-static {v0, v2, v1}, Landroid/widget/Toast;->makeText(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;

    move-result-object v0

    .line 341
    invoke-virtual {v0}, Landroid/widget/Toast;->show()V

    .line 342
    iget-object v0, p0, Lcom/mxp/Helper$5;->val$countdown:[I

    aget v2, v0, v1

    add-int/lit8 v2, v2, -0x1

    aput v2, v0, v1

    .line 343
    iget-object v0, p0, Lcom/mxp/Helper$5;->val$handler:Landroid/os/Handler;

    const-wide/16 v1, 0x3e8

    invoke-virtual {v0, p0, v1, v2}, Landroid/os/Handler;->postDelayed(Ljava/lang/Runnable;J)Z

    goto :goto_49

    .line 345
    :cond_3d
    sget-object v0, Lcom/mxp/Helper;->activity:Landroid/app/Activity;

    invoke-virtual {v0}, Landroid/app/Activity;->finishAffinity()V

    .line 346
    invoke-static {}, Landroid/os/Process;->myPid()I

    move-result v0

    invoke-static {v0}, Landroid/os/Process;->killProcess(I)V

    .line 348
    :goto_49
    return-void
.end method
