gclient config http://webrtc.googlecode.com/svn/trunk
gclient sync --force

sed -i '.orig' -e 's/10.5/5.0/g' -e 's/macosx\</iphoneos\</g' trunk/build/common.gypi

#src/build/common.gypi 'enable_protobuf%': 0 
#'enable_video%': 0 
#include_internal_audio_device

gclient runhooks --force

