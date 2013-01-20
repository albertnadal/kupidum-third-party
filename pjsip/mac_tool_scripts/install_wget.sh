#!/bin/sh

curl -O http://ftp.gnu.org/gnu/wget/wget-1.13.4.tar.gz
tar -xzf wget-1.13.4.tar.gz
cd wget-1.13.4
./configure --with-ssl=openssl
make
sudo make install
wget --help
cd .. && rm -fr wget-1.13.4*

