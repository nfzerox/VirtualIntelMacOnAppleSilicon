project('usbredir', 'c',
    version: '0.15.0',
    license: 'LGPLv2.1+',
    meson_version : '>= 0.53',
    default_options : [
        'buildtype=debugoptimized',
        'warning_level=1',
    ])

summary_info = {'prefix': get_option('prefix')}

usbredir_include_root_dir = include_directories('.')

cc_flags = [
    '--param=ssp-buffer-size=4',
]
if host_machine.system() != 'windows'
    cc_flags += [ '-D_FORTIFY_SOURCE=2' ]
    if not get_option('stack_protector').disabled()
        cc_flags += [ '-fstack-protector' ]
    endif
endif

# Check if we are building from .git
git = run_command('test', '-d', '.git', check : false).returncode() == 0
git_werror = get_option('git_werror')
if git_werror.enabled() or git_werror.auto() and git
  cc_flags += [ '-Werror' ]
endif

compiler = meson.get_compiler('c')
supported_cc_flags = compiler.get_supported_arguments(cc_flags)
add_project_arguments(supported_cc_flags, language: 'c')

config = configuration_data()

if get_option('extra-checks')
  config.set('ENABLE_EXTRA_CHECKS', '1')
endif

config.set('USBREDIR_VISIBLE', '')
foreach visibility : [
    '__attribute__((visibility ("default")))',
    '__attribute__((dllexport))',
    '__declspec(dllexport)',
]
    code = '@0@ int func() { return 123; }'.format(visibility)
    if compiler.compiles(code, name : 'visibility check')
	config.set('USBREDIR_VISIBLE', visibility)
	break
    endif
endforeach

#
# write config.h
#
proj_name = meson.project_name()
proj_version = meson.project_version()
config_data = {
    'VERSION' : proj_version,
    'PACKAGE_VERSION' : proj_version,
    'PACKAGE_STRING' : '@0@ @1@'.format(proj_name, proj_version),
    'PACKAGE_BUGREPORT' : 'https://gitlab.freedesktop.org/spice/usbredir/issues',
}

foreach key, value : config_data
    config.set_quoted(key, value)
endforeach

#
# check for system headers
#
headers = [
    'inttypes.h',
    'stdint.h',
    'stdlib.h',
    'strings.h',
    'string.h',
    'sys/stat.h',
    'sys/types.h',
    'unistd.h',
]

foreach header : headers
    if compiler.has_header(header)
        config.set('HAVE_@0@'.format(header.underscorify().to_upper()), '1')
    endif
endforeach

if host_machine.system() == 'windows'
    wixl_arch = 'x64'
    if host_machine.cpu() != 'x86_64'
        wixl_arch = 'x86'
    endif
    config.set('WIXL_ARCH', wixl_arch)
endif

configure_file(output : 'config.h', configuration : config)

subdir('usbredirparser')
subdir('usbredirhost')
if get_option('tools').enabled()
    subdir('tools')
endif
if host_machine.system() != 'windows'
    subdir('usbredirtestclient')

    if get_option('fuzzing').enabled()
        subdir('fuzzing')
    endif
endif
if get_option('tests').enabled()
    subdir('tests')
endif
subdir('data')

summary(summary_info, bool_yn: true)
