subdir('proto')

executable(
    'client',
    'client.cc',
    './pb/bench.pb.cc',
    include_directories: [incs, './pb'],
    dependencies: deps,
    link_with: rdma_rpc_lib,
)

executable(
    'server',
    'server.cc',
    './pb/bench.pb.cc',
    include_directories: [incs, './pb'],
    dependencies: deps,
    link_with: rdma_rpc_lib,
)
