// Reimplementation of android_main based on Rizin disassembly
// Evidence: sym.android_main @ 0x43470 — Console::SetWriter, GLApplication ctor, AndroidMain

#include "GLApplication.h"
#include <android/log.h>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "ExplorationLite", __VA_ARGS__))

extern "C" void android_main(struct android_app* state) {
    app_dummy();
    LOGI("android_main entered (reimpl)");
    GLApplication* app = new GLApplication(0, 0);
    ApplicationBase::SetGLApp(app);
    ApplicationBase::AndroidMain(state);
    ApplicationBase::SetGLApp(nullptr);
    delete app;
}

extern "C" void ANativeActivity_onCreate(ANativeActivity* activity,
                                         void* savedState, size_t savedStateSize) {
    (void)activity; (void)savedState; (void)savedStateSize;
    LOGI("ANativeActivity_onCreate");
}
