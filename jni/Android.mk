LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := ExplorationLite

LOCAL_SRC_FILES := \
    ../src/android_main.cpp \
    ../src/Assets.cpp \
    ../src/ApplicationBase.cpp \
    ../src/GLApplication.cpp \
    ../src/Game.cpp \
    ../src/GridData.cpp \
    ../src/BlockDef.cpp \
    ../src/Scene.cpp \
    ../src/Noise.cpp \
    ../src/TerrainGenerator.cpp \
    ../src/TreeBuilder.cpp \
    ../src/Stream.cpp \
    ../src/Entity.cpp \
    ../src/Camera.cpp \
    ../src/InputMgr.cpp \
    ../src/Mesh.cpp \
    ../src/Texture.cpp \
    ../src/Lighting.cpp \
    ../src/Clouds.cpp \
    ../src/Font.cpp \
    ../src/SkyBox.cpp

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../include
LOCAL_LDLIBS     := -llog -landroid -lEGL -lGLESv1_CM -lz
LOCAL_CFLAGS     := -DANDROID -O2
LOCAL_CPPFLAGS   := -std=c++11 -DANDROID -fno-exceptions -fno-rtti
LOCAL_STATIC_LIBRARIES := android_native_app_glue

include $(BUILD_SHARED_LIBRARY)
$(call import-module,android/native_app_glue)
