project('hello-rdma', 'cpp',
    version : '1.0',
    default_options: ['cpp_std=c++11']
)

libevent_dep = dependency('libevent')
libibverbs_dep = dependency('libibverbs')
librdmacm_dep = dependency('librdmacm')
project_dependencies = [libibverbs_dep, librdmacm_dep, libevent_dep]

project_header_files = []
project_source_files = []
project_include_directories = [include_directories('include')]

subdir('include')
subdir('src')
subdir('app')