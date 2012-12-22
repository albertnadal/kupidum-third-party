# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'variables': {
    'conditions': [
      ['os_posix==1', {
        'asm_obj_extension': 'o',
      }],
      ['OS=="win"', {
        'asm_obj_extension': 'obj',
      }],
    ],
  },
  'conditions': [
    [ 'target_arch != "arm"', {
      'targets': [
        {
          # This libvpx target contains both encoder and decoder.
          # Encoder is configured to be realtime only.
          'target_name': 'libvpx',
          'type': 'static_library',
          'variables': {
            'yasm_output_path': '<(SHARED_INTERMEDIATE_DIR)/third_party/libvpx',
            'variables': {
              'conditions': [
                # Reuse linux config for unsupported platforms like BSD and
                # Solaris.  This compiles but no guarantee it'll run.
                ['os_posix == 1 and OS != "mac"', {
                  'OS_CATEGORY%': 'linux',
                }, {
                  'OS_CATEGORY%': '<(OS)',
                }],
              ]},
            'OS_CATEGORY%': '<(OS_CATEGORY)',
            'yasm_flags': [
              '-I', 'source/config/<(OS_CATEGORY)/<(target_arch)',
              '-I', 'source/libvpx',
            ],
          },
          'includes': [
            '../yasm/yasm_compile.gypi'
          ],
          'include_dirs': [
            'source/config/<(OS_CATEGORY)/<(target_arch)',
            'source/libvpx',
            'source/libvpx/vp8/common',
            'source/libvpx/vp8/decoder',
            'source/libvpx/vp8/encoder',
          ],
          'direct_dependent_settings': {
            'include_dirs': [
              'source/libvpx',
            ],
          },
          # VS2010 does not correctly incrementally link obj files generated
          # from asm files. This flag disables UseLibraryDependencyInputs to
          # avoid this problem.
          'msvs_2010_disable_uldi_when_referenced': 1,
          'conditions': [
            [ 'target_arch=="ia32"', {
              'includes': [
                'libvpx_srcs_x86.gypi',
              ],
            }],
            [ 'target_arch=="x64"', {
              'includes': [
                'libvpx_srcs_x86_64.gypi',
              ],
            }],
            ['clang == 1', {
              'xcode_settings': {
                'WARNING_CFLAGS': [
                  # libvpx heavily relies on implicit enum casting.
                  '-Wno-conversion',
                  # libvpx does `if ((a == b))` in some places.
                  '-Wno-parentheses-equality',
                ],
              },
              'cflags': [
                '-Wno-conversion',
                '-Wno-parentheses-equality',
              ],
            }],
          ],
        },
      ],
    },
    ],
    # 'libvpx' target for ARM builds.
    [ 'target_arch=="arm" ', {
      'targets': [
        {
          # This libvpx target contains both encoder and decoder.
          # Encoder is configured to be realtime only.
          'target_name': 'libvpx',
          'type': 'static_library',

          # Copy the script to the output folder so that we can use it with
          # absolute path.
          'copies': [{
            'destination': '<(shared_generated_dir)',
            'files': [
              '<(ads2gas_script_path)',
            ],
          }],

          # Rule to convert .asm files to .S files.
          'rules': [
            {
              'rule_name': 'convert_asm',
              'extension': 'asm',
              'inputs': [ '<(shared_generated_dir)/<(ads2gas_script)', ],
              'outputs': [
                '<(shared_generated_dir)/<(RULE_INPUT_ROOT).S',
              ],
              'action': [
                'bash',
                '-c',
                'cat <(RULE_INPUT_PATH) | perl <(shared_generated_dir)/<(ads2gas_script) > <(shared_generated_dir)/<(RULE_INPUT_ROOT).S',
              ],
              'process_outputs_as_sources': 1,
              'message': 'Convert libvpx asm file for ARM <(RULE_INPUT_PATH).',
            },
          ],

          'variables': {
            # Location of the assembly conversion script.
            'ads2gas_script': 'ads2gas.pl',
            'ads2gas_script_path': 'source/libvpx/build/make/<(ads2gas_script)',

            # Location of the intermediate output.
            'shared_generated_dir': '<(SHARED_INTERMEDIATE_DIR)/third_party/libvpx',

            # Conditions to generate arm-neon as an target.
            'conditions': [
              ['target_arch=="arm" and arm_neon==1', {
                'target_arch_full': 'arm-neon',
              }, {
                'target_arch_full': '<(target_arch)',
              }],
              ['OS=="android"', {
                'OS_CATEGORY': 'linux',
              }, {
                'OS_CATEGORY': '<(OS)',
              }],
            ],
          },
          'cflags': [
            # We need to explicitly tell the GCC assembler to look for
            # .include directive files from the place where they're
            # generated to.
            '-Wa,-I,<!(pwd)/source/config/<(OS_CATEGORY)/<(target_arch_full)',
          ],
          'include_dirs': [
            'source/config/<(OS_CATEGORY)/<(target_arch_full)',
            'source/libvpx',
          ],
          'direct_dependent_settings': {
            'include_dirs': [
              'source/libvpx',
            ],
          },
          'conditions': [
            # Libvpx optimizations for ARMv6 or ARMv7 without NEON.
            ['arm_neon==0', {
              'includes': [
                'libvpx_srcs_arm.gypi',
              ],
            }],
            # Libvpx optimizations for ARMv7 with NEON.
            ['arm_neon==1', {
              'includes': [
                'libvpx_srcs_arm_neon.gypi',
              ],
            }],
            ['OS == "android"', {
              'include_dirs': [
                '<(android_ndk_include)',
                '<(android_ndk_include)/machine',
              ],
              'defines': [
                'ANDROID_CPU_ARM_FEATURE_NEON=4',
              ],
            }],
          ],
        },
      ],
    }],
  ],
  'targets': [
    {
      'target_name': 'simple_encoder',
      'type': 'executable',
      'dependencies': [
        'libvpx',
        '../../base/base.gyp:base',
      ],

      'variables': {
        # Location of the intermediate output.
        'shared_generated_dir': '<(SHARED_INTERMEDIATE_DIR)/third_party/libvpx',
      },

      # Copy the script to the output folder so that we can use it with
      # absolute path.
      'copies': [{
        'destination': '<(shared_generated_dir)/simple_encoder',
        'files': [
          'source/libvpx/examples/gen_example_code.sh',
        ],
      }],

      # Rule to convert .txt files to .c files.
      'rules': [
        {
          'rule_name': 'generate_example',
          'extension': 'txt',
          'inputs': [ '<(shared_generated_dir)/simple_encoder/gen_example_code.sh', ],
          'outputs': [
            '<(shared_generated_dir)/<(RULE_INPUT_ROOT).c',
          ],
          'action': [
            'bash',
            '-c',
            '<(shared_generated_dir)/simple_encoder/gen_example_code.sh <(RULE_INPUT_PATH) > <(shared_generated_dir)/<(RULE_INPUT_ROOT).c',
          ],
          'process_outputs_as_sources': 1,
          'message': 'Generate libvpx example code <(RULE_INPUT_PATH).',
        },
      ],
      'sources': [
        'source/libvpx/examples/simple_encoder.txt',
      ]
    },
    {
      'target_name': 'simple_decoder',
      'type': 'executable',
      'dependencies': [
        'libvpx',
        '../../base/base.gyp:base',
      ],

      'variables': {
        # Location of the intermediate output.
        'shared_generated_dir': '<(SHARED_INTERMEDIATE_DIR)/third_party/libvpx',
      },

      # Copy the script to the output folder so that we can use it with
      # absolute path.
      'copies': [{
        'destination': '<(shared_generated_dir)/simple_decoder',
        'files': [
          'source/libvpx/examples/gen_example_code.sh',
        ],
      }],

      # Rule to convert .txt files to .c files.
      'rules': [
        {
          'rule_name': 'generate_example',
          'extension': 'txt',
          'inputs': [ '<(shared_generated_dir)/simple_decoder/gen_example_code.sh', ],
          'outputs': [
            '<(shared_generated_dir)/<(RULE_INPUT_ROOT).c',
          ],
          'action': [
            'bash',
            '-c',
            '<(shared_generated_dir)/simple_decoder/gen_example_code.sh <(RULE_INPUT_PATH) > <(shared_generated_dir)/<(RULE_INPUT_ROOT).c',
          ],
          'process_outputs_as_sources': 1,
          'message': 'Generate libvpx example code <(RULE_INPUT_PATH).',
        },
      ],
      'sources': [
        'source/libvpx/examples/simple_decoder.txt',
      ]
    },
    # TODO(hclam): Remove these targets once webrtc doesn't depend on them.
    {
      'target_name': 'libvpx_include',
      'type': 'none',
      'direct_dependent_settings': {
        'include_dirs': [
          'source/libvpx',
        ],
      },
    },
    {
      'target_name': 'libvpx_lib',
      'type': 'none',
      'dependencies': [
        'libvpx',
      ],
    },
  ],
}

# Local Variables:
# tab-width:2
# indent-tabs-mode:nil
# End:
# vim: set expandtab tabstop=2 shiftwidth=2:
