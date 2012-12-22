cd CSipSimple
#patch -N -p0 < ../patchs/complete.diff.patch
patch -N -p0 < ../patchs/newversion.patch
make ext-sources swig-glue

