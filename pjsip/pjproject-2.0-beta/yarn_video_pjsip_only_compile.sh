#!/bin/bash
#VIDEO ENABLED OR NOT
export YARN_HAS_VIDEO=1
###
#echo "Generating FFMPEG Libraries..."
#cd ./third_party/ffmpeg/
#./ffmpeg_hvc.sh
#cd ../../
#echo " done."
echo "Generating PJSIP Libraries..."
if test "$YARN_HAS_VIDEO" = "1"; then
./configure-iphone --disable-speex-aec
make clean && make distclean && make realclean
rm -f $(find . -name "*.depend")
./configure-iphone --disable-speex-aec
make dep && make clean && make
else
./configure-iphone --disable-speex-aec --disable-speex-codec --disable-ffmpeg
make clean && make distclean
./configure-iphone --disable-speex-aec --disable-speex-codec --disable-ffmpeg
make dep && make clean && make
fi
echo " done."


