/*
 * Decompiled with CFR 0.152.
 * 
 * Could not load the following classes:
 *  android.app.Activity
 *  android.content.Context
 *  android.net.ConnectivityManager
 *  android.net.Network
 *  android.net.NetworkCapabilities
 *  android.os.Build$VERSION
 *  android.os.Handler
 *  android.os.Looper
 *  android.os.Process
 *  android.text.Editable
 *  android.text.TextWatcher
 *  android.view.KeyEvent
 *  android.view.SurfaceView
 *  android.view.View
 *  android.view.View$OnKeyListener
 *  android.view.ViewGroup
 *  android.view.ViewGroup$LayoutParams
 *  android.view.WindowManager$LayoutParams
 *  android.view.inputmethod.InputMethodManager
 *  android.widget.EditText
 *  android.widget.TextView
 *  android.widget.TextView$OnEditorActionListener
 *  android.widget.Toast
 */
package com.mxp;

import android.app.Activity;
import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.KeyEvent;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;
import com.mxp.Helper$$ExternalSyntheticLambda0;
import com.mxp.Helper$$ExternalSyntheticLambda1;
import com.mxp.Helper$$ExternalSyntheticLambda2;
import com.mxp.Helper$$ExternalSyntheticLambda3;
import com.mxp.Helper$$ExternalSyntheticLambda4;
import java.util.Stack;

public class Helper {
    private static final String KB_SENTINEL = "        ";
    private static final int KEY_REPEAT_INITIAL_DELAY = 400;
    private static final int KEY_REPEAT_INTERVAL = 50;
    public static Activity activity;
    private static boolean isCountingDown;
    private static boolean isSecureActive;
    private static boolean isVpnWatcherActive;
    private static Handler keyRepeatHandler;
    private static Runnable keyRepeatRunnable;
    private static boolean keyboardActive;
    private static EditText keyboardEditText;
    private static InputMethodManager keyboardImm;
    private static TextWatcher keyboardWatcher;
    private static Handler secureWatcherHandler;
    private static Runnable secureWatcherRunnable;
    private static Handler vpnWatcherHandler;
    private static Runnable vpnWatcherRunnable;

    static {
        isSecureActive = false;
        isVpnWatcherActive = false;
        isCountingDown = false;
        keyboardActive = false;
        keyRepeatHandler = null;
        keyRepeatRunnable = null;
    }

    public static void IsVpnActive(boolean bl) {
        if (!bl) {
            isVpnWatcherActive = false;
            isCountingDown = false;
            if (vpnWatcherHandler != null && vpnWatcherRunnable != null) {
                vpnWatcherHandler.removeCallbacks(vpnWatcherRunnable);
                vpnWatcherHandler = null;
                vpnWatcherRunnable = null;
            }
            return;
        }
        isVpnWatcherActive = true;
        vpnWatcherHandler = new Handler(Looper.getMainLooper());
        vpnWatcherRunnable = new Runnable(){

            @Override
            public void run() {
                int n;
                block5: {
                    if (!isVpnWatcherActive) {
                        return;
                    }
                    ConnectivityManager connectivityManager = (ConnectivityManager)activity.getSystemService("connectivity");
                    Network[] networkArray = connectivityManager.getAllNetworks();
                    int n2 = networkArray.length;
                    for (n = 0; n < n2; ++n) {
                        NetworkCapabilities networkCapabilities = connectivityManager.getNetworkCapabilities(networkArray[n]);
                        if (networkCapabilities == null || !networkCapabilities.hasTransport(4)) continue;
                        n = 1;
                        break block5;
                    }
                    n = 0;
                }
                if (n != 0 && !isCountingDown) {
                    isCountingDown = true;
                    Helper.startCountdownAndExit();
                } else if (n == 0) {
                    isCountingDown = false;
                }
                vpnWatcherHandler.postDelayed((Runnable)this, 1000L);
            }
        };
        vpnWatcherHandler.post(vpnWatcherRunnable);
    }

    public static void hideScreen(boolean bl) {
        new Handler(Looper.getMainLooper()).post((Runnable)new Helper$$ExternalSyntheticLambda1(bl));
    }

    public static void init(Activity activity) {
        Helper.activity = activity;
    }

    /*
     * WARNING - Removed back jump from a try to a catch block - possible behaviour change.
     * Unable to fully structure code
     * Enabled aggressive block sorting
     * Enabled unnecessary exception pruning
     * Enabled aggressive exception aggregation
     */
    static /* synthetic */ void lambda$hideScreen$4(boolean var0) {
        try {
            Helper.isSecureActive = var0;
            var4_1 = Helper.activity.getWindow();
            var2_3 /* !! */  = var4_1.getDecorView();
            if (!var0) ** GOTO lbl57
            var4_1.addFlags(8192);
            var3_6 = var4_1.getAttributes();
            var3_6.flags |= 8192;
            var4_1.setAttributes(var3_6);
            var1_8 = Build.VERSION.SDK_INT;
            if (var1_8 >= 33) {
            }
            ** GOTO lbl-1000
        }
        catch (Exception var2_5) {
            if (var0) {
                Helper.activity.getWindow().addFlags(8192);
                return;
            }
            Helper.activity.getWindow().clearFlags(8192);
            return;
        }
        try {
            var5_10 = WindowManager.LayoutParams.class.getDeclaredField("privateFlags");
            var5_10.setAccessible(true);
            var5_10.set(var3_6, (Integer)var5_10.get(var3_6) | 4);
            var4_1.setAttributes(var3_6);
        }
        catch (Exception var4_2) {
            // empty catch block
        }
lbl-1000:
        // 3 sources

        {
            var4_1 = new Stack();
            var4_1.push(var2_3 /* !! */ );
            while (!var4_1.isEmpty()) {
                var5_10 = (View)var4_1.pop();
                if (var5_10 instanceof SurfaceView) {
                    ((SurfaceView)var5_10).setSecure(true);
                }
                if (!(var5_10 instanceof ViewGroup)) continue;
                var5_10 = (ViewGroup)var5_10;
                for (var1_8 = 0; var1_8 < var5_10.getChildCount(); ++var1_8) {
                    var4_1.push(var5_10.getChildAt(var1_8));
                }
            }
        }
        try {
            var4_1 = Helper.activity.getSystemService("window");
            var4_1.getClass().getMethod("updateViewLayout", new Class[]{View.class, ViewGroup.LayoutParams.class}).invoke(var4_1, new Object[]{var2_3 /* !! */ , var3_6});
        }
        catch (Exception var2_4) {
            // empty catch block
        }
        {
            if (Helper.secureWatcherHandler != null) return;
            var2_3 /* !! */  = new Handler(Looper.getMainLooper());
            Helper.secureWatcherHandler = var2_3 /* !! */ ;
            var2_3 /* !! */  = new Runnable(){

                @Override
                public void run() {
                    if (!isSecureActive) {
                        return;
                    }
                    if ((Helper.activity.getWindow().getAttributes().flags & 0x2000) == 0) {
                        Helper.hideScreen(true);
                    }
                    secureWatcherHandler.postDelayed((Runnable)this, 300L);
                }
            };
            Helper.secureWatcherRunnable = var2_3 /* !! */ ;
            Helper.secureWatcherHandler.postDelayed(Helper.secureWatcherRunnable, 300L);
            return;
lbl57:
            // 1 sources

            Helper.isSecureActive = false;
            if (Helper.secureWatcherHandler != null) {
                Helper.secureWatcherHandler.removeCallbacks(Helper.secureWatcherRunnable);
                Helper.secureWatcherHandler = null;
                Helper.secureWatcherRunnable = null;
            }
            var4_1.clearFlags(8192);
            var3_7 = var4_1.getAttributes();
            var3_7.flags &= -8193;
            var4_1.setAttributes((WindowManager.LayoutParams)var3_7);
            var3_7 = new Stack();
            var3_7.push(var2_3 /* !! */ );
            block10: while (true) {
                if (var3_7.isEmpty() != false) return;
                var2_3 /* !! */  = (View)var3_7.pop();
                if (var2_3 /* !! */  instanceof SurfaceView) {
                    ((SurfaceView)var2_3 /* !! */ ).setSecure(false);
                }
                if (!(var2_3 /* !! */  instanceof ViewGroup)) continue;
                var2_3 /* !! */  = (ViewGroup)var2_3 /* !! */ ;
                var1_9 = 0;
                while (true) {
                    if (var1_9 < var2_3 /* !! */ .getChildCount()) ** break;
                    continue block10;
                    var3_7.push(var2_3 /* !! */ .getChildAt(var1_9));
                    ++var1_9;
                }
                break;
            }
        }
    }

    static /* synthetic */ boolean lambda$showSoftKeyboard$1(TextView textView, int n, KeyEvent keyEvent) {
        Helper.nativeKeyEvent(66);
        return true;
    }

    static /* synthetic */ boolean lambda$showSoftKeyboard$2(View view, int n, KeyEvent keyEvent) {
        if (n == 67) {
            if (keyEvent.getAction() == 0) {
                Helper.nativeKeyEvent(67);
                if (keyRepeatHandler == null) {
                    keyRepeatHandler = new Handler(Looper.getMainLooper());
                    keyRepeatRunnable = new Runnable(){

                        @Override
                        public void run() {
                            if (keyRepeatHandler == null) {
                                return;
                            }
                            Helper.nativeKeyEvent(67);
                            keyRepeatHandler.postDelayed((Runnable)this, 50L);
                        }
                    };
                    keyRepeatHandler.postDelayed(keyRepeatRunnable, 400L);
                }
                return true;
            }
            if (keyEvent.getAction() == 1) {
                if (keyRepeatHandler != null) {
                    keyRepeatHandler.removeCallbacks(keyRepeatRunnable);
                    keyRepeatHandler = null;
                    keyRepeatRunnable = null;
                }
                return true;
            }
        }
        return false;
    }

    static /* synthetic */ void lambda$showSoftKeyboard$3(boolean bl) {
        if (activity == null) {
            return;
        }
        if (bl) {
            if (keyboardEditText == null) {
                keyboardImm = (InputMethodManager)activity.getSystemService("input_method");
                keyboardEditText = new EditText((Context)activity);
                keyboardEditText.setAlpha(0.0f);
                keyboardEditText.setInputType(524289);
                keyboardEditText.setImeOptions(0x12000001);
                keyboardEditText.setPrivateImeOptions("nm");
                ((ViewGroup)activity.getWindow().getDecorView()).addView((View)keyboardEditText, new ViewGroup.LayoutParams(1, 1));
                keyboardWatcher = new TextWatcher(){
                    private boolean selfChange = false;

                    public void afterTextChanged(Editable object) {
                        if (this.selfChange) {
                            return;
                        }
                        if (((String)(object = object.toString())).equals(Helper.KB_SENTINEL)) {
                            return;
                        }
                        this.selfChange = true;
                        keyboardEditText.removeTextChangedListener((TextWatcher)this);
                        int n = Helper.KB_SENTINEL.length();
                        int n2 = ((String)object).length();
                        if (n2 < n) {
                            for (int i = 0; i < n - n2; ++i) {
                                Helper.nativeKeyEvent(67);
                            }
                        } else if (n2 > n) {
                            object = ((String)object).substring(n);
                            for (int i = 0; i < ((String)object).length(); ++i) {
                                char c = ((String)object).charAt(i);
                                if (c == '\n') {
                                    Helper.nativeKeyEvent(66);
                                    continue;
                                }
                                Helper.nativeAddChar(c);
                            }
                        } else {
                            int n3 = 0;
                            for (int i = 0; i < n2; ++i) {
                                int n4 = n3;
                                if (i < n) {
                                    n4 = n3;
                                    if (((String)object).charAt(i) != Helper.KB_SENTINEL.charAt(i)) {
                                        n4 = n3 + 1;
                                        n3 = ((String)object).charAt(i);
                                        if (n3 == 10) {
                                            Helper.nativeKeyEvent(66);
                                        } else {
                                            Helper.nativeAddChar(n3);
                                        }
                                    }
                                }
                                n3 = n4;
                            }
                            if (n3 == 0) {
                                Helper.nativeKeyEvent(67);
                            }
                        }
                        keyboardEditText.getText().replace(0, keyboardEditText.getText().length(), (CharSequence)Helper.KB_SENTINEL);
                        keyboardEditText.setSelection(Helper.KB_SENTINEL.length());
                        keyboardEditText.addTextChangedListener((TextWatcher)this);
                        this.selfChange = false;
                    }

                    public void beforeTextChanged(CharSequence charSequence, int n, int n2, int n3) {
                    }

                    public void onTextChanged(CharSequence charSequence, int n, int n2, int n3) {
                    }
                };
                keyboardEditText.setOnEditorActionListener((TextView.OnEditorActionListener)new Helper$$ExternalSyntheticLambda2());
                keyboardEditText.setOnKeyListener((View.OnKeyListener)new Helper$$ExternalSyntheticLambda3());
                keyboardEditText.addTextChangedListener(keyboardWatcher);
            }
            if (!keyboardActive) {
                keyboardEditText.getText().replace(0, keyboardEditText.getText().length(), (CharSequence)KB_SENTINEL);
                keyboardEditText.setSelection(KB_SENTINEL.length());
            }
            keyboardEditText.requestFocus();
            keyboardImm.showSoftInput((View)keyboardEditText, 2);
            keyboardActive = true;
        } else {
            if (keyRepeatHandler != null) {
                keyRepeatHandler.removeCallbacks(keyRepeatRunnable);
                keyRepeatHandler = null;
                keyRepeatRunnable = null;
            }
            if (keyboardActive && keyboardEditText != null && keyboardImm != null) {
                keyboardImm.hideSoftInputFromWindow(keyboardEditText.getWindowToken(), 0);
                keyboardEditText.clearFocus();
                keyboardActive = false;
            }
        }
    }

    static /* synthetic */ void lambda$showToast$0(String string) {
        Toast.makeText((Context)activity, (CharSequence)string, (int)0).show();
    }

    public static native void nativeAddChar(int var0);

    public static native void nativeKeyEvent(int var0);

    public static void showSoftKeyboard(boolean bl) {
        new Handler(Looper.getMainLooper()).post((Runnable)new Helper$$ExternalSyntheticLambda0(bl));
    }

    public static void showToast(String string) {
        new Handler(Looper.getMainLooper()).post((Runnable)new Helper$$ExternalSyntheticLambda4(string));
    }

    private static void startCountdownAndExit() {
        Handler handler = new Handler(Looper.getMainLooper());
        handler.post(new Runnable(){
            final int[] val$countdown;
            final Handler val$handler;
            {
                this.val$countdown = nArray;
                this.val$handler = handler;
            }

            @Override
            public void run() {
                if (this.val$countdown[0] > 0) {
                    Object object = activity;
                    int n = this.val$countdown[0];
                    Toast.makeText((Context)object, (CharSequence)("VPN detected! Please turn off VPN (App will close in " + n + ")"), (int)0).show();
                    object = this.val$countdown;
                    object[0] = object[0] - true;
                    this.val$handler.postDelayed((Runnable)this, 1000L);
                } else {
                    activity.finishAffinity();
                    Process.killProcess((int)Process.myPid());
                }
            }
        });
    }
}

