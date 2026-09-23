/*
 * Minimal built-in platform expert for the BCM2837 machine supplied by xnuhv.
 *
 * Boot requirements and their source:
 *
 * - A platform expert must match IOPlatformExpertDevice before the fallback
 *   IOPanicPlatform (iokit/Kernel/IOStartIOKit.cpp:173-184 and
 *   iokit/Kernel/IOPlatformExpert.cpp:1968-1995).  The built-in personality
 *   matches xnuhv's device-tree root compatible string, "RaspberryPi3,1".
 * - IODTPlatformExpert::configure publishes /cpus and the remaining top-level
 *   device-tree nodes (iokit/Kernel/IOPlatformExpert.cpp:1500-1590), so this
 *   subclass only supplies the required deletion/exclusion lists.
 * - The bootstrap processor, processor object, and scheduler exist before
 *   PE_init_platform starts IOKit (osfmk/arm/arm_init.c:358-418,530 and
 *   pexpert/arm/pe_init.c:337).  Therefore the boot IOCPU must not call
 *   ml_processor_register a second time.
 * - Enabling interrupts after PE_init_iokit requires the boot cpu_data's
 *   interrupts_enabled flag.  IOCPUInterruptController::enableCPUInterrupt
 *   supplies it through ml_install_interrupt_handler
 *   (iokit/Kernel/IOCPU.cpp:932-949 and
 *   osfmk/arm64/machine_routines.c:991-1018).  The tiny CPU driver below does
 *   only that registration.  xnuhv delivers
 *   the physical generic timer as FIQ; sleh_fiq handles it in osfmk by calling
 *   rtclock_intr directly (osfmk/arm64/sleh.c:1582-1656).  IOKit must not
 *   install a handler for that timer.
 * - The only early IOKit time waits are IORTC and, on x86, IONVRAM;
 *   BCM2837 explicitly skips IORTC and arm64 does not wait for IONVRAM
 *   (iokit/Kernel/IOStartIOKit.cpp:67-84).  Publishing a fake NVRAM service is
 *   therefore neither required nor desirable.
 * - IODTPlatformExpert already implements model/machine names from the root
 *   compatible/model properties (iokit/Kernel/IOPlatformExpert.cpp:1613-1665).
 */

#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOCPU.h>

#define super IODTPlatformExpert

class RavynBCM2837PlatformExpert : public IODTPlatformExpert
{
	OSDeclareDefaultStructors(RavynBCM2837PlatformExpert);

public:
	virtual const char *deleteList(void) APPLE_KEXT_OVERRIDE;
	virtual const char *excludeList(void) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(RavynBCM2837PlatformExpert, IODTPlatformExpert);

#undef super
#define super IOCPUInterruptController

class RavynBCM2837CPUInterruptController : public IOCPUInterruptController
{
	OSDeclareDefaultStructors(RavynBCM2837CPUInterruptController);
};

OSDefineMetaClassAndStructors(RavynBCM2837CPUInterruptController,
    IOCPUInterruptController);

#undef super
#define super IOCPU

class RavynBCM2837CPU : public IOCPU
{
	OSDeclareDefaultStructors(RavynBCM2837CPU);

private:
	RavynBCM2837CPUInterruptController *interruptController;

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual void initCPU(bool boot) APPLE_KEXT_OVERRIDE;
	virtual void quiesceCPU(void) APPLE_KEXT_OVERRIDE;
	virtual kern_return_t startCPU(vm_offset_t start_paddr,
	    vm_offset_t arg_paddr) APPLE_KEXT_OVERRIDE;
	virtual void haltCPU(void) APPLE_KEXT_OVERRIDE;
	virtual const OSSymbol *getCPUName(void) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(RavynBCM2837CPU, IOCPU);

const char *
RavynBCM2837PlatformExpert::deleteList(void)
{
	return "()";
}

const char *
RavynBCM2837PlatformExpert::excludeList(void)
{
	return "()";
}

bool
RavynBCM2837CPU::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}

	interruptController = new RavynBCM2837CPUInterruptController;
	if (!interruptController ||
	    interruptController->initCPUInterruptController(1) != kIOReturnSuccess) {
		return false;
	}

	interruptController->attach(this);
	interruptController->registerCPUInterruptController();
	setCPUState(kIOCPUStateUninitalized);
	initCPU(true);
	registerService();
	return true;
}

void
RavynBCM2837CPU::initCPU(bool boot __unused)
{
	interruptController->enableCPUInterrupt(this);
	setCPUState(kIOCPUStateRunning);
}

void
RavynBCM2837CPU::quiesceCPU(void)
{
}

kern_return_t
RavynBCM2837CPU::startCPU(vm_offset_t start_paddr __unused,
    vm_offset_t arg_paddr __unused)
{
	return KERN_FAILURE;
}

void
RavynBCM2837CPU::haltCPU(void)
{
}

const OSSymbol *
RavynBCM2837CPU::getCPUName(void)
{
	return OSSymbol::withCString("cpu0");
}
