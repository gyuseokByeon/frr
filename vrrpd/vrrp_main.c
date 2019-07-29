/*
 * VRRP entry point.
 * Copyright (C) 2018-2019 Cumulus Networks, Inc.
 * Quentin Young
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; see the file COPYING; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <zebra.h>

#include <lib/version.h>

#include "lib/command.h"
#include "lib/filter.h"
#include "lib/getopt.h"
#include "lib/if.h"
#include "lib/libfrr.h"
#include "lib/log.h"
#include "lib/memory.h"
#include "lib/nexthop.h"
#include "lib/privs.h"
#include "lib/sigevent.h"
#include "lib/thread.h"
#include "lib/vrf.h"

#include "vrrp.h"
#include "vrrp_debug.h"
#include "vrrp_vty.h"
#include "vrrp_zebra.h"

DEFINE_MGROUP(VRRPD, "vrrpd")

char backup_config_file[256];

zebra_capabilities_t _caps_p[] = {
	ZCAP_NET_RAW,
};

struct zebra_privs_t vrrp_privs = {
#if defined(FRR_USER) && defined(FRR_GROUP)
	.user = FRR_USER,
	.group = FRR_GROUP,
#endif
#if defined(VTY_GROUP)
	.vty_group = VTY_GROUP,
#endif
	.caps_p = _caps_p,
	.cap_num_p = array_size(_caps_p),
	.cap_num_i = 0};

struct option longopts[] = { {0} };

/* Master of threads. */
struct thread_master *master;

/* SIGHUP handler. */
static void sighup(void)
{
	zlog_info("SIGHUP received");
}

/* SIGINT / SIGTERM handler. */
static void __attribute__((noreturn)) sigint(void)
{
	zlog_notice("Terminating on signal");

	vrrp_fini();

	exit(0);
}

/* SIGUSR1 handler. */
static void sigusr1(void)
{
	zlog_rotate();
}

struct quagga_signal_t vrrp_signals[] = {
	{
		.signal = SIGHUP,
		.handler = &sighup,
	},
	{
		.signal = SIGUSR1,
		.handler = &sigusr1,
	},
	{
		.signal = SIGINT,
		.handler = &sigint,
	},
	{
		.signal = SIGTERM,
		.handler = &sigint,
	},
};

static const struct frr_yang_module_info *vrrp_yang_modules[] = {
	&frr_interface_info,
};

#define VRRP_VTY_PORT 2619

FRR_DAEMON_INFO(vrrpd, VRRP, .vty_port = VRRP_VTY_PORT,
		.proghelp = "Virtual Router Redundancy Protocol",
		.signals = vrrp_signals,
		.n_signals = array_size(vrrp_signals),
		.privs = &vrrp_privs,
		.yang_modules = vrrp_yang_modules,
		.n_yang_modules = array_size(vrrp_yang_modules),
)

int main(int argc, char **argv, char **envp)
{
	frr_preinit(&vrrpd_di, argc, argv);
	frr_opt_add("", longopts, "");

	while (1) {
		int opt;

		opt = frr_getopt(argc, argv, NULL);

		if (opt == EOF)
			break;

		switch (opt) {
		case 0:
			break;
		default:
			frr_help_exit(1);
			break;
		}
	}

	master = frr_init();

	access_list_init();
	vrrp_debug_init();
	vrrp_zebra_init();
	vrrp_vty_init();
	vrrp_init();

	snprintf(backup_config_file, sizeof(backup_config_file),
		 "%s/vrrpd.conf", frr_sysconfdir);
	vrrpd_di.backup_config_file = backup_config_file;

	frr_config_fork();
	frr_run(master);

	/* Not reached. */
	return 0;
}