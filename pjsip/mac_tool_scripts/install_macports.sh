#!/bin/sh

#curl -O https://distfiles.macports.org/MacPorts/MacPorts-2.1.2.tar.bz2
#tar -xzvf MacPorts-2.1.2.tar.bz2
#cd MacPorts-2.1.2
#./configure
#make
#sudo make install
#cd .. && rm -fr MacPorts-2.1.2*

curl -O -L https://distfiles.macports.org/MacPorts/MacPorts-2.1.2-10.8-MountainLion.pkg
sudo installer -pkg MacPorts-2.1.2-10.8-MountainLion.pkg -target /

sudo port -v selfupdate
sudo port install gsed
sudo port install getopt
sudo port install yasm
