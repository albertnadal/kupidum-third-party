rm -rf sdk
mkdir sdk
mkdir sdk/libs
mkdir sdk/libs/iphoneos
mkdir sdk/libs/iphonesimulator
mkdir sdk/headers

cp -r pjlib/include sdk/headers/pjlib
cp -r pjlib-util/include sdk/headers/pjlib-util
cp -r pjmedia/include sdk/headers/pjmedia
cp -r pjnath/include sdk/headers/pjnath
cp -r pjsip/include sdk/headers/pjsip
find sdk -name '.svn' -print0 | xargs -0 rm -rf

export OPENSSL=/Users/tid/projects/openssl-1.0.0g

find . -name *.depend -print0 | xargs -0 rm -rf
export ARCH="-arch armv7"
export DEVPATH=/Developer/Platforms/iPhoneOS.platform/Developer
export CC=/Developer/Platforms/iPhoneOS.platform/Developer/usr/bin/llvm-gcc
export CFLAGS="-O2 -Wno-unused-label -I${OPENSSL}/include"
export LDFLAGS="-L${OPENSSL}/lib"
./configure-iphone
make dep && make clean && make

find . -name *.a -print0 | xargs -0 -I {} cp {} sdk/libs/iphoneos
cp $OPENSSL/bin/iPhoneOS5.0-armv7.sdk/lib/*.a sdk/libs/iphoneos

find . -name *.depend -print0 | xargs -0 rm -rf
export ARCH="-arch i386"
export DEVPATH=/Developer/Platforms/iPhoneSimulator.platform/Developer
export CC=/Developer/Platforms/iPhoneSimulator.platform/Developer/usr/bin/llvm-gcc
export CFLAGS="-O2 -m32 -miphoneos-version-min=5.0 -O2 -Wno-unused-label -I${OPENSSL}/include"
export LDFLAGS="-O2 -m32 -L${OPENSSL}/lib"
./configure-iphone
make dep && make clean && make

find . -name *.a -print0 | xargs -0 -I {} cp {} sdk/libs/iphonesimulator
cp $OPENSSL/bin/iPhoneSimulator5.0-i386.sdk/lib/*.a sdk/libs/iphonesimulator



