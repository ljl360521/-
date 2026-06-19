package com.example.imgui;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.widget.FrameLayout;
import android.view.MotionEvent;
import android.util.Log;
import java.lang.ref.WeakReference;
import java.util.Map;
import java.util.List;
import android.view.View;
import android.view.ViewGroup;
import java.util.HashMap;
import java.util.ArrayList;
import android.graphics.PixelFormat;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyEvent;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

public class ImGui {
    private static final String TAG = "ImGui";
    private static final String IMGUI_VIEW_TAG = "imgui_overlay_view";
    private static VulkanView display;
    private WeakReference<Activity> currentActivityRef;

    private static final int MAX_POINTERS = 10;
    private boolean[] pointerInImGui  = new boolean[MAX_POINTERS];
    private boolean[] pointerInButton = new boolean[MAX_POINTERS];

    private Map<Integer, Integer> gamePointerIdMap = new HashMap<>();
    private List<Integer> activeGamePointers = new ArrayList<>();
    private long gameDownTime = 0;
    private int imguiPointerCount = 0;

    private static class VolumeKeyCallback implements InvocationHandler {
        private final android.view.Window.Callback originalCallback;
        private final Activity activity;

        VolumeKeyCallback(android.view.Window.Callback originalCallback, Activity activity) {
            this.originalCallback = originalCallback;
            this.activity = activity;
        }

        @Override
        public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {
            if ("dispatchKeyEvent".equals(method.getName())
                    && args != null && args.length == 1 && args[0] instanceof KeyEvent) {
                KeyEvent event = (KeyEvent) args[0];
                int keyCode = event.getKeyCode();
                if (VulkanView.interceptVolumeKeys && isVolumeKey(keyCode)
                        && activity.hasWindowFocus()) {
                    if (event.getAction() == KeyEvent.ACTION_DOWN) {
                        VulkanView.nativeOnKeyEvent(0, keyCode, event.getScanCode(),
                                event.getMetaState(), event.getDeviceId(), event.getSource());
                    } else if (event.getAction() == KeyEvent.ACTION_UP) {
                        VulkanView.nativeOnKeyEvent(1, keyCode, event.getScanCode(),
                                event.getMetaState(), event.getDeviceId(), event.getSource());
                    }
                    return true;
                }
            }
            return method.invoke(originalCallback, args);
        }

        private boolean isVolumeKey(int keyCode) {
            return keyCode == KeyEvent.KEYCODE_VOLUME_UP
                    || keyCode == KeyEvent.KEYCODE_VOLUME_DOWN
                    || keyCode == KeyEvent.KEYCODE_VOLUME_MUTE;
        }
    }

    private static class SurfaceSecurityAttachListener implements View.OnAttachStateChangeListener {
        private final VulkanView view;
        SurfaceSecurityAttachListener(VulkanView view) { this.view = view; }
        @Override public void onViewAttachedToWindow(View v) {
            view.post(new SurfaceSecurityRunnable(view));
        }
        @Override public void onViewDetachedFromWindow(View v) {}
    }

    private static class SurfaceSecurityRunnable implements Runnable {
        private final VulkanView view;
        SurfaceSecurityRunnable(VulkanView view) { this.view = view; }
        @Override public void run() { view.applySurfaceSecurity(); }
    }

    private static class SetupRunnable implements Runnable {
        private final ImGui imgui;
        private final Activity activity;
        SetupRunnable(ImGui imgui, Activity activity) { this.imgui = imgui; this.activity = activity; }
        @Override public void run() { imgui.setupImGuiViewInternal(activity); }
    }

    private static class SetupOnMainThreadRunnable implements Runnable {
        private final Activity activity;
        SetupOnMainThreadRunnable(Activity activity) { this.activity = activity; }
        @Override public void run() {
            Log.i(TAG, "Now on main thread, executing setupImGuiViewImpl");
            ImGui imgui = new ImGui();
            imgui.setupImGuiView(activity);
        }
    }

    private static class ImGuiTouchListener implements View.OnTouchListener {
        private final ImGui imgui;
        private final ViewGroup rootView;
        ImGuiTouchListener(ImGui imgui, ViewGroup rootView) {
            this.imgui = imgui; this.rootView = rootView;
        }
        @Override
        public boolean onTouch(View v, MotionEvent event) {
            int source = event.getSource();
            if ((source & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE) {
                return imgui.handleMouseTouchEvent(v, event, rootView);
            }
            return imgui.handleTouchEvent(v, event, rootView);
        }
    }

    private static class ImGuiKeyListener implements View.OnKeyListener {
        @Override public boolean onKey(View v, int keyCode, KeyEvent event) { return false; }
    }


    private void initPointerTracking() {
        for (int i = 0; i < MAX_POINTERS; i++) {
            pointerInImGui[i] = false;
            pointerInButton[i] = false;
        }
        imguiPointerCount = 0;
        gamePointerIdMap.clear();
        activeGamePointers.clear();
        gameDownTime = 0;
    }

    public static void setupImGuiViewOnMainThread(Activity activity) {
        Log.i(TAG, "setupImGuiViewOnMainThread called");
        if (Looper.getMainLooper() == Looper.myLooper()) {
            ImGui imgui = new ImGui();
            imgui.setupImGuiView(activity);
        } else {
            new Handler(Looper.getMainLooper()).post(new SetupOnMainThreadRunnable(activity));
        }
    }

    public void setupImGuiView(final Activity activity) {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            new Handler(Looper.getMainLooper()).post(new SetupRunnable(this, activity));
        } else {
            setupImGuiViewInternal(activity);
        }
    }

    private void setupImGuiViewInternal(Activity activity) {
        try {
            initPointerTracking();

            FrameLayout rootView = activity.findViewById(android.R.id.content);
            if (rootView == null) { Log.e(TAG, "Root view is null"); return; }
            if (rootView.findViewWithTag(IMGUI_VIEW_TAG) != null) {
                Log.i(TAG, "IMGUI view already exists"); return;
            }

            currentActivityRef = new WeakReference<>(activity);

            Log.i(TAG, "Initializing IMEHelper");
            IMEHelper.init(activity);
            ImGuiToolsHelper.init(activity);

            display = new VulkanView(activity);
            display.setFocusable(true);
            display.setFocusableInTouchMode(true);
            display.requestFocus();
            display.setTag(IMGUI_VIEW_TAG);


            display.setOnTouchListener(new ImGuiTouchListener(this, rootView));
            display.setOnKeyListener(new ImGuiKeyListener());
            display.addOnAttachStateChangeListener(new SurfaceSecurityAttachListener(display));

            FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    FrameLayout.LayoutParams.MATCH_PARENT);
            display.setLayoutParams(lp);
            rootView.addView(display);
            hookActivityKeyEvent(activity);

            Log.i(TAG, "ImGui view setup completed (Vulkan)");
        } catch (Exception e) {
            Log.e(TAG, "Error in setupImGuiViewInternal", e);
        }
    }

    private void hookActivityKeyEvent(Activity activity) {
        final android.view.Window window = activity.getWindow();
        final android.view.Window.Callback original = window.getCallback();
        if (original == null) return;

        android.view.Window.Callback proxy = (android.view.Window.Callback)
                Proxy.newProxyInstance(
                        original.getClass().getClassLoader(),
                        new Class[]{android.view.Window.Callback.class},
                        new VolumeKeyCallback(original, activity));
        window.setCallback(proxy);
    }

    private boolean handleMouseTouchEvent(View v, MotionEvent event, ViewGroup rootView) {
        int action = event.getActionMasked();
        float x = event.getX(), y = event.getY();
        int deviceId = event.getDeviceId(), source = event.getSource();

        int button = 0;
        int bs = event.getButtonState();
        if ((bs & MotionEvent.BUTTON_SECONDARY)  != 0) button = 1;
        else if ((bs & MotionEvent.BUTTON_TERTIARY) != 0) button = 2;
        else if ((bs & MotionEvent.BUTTON_BACK)     != 0) button = 3;
        else if ((bs & MotionEvent.BUTTON_FORWARD)  != 0) button = 4;

        switch (action) {
            case MotionEvent.ACTION_DOWN:
                VulkanView.nativeOnMouseEvent(1, x, y, button, 0, 0, deviceId, source);
                return handleTouchEvent(v, event, rootView);
            case MotionEvent.ACTION_UP:
                VulkanView.nativeOnMouseEvent(2, x, y, button, 0, 0, deviceId, source);
                return handleTouchEvent(v, event, rootView);
            case MotionEvent.ACTION_MOVE:
                VulkanView.nativeOnMouseEvent(0, x, y, 0, 0, 0, deviceId, source);
                return handleTouchEvent(v, event, rootView);
            default:
                return handleTouchEvent(v, event, rootView);
        }
    }

    private boolean handleTouchEvent(View v, MotionEvent event, ViewGroup rootView) {
        int action = event.getActionMasked();
        int pointerIndex = event.getActionIndex();
        int pointerCount = event.getPointerCount();

        int[]   ids = new int[pointerCount];
        float[] xs  = new float[pointerCount];
        float[] ys  = new float[pointerCount];
        float[] pr  = new float[pointerCount];

        for (int i = 0; i < pointerCount; i++) {
            ids[i] = event.getPointerId(i);
            xs[i]  = event.getX(i);
            ys[i]  = event.getY(i);
            pr[i]  = event.getPressure(i);
        }

        VulkanView.nativeOnTouchEvent(action, pointerIndex, ids, xs, ys, pr, pointerCount);

        switch (action) {
            case MotionEvent.ACTION_DOWN: {
                int pid = event.getPointerId(pointerIndex);
                float x = event.getX(pointerIndex), y = event.getY(pointerIndex);
                boolean inImGui  = isImGuiComponentTouched(x, y);
                boolean inButton = isCircularButtonTouched(x, y);
                pointerInImGui[pid]  = inImGui;
                pointerInButton[pid] = inButton;
                if (inImGui || inButton) {
                    imguiPointerCount++;
                    if (inImGui) VulkanView.MotionEventClick(true, x, y);
                    return true;
                }
                return false;
            }

            case MotionEvent.ACTION_POINTER_DOWN: {
                int pid = event.getPointerId(pointerIndex);
                float x = event.getX(pointerIndex), y = event.getY(pointerIndex);
                boolean inImGui  = isImGuiComponentTouched(x, y);
                boolean inButton = isCircularButtonTouched(x, y);
                pointerInImGui[pid]  = inImGui;
                pointerInButton[pid] = inButton;
                if (inImGui || inButton) {
                    imguiPointerCount++;
                    if (inImGui) VulkanView.MotionEventClick(true, x, y);
                } else {
                    addGamePointer(pid);
                    dispatchGameEvent(event, MotionEvent.ACTION_DOWN, pid, rootView);
                }
                return imguiPointerCount > 0 || activeGamePointers.size() > 0;
            }

            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < pointerCount; i++) {
                    int pid = event.getPointerId(i);
                    if (pointerInImGui[pid])
                        VulkanView.MotionEventClick(true, event.getX(i), event.getY(i));
                }
                if (!activeGamePointers.isEmpty()) dispatchGameMoveEvent(event, rootView);
                return imguiPointerCount > 0 || !activeGamePointers.isEmpty();
            }

            case MotionEvent.ACTION_UP: {
                int pid = event.getPointerId(pointerIndex);
                float x = event.getX(pointerIndex), y = event.getY(pointerIndex);
                boolean wasImGui  = pointerInImGui[pid];
                boolean wasButton = pointerInButton[pid];
                if (wasImGui) { VulkanView.MotionEventClick(false, x, y); imguiPointerCount--; }
                else if (wasButton) { imguiPointerCount--; }
                else if (activeGamePointers.contains(pid)) {
                    dispatchGameEvent(event, MotionEvent.ACTION_UP, pid, rootView);
                    removeGamePointer(pid);
                }
                pointerInImGui[pid] = false; pointerInButton[pid] = false;
                if (imguiPointerCount == 0 && activeGamePointers.isEmpty()) gameDownTime = 0;
                return wasImGui || wasButton || !activeGamePointers.isEmpty();
            }

            case MotionEvent.ACTION_POINTER_UP: {
                int pid = event.getPointerId(pointerIndex);
                float x = event.getX(pointerIndex), y = event.getY(pointerIndex);
                boolean wasImGui  = pointerInImGui[pid];
                boolean wasButton = pointerInButton[pid];
                if (wasImGui) { VulkanView.MotionEventClick(false, x, y); imguiPointerCount--; }
                else if (wasButton) { imguiPointerCount--; }
                else if (activeGamePointers.contains(pid)) {
                    dispatchGameEvent(event, MotionEvent.ACTION_POINTER_UP, pid, rootView);
                    removeGamePointer(pid);
                }
                pointerInImGui[pid] = false; pointerInButton[pid] = false;
                return imguiPointerCount > 0 || !activeGamePointers.isEmpty();
            }

            case MotionEvent.ACTION_CANCEL: {
                for (int i = 0; i < pointerCount; i++) {
                    int pid = event.getPointerId(i);
                    if (pointerInImGui[pid])
                        VulkanView.MotionEventClick(false, event.getX(i), event.getY(i));
                    pointerInImGui[pid] = false; pointerInButton[pid] = false;
                }
                if (!activeGamePointers.isEmpty()) dispatchGameCancelEvent(event, rootView);
                imguiPointerCount = 0;
                gamePointerIdMap.clear(); activeGamePointers.clear(); gameDownTime = 0;
                return false;
            }
        }
        return false;
    }

    private void addGamePointer(int origId) {
        if (!activeGamePointers.contains(origId)) {
            gamePointerIdMap.put(origId, activeGamePointers.size());
            activeGamePointers.add(origId);
            if (activeGamePointers.size() == 1) gameDownTime = SystemClock.uptimeMillis();
        }
    }

    private void removeGamePointer(int origId) {
        activeGamePointers.remove(Integer.valueOf(origId));
        gamePointerIdMap.remove(origId);
        gamePointerIdMap.clear();
        for (int i = 0; i < activeGamePointers.size(); i++)
            gamePointerIdMap.put(activeGamePointers.get(i), i);
    }

    private void dispatchGameEvent(MotionEvent orig, int action, int targetId, ViewGroup root) {
        try {
            int gc = activeGamePointers.size();
            if (gc == 0) return;
            int ti = activeGamePointers.indexOf(targetId);
            if (ti == -1 && action == MotionEvent.ACTION_DOWN) ti = gc - 1;

            MotionEvent.PointerProperties[] pp = new MotionEvent.PointerProperties[gc];
            MotionEvent.PointerCoords[]     pc = new MotionEvent.PointerCoords[gc];
            for (int i = 0; i < gc; i++) {
                int oid = activeGamePointers.get(i);
                int oi  = findPointerIndex(orig, oid);
                pp[i] = new MotionEvent.PointerProperties();
                pc[i] = new MotionEvent.PointerCoords();
                if (oi >= 0) { orig.getPointerProperties(oi, pp[i]); orig.getPointerCoords(oi, pc[i]); }
                else { pp[i].id = oid; pp[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
                       pc[i].pressure = 1f; pc[i].size = 1f; }
            }

            int fa;
            if (action == MotionEvent.ACTION_DOWN) {
                fa = gc == 1 ? MotionEvent.ACTION_DOWN
                        : MotionEvent.ACTION_POINTER_DOWN | (ti << MotionEvent.ACTION_POINTER_INDEX_SHIFT);
            } else if (action == MotionEvent.ACTION_UP) {
                fa = gc == 1 ? MotionEvent.ACTION_UP
                        : MotionEvent.ACTION_POINTER_UP | (ti << MotionEvent.ACTION_POINTER_INDEX_SHIFT);
            } else if (action == MotionEvent.ACTION_POINTER_UP) {
                fa = MotionEvent.ACTION_POINTER_UP | (ti << MotionEvent.ACTION_POINTER_INDEX_SHIFT);
            } else { fa = action; }

            MotionEvent ne = MotionEvent.obtain(gameDownTime, orig.getEventTime(), fa, gc, pp, pc,
                    orig.getMetaState(), orig.getButtonState(), orig.getXPrecision(),
                    orig.getYPrecision(), orig.getDeviceId(), orig.getEdgeFlags(),
                    orig.getSource(), orig.getFlags());
            dispatchToChildren(root, ne);
            ne.recycle();
        } catch (Exception e) { Log.e(TAG, "dispatchGameEvent", e); }
    }

    private void dispatchGameMoveEvent(MotionEvent orig, ViewGroup root) {
        try {
            int gc = activeGamePointers.size();
            if (gc == 0) return;
            MotionEvent.PointerProperties[] pp = new MotionEvent.PointerProperties[gc];
            MotionEvent.PointerCoords[]     pc = new MotionEvent.PointerCoords[gc];
            for (int i = 0; i < gc; i++) {
                int oid = activeGamePointers.get(i);
                int oi  = findPointerIndex(orig, oid);
                pp[i] = new MotionEvent.PointerProperties();
                pc[i] = new MotionEvent.PointerCoords();
                if (oi >= 0) { orig.getPointerProperties(oi, pp[i]); orig.getPointerCoords(oi, pc[i]); pp[i].id = oid; }
                else { pp[i].id = oid; pp[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
                       pc[i].pressure = 1f; pc[i].size = 1f; }
            }
            MotionEvent ne = MotionEvent.obtain(gameDownTime, orig.getEventTime(),
                    MotionEvent.ACTION_MOVE, gc, pp, pc, orig.getMetaState(), orig.getButtonState(),
                    orig.getXPrecision(), orig.getYPrecision(), orig.getDeviceId(),
                    orig.getEdgeFlags(), orig.getSource(), orig.getFlags());
            dispatchToChildren(root, ne);
            ne.recycle();
        } catch (Exception e) { Log.e(TAG, "dispatchGameMoveEvent", e); }
    }

    private void dispatchGameCancelEvent(MotionEvent orig, ViewGroup root) {
        try {
            int gc = activeGamePointers.size();
            if (gc == 0) return;
            MotionEvent.PointerProperties[] pp = new MotionEvent.PointerProperties[gc];
            MotionEvent.PointerCoords[]     pc = new MotionEvent.PointerCoords[gc];
            for (int i = 0; i < gc; i++) {
                int oid = activeGamePointers.get(i);
                int oi  = findPointerIndex(orig, oid);
                pp[i] = new MotionEvent.PointerProperties();
                pc[i] = new MotionEvent.PointerCoords();
                if (oi >= 0) { orig.getPointerProperties(oi, pp[i]); orig.getPointerCoords(oi, pc[i]); }
                pp[i].id = i;
            }
            MotionEvent ne = MotionEvent.obtain(gameDownTime, orig.getEventTime(),
                    MotionEvent.ACTION_CANCEL, gc, pp, pc, orig.getMetaState(),
                    orig.getButtonState(), orig.getXPrecision(), orig.getYPrecision(),
                    orig.getDeviceId(), orig.getEdgeFlags(), orig.getSource(), orig.getFlags());
            dispatchToChildren(root, ne);
            ne.recycle();
        } catch (Exception e) { Log.e(TAG, "dispatchGameCancelEvent", e); }
    }

    private int findPointerIndex(MotionEvent event, int pointerId) {
        for (int i = 0; i < event.getPointerCount(); i++)
            if (event.getPointerId(i) == pointerId) return i;
        return -1;
    }

    private void dispatchToChildren(ViewGroup parent, MotionEvent event) {
        for (int i = parent.getChildCount() - 1; i >= 0; i--) {
            View child = parent.getChildAt(i);
            if (IMGUI_VIEW_TAG.equals(child.getTag())) continue;
            try {
                if (child.getVisibility() != View.VISIBLE) continue;
                float ox = child.getLeft(), oy = child.getTop();
                MotionEvent.PointerProperties[] pp = new MotionEvent.PointerProperties[event.getPointerCount()];
                MotionEvent.PointerCoords[]     pc = new MotionEvent.PointerCoords[event.getPointerCount()];
                for (int j = 0; j < event.getPointerCount(); j++) {
                    pp[j] = new MotionEvent.PointerProperties();
                    pc[j] = new MotionEvent.PointerCoords();
                    event.getPointerProperties(j, pp[j]);
                    event.getPointerCoords(j, pc[j]);
                    pc[j].x -= ox; pc[j].y -= oy;
                }
                MotionEvent ce = MotionEvent.obtain(event.getDownTime(), event.getEventTime(),
                        event.getAction(), event.getPointerCount(), pp, pc,
                        event.getMetaState(), event.getButtonState(), event.getXPrecision(),
                        event.getYPrecision(), event.getDeviceId(), event.getEdgeFlags(),
                        event.getSource(), event.getFlags());
                boolean handled = child.dispatchTouchEvent(ce);
                ce.recycle();
                if (handled) return;
            } catch (Exception e) { Log.e(TAG, "dispatchToChildren", e); }
        }
    }

    public static boolean isImGuiComponentTouched(float x, float y) {
        float[] bounds = nativeGetImGuiWindowBounds();
        if (bounds == null || bounds.length == 0) return false;
        int wc = bounds.length / 4;
        for (int i = 0; i < wc; i++) {
            int bi = i * 4;
            if (x >= bounds[bi] && x <= bounds[bi+2] && y >= bounds[bi+1] && y <= bounds[bi+3])
                return true;
        }
        return false;
    }

    public static boolean isCircularButtonTouched(float x, float y) {
        float[] bb = nativeGetCircularButtonBounds();
        if (bb == null || bb.length == 0) return false;
        int count = (int) bb[0];
        for (int i = 0; i < count; i++) {
            int bi = 1 + i * 3;
            if (bi + 2 >= bb.length) break;
            float dx = x - bb[bi], dy = y - bb[bi+1], r = bb[bi+2];
            if (dx*dx + dy*dy <= r*r) return true;
        }
        return false;
    }

    private static native float[] nativeGetImGuiWindowBounds();
    private static native float[] nativeGetCircularButtonBounds();
}