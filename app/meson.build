executable(
    'client',
    'client.cc',
    include_directories: project_include_directories,
    dependencies: project_dependencies,
    link_with: rdma_common_lib,
)

executable(
    'server',
    'server.cc',
    include_directories: project_include_directories,
    dependencies: project_dependencies,
    link_with: rdma_common_lib,
)
