#pragma once
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES/gl.h>

class GLApplication;

class ApplicationBase {
public:
    ApplicationBase(bool, int);
    virtual ~ApplicationBase();
    void AndroidMain(struct android_app* app);
    static void SetGLApp(GLApplication* app);
    virtual void OnCreateGLContext();
    virtual void OnDeleteGLContext();
    void ProcessInput(struct android_app*, struct android_poll_source*);
    void OnAppCmd(struct android_app*, int32_t);
    int32_t OnInputEvent(struct android_app*, AInputEvent*);

    bool mRunning = false;
    struct android_app* mApp = nullptr;
    EGLDisplay mDisplay = EGL_NO_DISPLAY;
    EGLSurface mSurface = EGL_NO_SURFACE;
    EGLContext mContext = EGL_NO_CONTEXT;
};
