project(
    'rdma-based-rpc',
    'c',
    'cpp',
    version: '0.1',
    default_options: ['warning_level=3', 'c_std=gnu11', 'cpp_std=c++11'],
)

project_source_files = []
project_include_directories = []
project_dependencies = []

project_include_directories += include_directories('./third_party/deps/include')
project_include_directories += include_directories('.')
project_dependencies += dependency('spdk_thread')
project_dependencies += dependency('spdk_event')
project_dependencies += dependency('spdk_env_dpdk')
project_dependencies += subproject('uthash').get_variable('uthash_dep')
project_dependencies += dependency('libibverbs')
project_dependencies += dependency('librdmacm')
project_dependencies += dependency('threads')

project_source_files += files(
    'concurrentqueue.cpp',
    'kv_app.c',
    'kv_memory.c',
    'kv_rdma.c',
)

libkv_rdma = library(
    'kv_rdma',
    project_source_files,
    include_directories: project_include_directories,
    dependencies: project_dependencies,
)

executable(
    'kv_rdma_client',
    'kv_rdma_client.c',
    dependencies: project_dependencies,
    link_with: libkv_rdma,
)
executable(
    'kv_rdma_server',
    'kv_rdma_server.c',
    dependencies: project_dependencies,
    link_with: libkv_rdma,
)
