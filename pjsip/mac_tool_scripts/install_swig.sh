#!/bin/sh

curl -O -L http://downloads.sourceforge.net/project/swig/swig/swig-2.0.8/swig-2.0.8.tar.gz
tar -xzvf swig-2.0.8.tar.gz
cd swig-2.0.8
curl -O ftp://ftp.csx.cam.ac.uk/pub/software/programming/pcre/pcre-8.31.tar.gz
./Tools/pcre-build.sh
./configure
make
sudo make install
cd .. && rm -fr swig-2.0.8*
sudo ln -s /usr/local/bin/swig /opt/local/bin/swig2.0

