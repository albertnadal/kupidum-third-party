#!/bin/bash
#
# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script is used to modify libvpx assembly source files to add
# PRIVATE directive for global symbols.
# Run this script after source/libvpx has been replaced with the latest
# source.

# Patch
echo "<<EOF
Index: source/libvpx/vpx_ports/x86_abi_support.asm
===================================================================
--- source/libvpx/vpx_ports/x86_abi_support.asm (revision 107119)
+++ source/libvpx/vpx_ports/x86_abi_support.asm (working copy)
@@ -92,6 +92,26 @@
 %define sym(x) _ %+ x
 %endif
 
+;  PRIVATE
+;  Macro for the attribute to hide a global symbol for the target ABI.
+;
+;  Chromium doesn't like exported global symbols due to symbol clashing with
+;  plugins among other things.
+;
+;  Requires Chromium's patched copy of yasm:
+;    http://src.chromium.org/viewvc/chrome?view=rev&revision=73761
+;    http://www.tortall.net/projects/yasm/ticket/236
+;
+%ifidn   __OUTPUT_FORMAT__,elf32
+%define PRIVATE :hidden
+%elifidn __OUTPUT_FORMAT__,elf64
+%define PRIVATE :hidden
+%elifidn __OUTPUT_FORMAT__,x64
+%define PRIVATE
+%else
+%define PRIVATE :private_extern
+%endif
+
 ; arg()
 ; Return the address specification of the given argument
 ;
@@ -179,7 +199,12 @@
     %endmacro
   %endif
   %endif
-  %define HIDDEN_DATA(x) x
+
+  %ifidn __OUTPUT_FORMAT__,macho32
+    %define HIDDEN_DATA(x) x:private_extern
+  %else
+    %define HIDDEN_DATA(x) x
+  %endif
 %else
   %macro GET_GOT 1
   %endmacro
EOF" | patch -p0

# Add PRIVATE directive to all assembly functions.
find source/libvpx -type f -name '*.asm' | xargs -i sed -i -E 's/^\s*global\s+sym\((.*)\)\s*$/global sym(\1) PRIVATE/' {}
