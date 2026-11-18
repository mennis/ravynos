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
 * - The only early IOKit time waits are IORTC and, on x86, IONVRAM; arm64
 *   does not wait for IONVRAM (iokit/Kernel/IOStartIOKit.cpp:67-84).
 *   Publishing a fake NVRAM service is therefore neither required nor
 *   desirable.  IORTC is published by start() below once the calendar source
 *   is mapped, so clock_initialize_calendar never runs before
 *   getGMTTimeOfDay can answer.
 * - The Pi 3 has no RTC.  xnuhv supplies a PL031-compatible one and describes
 *   it at /arm-io/rtc; its "reg" is an offset from the arm-io "ranges" parent
 *   address, as for every other arm-io child.  Without the node,
 *   getGMTTimeOfDay returns 0 and the calendar starts in 1970.
 * - IODTPlatformExpert already implements model/machine names from the root
 *   compatible/model properties (iokit/Kernel/IOPlatformExpert.cpp:1613-1665).
 */

#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOCPU.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>

#define super IODTPlatformExpert

class RavynBCM2837PlatformExpert : public IODTPlatformExpert
{
	OSDeclareDefaultStructors(RavynBCM2837PlatformExpert);

public:
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual const char *deleteList(void) APPLE_KEXT_OVERRIDE;
	virtual const char *excludeList(void) APPLE_KEXT_OVERRIDE;
	virtual long getGMTTimeOfDay(void) APPLE_KEXT_OVERRIDE;
	virtual void setGMTTimeOfDay(long secs) APPLE_KEXT_OVERRIDE;

private:
	IOMemoryMap *rtcMap;
	volatile uint32_t *rtcRegs;

	void mapRTC(void);
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

/*
 * IOPlatformExpert::start does not register the service, so nothing waiting
 * on IOPlatformExpert matching would find it. On embedded configs
 * IOSecureBSDRoot waits 30 s for one and asserts it exists
 * (iokit/bsddev/IOKitBSDInit.cpp:737-741). ACPIPlatformExpert registers
 * itself the same way on x86 (Kernel/Extensions/IOACPIFamily/
 * ACPIPlatformExpert.cpp:150).
 */
bool
RavynBCM2837PlatformExpert::start(IOService *provider)
{
	if (!IODTPlatformExpert::start(provider)) {
		return false;
	}
	mapRTC();
	registerService();
	publishResource("IORTC", this);
	return true;
}

/* PL031 registers (ARM DDI 0224): DR is the seconds counter, LR loads it. */
enum {
	kPL031DR = 0x000 / sizeof(uint32_t),
	kPL031LR = 0x008 / sizeof(uint32_t),
};

/*
 * Map /arm-io/rtc.  Both properties are pairs or triples of 64-bit cells
 * (#address-cells = #size-cells = 2): arm-io "ranges" is {child, parent,
 * size}, rtc "reg" is {offset, size}.  The same arithmetic pe_serial.c uses
 * for the UART (soc base + reg[0]).
 */
void
RavynBCM2837PlatformExpert::mapRTC(void)
{
	IORegistryEntry *io = IORegistryEntry::fromPath("/arm-io", gIODTPlane);
	IORegistryEntry *rtc = IORegistryEntry::fromPath("/arm-io/rtc", gIODTPlane);
	OSData *ranges = io ? OSDynamicCast(OSData, io->getProperty("ranges")) : NULL;
	OSData *reg = rtc ? OSDynamicCast(OSData, rtc->getProperty("reg")) : NULL;

	if (ranges && ranges->getLength() >= 2 * sizeof(uint64_t) &&
	    reg && reg->getLength() >= 2 * sizeof(uint64_t)) {
		const uint64_t *r = (const uint64_t *)ranges->getBytesNoCopy();
		const uint64_t *g = (const uint64_t *)reg->getBytesNoCopy();
		IODeviceMemory *mem = IODeviceMemory::withRange(r[1] + g[0], g[1]);
		if (mem) {
			rtcMap = mem->map(kIOMapInhibitCache);
			mem->release();
		}
		if (rtcMap) {
			rtcRegs = (volatile uint32_t *)rtcMap->getVirtualAddress();
		}
	}
	if (!rtcRegs) {
		IOLog("RavynBCM2837PlatformExpert: no /arm-io/rtc; calendar starts at 0\n");
	}
	OSSafeReleaseNULL(io);
	OSSafeReleaseNULL(rtc);
}

long
RavynBCM2837PlatformExpert::getGMTTimeOfDay(void)
{
	return rtcRegs ? (long)rtcRegs[kPL031DR] : 0;
}

void
RavynBCM2837PlatformExpert::setGMTTimeOfDay(long secs)
{
	if (rtcRegs) {
		rtcRegs[kPL031LR] = (uint32_t)secs;
	}
}

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
