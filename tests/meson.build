tests = [
  'test-tuple',
  'test-mjpeg-request',
  'test-authenticator',
]

foreach t: tests
  installed_test = '@0@.test'.format(t)

  exe = executable(
    t, '@0@.c'.format(t),
    c_args: '-DG_LOG_DOMAIN="g2-tests"',
    include_directories: gaeul_incs,
    dependencies: [
      libgaeul_dep,
      libgaeul_mjpeg_dep,
      hwangsae_dep,
      hwangsae_test_common_dep ],
    install: false,
  )

  env = environment()
  env.set('G_TEST_SRCDIR', meson.current_source_dir())
  env.set('G_TEST_BUILDDIR', meson.current_build_dir())

  test(
    t, exe,
    args: [ '--tap', '-k' ],
    env: env,
    timeout: 120,
    is_parallel: false
  )
endforeach

debugenv = environment()
add_test_setup('debug', env: debugenv)
