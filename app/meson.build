executable(
    'client',
    'client.cc',
    include_directories: incs,
    dependencies: deps,
    link_with: rdma_common_lib,
)

executable(
    'server',
    'server.cc',
    include_directories: incs,
    dependencies: deps,
    link_with: rdma_common_lib,
)
