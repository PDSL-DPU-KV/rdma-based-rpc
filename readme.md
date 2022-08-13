# RDMA-Example

A simple example for the RDMA beginner.

## Prerequisite

- Build tool: meson and ninja

- Dependent library: libibverbs librdmacm

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

```bash
meson build
meson compile -C build
```

### Play

```bash
# start the server
./build/app/server <NIC ip> <port>

# start the client
./build/app/client <server ip> <server port>
```

## Reference

- [Note](https://branch-nephew-4b8.notion.site/Basic-RDMA-Communication-Control-Flow-40e7c82d848e4c17b36eab9f1a170195)