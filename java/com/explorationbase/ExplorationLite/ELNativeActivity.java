package com.explorationbase.ExplorationLite;

import android.app.NativeActivity;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

/**
 * Thin NativeActivity subclass: immersive sticky + edge-to-edge so status and
 * navigation bars stay hidden. Game logic remains pure native in
 * libExplorationLite.so — this class only handles system chrome.
 *
 * Not present in the original binary; added for modern Android UX.
 *
 * API 30+ InsetsController is invoked via reflection so this file still
 * compiles against android-28 (workflow platform jar).
 */
public class ELNativeActivity extends NativeActivity {

    private static final int UI_FLAGS =
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
          | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
          | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
          | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
          | View.SYSTEM_UI_FLAG_FULLSCREEN
          | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
          | View.SYSTEM_UI_FLAG_LOW_PROFILE;

    private void applyImmersive() {
        final Window w = getWindow();
        if (w == null) return;

        w.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        w.addFlags(WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);
        w.clearFlags(WindowManager.LayoutParams.FLAG_FORCE_NOT_FULLSCREEN);
        w.clearFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS);
        w.clearFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_NAVIGATION);

        if (Build.VERSION.SDK_INT >= 21) {
            w.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
            try {
                w.setStatusBarColor(Color.TRANSPARENT);
                w.setNavigationBarColor(Color.TRANSPARENT);
            } catch (Throwable ignored) {}
        }

        // API 30+: Window.setDecorFitsSystemWindows(false) + InsetsController.hide
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                Window.class.getMethod("setDecorFitsSystemWindows", boolean.class)
                        .invoke(w, Boolean.FALSE);
                Object controller = Window.class.getMethod("getInsetsController").invoke(w);
                if (controller != null) {
                    Class<?> typeClass = Class.forName("android.view.WindowInsets$Type");
                    int bars = ((Integer) typeClass.getMethod("statusBars").invoke(null)).intValue()
                             | ((Integer) typeClass.getMethod("navigationBars").invoke(null)).intValue();
                    controller.getClass().getMethod("hide", int.class).invoke(controller, Integer.valueOf(bars));
                    // BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE = 2
                    controller.getClass()
                            .getMethod("setSystemBarsBehavior", int.class)
                            .invoke(controller, Integer.valueOf(2));
                }
            } catch (Throwable ignored) {
                // Fall through to classic flags
            }
        }

        final View decor = w.getDecorView();
        if (decor != null) {
            decor.setSystemUiVisibility(UI_FLAGS);
            decor.setOnSystemUiVisibilityChangeListener(new View.OnSystemUiVisibilityChangeListener() {
                @Override
                public void onSystemUiVisibilityChange(int visibility) {
                    if ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
                        decor.setSystemUiVisibility(UI_FLAGS);
                    }
                }
            });
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        super.onCreate(savedInstanceState);
        applyImmersive();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus)
            applyImmersive();
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyImmersive();
    }
}
