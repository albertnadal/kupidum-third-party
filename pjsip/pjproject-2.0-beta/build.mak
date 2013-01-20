# build.mak.  Generated from build.mak.in by configure.
export MACHINE_NAME := auto
export OS_NAME := auto
export HOST_NAME := unix
export CC_NAME := gcc
export TARGET_NAME := arm-apple-darwin9
export CROSS_COMPILE := arm-apple-darwin9-
export LINUX_POLL := select 

export ac_prefix := /usr/local

# ADDED for SPECIFIC VIDEO COMPILATION
export YARN_HAS_VIDEO := 1

LIB_SUFFIX = $(TARGET_NAME).a

# Determine which party libraries to use
export APP_THIRD_PARTY_LIBS := -lresample-$(TARGET_NAME) -lmilenage-$(TARGET_NAME) -lsrtp-$(TARGET_NAME)
export APP_THIRD_PARTY_EXT :=
export APP_THIRD_PARTY_LIB_FILES = $(PJ_DIR)/third_party/lib/libresample-$(LIB_SUFFIX) $(PJ_DIR)/third_party/lib/libmilenage-$(LIB_SUFFIX) $(PJ_DIR)/third_party/lib/libsrtp-$(LIB_SUFFIX)

ifneq (,1)
ifeq (0,1)
# External GSM library
APP_THIRD_PARTY_EXT += -lgsm
else
APP_THIRD_PARTY_LIBS += -lgsmcodec-$(TARGET_NAME)
APP_THIRD_PARTY_LIB_FILES += $(PJ_DIR)/third_party/lib/libgsmcodec-$(LIB_SUFFIX)
endif
endif

ifneq (,1)
ifeq (0,1)
APP_THIRD_PARTY_EXT += -lspeex -lspeexdsp
else
APP_THIRD_PARTY_LIBS += -lspeex-$(TARGET_NAME)
APP_THIRD_PARTY_LIB_FILES += $(PJ_DIR)/third_party/lib/libspeex-$(LIB_SUFFIX)
endif
endif

ifneq (,1)
APP_THIRD_PARTY_LIBS += -lilbccodec-$(TARGET_NAME)
APP_THIRD_PARTY_LIB_FILES += $(PJ_DIR)/third_party/lib/libilbccodec-$(LIB_SUFFIX)
endif

ifneq (,1)
APP_THIRD_PARTY_LIBS += -lg7221codec-$(TARGET_NAME)
APP_THIRD_PARTY_LIB_FILES += $(PJ_DIR)/third_party/lib/libg7221codec-$(LIB_SUFFIX)
endif

ifneq ($(findstring pa,),)
ifeq (0,1)
# External PA
APP_THIRD_PARTY_EXT += -lportaudio
else
APP_THIRD_PARTY_LIBS += -lportaudio-$(TARGET_NAME)
APP_THIRD_PARTY_LIB_FILES += $(PJ_DIR)/third_party/lib/libportaudio-$(LIB_SUFFIX)
endif
endif

# Additional flags


#
# Video
# Note: there are duplicated macros in pjmedia/os-auto.mak.in (and that's not
#       good!

# SDL flags
SDL_CFLAGS = 
SDL_LDFLAGS = 

# FFMPEG dlags
FFMPEG_CFLAGS =  -DPJMEDIA_HAS_LIBAVDEVICE=1 -DPJMEDIA_HAS_LIBAVFORMAT=1 -DPJMEDIA_HAS_LIBSWSCALE=1 -DPJMEDIA_HAS_LIBAVUTIL=1 
FFMPEG_LDFLAGS =   -lavdevice -lavformat -lswscale -lavutil

# Video4Linux2
V4L2_CFLAGS = 
V4L2_LDFLAGS = 

# QT
AC_PJMEDIA_VIDEO_HAS_QT = 
QT_CFLAGS = 

# iOS
IOS_CFLAGS = -DPJMEDIA_VIDEO_DEV_HAS_IOS=1

# PJMEDIA features exclusion
PJ_VIDEO_CFLAGS += $(SDL_CFLAGS) $(FFMPEG_CFLAGS) $(V4L2_CFLAGS) $(QT_CFLAGS) \
		   $(IOS_CFLAGS)
PJ_VIDEO_LDFLAGS += $(SDL_LDFLAGS) $(FFMPEG_LDFLAGS) $(V4L2_LDFLAGS)


# CFLAGS, LDFLAGS, and LIBS to be used by applications
export PJDIR := /Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta
export APP_CC := /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/usr/bin/llvm-gcc
export APP_CXX := /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/usr/bin/llvm-g++
export APP_CFLAGS := -DPJ_AUTOCONF=1\
	-O3 -g -Wno-unused-label -I/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/ffmpeg -I/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/openssl/include -I/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/speex/libspeex -DPJ_SDK_NAME="\"iPhoneOS6.0.sdk\"" -arch armv7 -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS6.0.sdk -DPJ_IS_BIG_ENDIAN=0 -DPJ_IS_LITTLE_ENDIAN=1\
	$(PJ_VIDEO_CFLAGS) \
	-I$(PJDIR)/pjlib/include\
	-I$(PJDIR)/pjlib-util/include\
	-I$(PJDIR)/pjnath/include\
	-I$(PJDIR)/pjmedia/include\
	-I$(PJDIR)/pjsip/include
export APP_CXXFLAGS := $(APP_CFLAGS)
export APP_LDFLAGS := -L$(PJDIR)/pjlib/lib\
	-L$(PJDIR)/pjlib-util/lib\
	-L$(PJDIR)/pjnath/lib\
	-L$(PJDIR)/pjmedia/lib\
	-L$(PJDIR)/pjsip/lib\
	-L$(PJDIR)/third_party/lib\
	$(PJ_VIDEO_LDFLAGS) \
	-O3 -g -arch armv7 -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/openssl/lib -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/speex/libspeex -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/ffmpeg/libavcodec -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/ffmpeg/libavdevice -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/ffmpeg/libavformat -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/ffmpeg/libavutil -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/ffmpeg/libswresample -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/ffmpeg/libswscale -lspeex-arm-apple-darwin9 -lswresample -lswscale -lavdevice -lavformat -lavcodec -lavutil -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS6.0.sdk -framework CoreAudio -lpthread -lm -framework AudioToolbox -framework Foundation -framework AVFoundation -framework CoreVideo -framework CoreMedia -framework QuartzCore -framework OpenGLES
export APP_LDLIBS := -lpjsua-$(TARGET_NAME)\
	-lpjsip-ua-$(TARGET_NAME)\
	-lpjsip-simple-$(TARGET_NAME)\
	-lpjsip-$(TARGET_NAME)\
	-lpjmedia-codec-$(TARGET_NAME)\
	-lpjmedia-videodev-$(TARGET_NAME)\
	-lpjmedia-$(TARGET_NAME)\
	-lpjmedia-audiodev-$(TARGET_NAME)\
	-lpjnath-$(TARGET_NAME)\
	-lpjlib-util-$(TARGET_NAME)\
	$(APP_THIRD_PARTY_LIBS)\
	$(APP_THIRD_PARTY_EXT)\
	-lpj-$(TARGET_NAME)\
	-lbz2 -lz -lpthread  -framework CoreAudio -framework CoreFoundation -framework AudioToolbox -framework CFNetwork -framework UIKit -framework AVFoundation -framework UIKit -framework CoreGraphics -framework QuartzCore -framework CoreVideo -framework CoreMedia  -lavdevice -lavformat -lswscale -lavutil -lcrypto -lssl
export APP_LIB_FILES = $(PJ_DIR)/pjsip/lib/libpjsua-$(LIB_SUFFIX) \
	$(PJ_DIR)/pjsip/lib/libpjsip-ua-$(LIB_SUFFIX) \
	$(PJ_DIR)/pjsip/lib/libpjsip-simple-$(LIB_SUFFIX) \
	$(PJ_DIR)/pjsip/lib/libpjsip-$(LIB_SUFFIX) \
	$(PJ_DIR)/pjmedia/lib/libpjmedia-codec-$(LIB_SUFFIX) \
	$(PJ_DIR)/pjmedia/lib/libpjmedia-videodev-$(LIB_SUFFIX) \
	$(PJ_DIR)/pjmedia/lib/libpjmedia-$(LIB_SUFFIX) \
	$(PJ_DIR)/pjmedia/lib/libpjmedia-audiodev-$(LIB_SUFFIX) \
	$(PJ_DIR)/pjnath/lib/libpjnath-$(LIB_SUFFIX) \
	$(PJ_DIR)/pjlib-util/lib/libpjlib-util-$(LIB_SUFFIX) \
	$(APP_THIRD_PARTY_LIB_FILES) \
	$(PJ_DIR)/pjlib/lib/libpj-$(LIB_SUFFIX)

# Here are the variabels to use if application is using the library
# from within the source distribution
export PJ_DIR := $(PJDIR)
export PJ_CC := $(APP_CC)
export PJ_CXX := $(APP_CXX)
export PJ_CFLAGS := $(APP_CFLAGS)
export PJ_CXXFLAGS := $(APP_CXXFLAGS)
export PJ_LDFLAGS := $(APP_LDFLAGS)
export PJ_LDLIBS := $(APP_LDLIBS)
export PJ_LIB_FILES := $(APP_LIB_FILES)

# And here are the variables to use if application is using the
# library from the install location (i.e. --prefix)
export PJ_INSTALL_DIR := /usr/local
export PJ_INSTALL_INC_DIR := $(PJ_INSTALL_DIR)/include
export PJ_INSTALL_LIB_DIR := $(PJ_INSTALL_DIR)/lib
export PJ_INSTALL_CFLAGS := -I$(PJ_INSTALL_INC_DIR) -DPJ_AUTOCONF=1	-O3 -g -Wno-unused-label -I/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/ffmpeg -I/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/openssl/include -I/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/speex/libspeex -DPJ_SDK_NAME="\"iPhoneOS6.0.sdk\"" -arch armv7 -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS6.0.sdk -DPJ_IS_BIG_ENDIAN=0 -DPJ_IS_LITTLE_ENDIAN=1
export PJ_INSTALL_CXXFLAGS := $(PJ_INSTALL_CFLAGS)
export PJ_INSTALL_LDFLAGS := -L$(PJ_INSTALL_LIB_DIR) $(APP_LDLIBS)
