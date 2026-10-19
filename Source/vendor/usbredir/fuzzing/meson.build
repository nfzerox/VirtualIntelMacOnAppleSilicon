add_languages('cpp', required: true)

cpp_compiler = meson.get_compiler('cpp')
cpp_compiler.has_header('fuzzer/FuzzedDataProvider.h', required: true)

link_args = []
link_with = []

fuzz_engine = get_option('fuzzing-engine')
fuzz_install_dir = get_option('fuzzing-install-dir')

if fuzz_engine == 'standalone'
    standalone_lib = static_library('standalone',
        sources: ['standalone.c'],
        install: false,
        )

    link_with += [standalone_lib]
else
    link_args += fuzz_engine.split()

    if not cpp_compiler.links('''
        #include <cstddef>
        #include <cstdint>
        extern "C" int LLVMFuzzerTestOneInput(const uint8_t *, size_t) {
          return 0;
        }
        ''',
        args: link_args,
        )
        error('Linking fuzzing test failed')
    endif
endif

fuzz_targets = {
    'usbredirparserfuzz': [usbredir_parser_lib_dep],
    'usbredirfilterfuzz': [usbredir_parser_lib_dep],
    }

foreach target_name, deps : fuzz_targets
    executable(target_name,
        sources: [target_name + '.cc'],
        dependencies: deps,
        link_language: 'cpp',
        link_args: link_args,
        link_with: link_with,
        install: (fuzz_install_dir != ''),
        install_dir: fuzz_install_dir,
        )

    if fuzz_install_dir != ''
        install_data('default.options',
            install_dir: fuzz_install_dir,
            rename: target_name + '.options',
            )
    endif
endforeach
