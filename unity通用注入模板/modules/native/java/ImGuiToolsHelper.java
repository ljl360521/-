package com.example.imgui;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.PopupWindow;

import java.lang.ref.WeakReference;

/**
 * ImGui 工具辅助类
 * 提供打开网页（应用内WebView / 外部浏览器）等功能
 * 可扩展更多工具方法
 */
public class ImGuiToolsHelper {
    private static final String TAG = "ImGuiToolsHelper";

    private static WeakReference<Activity> activityRef;
    private static PopupWindow webViewPopup;
    private static WebView webView;
    private static volatile boolean isShowing = false;
    private static boolean initialized = false;

    public static void init(Activity activity) {
        if (activity == null) return;
        activityRef = new WeakReference<>(activity);
        initialized = true;
        Log.i(TAG, "ImGuiToolsHelper initialized");
    }

    /**
     * 打开网页
     * @param url      网页地址
     * @param internal true=应用内WebView, false=外部浏览器
     */
    public static void openURL(String url, boolean internal) {
        if (url == null || url.isEmpty()) {
            Log.e(TAG, "URL is null or empty");
            return;
        }

        if (!url.startsWith("http://") && !url.startsWith("https://")) {
            url = "https://" + url;
        }

        final String finalUrl = url;

        if (internal) {
            runOnUI(() -> openInternalWebView(finalUrl));
        } else {
            runOnUI(() -> openExternalBrowser(finalUrl));
        }
    }

    /**
     * 外部浏览器打开
     */
    private static void openExternalBrowser(String url) {
        try {
            Activity activity = getActivity();
            if (activity == null) return;

            Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            activity.startActivity(intent);
            Log.i(TAG, "Opened in external browser: " + url);
        } catch (Exception e) {
            Log.e(TAG, "Failed to open external browser", e);
        }
    }

    /**
     * 应用内 WebView 打开（使用 PopupWindow 覆盖在 GLSurfaceView 上方）
     */
    private static void openInternalWebView(String url) {
        try {
            Activity activity = getActivity();
            if (activity == null) return;

            // 如果已经在显示，直接加载新 URL
            if (isShowing && webViewPopup != null && webViewPopup.isShowing() && webView != null) {
                webView.loadUrl(url);
                Log.i(TAG, "WebView already showing, loading new URL: " + url);
                return;
            }

            destroyWebViewPopup();

            LinearLayout rootLayout = new LinearLayout(activity);
            rootLayout.setOrientation(LinearLayout.VERTICAL);
            rootLayout.setBackgroundColor(Color.WHITE);

            LinearLayout toolbar = createToolbar(activity, url);
            rootLayout.addView(toolbar, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, dpToPx(activity, 48)));

            ProgressBar progressBar = new ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal);
            progressBar.setMax(100);
            progressBar.setProgress(0);
            progressBar.setVisibility(View.GONE);
            LinearLayout.LayoutParams progressParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, dpToPx(activity, 3));
            rootLayout.addView(progressBar, progressParams);

            webView = new WebView(activity);
            setupWebView(webView, progressBar);
            rootLayout.addView(webView, new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f));

            webViewPopup = new PopupWindow(rootLayout,
                    WindowManager.LayoutParams.MATCH_PARENT,
                    WindowManager.LayoutParams.MATCH_PARENT,
                    true);
            webViewPopup.setBackgroundDrawable(null);
            webViewPopup.setClippingEnabled(false);
            webViewPopup.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);

            webViewPopup.setOnDismissListener(() -> {
                isShowing = false;
                cleanupWebView();
                Log.i(TAG, "WebView popup dismissed");
            });

            View anchor = activity.getWindow().getDecorView();
            anchor.post(() -> {
                try {
                    if (activity.isFinishing()) return;
                    webViewPopup.showAtLocation(anchor, Gravity.CENTER, 0, 0);
                    isShowing = true;
                    webView.loadUrl(url);
                    Log.i(TAG, "WebView popup shown, loading: " + url);
                } catch (Exception e) {
                    Log.e(TAG, "Failed to show WebView popup", e);
                }
            });

        } catch (Exception e) {
            Log.e(TAG, "Failed to open internal WebView", e);
        }
    }

    private static LinearLayout createToolbar(Activity activity, String url) {
        LinearLayout toolbar = new LinearLayout(activity);
        toolbar.setOrientation(LinearLayout.HORIZONTAL);
        toolbar.setBackgroundColor(0xFF2D2D2D);
        toolbar.setGravity(Gravity.CENTER_VERTICAL);
        toolbar.setPadding(dpToPx(activity, 8), 0, dpToPx(activity, 8), 0);

        TextView closeBtn = new TextView(activity);
        closeBtn.setText("✕");
        closeBtn.setTextColor(Color.WHITE);
        closeBtn.setTextSize(20);
        closeBtn.setPadding(dpToPx(activity, 12), 0, dpToPx(activity, 12), 0);
        closeBtn.setGravity(Gravity.CENTER);
        closeBtn.setOnClickListener(v -> closeWebView());
        toolbar.addView(closeBtn, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.MATCH_PARENT));

        TextView backBtn = new TextView(activity);
        backBtn.setText("◀");
        backBtn.setTextColor(Color.WHITE);
        backBtn.setTextSize(16);
        backBtn.setPadding(dpToPx(activity, 8), 0, dpToPx(activity, 8), 0);
        backBtn.setGravity(Gravity.CENTER);
        backBtn.setOnClickListener(v -> {
            if (webView != null && webView.canGoBack()) {
                webView.goBack();
            }
        });
        toolbar.addView(backBtn, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.MATCH_PARENT));

        TextView forwardBtn = new TextView(activity);
        forwardBtn.setText("▶");
        forwardBtn.setTextColor(Color.WHITE);
        forwardBtn.setTextSize(16);
        forwardBtn.setPadding(dpToPx(activity, 8), 0, dpToPx(activity, 8), 0);
        forwardBtn.setGravity(Gravity.CENTER);
        forwardBtn.setOnClickListener(v -> {
            if (webView != null && webView.canGoForward()) {
                webView.goForward();
            }
        });
        toolbar.addView(forwardBtn, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.MATCH_PARENT));

        TextView urlText = new TextView(activity);
        urlText.setText(url);
        urlText.setTextColor(0xFFCCCCCC);
        urlText.setTextSize(12);
        urlText.setSingleLine(true);
        urlText.setEllipsize(android.text.TextUtils.TruncateAt.MIDDLE);
        urlText.setPadding(dpToPx(activity, 8), 0, dpToPx(activity, 8), 0);
        urlText.setTag("url_text");
        toolbar.addView(urlText, new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.MATCH_PARENT, 1.0f));

        TextView refreshBtn = new TextView(activity);
        refreshBtn.setText("⟳");
        refreshBtn.setTextColor(Color.WHITE);
        refreshBtn.setTextSize(20);
        refreshBtn.setPadding(dpToPx(activity, 12), 0, dpToPx(activity, 12), 0);
        refreshBtn.setGravity(Gravity.CENTER);
        refreshBtn.setOnClickListener(v -> {
            if (webView != null) webView.reload();
        });
        toolbar.addView(refreshBtn, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.MATCH_PARENT));

        TextView externalBtn = new TextView(activity);
        externalBtn.setText("↗");
        externalBtn.setTextColor(Color.WHITE);
        externalBtn.setTextSize(20);
        externalBtn.setPadding(dpToPx(activity, 12), 0, dpToPx(activity, 12), 0);
        externalBtn.setGravity(Gravity.CENTER);
        externalBtn.setOnClickListener(v -> {
            if (webView != null) {
                String currentUrl = webView.getUrl();
                if (currentUrl != null) {
                    openExternalBrowser(currentUrl);
                }
            }
        });
        toolbar.addView(externalBtn, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.MATCH_PARENT));

        return toolbar;
    }

    /**
     * 配置 WebView
     */
    private static void setupWebView(WebView wv, ProgressBar progressBar) {
        WebSettings settings = wv.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setBuiltInZoomControls(true);
        settings.setDisplayZoomControls(false);
        settings.setUseWideViewPort(true);
        settings.setLoadWithOverviewMode(true);
        settings.setSupportZoom(true);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_ALWAYS_ALLOW);
        settings.setCacheMode(WebSettings.LOAD_DEFAULT);
        settings.setAllowFileAccess(false);
        settings.setUserAgentString(settings.getUserAgentString() + " ImGuiWebView");

        wv.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                String url = request.getUrl().toString();

                if (url.startsWith("http://") || url.startsWith("https://")) {
                    return false;
                }

                try {
                    Activity activity = getActivity();
                    if (activity != null) {
                        Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
                        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                        activity.startActivity(intent);
                    }
                } catch (Exception e) {
                    Log.w(TAG, "No app to handle scheme: " + url);
                }
                return true;
            }
        
            @Override
            public void onPageStarted(WebView view, String url, Bitmap favicon) {
                super.onPageStarted(view, url, favicon);
                progressBar.setVisibility(View.VISIBLE);
                progressBar.setProgress(0);
                updateToolbarUrl(view, url);
            }
        
            @Override
            public void onPageFinished(WebView view, String url) {
                super.onPageFinished(view, url);
                progressBar.setVisibility(View.GONE);
            }
        
            @Override
            public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
                super.onReceivedError(view, request, error);
                if (request.isForMainFrame()) {
                    Log.e(TAG, "WebView error: " + error.getDescription());
                }
            }
        });

        wv.setWebChromeClient(new WebChromeClient() {
            @Override
            public void onProgressChanged(WebView view, int newProgress) {
                progressBar.setProgress(newProgress);
                if (newProgress >= 100) {
                    progressBar.setVisibility(View.GONE);
                }
            }

            @Override
            public void onReceivedTitle(WebView view, String title) {
                super.onReceivedTitle(view, title);
                Log.d(TAG, "Page title: " + title);
            }
        });
    }

    /**
     * 更新工具栏中的 URL 显示
     */
    private static void updateToolbarUrl(WebView wv, String url) {
        try {
            ViewGroup parent = (ViewGroup) wv.getParent();
            if (parent != null) {
                View urlText = parent.findViewWithTag("url_text");
                if (urlText instanceof TextView) {
                    ((TextView) urlText).setText(url);
                }
            }
        } catch (Exception e) {
        
        }
    }

    /**
     * 关闭应用内 WebView
     */
    public static void closeWebView() {
        runOnUI(() -> {
            try {
                destroyWebViewPopup();
                Log.i(TAG, "WebView closed");
            } catch (Exception e) {
                Log.e(TAG, "Error closing WebView", e);
            }
        });
    }

    private static void destroyWebViewPopup() {
        isShowing = false;
        if (webView != null) {
            webView.stopLoading();
            webView.loadUrl("about:blank");
        }
        if (webViewPopup != null && webViewPopup.isShowing()) {
            webViewPopup.dismiss();
        }
        cleanupWebView();
        webViewPopup = null;
    }

    private static void cleanupWebView() {
        if (webView != null) {
            webView.stopLoading();
            webView.setWebViewClient(null);
            webView.setWebChromeClient(null);
            ViewGroup parent = (ViewGroup) webView.getParent();
            if (parent != null) {
                parent.removeView(webView);
            }
            webView.destroy();
            webView = null;
        }
    }

    /**
     * WebView 是否正在显示
     */
    public static boolean isWebViewVisible() {
        return isShowing && webViewPopup != null && webViewPopup.isShowing();
    }

    private static Activity getActivity() {
        return activityRef != null ? activityRef.get() : null;
    }

    private static void runOnUI(Runnable action) {
        if (Looper.getMainLooper() == Looper.myLooper()) {
            action.run();
        } else {
            new Handler(Looper.getMainLooper()).post(action);
        }
    }

    private static int dpToPx(Activity activity, int dp) {
        float density = activity.getResources().getDisplayMetrics().density;
        return Math.round(dp * density);
    }

    /**
     * 销毁，清理所有资源
     */
    public static void destroy() {
        runOnUI(() -> {
            destroyWebViewPopup();
            if (activityRef != null) activityRef.clear();
            initialized = false;
            Log.i(TAG, "ImGuiToolsHelper destroyed");
        });
    }
}