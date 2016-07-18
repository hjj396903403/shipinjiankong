ADDR=/work/linux-3.5/linux-3.5
FILE=/work/rootfs/test

all:
	@make -C $(ADDR) M=`pwd` modules
	@cp *.ko $(FILE) -v
	@make -C $(ADDR) M=`pwd` modules clean
	@arm-linux-gcc color.c server.c  -lts -lpthread
	@cp a.out $(FILE) -v

obj-m +=led_driver.o
