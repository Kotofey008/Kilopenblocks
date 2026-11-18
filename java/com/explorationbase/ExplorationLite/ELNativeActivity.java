package com.explorationbase.ExplorationLite;

import android.app.NativeActivity;
import android.os.Bundle;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

/**
 * Thin NativeActivity subclass that applies immersive sticky fullscreen on the
 * UI thread (status + navigation bars hidden). Game logic stays pure native
 * in libExplorationLite.so — this class only handles system chrome.
 *
 * Not present in the original binary; added for modern Android UX.
 */
public class ELNativeActivity extends NativeActivity {
    private static final int UI_FLAGS =
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE
          | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
          | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
          | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
          | View.SYSTEM_UI_FLAG_FULLSCREEN
          | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;

    private void applyImmersive() {
        Window w = getWindow();
        if (w == null) return;
        w.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        View decor = w.getDecorView();
        if (decor != null)
            decor.setSystemUiVisibility(UI_FLAGS);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        applyImmersive();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        // Re-apply after the user swipes bars into view (immersive sticky).
        if (hasFocus)
            applyImmersive();
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyImmersive();
    }
}
