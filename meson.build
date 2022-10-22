project(
    'hello-rdma',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++14',
        'buildtype=debug',
        'warning_level=3',
    ],
)

# options

if get_option('use_hugepage').enabled()
    add_project_arguments('-DUSE_HUGEPAGE', language: 'cpp')
endif

# sub-projects
protobuf = subproject('protobuf', version: '=3.20.1')

# dependencies

deps = [
    dependency('libevent'),
    dependency('libevent_pthreads'),
    dependency('libibverbs'),
    dependency('librdmacm'),
    dependency('threads'),
    protobuf.get_variable('protobuf_dep'),
]

# sources

project_header_files = []
project_source_files = []

# include directories

incs = [
    include_directories('include/rdma_rpc'),
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
subdir('include/rdma_rpc')
subdir('src')

if not get_option('as_subproject')
    subdir('app')
    subdir('bench')
endif

# declare dependency
rdma_based_rpc_dep = declare_dependency(
    link_with: rdma_rpc_lib,
    dependencies: deps,
    include_directories: include_directories('include'),
)

# package config
pkg = import('pkgconfig')
pkg.generate(rdma_rpc_lib, description: 'RPC over RDMA')
