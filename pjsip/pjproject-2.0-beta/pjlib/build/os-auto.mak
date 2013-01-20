# pjlib/build/os-auto.mak.  Generated from os-auto.mak.in by configure.

# Determine OS specific files
AC_OS_OBJS=ioqueue_select.o file_access_unistd.o file_io_ansi.o os_core_unix.o os_error_unix.o os_time_unix.o os_timestamp_posix.o os_info_iphone.o guid_simple.o os_core_darwin.o

#
# PJLIB_OBJS specified here are object files to be included in PJLIB
# (the library) for this specific operating system. Object files common 
# to all operating systems should go in Makefile instead.
#
export PJLIB_OBJS +=	$(AC_OS_OBJS) \
			addr_resolv_sock.o \
			log_writer_stdout.o \
			os_timestamp_common.o \
			pool_policy_malloc.o sock_bsd.o sock_select.o

#
# TEST_OBJS are operating system specific object files to be included in
# the test application.
#
export TEST_OBJS +=	main.o

#
# Additional LDFLAGS for pjlib-test
#
export TEST_LDFLAGS += -O3 -g -arch armv7 -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/openssl/lib -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/speex/libspeex -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/ffmpeg/libavcodec -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/ffmpeg/libavdevice -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/ffmpeg/libavformat -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/ffmpeg/libavutil -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/ffmpeg/libswresample -L/Users/ALTEN/iphone/kupidum/kupidum_third_party/pjsip/pjproject-2.0-beta/third_party/ffmpeg/libswscale -lspeex-arm-apple-darwin9 -lswresample -lswscale -lavdevice -lavformat -lavcodec -lavutil -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS6.0.sdk -framework CoreAudio -lpthread -lm -framework AudioToolbox -framework Foundation -framework AVFoundation -framework CoreVideo -framework CoreMedia -framework QuartzCore -framework OpenGLES -lbz2 -lz -lpthread  -framework CoreAudio -framework CoreFoundation -framework AudioToolbox -framework CFNetwork -framework UIKit -framework AVFoundation -framework UIKit -framework CoreGraphics -framework QuartzCore -framework CoreVideo -framework CoreMedia  -lavdevice -lavformat -lswscale -lavutil -lcrypto -lssl

#
# TARGETS are make targets in the Makefile, to be executed for this given
# operating system.
#
export TARGETS	    =	pjlib pjlib-test



