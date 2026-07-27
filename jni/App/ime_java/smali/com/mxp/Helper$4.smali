.class Lcom/mxp/Helper$4;
.super Ljava/lang/Object;
.source "Helper.java"

# interfaces
.implements Ljava/lang/Runnable;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lcom/mxp/Helper;->IsVpnActive(Z)V
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x0
    name = null
.end annotation


# direct methods
.method constructor <init>()V
    .registers 1

    .line 297
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


# virtual methods
.method public run()V
    .registers 9

    .line 300
    invoke-static {}, Lcom/mxp/Helper;->-$$Nest$sfgetisVpnWatcherActive()Z

    move-result v0

    if-nez v0, :cond_7

    return-void

    .line 301
    :cond_7
    nop

    .line 302
    sget-object v0, Lcom/mxp/Helper;->activity:Landroid/app/Activity;

    .line 303
    const-string v1, "connectivity"

    invoke-virtual {v0, v1}, Landroid/app/Activity;->getSystemService(Ljava/lang/String;)Ljava/lang/Object;

    move-result-object v0

    check-cast v0, Landroid/net/ConnectivityManager;

    .line 305
    nop

    .line 306
    invoke-virtual {v0}, Landroid/net/ConnectivityManager;->getAllNetworks()[Landroid/net/Network;

    move-result-object v1

    .line 307
    array-length v2, v1

    const/4 v3, 0x0

    move v4, v3

    :goto_1a
    const/4 v5, 0x1

    if-ge v4, v2, :cond_32

    aget-object v6, v1, v4

    .line 308
    invoke-virtual {v0, v6}, Landroid/net/ConnectivityManager;->getNetworkCapabilities(Landroid/net/Network;)Landroid/net/NetworkCapabilities;

    move-result-object v6

    .line 309
    if-eqz v6, :cond_2f

    const/4 v7, 0x4

    invoke-virtual {v6, v7}, Landroid/net/NetworkCapabilities;->hasTransport(I)Z

    move-result v6

    if-eqz v6, :cond_2f

    .line 310
    nop

    .line 311
    move v0, v5

    goto :goto_33

    .line 307
    :cond_2f
    add-int/lit8 v4, v4, 0x1

    goto :goto_1a

    :cond_32
    move v0, v3

    .line 314
    :goto_33
    nop

    .line 319
    if-eqz v0, :cond_43

    invoke-static {}, Lcom/mxp/Helper;->-$$Nest$sfgetisCountingDown()Z

    move-result v1

    if-nez v1, :cond_43

    .line 320
    invoke-static {v5}, Lcom/mxp/Helper;->-$$Nest$sfputisCountingDown(Z)V

    .line 321
    invoke-static {}, Lcom/mxp/Helper;->-$$Nest$smstartCountdownAndExit()V

    goto :goto_48

    .line 322
    :cond_43
    if-nez v0, :cond_48

    .line 323
    invoke-static {v3}, Lcom/mxp/Helper;->-$$Nest$sfputisCountingDown(Z)V

    .line 326
    :cond_48
    :goto_48
    invoke-static {}, Lcom/mxp/Helper;->-$$Nest$sfgetvpnWatcherHandler()Landroid/os/Handler;

    move-result-object v0

    const-wide/16 v1, 0x3e8

    invoke-virtual {v0, p0, v1, v2}, Landroid/os/Handler;->postDelayed(Ljava/lang/Runnable;J)Z

    .line 327
    return-void
.end method
