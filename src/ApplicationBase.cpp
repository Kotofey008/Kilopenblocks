// Reconstructed from Rizin disassembly of ApplicationBase::AndroidMain @ 0x44400
#include "ApplicationBase.h"
#include "GLApplication.h"
#include <android/log.h>
#include <android/native_window.h>
#include <jni.h>
#include <EGL/egl.h>
#include <GLES/gl.h>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "ExplorationLite", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "ExplorationLite", __VA_ARGS__))

static GLApplication* g_glApp = nullptr;

void ApplicationBase::SetGLApp(GLApplication* app) { g_glApp = app; }

// Immersive sticky fullscreen (not in original binary — modern Android UX).
// Hides status bar + navigation bar; bars re-hide after transient swipe.
static void EnableImmersiveFullscreen(struct android_app* app) {
    if (!app || !app->activity) return;
    JavaVM* vm = app->activity->vm;
    jobject activity = app->activity->clazz;
    if (!vm || !activity) return;

    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env)
            return;
        attached = true;
    }

    // View.SYSTEM_UI_FLAG_* constants
    const int FLAG_LAYOUT_STABLE = 0x00000100;
    const int FLAG_LAYOUT_HIDE_NAV = 0x00000200;
    const int FLAG_LAYOUT_FULLSCREEN = 0x00000400;
    const int FLAG_HIDE_NAV = 0x00000002;
    const int FLAG_FULLSCREEN = 0x00000004;
    const int FLAG_IMMERSIVE_STICKY = 0x00001000;
    const int flags = FLAG_LAYOUT_STABLE | FLAG_LAYOUT_HIDE_NAV | FLAG_LAYOUT_FULLSCREEN
                    | FLAG_HIDE_NAV | FLAG_FULLSCREEN | FLAG_IMMERSIVE_STICKY;

    jclass activityClass = env->GetObjectClass(activity);
    if (!activityClass) {
        if (attached) vm->DetachCurrentThread();
        return;
    }
    jmethodID getWindow = env->GetMethodID(activityClass, "getWindow", "()Landroid/view/Window;");
    if (!getWindow) {
        env->DeleteLocalRef(activityClass);
        if (attached) vm->DetachCurrentThread();
        return;
    }
    jobject window = env->CallObjectMethod(activity, getWindow);
    if (!window) {
        env->DeleteLocalRef(activityClass);
        if (attached) vm->DetachCurrentThread();
        return;
    }
    jclass windowClass = env->GetObjectClass(window);
    jmethodID getDecor = env->GetMethodID(windowClass, "getDecorView", "()Landroid/view/View;");
    jobject decor = getDecor ? env->CallObjectMethod(window, getDecor) : nullptr;
    if (decor) {
        jclass viewClass = env->GetObjectClass(decor);
        jmethodID setFlags = env->GetMethodID(viewClass, "setSystemUiVisibility", "(I)V");
        if (setFlags)
            env->CallVoidMethod(decor, setFlags, flags);
        // Also clear translucent bars that leave a transparent nav strip on some OEMs
        jmethodID addFlags = env->GetMethodID(windowClass, "addFlags", "(I)V");
        // WindowManager.LayoutParams.FLAG_FULLSCREEN = 0x400
        if (addFlags)
            env->CallVoidMethod(window, addFlags, 0x00000400);
        env->DeleteLocalRef(viewClass);
        env->DeleteLocalRef(decor);
    }
    env->DeleteLocalRef(windowClass);
    env->DeleteLocalRef(window);
    env->DeleteLocalRef(activityClass);
    if (attached) vm->DetachCurrentThread();
    LOGI("immersive fullscreen flags applied");
}

ApplicationBase::ApplicationBase(bool, int)
    : mApp(nullptr), mDisplay(EGL_NO_DISPLAY), mSurface(EGL_NO_SURFACE),
      mContext(EGL_NO_CONTEXT), mRunning(false) {
}

ApplicationBase::~ApplicationBase() {
    OnDeleteGLContext();
}

void ApplicationBase::OnCreateGLContext() {}
void ApplicationBase::OnDeleteGLContext() {
    if (mDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (mContext != EGL_NO_CONTEXT) {
            eglDestroyContext(mDisplay, mContext);
            mContext = EGL_NO_CONTEXT;
        }
        if (mSurface != EGL_NO_SURFACE) {
            eglDestroySurface(mDisplay, mSurface);
            mSurface = EGL_NO_SURFACE;
        }
        eglTerminate(mDisplay);
        mDisplay = EGL_NO_DISPLAY;
    }
}

void ApplicationBase::ProcessInput(struct android_app* app, struct android_poll_source* source) {
    if (source) source->process(app, source);
}

void ApplicationBase::OnAppCmd(struct android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                mApp = app;
                EnableImmersiveFullscreen(app);
                OnCreateGLContext();
                mRunning = true;
            }
            break;
        case APP_CMD_TERM_WINDOW:
            mRunning = false;
            OnDeleteGLContext();
            break;
        case APP_CMD_GAINED_FOCUS:
            // Re-apply after notification shade / nav gesture briefly shows bars
            EnableImmersiveFullscreen(app);
            mRunning = true;
            break;
        case APP_CMD_LOST_FOCUS:
            break;
        default:
            break;
    }
}

static void on_app_cmd(struct android_app* app, int32_t cmd) {
    if (g_glApp) g_glApp->OnAppCmd(app, cmd);
    else if (app->userData)
        static_cast<ApplicationBase*>(app->userData)->OnAppCmd(app, cmd);
}

static int32_t on_input_event(struct android_app* app, AInputEvent* event) {
    if (!g_glApp) return 0;
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int action = AMotionEvent_getAction(event);
        int masked = action & AMOTION_EVENT_ACTION_MASK;
        int idx = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                  >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        InputMgr& in = g_glApp->GetGame().GetInput();
        if (masked == AMOTION_EVENT_ACTION_MOVE) {
            // MOVE must update every active pointer (not only index 0)
            int n = AMotionEvent_getPointerCount(event);
            for (int i = 0; i < n; ++i) {
                int id = AMotionEvent_getPointerId(event, i);
                int x = (int)AMotionEvent_getX(event, i);
                int y = (int)AMotionEvent_getY(event, i);
                in.TouchMoveEvent((unsigned)id, x, y);
                g_glApp->GetGame().HandleTouchMove(x, y);
            }
        } else {
            int id = AMotionEvent_getPointerId(event, idx);
            int x = (int)AMotionEvent_getX(event, idx);
            int y = (int)AMotionEvent_getY(event, idx);
            if (masked == AMOTION_EVENT_ACTION_DOWN || masked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
                in.TouchPressEvent((unsigned)id, x, y);
                g_glApp->GetGame().HandleTouchPress(x, y);
            }
            else if (masked == AMOTION_EVENT_ACTION_UP || masked == AMOTION_EVENT_ACTION_POINTER_UP
                     || masked == AMOTION_EVENT_ACTION_CANCEL) {
                in.TouchReleaseEvent((unsigned)id, x, y);
                g_glApp->GetGame().HandleTouchRelease(x, y);
            }
        }
        return 1;
    }
    return 0;
}

void ApplicationBase::AndroidMain(struct android_app* state) {
    // Observed main loop: poll events, process, render while not destroyed
    state->onAppCmd = on_app_cmd;
    state->onInputEvent = on_input_event;
    state->userData = g_glApp;

    while (true) {
        int events;
        struct android_poll_source* source;
        while (ALooper_pollAll(g_glApp && g_glApp->mRunning ? 0 : -1,
                               nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(state, source);
            if (state->destroyRequested) return;
        }
        if (g_glApp && g_glApp->mRunning)
            g_glApp->Frame();
    }
}

int32_t ApplicationBase::OnInputEvent(struct android_app*, AInputEvent*) { return 0; }
