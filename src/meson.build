rdma_common_source = files(
    'client.cc',
    'connection.cc',
    'context.cc',
    'server.cc',
)

rdma_rpc_lib = library(
    'rdma_rpc',
    rdma_common_source,
    dependencies: deps,
    include_directories: incs,
    version: meson.project_version(),
    install: true,
)

project_source_files += rdma_common_source
