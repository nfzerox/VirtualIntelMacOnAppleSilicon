usbredirect_sources = [
    'usbredirect.c',
]

usbredirect_deps = [usbredir_host_lib_dep]

glib_version = '>= 2.44'
deps = {'glib-2.0': glib_version}

if host_machine.system() == 'windows'
    deps += {'gio-windows-2.0': glib_version}
else
    deps += {'gio-unix-2.0': glib_version}
endif

foreach dep, version : deps
    usbredirect_deps += dependency(dep, version : version)
endforeach

executable('usbredirect',
    sources : usbredirect_sources,
    c_args : '-Wno-deprecated-declarations',
    install : true,
    dependencies : usbredirect_deps)

install_man('usbredirect.1')
