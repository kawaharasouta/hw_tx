obj-m += hw_tx.o
hw_tx-objs := hw_tx_main.o
#PWD := $(shell pwd)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(CURDIR) clean
