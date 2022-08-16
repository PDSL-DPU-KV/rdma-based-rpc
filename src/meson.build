rdma_common_source = files(
    'common.cc',
    'server.cc',
    'client.cc'
)
project_source_files += rdma_common_source

rdma_common_lib = library('rdma-common', rdma_common_source, 
                            dependencies: project_dependencies,
                            include_directories: project_include_directories,
                            link_args: project_link_flags)
