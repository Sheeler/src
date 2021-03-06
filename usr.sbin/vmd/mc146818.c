/* $OpenBSD: mc146818.c,v 1.4 2016/10/26 05:26:36 mlarkin Exp $ */
/*
 * Copyright (c) 2016 Mike Larkin <mlarkin@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <dev/ic/mc146818reg.h>
#include <dev/isa/isareg.h>

#include <machine/vmmvar.h>

#include <event.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "mc146818.h"
#include "proc.h"
#include "vmm.h"

#define MC_DIVIDER_MASK 0xe0
#define MC_RATE_MASK 0xf

#define NVRAM_CENTURY 0x32

#define TOBCD(x)	(((x) / 10 * 16) + ((x) % 10))

struct mc146818 {
	time_t now;
	uint8_t idx;
	uint8_t regs[MC_NREGS + MC_NVRAM_SIZE];
	uint32_t vm_id;
	struct event sec;
	struct timeval sec_tv;
	struct event per;
	struct timeval per_tv;
	uint8_t irq_blocked;
};

struct mc146818 rtc;

/*
 * rtc_updateregs
 *
 * Updates the RTC TOD bytes, reflecting 'now'.
 */
static void
rtc_updateregs(void)
{
	struct tm *gnow;

	rtc.regs[MC_REGD] &= ~MC_REGD_VRT;
	gnow = gmtime(&rtc.now);

	rtc.regs[MC_SEC] = TOBCD(gnow->tm_sec);
	rtc.regs[MC_MIN] = TOBCD(gnow->tm_min);
	rtc.regs[MC_HOUR] = TOBCD(gnow->tm_hour);
	rtc.regs[MC_DOW] = TOBCD(gnow->tm_wday + 1);
	rtc.regs[MC_DOM] = TOBCD(gnow->tm_mday);
	rtc.regs[MC_MONTH] = TOBCD(gnow->tm_mon + 1);
	rtc.regs[MC_YEAR] = TOBCD((gnow->tm_year + 1900) % 100);
	rtc.regs[NVRAM_CENTURY] = TOBCD((gnow->tm_year + 1900) / 100);
	rtc.regs[MC_REGD] |= MC_REGD_VRT;
}

/*
 * rtc_fire1
 *
 * Callback for the 1s periodic TOD refresh timer
 *
 * Parameters:
 *  fd: unused
 *  type: unused
 *  arg: unused
 */
static void
rtc_fire1(int fd, short type, void *arg)
{
	rtc.now++;
	rtc_updateregs();
	evtimer_add(&rtc.sec, &rtc.sec_tv);
}

/*
 * rtc_fireper
 *
 * Callback for the periodic interrupt timer
 *
 * Parameters:
 *  fd: unused
 *  type: unused
 *  arg: (as uint32_t), VM ID to which this RTC belongs
 */
static void
rtc_fireper(int fd, short type, void *arg)
{
	rtc.regs[MC_REGC] |= MC_REGC_PF;

	if (!rtc.irq_blocked)
		vcpu_assert_pic_irq((ptrdiff_t)arg, 0, 8);

	/* Next irq is blocked until read of REGC */
	rtc.irq_blocked = 1;

	evtimer_add(&rtc.per, &rtc.per_tv);
}

/*
 * mc146818_init
 *
 * Initializes the emulated RTC/NVRAM
 *
 * Parameters:
 *  vm_id: VM ID to which this RTC belongs
 */
void
mc146818_init(uint32_t vm_id)
{
	memset(&rtc, 0, sizeof(rtc));
	time(&rtc.now);	

	rtc.regs[MC_REGB] = MC_REGB_24HR | MC_REGB_BINARY;
	rtc_updateregs();
	rtc.vm_id = vm_id;

	timerclear(&rtc.sec_tv);
	rtc.sec_tv.tv_sec = 1;

	evtimer_set(&rtc.sec, rtc_fire1, NULL);
	evtimer_add(&rtc.sec, &rtc.sec_tv);

	evtimer_set(&rtc.per, rtc_fireper, (void *)(intptr_t)rtc.vm_id);
}

/*
 * rtc_reschedule_per
 *
 * Reschedule the periodic interrupt firing rate, based on the currently
 * selected REGB values.
 */
static void
rtc_reschedule_per(void)
{
	uint16_t rate;
	uint64_t us;

	if (rtc.regs[MC_REGB] & MC_REGB_PIE) {
		rate = 32768 >> ((rtc.regs[MC_REGA] & MC_RATE_MASK) - 1);
		us = (1.0 / rate) * 1000000;
		rtc.per_tv.tv_usec = us;
		if (evtimer_pending(&rtc.per, NULL))
			evtimer_del(&rtc.per);

		evtimer_add(&rtc.per, &rtc.per_tv);
	}
}

/*
 * rtc_update_rega
 *
 * Updates the RTC's REGA register
 *
 * Parameters:
 *  data: REGA register data
 */
static void
rtc_update_rega(uint32_t data)
{
	if ((data & MC_DIVIDER_MASK) != MC_BASE_32_KHz)
		log_warnx("%s: set non-32KHz timebase not supported",
		    __func__);

	rtc.regs[MC_REGA] = data;
	if (rtc.regs[MC_REGB] & MC_REGB_PIE)
		rtc_reschedule_per();
}

/*
 * rtc_update_regb
 *
 * Updates the RTC's REGB register
 *
 * Parameters:
 *  data: REGB register data
 */
static void
rtc_update_regb(uint32_t data)
{
	if (data & MC_REGB_DSE)
		log_warnx("%s: DSE mode not supported", __func__);

	if (!(data & MC_REGB_24HR))
		log_warnx("%s: 12 hour mode not supported", __func__);

	rtc.regs[MC_REGB] = data;

	if (data & MC_REGB_PIE)
		rtc_reschedule_per();
}

/*
 * vcpu_exit_mc146818
 *
 * Handles emulated MC146818 RTC access (in/out instruction to RTC ports).
 *
 * Parameters:
 *  vrp: vm run parameters containing exit information for the I/O
 *      instruction being performed
 *
 * Return value:
 *  Interrupt to inject to the guest VM, or 0xFF if no interrupt should
 *      be injected.
 */
uint8_t
vcpu_exit_mc146818(struct vm_run_params *vrp)
{
	union vm_exit *vei = vrp->vrp_exit;
	uint16_t port = vei->vei.vei_port;
	uint8_t dir = vei->vei.vei_dir;
	uint32_t data = vei->vei.vei_data;

	if (port == IO_RTC) {
		if (dir == 0) {
			if (data <= (MC_NREGS + MC_NVRAM_SIZE))
				rtc.idx = data;
			else {
				log_warnx("%s: mc146818 bogus register 0x%x",
				    __func__, data);
				rtc.idx = MC_REGD;
			}
		} else {
			log_warnx("%s: mc146818 illegal read from port 0x%x",
			    __func__, port);
			vei->vei.vei_data = 0xFF;
		}
	} else if (port == IO_RTC + 1) {
		if (dir == 0) {
			switch (rtc.idx) {
			case MC_SEC ... MC_YEAR:
			case MC_NVRAM_START ... MC_NVRAM_START + MC_NVRAM_SIZE:
				rtc.regs[rtc.idx] = data;
				break;
			case MC_REGA:
				rtc_update_rega(data);
				break;
			case MC_REGB:
				rtc_update_regb(data);
				break;
			case MC_REGC:
			case MC_REGD:
				log_warnx("%s: mc146818 illegal write "
				    "of reg 0x%x", __func__, rtc.idx);
				break;
			default:
				log_warnx("%s: mc146818 illegal reg %x\n",
				    __func__, rtc.idx);
			}
			rtc.idx = MC_REGD;
		} else {
			data = rtc.regs[rtc.idx]; 
			vei->vei.vei_data = data;

			if (rtc.idx == MC_REGC) {
				/* Reset IRQ state */
				rtc.irq_blocked = 0;
				rtc.regs[MC_REGC] &= ~MC_REGC_PF;
			}
			rtc.idx = MC_REGD;
		}
	} else {
		log_warnx("%s: mc146818 unknown port 0x%x",
		    __func__, vei->vei.vei_port);
	}

	return 0xFF;
}
