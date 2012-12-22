
#define YARN_HAS_VIDEO 1

#if YARN_HAS_VIDEO
#define PJMEDIA_HAS_VIDEO 1 //Si es == 1 es penja al fer el call_make_call
#define PJMEDIA_HAS_FFMPEG 1
#define PJMEDIA_VIDEO_DEV_HAS_SDL 0
//#define PJMEDIA_AUDIO_DEV_HAS_LEGACY_DEVICE 1
#else
#define PJMEDIA_HAS_VIDEO 0
#endif

#define PJ_IPHONE_OS_HAS_MULTITASKING_SUPPORT 1

//to be further checked inside config_site_sample.h first.
#define PJ_CONFIG_IPHONE 1
#include <pj/config_site_sample.h>

#if YARN_HAS_VIDEO

#define TARGET_OS_IPHONE 1
#define PJMEDIA_AUDIO_DEV_HAS_COREAUDIO 1
#define PJMEDIA_HAS_SPEEX_CODEC 0
//do nothing

#else
#define PJMEDIA_AUDIO_DEV_HAS_COREAUDIO 0
#endif


