subdir('proto')

app_incs = [incs, include_directories('./pb')]

executable(
    'client',
    'client.cc',
    './pb/hello.pb.cc',
    include_directories: app_incs,
    dependencies: deps,
    link_with: rdma_rpc_lib,
)

executable(
    'server',
    'server.cc',
    './pb/hello.pb.cc',
    include_directories: app_incs,
    dependencies: deps,
    link_with: rdma_rpc_lib,
)
