rdma_common_source = []

subdir('rdma')
subdir('util')

rdma_common_lib = library('rdma-common', rdma_common_source, 
                            dependencies: project_dependencies,
                            include_directories: project_include_directories,
                            link_args: project_link_flags)
project_source_files += rdma_common_source