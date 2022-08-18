# RDMA-Example

A simple example for the RDMA beginner.

## Prerequisite

- Build tool: meson and ninja

- Dependent library: libibverbs librdmacm libevent

- Dependent Environment: RDMA-enabled NIC

> If not, use Soft-RoCE instead. Follow the commands bellow.
>
> ```bash
> modprobe rdma_rxe
> # bind a normal NIC
> sudo rdma link add rxe_0 type rxe netdev <your NIC name>
> # use `ip addr` to get the name
> ibv_devices # you will see a virtualized NIC named rxe_0
> ```

## How to use

### Build

We have two modes, 'poll' and 'notify'.

- The former will use one thread for one connection to polling its CQ.

- The later will use libevent to handle all connections' completion events.

```bash
meson build -Dmode=poll
meson compile -C build
```

### Play

```bash
# start the server
./build/app/server <NIC ip> <port>

# start the client
./build/app/client <server ip> <server port>
```

## To-do

- [x] basic connection management
- [x] add send and recv verbs
- [x] add read and write verbs
- [ ] make the general rpc caller and handler
- [ ] add better message queue and thread pool
- [ ] add better memory pool

## Reference

- [Note](https://branch-nephew-4b8.notion.site/Basic-RDMA-Communication-Control-Flow-40e7c82d848e4c17b36eab9f1a170195)
- [RDMA Aware Networks Programming User Manual](https://docs.nvidia.com/networking/display/RDMAAwareProgrammingv17/RDMA+Aware+Networks+Programming+User+Manual)