subdir('proto')

bench_incs = [incs, './pb', '../include/rdma_rpc/util']

executable(
    'client',
    'client.cc',
    './pb/bench.pb.cc',
    include_directories: bench_incs,
    dependencies: deps,
    link_with: rdma_rpc_lib,
)

executable(
    'server',
    'server.cc',
    './pb/bench.pb.cc',
    include_directories: bench_incs,
    dependencies: deps,
    link_with: rdma_rpc_lib,
)
