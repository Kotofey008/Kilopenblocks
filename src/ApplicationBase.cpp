// Reconstructed from Rizin disassembly of ApplicationBase::AndroidMain @ 0x44400
#include "ApplicationBase.h"
#include "GLApplication.h"
#include <android/log.h>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <GLES/gl.h>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "ExplorationLite", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "ExplorationLite", __VA_ARGS__))

static GLApplication* g_glApp = nullptr;

void ApplicationBase::SetGLApp(GLApplication* app) { g_glApp = app; }

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
                OnCreateGLContext();
                mRunning = true;
            }
            break;
        case APP_CMD_TERM_WINDOW:
            mRunning = false;
            OnDeleteGLContext();
            break;
        case APP_CMD_GAINED_FOCUS:
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
    // Forward touches to InputMgr
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int action = AMotionEvent_getAction(event);
        int masked = action & AMOTION_EVENT_ACTION_MASK;
        int idx = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                  >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        int id = AMotionEvent_getPointerId(event, idx);
        int x = (int)AMotionEvent_getX(event, idx);
        int y = (int)AMotionEvent_getY(event, idx);
        InputMgr& in = g_glApp->GetGame().GetInput();
        if (masked == AMOTION_EVENT_ACTION_DOWN || masked == AMOTION_EVENT_ACTION_POINTER_DOWN)
            in.TouchPressEvent((unsigned)id, x, y);
        else if (masked == AMOTION_EVENT_ACTION_MOVE)
            in.TouchMoveEvent((unsigned)id, x, y);
        else if (masked == AMOTION_EVENT_ACTION_UP || masked == AMOTION_EVENT_ACTION_POINTER_UP)
            in.TouchReleaseEvent((unsigned)id, x, y);
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
