// android_main — entry called by android_native_app_glue on its app thread.
// Do NOT define ANativeActivity_onCreate here; the glue provides it and
// would be overridden, leaving the activity with no event loop (black screen).

#include "GLApplication.h"
#include <android_native_app_glue.h>
#include <android/log.h>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "ExplorationLite", __VA_ARGS__))

extern "C" void android_main(struct android_app* state) {
    LOGI("android_main entered");
    GLApplication* app = new GLApplication(0, 0);
    ApplicationBase::SetGLApp(app);
    app->AndroidMain(state);  // instance method uses this for EGL fields
    ApplicationBase::SetGLApp(nullptr);
    delete app;
    LOGI("android_main exit");
}
