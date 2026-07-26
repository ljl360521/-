.class public Lcom/mxp/Helper;
.super Ljava/lang/Object;
.source "Helper.java"


# static fields
.field private static final KB_SENTINEL:Ljava/lang/String; = "        "

.field private static final KEY_REPEAT_INITIAL_DELAY:I = 0x190

.field private static final KEY_REPEAT_INTERVAL:I = 0x32

.field public static activity:Landroid/app/Activity;

.field private static isCountingDown:Z

.field private static isSecureActive:Z

.field private static isVpnWatcherActive:Z

.field private static keyRepeatHandler:Landroid/os/Handler;

.field private static keyRepeatRunnable:Ljava/lang/Runnable;

.field private static keyboardActive:Z

.field private static keyboardEditText:Landroid/widget/EditText;

.field private static keyboardImm:Landroid/view/inputmethod/InputMethodManager;

.field private static keyboardWatcher:Landroid/text/TextWatcher;

.field private static secureWatcherHandler:Landroid/os/Handler;

.field private static secureWatcherRunnable:Ljava/lang/Runnable;

.field private static vpnWatcherHandler:Landroid/os/Handler;

.field private static vpnWatcherRunnable:Ljava/lang/Runnable;


# direct methods
.method static bridge synthetic -$$Nest$sfgetisCountingDown()Z
    .registers 1

    sget-boolean v0, Lcom/mxp/Helper;->isCountingDown:Z

    return v0
.end method

.method static bridge synthetic -$$Nest$sfgetisSecureActive()Z
    .registers 1

    sget-boolean v0, Lcom/mxp/Helper;->isSecureActive:Z

    return v0
.end method

.method static bridge synthetic -$$Nest$sfgetisVpnWatcherActive()Z
    .registers 1

    sget-boolean v0, Lcom/mxp/Helper;->isVpnWatcherActive:Z

    return v0
.end method

.method static bridge synthetic -$$Nest$sfgetkeyRepeatHandler()Landroid/os/Handler;
    .registers 1

    sget-object v0, Lcom/mxp/Helper;->keyRepeatHandler:Landroid/os/Handler;

    return-object v0
.end method

.method static bridge synthetic -$$Nest$sfgetkeyboardEditText()Landroid/widget/EditText;
    .registers 1

    sget-object v0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    return-object v0
.end method

.method static bridge synthetic -$$Nest$sfgetsecureWatcherHandler()Landroid/os/Handler;
    .registers 1

    sget-object v0, Lcom/mxp/Helper;->secureWatcherHandler:Landroid/os/Handler;

    return-object v0
.end method

.method static bridge synthetic -$$Nest$sfgetvpnWatcherHandler()Landroid/os/Handler;
    .registers 1

    sget-object v0, Lcom/mxp/Helper;->vpnWatcherHandler:Landroid/os/Handler;

    return-object v0
.end method

.method static bridge synthetic -$$Nest$sfputisCountingDown(Z)V
    .registers 1

    sput-boolean p0, Lcom/mxp/Helper;->isCountingDown:Z

    return-void
.end method

.method static bridge synthetic -$$Nest$smstartCountdownAndExit()V
    .registers 0

    invoke-static {}, Lcom/mxp/Helper;->startCountdownAndExit()V

    return-void
.end method

.method static constructor <clinit>()V
    .registers 1

    .line 34
    const/4 v0, 0x0

    sput-boolean v0, Lcom/mxp/Helper;->isSecureActive:Z

    .line 38
    sput-boolean v0, Lcom/mxp/Helper;->isVpnWatcherActive:Z

    .line 39
    sput-boolean v0, Lcom/mxp/Helper;->isCountingDown:Z

    .line 44
    sput-boolean v0, Lcom/mxp/Helper;->keyboardActive:Z

    .line 46
    const/4 v0, 0x0

    sput-object v0, Lcom/mxp/Helper;->keyRepeatHandler:Landroid/os/Handler;

    .line 47
    sput-object v0, Lcom/mxp/Helper;->keyRepeatRunnable:Ljava/lang/Runnable;

    return-void
.end method

.method public constructor <init>()V
    .registers 1

    .line 29
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method

.method public static IsVpnActive(Z)V
    .registers 2

    .line 284
    if-nez p0, :cond_1c

    .line 285
    const/4 p0, 0x0

    sput-boolean p0, Lcom/mxp/Helper;->isVpnWatcherActive:Z

    .line 286
    sput-boolean p0, Lcom/mxp/Helper;->isCountingDown:Z

    .line 287
    sget-object p0, Lcom/mxp/Helper;->vpnWatcherHandler:Landroid/os/Handler;

    if-eqz p0, :cond_1b

    sget-object p0, Lcom/mxp/Helper;->vpnWatcherRunnable:Ljava/lang/Runnable;

    if-eqz p0, :cond_1b

    .line 288
    sget-object p0, Lcom/mxp/Helper;->vpnWatcherHandler:Landroid/os/Handler;

    sget-object v0, Lcom/mxp/Helper;->vpnWatcherRunnable:Ljava/lang/Runnable;

    invoke-virtual {p0, v0}, Landroid/os/Handler;->removeCallbacks(Ljava/lang/Runnable;)V

    .line 289
    const/4 p0, 0x0

    sput-object p0, Lcom/mxp/Helper;->vpnWatcherHandler:Landroid/os/Handler;

    .line 290
    sput-object p0, Lcom/mxp/Helper;->vpnWatcherRunnable:Ljava/lang/Runnable;

    .line 292
    :cond_1b
    return-void

    .line 295
    :cond_1c
    const/4 p0, 0x1

    sput-boolean p0, Lcom/mxp/Helper;->isVpnWatcherActive:Z

    .line 296
    new-instance p0, Landroid/os/Handler;

    invoke-static {}, Landroid/os/Looper;->getMainLooper()Landroid/os/Looper;

    move-result-object v0

    invoke-direct {p0, v0}, Landroid/os/Handler;-><init>(Landroid/os/Looper;)V

    sput-object p0, Lcom/mxp/Helper;->vpnWatcherHandler:Landroid/os/Handler;

    .line 297
    new-instance p0, Lcom/mxp/Helper$4;

    invoke-direct {p0}, Lcom/mxp/Helper$4;-><init>()V

    sput-object p0, Lcom/mxp/Helper;->vpnWatcherRunnable:Ljava/lang/Runnable;

    .line 329
    sget-object p0, Lcom/mxp/Helper;->vpnWatcherHandler:Landroid/os/Handler;

    sget-object v0, Lcom/mxp/Helper;->vpnWatcherRunnable:Ljava/lang/Runnable;

    invoke-virtual {p0, v0}, Landroid/os/Handler;->post(Ljava/lang/Runnable;)Z

    .line 330
    return-void
.end method

.method public static hideScreen(Z)V
    .registers 3

    .line 199
    new-instance v0, Landroid/os/Handler;

    invoke-static {}, Landroid/os/Looper;->getMainLooper()Landroid/os/Looper;

    move-result-object v1

    invoke-direct {v0, v1}, Landroid/os/Handler;-><init>(Landroid/os/Looper;)V

    new-instance v1, Lcom/mxp/Helper$$ExternalSyntheticLambda1;

    invoke-direct {v1, p0}, Lcom/mxp/Helper$$ExternalSyntheticLambda1;-><init>(Z)V

    invoke-virtual {v0, v1}, Landroid/os/Handler;->post(Ljava/lang/Runnable;)Z

    .line 281
    return-void
.end method

.method public static init(Landroid/app/Activity;)V
    .registers 1

    .line 52
    sput-object p0, Lcom/mxp/Helper;->activity:Landroid/app/Activity;

    .line 53
    return-void
.end method

.method static synthetic lambda$hideScreen$4(Z)V
    .registers 10

    .line 201
    const/16 v0, 0x2000

    :try_start_2
    sput-boolean p0, Lcom/mxp/Helper;->isSecureActive:Z

    .line 202
    sget-object v1, Lcom/mxp/Helper;->activity:Landroid/app/Activity;

    invoke-virtual {v1}, Landroid/app/Activity;->getWindow()Landroid/view/Window;

    move-result-object v1

    .line 203
    invoke-virtual {v1}, Landroid/view/Window;->getDecorView()Landroid/view/View;

    move-result-object v2

    .line 205
    const/4 v3, 0x0

    if-eqz p0, :cond_c5

    .line 206
    invoke-virtual {v1, v0}, Landroid/view/Window;->addFlags(I)V

    .line 207
    invoke-virtual {v1}, Landroid/view/Window;->getAttributes()Landroid/view/WindowManager$LayoutParams;

    move-result-object v4

    .line 208
    iget v5, v4, Landroid/view/WindowManager$LayoutParams;->flags:I

    or-int/2addr v5, v0

    iput v5, v4, Landroid/view/WindowManager$LayoutParams;->flags:I

    .line 209
    invoke-virtual {v1, v4}, Landroid/view/Window;->setAttributes(Landroid/view/WindowManager$LayoutParams;)V

    .line 211
    sget v5, Landroid/os/Build$VERSION;->SDK_INT:I
    :try_end_22
    .catch Ljava/lang/Exception; {:try_start_2 .. :try_end_22} :catch_11e

    const/16 v6, 0x21

    const/4 v7, 0x1

    if-lt v5, v6, :cond_4b

    .line 213
    :try_start_27
    const-class v5, Landroid/view/WindowManager$LayoutParams;

    const-string v6, "privateFlags"

    invoke-virtual {v5, v6}, Ljava/lang/Class;->getDeclaredField(Ljava/lang/String;)Ljava/lang/reflect/Field;

    move-result-object v5

    .line 214
    invoke-virtual {v5, v7}, Ljava/lang/reflect/Field;->setAccessible(Z)V

    .line 215
    invoke-virtual {v5, v4}, Ljava/lang/reflect/Field;->get(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v6

    check-cast v6, Ljava/lang/Integer;

    invoke-virtual {v6}, Ljava/lang/Integer;->intValue()I

    move-result v6

    or-int/lit8 v6, v6, 0x4

    invoke-static {v6}, Ljava/lang/Integer;->valueOf(I)Ljava/lang/Integer;

    move-result-object v6

    invoke-virtual {v5, v4, v6}, Ljava/lang/reflect/Field;->set(Ljava/lang/Object;Ljava/lang/Object;)V

    .line 216
    invoke-virtual {v1, v4}, Landroid/view/Window;->setAttributes(Landroid/view/WindowManager$LayoutParams;)V
    :try_end_48
    .catch Ljava/lang/Exception; {:try_start_27 .. :try_end_48} :catch_49

    goto :goto_4a

    .line 217
    :catch_49
    move-exception v1

    :goto_4a
    nop

    .line 220
    :cond_4b
    :try_start_4b
    new-instance v1, Ljava/util/Stack;

    invoke-direct {v1}, Ljava/util/Stack;-><init>()V

    .line 221
    invoke-virtual {v1, v2}, Ljava/util/Stack;->push(Ljava/lang/Object;)Ljava/lang/Object;

    .line 222
    :goto_53
    invoke-virtual {v1}, Ljava/util/Stack;->isEmpty()Z

    move-result v5

    if-nez v5, :cond_81

    .line 223
    invoke-virtual {v1}, Ljava/util/Stack;->pop()Ljava/lang/Object;

    move-result-object v5

    check-cast v5, Landroid/view/View;

    .line 224
    instance-of v6, v5, Landroid/view/SurfaceView;

    if-eqz v6, :cond_69

    move-object v6, v5

    check-cast v6, Landroid/view/SurfaceView;

    invoke-virtual {v6, v7}, Landroid/view/SurfaceView;->setSecure(Z)V

    .line 225
    :cond_69
    instance-of v6, v5, Landroid/view/ViewGroup;

    if-eqz v6, :cond_80

    .line 226
    check-cast v5, Landroid/view/ViewGroup;

    .line 227
    move v6, v3

    :goto_70
    invoke-virtual {v5}, Landroid/view/ViewGroup;->getChildCount()I

    move-result v8

    if-ge v6, v8, :cond_80

    invoke-virtual {v5, v6}, Landroid/view/ViewGroup;->getChildAt(I)Landroid/view/View;

    move-result-object v8

    invoke-virtual {v1, v8}, Ljava/util/Stack;->push(Ljava/lang/Object;)Ljava/lang/Object;
    :try_end_7d
    .catch Ljava/lang/Exception; {:try_start_4b .. :try_end_7d} :catch_11e

    add-int/lit8 v6, v6, 0x1

    goto :goto_70

    .line 229
    :cond_80
    goto :goto_53

    .line 232
    :cond_81
    :try_start_81
    sget-object v1, Lcom/mxp/Helper;->activity:Landroid/app/Activity;

    const-string v3, "window"

    invoke-virtual {v1, v3}, Landroid/app/Activity;->getSystemService(Ljava/lang/String;)Ljava/lang/Object;

    move-result-object v1

    .line 233
    invoke-virtual {v1}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v3

    const-string v5, "updateViewLayout"

    const-class v6, Landroid/view/View;

    const-class v7, Landroid/view/ViewGroup$LayoutParams;

    filled-new-array {v6, v7}, [Ljava/lang/Class;

    move-result-object v6

    invoke-virtual {v3, v5, v6}, Ljava/lang/Class;->getMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;

    move-result-object v3

    .line 234
    filled-new-array {v2, v4}, [Ljava/lang/Object;

    move-result-object v2

    invoke-virtual {v3, v1, v2}, Ljava/lang/reflect/Method;->invoke(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;
    :try_end_a2
    .catch Ljava/lang/Exception; {:try_start_81 .. :try_end_a2} :catch_a3

    goto :goto_a4

    .line 235
    :catch_a3
    move-exception v1

    :goto_a4
    nop

    .line 237
    :try_start_a5
    sget-object v1, Lcom/mxp/Helper;->secureWatcherHandler:Landroid/os/Handler;

    if-nez v1, :cond_c4

    .line 238
    new-instance v1, Landroid/os/Handler;

    invoke-static {}, Landroid/os/Looper;->getMainLooper()Landroid/os/Looper;

    move-result-object v2

    invoke-direct {v1, v2}, Landroid/os/Handler;-><init>(Landroid/os/Looper;)V

    sput-object v1, Lcom/mxp/Helper;->secureWatcherHandler:Landroid/os/Handler;

    .line 239
    new-instance v1, Lcom/mxp/Helper$3;

    invoke-direct {v1}, Lcom/mxp/Helper$3;-><init>()V

    sput-object v1, Lcom/mxp/Helper;->secureWatcherRunnable:Ljava/lang/Runnable;

    .line 250
    sget-object v1, Lcom/mxp/Helper;->secureWatcherHandler:Landroid/os/Handler;

    sget-object v2, Lcom/mxp/Helper;->secureWatcherRunnable:Ljava/lang/Runnable;

    const-wide/16 v3, 0x12c

    invoke-virtual {v1, v2, v3, v4}, Landroid/os/Handler;->postDelayed(Ljava/lang/Runnable;J)Z

    .line 253
    :cond_c4
    goto :goto_11d

    .line 254
    :cond_c5
    sput-boolean v3, Lcom/mxp/Helper;->isSecureActive:Z

    .line 255
    sget-object v4, Lcom/mxp/Helper;->secureWatcherHandler:Landroid/os/Handler;

    if-eqz v4, :cond_d7

    .line 256
    sget-object v4, Lcom/mxp/Helper;->secureWatcherHandler:Landroid/os/Handler;

    sget-object v5, Lcom/mxp/Helper;->secureWatcherRunnable:Ljava/lang/Runnable;

    invoke-virtual {v4, v5}, Landroid/os/Handler;->removeCallbacks(Ljava/lang/Runnable;)V

    .line 257
    const/4 v4, 0x0

    sput-object v4, Lcom/mxp/Helper;->secureWatcherHandler:Landroid/os/Handler;

    .line 258
    sput-object v4, Lcom/mxp/Helper;->secureWatcherRunnable:Ljava/lang/Runnable;

    .line 260
    :cond_d7
    invoke-virtual {v1, v0}, Landroid/view/Window;->clearFlags(I)V

    .line 261
    invoke-virtual {v1}, Landroid/view/Window;->getAttributes()Landroid/view/WindowManager$LayoutParams;

    move-result-object v4

    .line 262
    iget v5, v4, Landroid/view/WindowManager$LayoutParams;->flags:I

    and-int/lit16 v5, v5, -0x2001

    iput v5, v4, Landroid/view/WindowManager$LayoutParams;->flags:I

    .line 263
    invoke-virtual {v1, v4}, Landroid/view/Window;->setAttributes(Landroid/view/WindowManager$LayoutParams;)V

    .line 265
    new-instance v1, Ljava/util/Stack;

    invoke-direct {v1}, Ljava/util/Stack;-><init>()V

    .line 266
    invoke-virtual {v1, v2}, Ljava/util/Stack;->push(Ljava/lang/Object;)Ljava/lang/Object;

    .line 267
    :goto_ef
    invoke-virtual {v1}, Ljava/util/Stack;->isEmpty()Z

    move-result v2

    if-nez v2, :cond_11d

    .line 268
    invoke-virtual {v1}, Ljava/util/Stack;->pop()Ljava/lang/Object;

    move-result-object v2

    check-cast v2, Landroid/view/View;

    .line 269
    instance-of v4, v2, Landroid/view/SurfaceView;

    if-eqz v4, :cond_105

    move-object v4, v2

    check-cast v4, Landroid/view/SurfaceView;

    invoke-virtual {v4, v3}, Landroid/view/SurfaceView;->setSecure(Z)V

    .line 270
    :cond_105
    instance-of v4, v2, Landroid/view/ViewGroup;

    if-eqz v4, :cond_11c

    .line 271
    check-cast v2, Landroid/view/ViewGroup;

    .line 272
    move v4, v3

    :goto_10c
    invoke-virtual {v2}, Landroid/view/ViewGroup;->getChildCount()I

    move-result v5

    if-ge v4, v5, :cond_11c

    invoke-virtual {v2, v4}, Landroid/view/ViewGroup;->getChildAt(I)Landroid/view/View;

    move-result-object v5

    invoke-virtual {v1, v5}, Ljava/util/Stack;->push(Ljava/lang/Object;)Ljava/lang/Object;
    :try_end_119
    .catch Ljava/lang/Exception; {:try_start_a5 .. :try_end_119} :catch_11e

    add-int/lit8 v4, v4, 0x1

    goto :goto_10c

    .line 274
    :cond_11c
    goto :goto_ef

    .line 279
    :cond_11d
    :goto_11d
    goto :goto_134

    .line 276
    :catch_11e
    move-exception v1

    .line 277
    if-eqz p0, :cond_12b

    sget-object p0, Lcom/mxp/Helper;->activity:Landroid/app/Activity;

    invoke-virtual {p0}, Landroid/app/Activity;->getWindow()Landroid/view/Window;

    move-result-object p0

    invoke-virtual {p0, v0}, Landroid/view/Window;->addFlags(I)V

    goto :goto_134

    .line 278
    :cond_12b
    sget-object p0, Lcom/mxp/Helper;->activity:Landroid/app/Activity;

    invoke-virtual {p0}, Landroid/app/Activity;->getWindow()Landroid/view/Window;

    move-result-object p0

    invoke-virtual {p0, v0}, Landroid/view/Window;->clearFlags(I)V

    .line 280
    :goto_134
    return-void
.end method

.method static synthetic lambda$showSoftKeyboard$1(Landroid/widget/TextView;ILandroid/view/KeyEvent;)Z
    .registers 3

    .line 140
    const/16 p0, 0x42

    invoke-static {p0}, Lcom/mxp/Helper;->nativeKeyEvent(I)V

    .line 141
    const/4 p0, 0x1

    return p0
.end method

.method static synthetic lambda$showSoftKeyboard$2(Landroid/view/View;ILandroid/view/KeyEvent;)Z
    .registers 6

    .line 145
    const/16 p0, 0x43

    if-ne p1, p0, :cond_45

    .line 146
    invoke-virtual {p2}, Landroid/view/KeyEvent;->getAction()I

    move-result p1

    const/4 v0, 0x1

    if-nez p1, :cond_2e

    .line 147
    invoke-static {p0}, Lcom/mxp/Helper;->nativeKeyEvent(I)V

    .line 148
    sget-object p0, Lcom/mxp/Helper;->keyRepeatHandler:Landroid/os/Handler;

    if-nez p0, :cond_2d

    .line 149
    new-instance p0, Landroid/os/Handler;

    invoke-static {}, Landroid/os/Looper;->getMainLooper()Landroid/os/Looper;

    move-result-object p1

    invoke-direct {p0, p1}, Landroid/os/Handler;-><init>(Landroid/os/Looper;)V

    sput-object p0, Lcom/mxp/Helper;->keyRepeatHandler:Landroid/os/Handler;

    .line 150
    new-instance p0, Lcom/mxp/Helper$2;

    invoke-direct {p0}, Lcom/mxp/Helper$2;-><init>()V

    sput-object p0, Lcom/mxp/Helper;->keyRepeatRunnable:Ljava/lang/Runnable;

    .line 158
    sget-object p0, Lcom/mxp/Helper;->keyRepeatHandler:Landroid/os/Handler;

    sget-object p1, Lcom/mxp/Helper;->keyRepeatRunnable:Ljava/lang/Runnable;

    const-wide/16 v1, 0x190

    invoke-virtual {p0, p1, v1, v2}, Landroid/os/Handler;->postDelayed(Ljava/lang/Runnable;J)Z

    .line 160
    :cond_2d
    return v0

    .line 161
    :cond_2e
    invoke-virtual {p2}, Landroid/view/KeyEvent;->getAction()I

    move-result p0

    if-ne p0, v0, :cond_45

    .line 162
    sget-object p0, Lcom/mxp/Helper;->keyRepeatHandler:Landroid/os/Handler;

    if-eqz p0, :cond_44

    .line 163
    sget-object p0, Lcom/mxp/Helper;->keyRepeatHandler:Landroid/os/Handler;

    sget-object p1, Lcom/mxp/Helper;->keyRepeatRunnable:Ljava/lang/Runnable;

    invoke-virtual {p0, p1}, Landroid/os/Handler;->removeCallbacks(Ljava/lang/Runnable;)V

    .line 164
    const/4 p0, 0x0

    sput-object p0, Lcom/mxp/Helper;->keyRepeatHandler:Landroid/os/Handler;

    .line 165
    sput-object p0, Lcom/mxp/Helper;->keyRepeatRunnable:Ljava/lang/Runnable;

    .line 167
    :cond_44
    return v0

    .line 170
    :cond_45
    const/4 p0, 0x0

    return p0
.end method

.method static synthetic lambda$showSoftKeyboard$3(Z)V
    .registers 5

    .line 62
    sget-object v0, Lcom/mxp/Helper;->activity:Landroid/app/Activity;

    if-nez v0, :cond_5

    return-void

    .line 63
    :cond_5
    const/4 v0, 0x0

    if-eqz p0, :cond_a9

    .line 64
    sget-object p0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    const/4 v1, 0x1

    if-nez p0, :cond_77

    .line 65
    sget-object p0, Lcom/mxp/Helper;->activity:Landroid/app/Activity;

    const-string v2, "input_method"

    invoke-virtual {p0, v2}, Landroid/app/Activity;->getSystemService(Ljava/lang/String;)Ljava/lang/Object;

    move-result-object p0

    check-cast p0, Landroid/view/inputmethod/InputMethodManager;

    sput-object p0, Lcom/mxp/Helper;->keyboardImm:Landroid/view/inputmethod/InputMethodManager;

    .line 66
    new-instance p0, Landroid/widget/EditText;

    sget-object v2, Lcom/mxp/Helper;->activity:Landroid/app/Activity;

    invoke-direct {p0, v2}, Landroid/widget/EditText;-><init>(Landroid/content/Context;)V

    sput-object p0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    .line 67
    sget-object p0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    const/4 v2, 0x0

    invoke-virtual {p0, v2}, Landroid/widget/EditText;->setAlpha(F)V

    .line 68
    sget-object p0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    const v2, 0x80001

    invoke-virtual {p0, v2}, Landroid/widget/EditText;->setInputType(I)V

    .line 72
    sget-object p0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    const v2, 0x12000001

    invoke-virtual {p0, v2}, Landroid/widget/EditText;->setImeOptions(I)V

    .line 77
    sget-object p0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    const-string v2, "nm"

    invoke-virtual {p0, v2}, Landroid/widget/EditText;->setPrivateImeOptions(Ljava/lang/String;)V

    .line 78
    sget-object p0, Lcom/mxp/Helper;->activity:Landroid/app/Activity;

    invoke-virtual {p0}, Landroid/app/Activity;->getWindow()Landroid/view/Window;

    move-result-object p0

    invoke-virtual {p0}, Landroid/view/Window;->getDecorView()Landroid/view/View;

    move-result-object p0

    check-cast p0, Landroid/view/ViewGroup;

    .line 79
    sget-object v2, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    new-instance v3, Landroid/view/ViewGroup$LayoutParams;

    invoke-direct {v3, v1, v1}, Landroid/view/ViewGroup$LayoutParams;-><init>(II)V

    invoke-virtual {p0, v2, v3}, Landroid/view/ViewGroup;->addView(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V

    .line 81
    new-instance p0, Lcom/mxp/Helper$1;

    invoke-direct {p0}, Lcom/mxp/Helper$1;-><init>()V

    sput-object p0, Lcom/mxp/Helper;->keyboardWatcher:Landroid/text/TextWatcher;

    .line 139
    sget-object p0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    new-instance v2, Lcom/mxp/Helper$$ExternalSyntheticLambda2;

    invoke-direct {v2}, Lcom/mxp/Helper$$ExternalSyntheticLambda2;-><init>()V

    invoke-virtual {p0, v2}, Landroid/widget/EditText;->setOnEditorActionListener(Landroid/widget/TextView$OnEditorActionListener;)V

    .line 144
    sget-object p0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    new-instance v2, Lcom/mxp/Helper$$ExternalSyntheticLambda3;

    invoke-direct {v2}, Lcom/mxp/Helper$$ExternalSyntheticLambda3;-><init>()V

    invoke-virtual {p0, v2}, Landroid/widget/EditText;->setOnKeyListener(Landroid/view/View$OnKeyListener;)V

    .line 173
    sget-object p0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    sget-object v2, Lcom/mxp/Helper;->keyboardWatcher:Landroid/text/TextWatcher;

    invoke-virtual {p0, v2}, Landroid/widget/EditText;->addTextChangedListener(Landroid/text/TextWatcher;)V

    .line 176
    :cond_77
    sget-boolean p0, Lcom/mxp/Helper;->keyboardActive:Z

    if-nez p0, :cond_reassert

    .line 177
    sget-object p0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    invoke-virtual {p0}, Landroid/widget/EditText;->getText()Landroid/text/Editable;

    move-result-object p0

    sget-object v2, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    invoke-virtual {v2}, Landroid/widget/EditText;->getText()Landroid/text/Editable;

    move-result-object v2

    invoke-interface {v2}, Landroid/text/Editable;->length()I

    move-result v2

    const-string v3, "        "

    invoke-interface {p0, v0, v2, v3}, Landroid/text/Editable;->replace(IILjava/lang/CharSequence;)Landroid/text/Editable;

    .line 178
    sget-object p0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    invoke-virtual {v3}, Ljava/lang/String;->length()I

    move-result v0

    invoke-virtual {p0, v0}, Landroid/widget/EditText;->setSelection(I)V

    :cond_reassert
    .line 179
    sget-object p0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    invoke-virtual {p0}, Landroid/widget/EditText;->requestFocus()Z

    .line 180
    sget-object p0, Lcom/mxp/Helper;->keyboardImm:Landroid/view/inputmethod/InputMethodManager;

    sget-object v0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    const/4 v2, 0x2

    invoke-virtual {p0, v0, v2}, Landroid/view/inputmethod/InputMethodManager;->showSoftInput(Landroid/view/View;I)Z

    .line 181
    sput-boolean v1, Lcom/mxp/Helper;->keyboardActive:Z

    goto :goto_d7

    .line 184
    :cond_a9
    sget-object p0, Lcom/mxp/Helper;->keyRepeatHandler:Landroid/os/Handler;

    if-eqz p0, :cond_b9

    .line 185
    sget-object p0, Lcom/mxp/Helper;->keyRepeatHandler:Landroid/os/Handler;

    sget-object v1, Lcom/mxp/Helper;->keyRepeatRunnable:Ljava/lang/Runnable;

    invoke-virtual {p0, v1}, Landroid/os/Handler;->removeCallbacks(Ljava/lang/Runnable;)V

    .line 186
    const/4 p0, 0x0

    sput-object p0, Lcom/mxp/Helper;->keyRepeatHandler:Landroid/os/Handler;

    .line 187
    sput-object p0, Lcom/mxp/Helper;->keyRepeatRunnable:Ljava/lang/Runnable;

    .line 189
    :cond_b9
    sget-boolean p0, Lcom/mxp/Helper;->keyboardActive:Z

    if-eqz p0, :cond_d7

    sget-object p0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    if-eqz p0, :cond_d7

    sget-object p0, Lcom/mxp/Helper;->keyboardImm:Landroid/view/inputmethod/InputMethodManager;

    if-eqz p0, :cond_d7

    .line 190
    sget-object p0, Lcom/mxp/Helper;->keyboardImm:Landroid/view/inputmethod/InputMethodManager;

    sget-object v1, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    invoke-virtual {v1}, Landroid/widget/EditText;->getWindowToken()Landroid/os/IBinder;

    move-result-object v1

    invoke-virtual {p0, v1, v0}, Landroid/view/inputmethod/InputMethodManager;->hideSoftInputFromWindow(Landroid/os/IBinder;I)Z

    .line 191
    sget-object p0, Lcom/mxp/Helper;->keyboardEditText:Landroid/widget/EditText;

    invoke-virtual {p0}, Landroid/widget/EditText;->clearFocus()V

    .line 192
    sput-boolean v0, Lcom/mxp/Helper;->keyboardActive:Z

    .line 195
    :cond_d7
    :goto_d7
    return-void
.end method

.method static synthetic lambda$showToast$0(Ljava/lang/String;)V
    .registers 3

    .line 57
    sget-object v0, Lcom/mxp/Helper;->activity:Landroid/app/Activity;

    const/4 v1, 0x0

    invoke-static {v0, p0, v1}, Landroid/widget/Toast;->makeText(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;

    move-result-object p0

    invoke-virtual {p0}, Landroid/widget/Toast;->show()V

    return-void
.end method

.method public static native nativeAddChar(I)V
.end method

.method public static native nativeKeyEvent(I)V
.end method

.method public static showSoftKeyboard(Z)V
    .registers 3

    .line 61
    new-instance v0, Landroid/os/Handler;

    invoke-static {}, Landroid/os/Looper;->getMainLooper()Landroid/os/Looper;

    move-result-object v1

    invoke-direct {v0, v1}, Landroid/os/Handler;-><init>(Landroid/os/Looper;)V

    new-instance v1, Lcom/mxp/Helper$$ExternalSyntheticLambda0;

    invoke-direct {v1, p0}, Lcom/mxp/Helper$$ExternalSyntheticLambda0;-><init>(Z)V

    invoke-virtual {v0, v1}, Landroid/os/Handler;->post(Ljava/lang/Runnable;)Z

    .line 196
    return-void
.end method

.method public static showToast(Ljava/lang/String;)V
    .registers 3

    .line 56
    new-instance v0, Landroid/os/Handler;

    invoke-static {}, Landroid/os/Looper;->getMainLooper()Landroid/os/Looper;

    move-result-object v1

    invoke-direct {v0, v1}, Landroid/os/Handler;-><init>(Landroid/os/Looper;)V

    new-instance v1, Lcom/mxp/Helper$$ExternalSyntheticLambda4;

    invoke-direct {v1, p0}, Lcom/mxp/Helper$$ExternalSyntheticLambda4;-><init>(Ljava/lang/String;)V

    invoke-virtual {v0, v1}, Landroid/os/Handler;->post(Ljava/lang/Runnable;)Z

    .line 58
    return-void
.end method

.method private static startCountdownAndExit()V
    .registers 3

    .line 333
    new-instance v0, Landroid/os/Handler;

    invoke-static {}, Landroid/os/Looper;->getMainLooper()Landroid/os/Looper;

    move-result-object v1

    invoke-direct {v0, v1}, Landroid/os/Handler;-><init>(Landroid/os/Looper;)V

    .line 334
    const/4 v1, 0x5

    filled-new-array {v1}, [I

    move-result-object v1

    .line 335
    new-instance v2, Lcom/mxp/Helper$5;

    invoke-direct {v2, v1, v0}, Lcom/mxp/Helper$5;-><init>([ILandroid/os/Handler;)V

    .line 350
    invoke-virtual {v0, v2}, Landroid/os/Handler;->post(Ljava/lang/Runnable;)Z

    .line 351
    return-void
.end method
