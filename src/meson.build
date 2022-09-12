rdma_common_source = []

subdir('rdma')

rdma_common_lib = library(
    'rdma-common',
    rdma_common_source,
    dependencies: deps,
    include_directories: incs,
)

project_source_files += rdma_common_source
