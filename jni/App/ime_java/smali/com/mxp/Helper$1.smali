.class Lcom/mxp/Helper$1;
.super Ljava/lang/Object;
.source "Helper.java"

# interfaces
.implements Landroid/text/TextWatcher;


# annotations
.annotation system Ldalvik/annotation/EnclosingMethod;
    value = Lcom/mxp/Helper;->showSoftKeyboard(Z)V
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x0
    name = null
.end annotation


# instance fields
.field private selfChange:Z


# direct methods
.method constructor <init>()V
    .registers 2

    .line 81
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    .line 82
    const/4 v0, 0x0

    iput-boolean v0, p0, Lcom/mxp/Helper$1;->selfChange:Z

    return-void
.end method


# virtual methods
.method public afterTextChanged(Landroid/text/Editable;)V
    .registers 13

    .line 89
    iget-boolean v0, p0, Lcom/mxp/Helper$1;->selfChange:Z

    if-eqz v0, :cond_5

    return-void

    .line 90
    :cond_5
    invoke-virtual {p1}, Ljava/lang/Object;->toString()Ljava/lang/String;

    move-result-object p1

    .line 91
    const-string v0, "        "

    invoke-virtual {p1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v1

    if-eqz v1, :cond_12

    return-void

    .line 93
    :cond_12
    const/4 v1, 0x1

    iput-boolean v1, p0, Lcom/mxp/Helper$1;->selfChange:Z

    .line 94
    invoke-static {}, Lcom/mxp/Helper;->-$$Nest$sfgetkeyboardEditText()Landroid/widget/EditText;

    move-result-object v1

    invoke-virtual {v1, p0}, Landroid/widget/EditText;->removeTextChangedListener(Landroid/text/TextWatcher;)V

    .line 96
    invoke-virtual {v0}, Ljava/lang/String;->length()I

    move-result v1

    .line 97
    invoke-virtual {p1}, Ljava/lang/String;->length()I

    move-result v2

    .line 99
    const/16 v3, 0x43

    const/4 v4, 0x0

    if-ge v2, v1, :cond_34

    .line 100
    sub-int/2addr v1, v2

    .line 101
    move p1, v4

    :goto_2b
    if-ge p1, v1, :cond_33

    .line 102
    invoke-static {v3}, Lcom/mxp/Helper;->nativeKeyEvent(I)V

    .line 101
    add-int/lit8 p1, p1, 0x1

    goto :goto_2b

    .line 104
    :cond_33
    goto :goto_7e

    :cond_34
    const/16 v5, 0x42

    const/16 v6, 0xa

    if-le v2, v1, :cond_56

    .line 105
    invoke-virtual {p1, v1}, Ljava/lang/String;->substring(I)Ljava/lang/String;

    move-result-object p1

    .line 106
    move v1, v4

    :goto_3f
    invoke-virtual {p1}, Ljava/lang/String;->length()I

    move-result v2

    if-ge v1, v2, :cond_55

    .line 107
    invoke-virtual {p1, v1}, Ljava/lang/String;->charAt(I)C

    move-result v2

    .line 108
    if-ne v2, v6, :cond_4f

    .line 109
    invoke-static {v5}, Lcom/mxp/Helper;->nativeKeyEvent(I)V

    goto :goto_52

    .line 111
    :cond_4f
    invoke-static {v2}, Lcom/mxp/Helper;->nativeAddChar(I)V

    .line 106
    :goto_52
    add-int/lit8 v1, v1, 0x1

    goto :goto_3f

    .line 114
    :cond_55
    goto :goto_7e

    .line 115
    :cond_56
    nop

    .line 116
    move v7, v4

    move v8, v7

    :goto_59
    if-ge v7, v2, :cond_79

    .line 117
    if-ge v7, v1, :cond_76

    invoke-virtual {p1, v7}, Ljava/lang/String;->charAt(I)C

    move-result v9

    invoke-virtual {v0, v7}, Ljava/lang/String;->charAt(I)C

    move-result v10

    if-eq v9, v10, :cond_76

    .line 118
    add-int/lit8 v8, v8, 0x1

    .line 119
    invoke-virtual {p1, v7}, Ljava/lang/String;->charAt(I)C

    move-result v9

    .line 120
    if-ne v9, v6, :cond_73

    .line 121
    invoke-static {v5}, Lcom/mxp/Helper;->nativeKeyEvent(I)V

    goto :goto_76

    .line 123
    :cond_73
    invoke-static {v9}, Lcom/mxp/Helper;->nativeAddChar(I)V

    .line 116
    :cond_76
    :goto_76
    add-int/lit8 v7, v7, 0x1

    goto :goto_59

    .line 127
    :cond_79
    if-nez v8, :cond_7e

    .line 128
    invoke-static {v3}, Lcom/mxp/Helper;->nativeKeyEvent(I)V

    .line 132
    :cond_7e
    :goto_7e
    invoke-static {}, Lcom/mxp/Helper;->-$$Nest$sfgetkeyboardEditText()Landroid/widget/EditText;

    move-result-object p1

    invoke-virtual {p1}, Landroid/widget/EditText;->getText()Landroid/text/Editable;

    move-result-object p1

    invoke-static {}, Lcom/mxp/Helper;->-$$Nest$sfgetkeyboardEditText()Landroid/widget/EditText;

    move-result-object v1

    invoke-virtual {v1}, Landroid/widget/EditText;->getText()Landroid/text/Editable;

    move-result-object v1

    invoke-interface {v1}, Landroid/text/Editable;->length()I

    move-result v1

    invoke-interface {p1, v4, v1, v0}, Landroid/text/Editable;->replace(IILjava/lang/CharSequence;)Landroid/text/Editable;

    .line 133
    invoke-static {}, Lcom/mxp/Helper;->-$$Nest$sfgetkeyboardEditText()Landroid/widget/EditText;

    move-result-object p1

    invoke-virtual {v0}, Ljava/lang/String;->length()I

    move-result v0

    invoke-virtual {p1, v0}, Landroid/widget/EditText;->setSelection(I)V

    .line 134
    invoke-static {}, Lcom/mxp/Helper;->-$$Nest$sfgetkeyboardEditText()Landroid/widget/EditText;

    move-result-object p1

    invoke-virtual {p1, p0}, Landroid/widget/EditText;->addTextChangedListener(Landroid/text/TextWatcher;)V

    .line 135
    iput-boolean v4, p0, Lcom/mxp/Helper$1;->selfChange:Z

    .line 136
    return-void
.end method

.method public beforeTextChanged(Ljava/lang/CharSequence;III)V
    .registers 5

    .line 84
    return-void
.end method

.method public onTextChanged(Ljava/lang/CharSequence;III)V
    .registers 5

    .line 85
    return-void
.end method
