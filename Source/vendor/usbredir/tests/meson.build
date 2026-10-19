tests = [
    'filter',
    'serializer',
]

deps = dependency('glib-2.0')

foreach t: tests
    source = t + '.c'
    runtime = 'test-' + t
    exe = executable(runtime,
        [source],
        install: false,
        dependencies: [deps, usbredir_parser_lib_dep])
    test(runtime, exe, timeout:10)
endforeach
