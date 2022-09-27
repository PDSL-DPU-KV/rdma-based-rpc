rdma_common_source = []

subdir('rdma')

rdma_rpc_lib = library(
    'rdma_rpc',
    rdma_common_source,
    dependencies: deps,
    include_directories: incs,
)

project_source_files += rdma_common_source
