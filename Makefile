#
# MIT License
#
# Copyright (c) 2017 Kalray S.A
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
ifeq ($(ARCH), )
ARCH := k1b
endif

ifeq ($(test_type), )
test_type := linear
endif

ifeq ($(nb_cluster), )
nb_cluster := 16
endif

ifeq ($(nb_core), )
nb_core := 16
endif

ifeq ($(cluster_system), )
cluster_system := bare
endif

arch := $(ARCH)
#board := large_memory
board := developer

system-name := bare
COMPILE_OPTI := -O3

JTAG_OPT := --no-pcie --no-pcie-load

# Cluster rules
cluster-bin := cluster_bin
cluster-system := $(cluster_system)
cluster_bin-srcs := src/cluster/cluster.c src/cluster/fft_kernels.c
cluster-cflags := -g -DNB_CLUSTER=$(nb_cluster) -DN_CORES=$(nb_core) \
                  ${COMPILE_OPTI} -mhypervisor -I . -Wall -std=gnu99 \
				 -Iinclude/common/
cluster-lflags := -g -mhypervisor -lm -Wl,--defsym=USER_STACK_SIZE=0x2000 \
                  -Wl,--defsym=KSTACK_SIZE=0x1000

ifeq ($(cluster_system), bare)
cluster-lflags += -lvbsp -lutask -lmppa_remote -lmppa_async \
                  -lmppa_request_engine -lmppapower -lmppanoc -lmpparouting \
				  -Wl,--defsym=_LIBNOC_DISABLE_FIFO_FULL_CHECK=0
endif
ifeq ($(cluster_system), nodeos)
cluster-lflags += -pthread -lmppa_remote -lmppa_async -lmppa_request_engine \
                  -lmppapower -lmppanoc -lmpparouting \
				  -Wl,--defsym=_LIBNOC_DISABLE_FIFO_FULL_CHECK=0
endif

io-bin := io_bin
io_bin-srcs := src/io/io_main.c
io_bin-cflags := -Iinclude/common/ -DNB_CLUSTER=$(nb_cluster) -DN_CORES=$(nb_core) -std=gnu99 -g \
                 ${COMPILE_OPTI} -DMPPA_TRACE_ENABLE -Wall -mhypervisor -I .
io_bin-lflags :=  -lvbsp -lmppa_remote -lmppa_async -lmppa_request_engine \
                  -lpcie_queue -lutask  -lmppapower -lmppanoc -lmpparouting \
				  -mhypervisor -Wl,--defsym=_LIBNOC_DISABLE_FIFO_FULL_CHECK=0 -lm

mppa-bin := multibin_bin
multibin_bin-objs = io_bin cluster_bin

host-bin := host_bin
host_bin-srcs := src/host/host_main.c
host-cflags := -Iinclude/common/ -Wall ${COMPILE_OPTI}
host-lflags := -lpthread -lm -lrt -lmppa_remote -lpcie
host-bin    := host_bin

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray

run_jtag: all
	$(K1_TOOLCHAIN_DIR)/bin/k1-jtag-runner $(JTAG_OPT) --no-printf-prefix --multibinary=./${O}/bin/multibin_bin.mpk --exec-multibin=IODDR0:io_bin

run_pcie: all
	./${O}/bin/host_bin ./${O}/bin/multibin_bin.mpk io_bin

