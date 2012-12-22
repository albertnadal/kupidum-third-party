#!/bin/sh

curl -O -L http://download.savannah.gnu.org/releases/quilt/quilt-0.60.tar.gz
tar -xzvf quilt-0.60.tar.gz
cd quilt-0.60
./configure --without-date --with-sed=/opt/local/bin/gsed --with-getopt=/opt/local/bin/getopt
make
sudo make install
cd .. && rm -fr quilt-0.60*

