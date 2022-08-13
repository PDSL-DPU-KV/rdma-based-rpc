project('hello-rdma', 'cpp',
    version : '1.0',
    default_options: ['cpp_std=c++11']
)

ibverbs_dep = dependency('libibverbs')
rdmacm_dep = dependency('librdmacm')
project_dependencies = [ibverbs_dep, rdmacm_dep]

project_header_files = []
project_source_files = []
project_include_directories = [include_directories('include')]

subdir('include')
subdir('src')
subdir('app')