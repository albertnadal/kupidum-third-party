ROOT_FOLDER=`pwd`
SDK=$ROOT_FOLDER/sdk_2.0-beta_0.1
OPENSSL=/Users/ggb/projects/openssl-1.0.0g
WEBRTC=/Users/ggb/projects/webrtc/trunk

#rm -rf $SDK
mkdir -p $SDK/libs/Release-iphoneos
mkdir -p $SDK/libs/Release-iphonesimulator
mkdir -p $SDK/headers/pjlib
mkdir -p $SDK/headers/pjlib-util
mkdir -p $SDK/headers/pjmedia
mkdir -p $SDK/headers/pjnath
mkdir -p $SDK/headers/pjsip

find . -name *.depend -print0 | xargs -0 rm -rf
export ARCH="-arch armv7"
export DEVPATH=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer
export CC=$DEVPATH/usr/bin/llvm-gcc
export CXX=$DEVPATH/usr/bin/llvm-g++
export CFLAGS="-O2 -Wno-unused-label -I${OPENSSL}/include -I${WEBRTC}/src"
export LDFLAGS="-L${OPENSSL}/lib -L${WEBRTC}/xcodebuild/Release-iphoneos -lvoice_engine_core -laudio_coding_module -lCNG -lsystem_wrappers -liSACFix -lG711 -liLBC -lvad -lNetEq -laudio_device -lmedia_file -laudio_processing -lresampler -lrtp_rtcp -lns -lagc -lsignal_processing -laec -laecm -lG722 -lapm_util -lPCM16B -laudio_conference_mixer -ludp_transport -lwebrtc_utility"
export LD=$DEVPATH/usr/bin/llvm-g++
./configure-iphone
make dep && make clean && make

find pj* -name *.a -print0 | xargs -0 -I {} cp {} $SDK/libs/Release-iphoneos
find third_party/* -name *.a -print0 | xargs -0 -I {} cp {} $SDK/libs/Release-iphoneos
cp $OPENSSL/bin/iPhoneOS5.0-armv7.sdk/lib/*.a $SDK/libs/Release-iphoneos
cp ${WEBRTC}/xcodebuild/Release-iphoneos/*.a $SDK/libs/Release-iphoneos

cd pjlib/include && find . \( -name "*.h" -o -name "*.hpp" \) -exec pax -rw {} $SDK/headers/pjlib \; && cd $ROOT_FOLDER
cd pjlib-util/include && find . \( -name "*.h" -o -name "*.hpp" \) -exec pax -rw {} $SDK/headers/pjlib-util \; && cd $ROOT_FOLDER
cd pjmedia/include && find . \( -name "*.h" -o -name "*.hpp" \) -exec pax -rw {} $SDK/headers/pjmedia \; && cd $ROOT_FOLDER
cd pjnath/include && find . \( -name "*.h" -o -name "*.hpp" \) -exec pax -rw {} $SDK/headers/pjnath \; && cd $ROOT_FOLDER
cd pjsip/include && find . \( -name "*.h" -o -name "*.hpp" \) -exec pax -rw {} $SDK/headers/pjsip \; && cd $ROOT_FOLDER

find . -name *.depend -print0 | xargs -0 rm -rf
export ARCH="-arch i386"
export DEVPATH=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer
export CC=$DEVPATH/usr/bin/llvm-gcc
export CXX=$DEVPATH/usr/bin/llvm-g++
export CFLAGS="-O2 -m32 -miphoneos-version-min=5.0 -O2 -Wno-unused-label -I${OPENSSL}/include -I${WEBRTC}/src"
export LDFLAGS="-L${OPENSSL}/lib -L${WEBRTC}/xcodebuild/Release-iphonesimulator -lvoice_engine_core -laudio_coding_module -lCNG -lsystem_wrappers -liSACFix -lG711 -liLBC -lvad -lNetEq -laudio_device -lmedia_file -laudio_processing -lresampler -lrtp_rtcp -lns -lagc -lsignal_processing -laec -laecm -lG722 -lapm_util -lPCM16B -laudio_conference_mixer -ludp_transport -lwebrtc_utility"
export LD=$DEVPATH/usr/bin/llvm-g++
./configure-iphone
make dep && make clean && make

find pj* -name *.a -print0 | xargs -0 -I {} cp {} $SDK/libs/Release-iphonesimulator
find third_party/* -name *.a -print0 | xargs -0 -I {} cp {} $SDK/libs/Release-iphonesimulator
cp $OPENSSL/bin/iPhoneSimulator5.0-i386.sdk/lib/*.a $SDK/libs/Release-iphonesimulator
cp ${WEBRTC}/xcodebuild/Release-iphonesimulator/*.a $SDK/libs/Release-iphonesimulator



