with_msi=false
if host_machine.system() == 'windows'
  wixl = find_program('wixl', required: false)
  wixl_heat = find_program('wixl-heat', required: false)

  python3 = find_program('python3', required: true)

  if wixl.found() and wixl_heat.found() and python3.found()
    with_msi=true
  endif
endif

if with_msi
  msi_filename = 'usbredirect-@0@-@1@.msi'.format(wixl_arch, meson.project_version())

  wxsfile = configure_file(
    input: 'usbredirect.wxs.in',
    output: 'usbredirect.wxs',
    configuration: config
  )

  msi = custom_target(
    msi_filename,
    input: [wxsfile],
    output: [msi_filename],
    build_by_default: false,
    command: [
      python3,
      join_paths(meson.source_root(), 'build-aux', 'msitool.py'),
      meson.build_root(),
      get_option('prefix'),
      wixl_arch,
      join_paths(meson.build_root(), 'data', msi_filename),
      wxsfile,
      wixl_heat,
      wixl,
    ],
  )

endif
