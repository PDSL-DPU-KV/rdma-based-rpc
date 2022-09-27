subdir('proto')

executable(
    'client',
    'client.cc',
    './pb/hello.pb.cc',
    include_directories: [incs, './pb'],
    dependencies: deps,
    link_with: rdma_rpc_lib,
)

executable(
    'server',
    'server.cc',
    './pb/hello.pb.cc',
    include_directories: [incs, './pb'],
    dependencies: deps,
    link_with: rdma_rpc_lib,
)
