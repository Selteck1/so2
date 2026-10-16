#pragma once

#include <android/log.h>

#define XIM_TAG "Ximira"

// XERR всегда активен — критично для дебага на устройстве
#define XERR(...)  __android_log_print(ANDROID_LOG_ERROR, XIM_TAG, __VA_ARGS__)

#if defined(XIM_DEBUG) && XIM_DEBUG
#define XLOG(...)  __android_log_print(ANDROID_LOG_INFO,  XIM_TAG, __VA_ARGS__)
#define XWARN(...) __android_log_print(ANDROID_LOG_WARN,  XIM_TAG, __VA_ARGS__)
#else
#define XLOG(...)  ((void)0)
#define XWARN(...) ((void)0)
#endif