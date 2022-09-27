project(
    'hello-rdma',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++14',
    ],
)

# options

if get_option('use_hugepage').enabled()
    add_project_arguments('-DUSE_HUGEPAGE', language: 'cpp')
endif

mode = get_option('mode')
if mode == 'poll'
    add_project_arguments('-DUSE_POLL', language: 'cpp')
elif mode == 'notify'
    add_project_arguments('-DUSE_NOTIFY', language: 'cpp')
else
    error('mode is not specified')
endif

# sub-projects
protobuf = subproject('protobuf', version: '=3.20.1')
rapidjson = subproject('rapidjson', version: '=1.1.0')

# dependencies

deps = [
    dependency('libevent'),
    dependency('libevent_pthreads'),
    dependency('libibverbs'),
    dependency('librdmacm'),
    dependency('threads'),
    protobuf.get_variable('protobuf_dep'),
    rapidjson.get_variable('rapidjson_dep'),
]

# sources

project_header_files = []
project_source_files = []

# include directories

incs = [
    include_directories('include/rdma'),
    include_directories('include/util'),
]

# program

protoc = find_program('./bin/protoc', required: false)
if not protoc.found()
    aarch = run_command('uname', '-m', capture: true, check: true)
    if aarch.stdout().startswith('x86_64')
        url = 'https://github.com/protocolbuffers/protobuf/releases/download/v3.20.1/protoc-3.20.1-linux-x86_64.zip'
    elif aarch.stdout().startswith('aarch64')
        url = 'https://github.com/protocolbuffers/protobuf/releases/download/v3.20.1/protoc-3.20.1-linux-aarch_64.zip'
    else
        error('unknown architecture: ' + aarch.stdout())
    endif
    run_command('wget', '-O', 'protoc.zip', url, check: true)
    run_command('unzip', 'protoc.zip', 'bin/protoc', check: true)
    run_command('chmod', '+x', './bin/protoc', check: true)
    protoc = find_program('./bin/protoc', required: true, version: '=3.20.1')
endif

# sub-directories

subdir('include')
subdir('src')
subdir('app')
subdir('bench')

# declare dependency
declare_dependency(
    link_with: [rdma_rpc_lib],
    include_directories: include_directories('include'),
)

# package config

pkg = import('pkgconfig')
pkg.generate(rdma_rpc_lib, description: 'RPC over RDMA')
