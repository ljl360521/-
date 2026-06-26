package com.example.imgui;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewTreeObserver;
import android.view.Window;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;

import java.lang.ref.WeakReference;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.Map;

public class IMEHelper {
    private static final String TAG = "IMEHelper";
    private static final long KEYBOARD_DETECT_DELAY_MS = 500;
    private static final int SHOW_KEYBOARD_DELAY_MS = 200;

    private static WeakReference<Activity> activityRef;
    private static EditText editText;
    private static InputMethodManager imm;
    private static InputTextCallback inputCallback;
    private static Dialog inputDialog;
    private static ViewTreeObserver.OnGlobalLayoutListener layoutListener;

    private static volatile boolean isShowing = false;
    private static volatile String cachedText = "";
    private static boolean initialized = false;
    private static long lastShowTime = 0;

    private static volatile String composingText = "";
    private static volatile int composingStart = -1;
    private static volatile int composingEnd = -1;

    public static String getComposingText() { return composingText; }
    public static int getComposingStart() { return composingStart; }
    public static int getComposingEnd() { return composingEnd; }

    private static final Handler mainHandler = new Handler(Looper.getMainLooper());

    public interface InputTextCallback {
        void onInputTextChanged(String text);
        void onDeletePressed();
    }

    public static void init() {
        runOnMain(() -> {
            Activity activity = getCurrentActivity();
            if (activity != null) {
                initInternal(activity);
            } else {
                Log.e(TAG, "init() 无法自动获取当前 Activity");
            }
        });
    }

    public static void init(Activity activity) {
        if (activity == null) return;
        runOnMain(() -> initInternal(activity));
    }

    public static void setInputTextCallback(InputTextCallback callback) {
        inputCallback = callback;
    }

    public static void showInputMethod() {
        runOnMain(IMEHelper::showInputMethodInternal);
    }

    public static void showWithText(String text) {
        cachedText = text != null ? text : "";
        runOnMain(IMEHelper::showInputMethodInternal);
    }

    public static void hideInputMethod() {
        runOnMain(IMEHelper::hideInputInternal);
    }

    public static String getInputText() {
        return cachedText;
    }

    public static void setInputText(String text) {
        cachedText = text != null ? text : "";
        runOnMain(() -> {
            if (editText != null) {
                editText.setText(cachedText);
                editText.setSelection(cachedText.length());
            }
        });
    }

    public static void clearInput() {
        setInputText("");
    }

    public static boolean isInputVisible() {
        return isShowing;
    }

    public static boolean isInitialized() {
        return initialized;
    }

    public static void destroy() {
        runOnMain(() -> {
            hideInputInternal();
            removeLayoutListener();
            inputDialog = null;
            editText = null;
            if (activityRef != null) activityRef.clear();
            inputCallback = null;
            initialized = false;
            cachedText = "";
        });
    }

    @SuppressWarnings("unchecked")
    private static Activity getCurrentActivity() {
        try {
            Class<?> atClass = Class.forName("android.app.ActivityThread");
            Method currentAT = atClass.getMethod("currentActivityThread");
            Object at = currentAT.invoke(null);

            Field field = atClass.getDeclaredField("mActivities");
            field.setAccessible(true);
            Map<Object, Object> activities = (Map<Object, Object>) field.get(at);
            if (activities == null) return null;

            for (Object record : activities.values()) {
                Class<?> recordClass = record.getClass();

                Field pausedField = recordClass.getDeclaredField("paused");
                pausedField.setAccessible(true);
                if (pausedField.getBoolean(record)) continue;

                Field activityField = recordClass.getDeclaredField("activity");
                activityField.setAccessible(true);
                Activity activity = (Activity) activityField.get(record);

                if (activity != null && !activity.isFinishing()) {
                    return activity;
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "getCurrentActivity 反射失败", e);
        }
        return null;
    }

    private static void runOnMain(Runnable action) {
        if (Looper.getMainLooper() == Looper.myLooper()) {
            action.run();
        } else {
            mainHandler.post(action);
        }
    }

    private static void initInternal(Activity activity) {
        try {
            activityRef = new WeakReference<>(activity);
            imm = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);

            final View rootView = activity.getWindow().getDecorView();
            removeLayoutListener();

            layoutListener = () -> {
                try {
                    android.graphics.Rect r = new android.graphics.Rect();
                    rootView.getWindowVisibleDisplayFrame(r);
                    int screenHeight = rootView.getRootView().getHeight();
                    int kbHeight = screenHeight - r.bottom;

                    if (kbHeight <= screenHeight * 0.15 && isShowing
                            && System.currentTimeMillis() - lastShowTime > KEYBOARD_DETECT_DELAY_MS) {
                        Log.i(TAG, "Keyboard dismissed, closing dialog");
                        dismissDialog();
                    }
                } catch (Exception e) {
                    Log.e(TAG, "Layout listener error", e);
                }
            };
            rootView.getViewTreeObserver().addOnGlobalLayoutListener(layoutListener);

            initialized = true;
            Log.i(TAG, "IMEHelper initialized (Activity: " + activity.getClass().getSimpleName() + ")");
        } catch (Exception e) {
            Log.e(TAG, "Init error", e);
        }
    }

    private static void removeLayoutListener() {
        if (layoutListener != null && activityRef != null) {
            Activity activity = activityRef.get();
            if (activity != null) {
                activity.getWindow().getDecorView()
                        .getViewTreeObserver().removeOnGlobalLayoutListener(layoutListener);
            }
            layoutListener = null;
        }
    }

    private static void showInputMethodInternal() {
        try {
            Activity activity = activityRef != null ? activityRef.get() : null;
            if (activity == null || !initialized || activity.isFinishing()) return;

            dismissDialog();
            lastShowTime = System.currentTimeMillis();

            editText = createEditText(activity);

            inputDialog = new Dialog(activity, android.R.style.Theme_Panel) {
                @Override
                public void onBackPressed() {
                    hideInputMethod();
                }
            };
            inputDialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
            inputDialog.setCancelable(true);
            inputDialog.setCanceledOnTouchOutside(false);

            FrameLayout container = new FrameLayout(activity);
            container.setBackgroundColor(Color.WHITE);
            container.addView(editText, new FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    FrameLayout.LayoutParams.WRAP_CONTENT));
            inputDialog.setContentView(container);

            inputDialog.setOnDismissListener(dialog -> {
                Log.i(TAG, "Dialog dismissed");
                isShowing = false;
            });

            configureDialogWindow(inputDialog.getWindow());

            inputDialog.show();
            isShowing = true;
            lastShowTime = System.currentTimeMillis();

            editText.requestFocus();
            mainHandler.postDelayed(() -> {
                if (editText != null && imm != null) {
                    imm.showSoftInput(editText, InputMethodManager.SHOW_IMPLICIT);
                }
            }, SHOW_KEYBOARD_DELAY_MS);

        } catch (Exception e) {
            Log.e(TAG, "Show error", e);
        }
    }

    private static EditText createEditText(Activity activity) {
        EditText et = new EditText(activity) {
            @Override
            public boolean onCheckIsTextEditor() { return true; }
        };

        et.setBackgroundColor(Color.TRANSPARENT);
        et.setTextColor(Color.TRANSPARENT);
        et.setCursorVisible(false);
        et.setTextSize(1);
        et.setAlpha(0f);
        et.setHeight(1);
        et.setWidth(1);

        et.setSingleLine(true);
        et.setImeOptions(EditorInfo.IME_ACTION_DONE);
        et.setInputType(EditorInfo.TYPE_CLASS_TEXT);
        et.setText(cachedText);
        et.setSelection(cachedText.length());

        et.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                cachedText = s.toString();

                if (s instanceof android.text.Spannable) {
                    android.text.Spannable spannable = (android.text.Spannable) s;
                    int cStart = -1, cEnd = -1;
                    for (Object span : spannable.getSpans(0, s.length(), Object.class)) {
                        int flags = spannable.getSpanFlags(span);
                        if ((flags & android.text.Spanned.SPAN_COMPOSING) != 0) {
                            int ss = spannable.getSpanStart(span);
                            int se = spannable.getSpanEnd(span);
                            if (cStart == -1 || ss < cStart) cStart = ss;
                            if (se > cEnd) cEnd = se;
                        }
                    }
                    composingStart = cStart;
                    composingEnd = cEnd;
                    composingText = (cStart >= 0 && cEnd > cStart)
                            ? s.subSequence(cStart, cEnd).toString() : "";
                }

                if (inputCallback != null) {
                    inputCallback.onInputTextChanged(cachedText);
                }
            }
            @Override public void afterTextChanged(Editable s) {}
        });

        et.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE) {
                hideInputMethod();
                return true;
            }
            return false;
        });

        return et;
    }

    private static void configureDialogWindow(Window window) {
        if (window == null) return;
        window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        window.setGravity(Gravity.BOTTOM);
        window.setSoftInputMode(
                WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE
                        | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING);

        WindowManager.LayoutParams lp = window.getAttributes();
        lp.width = 1;
        lp.height = 1;
        lp.flags &= ~(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                | WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM
                | WindowManager.LayoutParams.FLAG_DIM_BEHIND);
        lp.flags |= WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL;
        window.setAttributes(lp);
    }

    private static void hideInputInternal() {
        try {
            if (editText != null && imm != null) {
                imm.hideSoftInputFromWindow(editText.getWindowToken(), 0);
            }
            dismissDialog();
        } catch (Exception e) {
            Log.e(TAG, "Hide error", e);
        }
    }

    private static void dismissDialog() {
        try {
            isShowing = false;
            if (inputDialog != null && inputDialog.isShowing()) {
                inputDialog.dismiss();
            }
            inputDialog = null;
            editText = null;
        } catch (Exception e) {
            Log.e(TAG, "Dismiss error", e);
        }
    }
}