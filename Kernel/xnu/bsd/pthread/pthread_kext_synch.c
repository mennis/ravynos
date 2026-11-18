/*
 * Give pthread.kext's synchronization implementation a kernel-unique object
 * name.  bsd/kern/kern_synch.c already produces kern_synch.o.
 */
#include "../../../Extensions/pthread/kern_synch.c"
