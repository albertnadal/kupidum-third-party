echo "FFMPEG Cleaning..."
make clean
make distclean
rm -f $(find . -name "*.d")
echo " done."
echo "Configuring ios sdk..."
IOS_SDK_PATH=$(xcodebuild -version -sdk | sed -n "s/^Path: \(.*iPhoneOS.*\)$/\1/p" | tail -1)
if [ -z "$IOS_SDK_PATH" ]; then 
  echo "Could not find a suitable ios sdk."
  exit 1
fi
echo "Using IOS_SDK_PATH=$IOS_SDK_PATH"
echo "Configuring FFMPEG..."
./configure --disable-doc --disable-ffplay --disable-ffprobe --disable-ffserver --disable-ffmpeg --disable-w32threads --disable-os2threads --enable-cross-compile  \
--disable-everything \
--enable-encoder=h261 \
--enable-encoder=h263 \
--enable-encoder=h263p \
--enable-decoder=vp8 \
--enable-decoder=h261 \
--enable-decoder=h263 \
--enable-decoder=h263i \
--enable-hwaccel=h263_vaapi \
--enable-muxer=h261 \
--enable-muxer=h263 \
--enable-muxer=webm \
--enable-demuxer=h261 \
--enable-demuxer=h263 \
--enable-demuxer=iv8 \
--enable-parser=h261 \
--enable-parser=h263 \
--enable-parser=vp8 \
--enable-filter=scale \
--enable-libvpx \
--arch=arm --target-os=darwin --cc=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/usr/bin/gcc --as='gas-preprocessor.pl /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/usr/bin/gcc' --sysroot=$IOS_SDK_PATH --extra-cflags="-O3 -g -w -arch armv7 -mfpu=neon -I$IOS_SDK_PATH/usr/include " --extra-ldflags="-arch armv7 -mfpu=neon -isysroot $IOS_SDK_PATH" --enable-pic --disable-asm 
echo " done."
echo "Compiling FFMPEG..."
make

