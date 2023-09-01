#!/bin/bash

PROJECT_HOME=$(pwd)
SCRIPTS_DIR=$PROJECT_HOME/scripts
THIRD_PARTY_SRC_DIR=$PROJECT_HOME/third_party
INSTALL_PATH=$PROJECT_HOME/third_party/deps

printf "Download and build dependencies...\n"

printf "\nDownload and build dpdk v22.07...\n"

# dpdk-v22.07
DPDK_SOURCE_DIR=$THIRD_PARTY_SRC_DIR/dpdk-v22.07
if [ ! -d $DPDK_SOURCE_DIR ]; then
git clone https://github.com/dpdk/dpdk --depth 1 --branch v22.07 $DPDK_SOURCE_DIR &&
    cd $DPDK_SOURCE_DIR &&
    git apply --check $SCRIPTS_DIR/dpdk_build_with_rpath_fix.patch &&
    git apply $SCRIPTS_DIR/dpdk_build_with_rpath_fix.patch &&
    meson setup --buildtype=release --prefix=$INSTALL_PATH -Denable_docs=false -Dtests=false -Denable_kmods=false -Dcpu_instruction_set=native -Denable_drivers=bus,bus/pci,bus/vdev,mempool/ring -Ddisable_libs=ip_frag,bpf,distributor,pdump,fib,regexdev,gpudev,acl,bbdev,table,pcapng,graph,pipeline,flow_classify,member,port,ipsec,eventdev,lpm,rib,node,bitratestats,sched,efd,metrics,gso,kni,cfgfile,stack,latencystats,jobstats,gro,rawdev build &&
	meson compile -C build &&
    meson install -C build &&
	cd $PROJECT_HOME ||
	exit
fi

printf "\nDownload and build spdk v22.09...\n"

# spdk-v22.09
SPDK_SOURCE_DIR=$THIRD_PARTY_SRC_DIR/spdk-v22.09
if [ ! -d $SPDK_SOURCE_DIR ]; then
	git clone https://github.com/spdk/spdk --depth 1 --branch v22.09 $SPDK_SOURCE_DIR &&
		cd $SPDK_SOURCE_DIR &&
		git submodule update --init --depth 1 isa-l &&
		./configure --disable-tests --disable-unit-tests --disable-examples --disable-apps --with-shared --with-rdma --prefix=$INSTALL_PATH --with-dpdk=$INSTALL_PATH &&
		make -j8 &&
		make install &&
		cd $PROJECT_HOME ||
		exit
fi

printf "\nDependencies are all ready\n"

printf "Dll dependencies are installed in %s\n" "$INSTALL_PATH"
