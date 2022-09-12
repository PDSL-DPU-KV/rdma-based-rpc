project(
    'hello-rdma',
    'cpp',
    version: '1.0',
    default_options: ['cpp_std=c++11'],
)

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

deps = [
    dependency('libevent'),
    dependency('libevent_pthreads'),
    dependency('libibverbs'),
    dependency('librdmacm'),
    dependency('threads'),
]

project_header_files = []
project_source_files = []

incs = [
    include_directories('include/rdma'),
    include_directories('include/util'),
]

subdir('include')
subdir('src')
subdir('app')
