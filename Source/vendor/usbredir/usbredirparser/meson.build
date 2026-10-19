# so verison, see:
# http://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
usbredir_parser_current = 3
usbredir_parser_revision = 1
usbredir_parser_age = 2
usbredir_parser_so_version = '@0@.@1@.@2@'.format(
    usbredir_parser_current - usbredir_parser_age,
    usbredir_parser_age,
    usbredir_parser_revision)

summary_info += {'libusbredirparser.so version': usbredir_parser_so_version}

usbredir_parser_sources = [
    'usbredirparser.c',
    'usbredirfilter.c',
    'usbredirproto-compat.h',
    'usbredirparser.h',
    'usbredirfilter.h',
    'usbredirproto.h',
]

if host_machine.system() == 'windows'
    usbredir_parser_sources += [
        'strtok_r.c',
        'strtok_r.h',
    ]
endif

usbredir_parser_map_file = meson.current_source_dir() / 'usbredirparser.map'
usbredir_parser_link_args = compiler.get_supported_link_arguments([
    '-Wl,--version-script=@0@'.format(usbredir_parser_map_file),
    '-Wl,--no-undefined',
    '-Wl,-dead_strip',
])

usbredir_parser_include_directories = [
    include_directories('.'),
    usbredir_include_root_dir,
]

usbredir_parser_lib = library('usbredirparser',
    usbredir_parser_sources,
    version : usbredir_parser_so_version,
    install : true,
    include_directories: usbredir_parser_include_directories,
    link_args : [usbredir_parser_link_args],
    link_depends : usbredir_parser_map_file,
    gnu_symbol_visibility : 'hidden')

usbredir_parser_lib_dep = declare_dependency(
    link_with: usbredir_parser_lib,
    include_directories: usbredir_parser_include_directories)

headers = [
   'usbredirfilter.h',
   'usbredirparser.h',
   'usbredirproto.h'
]

install_headers(headers)

# libusbredirparser.pc
pkgconfig = import('pkgconfig')
pkgconfig.generate(usbredir_parser_lib,
                   name : 'libusbredirparser-0.5',
                   description : 'usbredirparser library',
                   variables : 'exec_prefix=${prefix}')
