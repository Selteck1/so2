LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := lemmingrmt
LOCAL_SRC_FILES := src/core/entry.cpp \
    third_party/imgui/imgui.cpp \
    third_party/imgui/imgui_draw.cpp \
    third_party/imgui/imgui_tables.cpp \
    third_party/imgui/imgui_widgets.cpp \
    third_party/imgui/backends/imgui_impl_opengl3.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/src $(LOCAL_PATH)/third_party/imgui $(LOCAL_PATH)/third_party/imgui/backends
LOCAL_CPPFLAGS := -DIMGUI_IMPL_OPENGL_ES3 -std=c++17 -O2 -ffunction-sections -fdata-sections -fvisibility=hidden -Wall -Wextra
LOCAL_LDFLAGS := -Wl,--gc-sections
LOCAL_LDLIBS := -llog -lEGL -lGLESv3 -landroid -ldl
include $(BUILD_SHARED_LIBRARY)
