#!/usr/bin/env python

import os
import re
import sys

file_list = {
    'build.mak.in': 'export YARN_HAS_VIDEO := ',
    'yarn_video.sh': 'export YARN_HAS_VIDEO=',
    'yarn_video_pjsip_only_compile.sh': 'export YARN_HAS_VIDEO=',
    'pjlib/include/pj/config_site.h': '#define YARN_HAS_VIDEO ',
    'pjmedia/src/pjmedia-videodev/ios_dev.m' : '#define YARN_HAS_VIDEO ',
}

def main(argv):
    flag = argv[0].upper()
    if flag == 'TRUE' or flag == 'FALSE':
        old = '1' if flag == 'FALSE' else '0'
        new = '0' if flag == 'FALSE' else '1'
        for file, prefix in file_list.iteritems():
            lines = None
            with open(file, 'r') as f:
                lines = f.readlines()
            search = prefix + old
            print file
            print search
            for index, line in enumerate(lines):
                if line[:-1] == search:
                    lines[index] = prefix + new + '\n'
                    print u'switched to: \n', lines[index]
            with open(file, 'w') as f:
                f.writelines(lines)

if __name__ == "__main__":
    main(sys.argv[1:])
