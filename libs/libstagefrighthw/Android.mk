LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	SprdOMXPlugin.cpp \
	SprdOMXComponent.cpp \
	SprdSimpleOMXComponent.cpp \
	SprdVideoDecoderOMXComponent.cpp \
	SprdVideoEncoderOMXComponent.cpp

LOCAL_CFLAGS := $(PV_CFLAGS_MINUS_VISIBILITY)

LOCAL_C_INCLUDES:= \
	frameworks/native/include/media/openmax \
	frameworks/native/include/media/hardware \
	$(LOCAL_PATH)/include \
	$(LOCAL_PATH)/../gralloc

LOCAL_SHARED_LIBRARIES := \
	libbinder \
	libutils \
	libcutils \
	libui \
	libdl \
	libhardware \
	libstagefright_foundation

LOCAL_MODULE := libstagefrighthw

include $(BUILD_SHARED_LIBRARY)
