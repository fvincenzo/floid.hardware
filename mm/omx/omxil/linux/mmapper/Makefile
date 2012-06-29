obj-m += mmapper.o

PWD		:= $(shell pwd)

default::
	$(MAKE) $(EXTRA_CFLAGS) -C $(KDIR) SUBDIRS=$(PWD) modules

clean::
	#-rm -f *~ *.o *.ko *.mod.c
	#-rm -f .version .*.o.flags .*.o.d .*.o.cmd .*.ko.cmd
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean
