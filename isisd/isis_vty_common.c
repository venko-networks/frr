/*
 * IS-IS Rout(e)ing protocol - isis_vty_common.c
 *
 * This file contains the CLI that is shared between OpenFabric and IS-IS
 *
 * Copyright (C) 2001,2002   Sampo Saaristo
 *                           Tampere University of Technology
 *                           Institute of Communications Engineering
 * Copyright (C) 2016        David Lamparter, for NetDEF, Inc.
 * Copyright (C) 2018        Christian Franke, for NetDEF, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public Licenseas published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; see the file COPYING; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <zebra.h>

#include "command.h"
#include "spf_backoff.h"
#include "bfd.h"

#include "isis_circuit.h"
#include "isis_csm.h"
#include "isis_misc.h"
#include "isis_mt.h"
#include "isisd.h"
#include "isis_bfd.h"
#include "isis_vty_common.h"

struct isis_circuit *isis_circuit_lookup(struct vty *vty)
{
	struct interface *ifp = VTY_GET_CONTEXT(interface);
	struct isis_circuit *circuit;

	if (!ifp) {
		vty_out(vty, "Invalid interface \n");
		return NULL;
	}

	circuit = circuit_scan_by_ifp(ifp);
	if (!circuit) {
		vty_out(vty, "ISIS is not enabled on circuit %s\n", ifp->name);
		return NULL;
	}

	return circuit;
}

DEFUN (isis_passive,
       isis_passive_cmd,
       PROTO_NAME " passive",
       PROTO_HELP
       "Configure the passive mode for interface\n")
{
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	if (!circuit)
		return CMD_ERR_NO_MATCH;

	CMD_FERR_RETURN(isis_circuit_passive_set(circuit, 1),
			"Cannot set passive: $ERR");
	return CMD_SUCCESS;
}

DEFUN (no_isis_passive,
       no_isis_passive_cmd,
       "no " PROTO_NAME " passive",
       NO_STR
       PROTO_HELP
       "Configure the passive mode for interface\n")
{
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	if (!circuit)
		return CMD_ERR_NO_MATCH;

	CMD_FERR_RETURN(isis_circuit_passive_set(circuit, 0),
			"Cannot set no passive: $ERR");
	return CMD_SUCCESS;
}

DEFUN (isis_passwd,
       isis_passwd_cmd,
       PROTO_NAME " password <md5|clear> WORD",
       PROTO_HELP
       "Configure the authentication password for a circuit\n"
       "HMAC-MD5 authentication\n"
       "Cleartext password\n"
       "Circuit password\n")
{
	int idx_encryption = 2;
	int idx_word = 3;
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	ferr_r rv;

	if (!circuit)
		return CMD_ERR_NO_MATCH;

	if (argv[idx_encryption]->arg[0] == 'm')
		rv = isis_circuit_passwd_hmac_md5_set(circuit,
						      argv[idx_word]->arg);
	else
		rv = isis_circuit_passwd_cleartext_set(circuit,
						       argv[idx_word]->arg);

	CMD_FERR_RETURN(rv, "Failed to set circuit password: $ERR");
	return CMD_SUCCESS;
}

DEFUN (no_isis_passwd,
       no_isis_passwd_cmd,
       "no " PROTO_NAME " password [<md5|clear> WORD]",
       NO_STR
       PROTO_HELP
       "Configure the authentication password for a circuit\n"
       "HMAC-MD5 authentication\n"
       "Cleartext password\n"
       "Circuit password\n")
{
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	if (!circuit)
		return CMD_ERR_NO_MATCH;

	CMD_FERR_RETURN(isis_circuit_passwd_unset(circuit),
			"Failed to unset circuit password: $ERR");
	return CMD_SUCCESS;
}

DEFUN (isis_metric,
       isis_metric_cmd,
       PROTO_NAME " metric (0-16777215)",
       PROTO_HELP
       "Set default metric for circuit\n"
       "Default metric value\n")
{
	int idx_number = 2;
	int met;
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	if (!circuit)
		return CMD_ERR_NO_MATCH;

	met = atoi(argv[idx_number]->arg);

	/* RFC3787 section 5.1 */
	if (circuit->area && circuit->area->oldmetric == 1
	    && met > MAX_NARROW_LINK_METRIC) {
		vty_out(vty,
			"Invalid metric %d - should be <0-63> "
			"when narrow metric type enabled\n",
			met);
		return CMD_WARNING_CONFIG_FAILED;
	}

	/* RFC4444 */
	if (circuit->area && circuit->area->newmetric == 1
	    && met > MAX_WIDE_LINK_METRIC) {
		vty_out(vty,
			"Invalid metric %d - should be <0-16777215> "
			"when wide metric type enabled\n",
			met);
		return CMD_WARNING_CONFIG_FAILED;
	}

	CMD_FERR_RETURN(isis_circuit_metric_set(circuit, IS_LEVEL_1, met),
			"Failed to set L1 metric: $ERR");
	CMD_FERR_RETURN(isis_circuit_metric_set(circuit, IS_LEVEL_2, met),
			"Failed to set L2 metric: $ERR");
	return CMD_SUCCESS;
}

DEFUN (no_isis_metric,
       no_isis_metric_cmd,
       "no " PROTO_NAME " metric [(0-16777215)]",
       NO_STR
       PROTO_HELP
       "Set default metric for circuit\n"
       "Default metric value\n")
{
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	if (!circuit)
		return CMD_ERR_NO_MATCH;

	CMD_FERR_RETURN(isis_circuit_metric_set(circuit, IS_LEVEL_1,
						DEFAULT_CIRCUIT_METRIC),
			"Failed to set L1 metric: $ERR");
	CMD_FERR_RETURN(isis_circuit_metric_set(circuit, IS_LEVEL_2,
						DEFAULT_CIRCUIT_METRIC),
			"Failed to set L2 metric: $ERR");
	return CMD_SUCCESS;
}

DEFUN (isis_hello_interval,
       isis_hello_interval_cmd,
       PROTO_NAME " hello-interval (1-600)",
       PROTO_HELP
       "Set Hello interval\n"
       "Holdtime 1 seconds, interval depends on multiplier\n")
{
	uint32_t interval = atoi(argv[2]->arg);
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	if (!circuit)
		return CMD_ERR_NO_MATCH;

	circuit->hello_interval[0] = interval;
	circuit->hello_interval[1] = interval;

	return CMD_SUCCESS;
}

DEFUN (no_isis_hello_interval,
       no_isis_hello_interval_cmd,
       "no " PROTO_NAME " hello-interval [(1-600)]",
       NO_STR
       PROTO_HELP
       "Set Hello interval\n"
       "Holdtime 1 second, interval depends on multiplier\n")
{
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	if (!circuit)
		return CMD_ERR_NO_MATCH;

	circuit->hello_interval[0] = DEFAULT_HELLO_INTERVAL;
	circuit->hello_interval[1] = DEFAULT_HELLO_INTERVAL;

	return CMD_SUCCESS;
}

DEFUN (isis_hello_multiplier,
       isis_hello_multiplier_cmd,
       PROTO_NAME " hello-multiplier (2-100)",
       PROTO_HELP
       "Set multiplier for Hello holding time\n"
       "Hello multiplier value\n")
{
	uint16_t mult = atoi(argv[2]->arg);
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	if (!circuit)
		return CMD_ERR_NO_MATCH;

	circuit->hello_multiplier[0] = mult;
	circuit->hello_multiplier[1] = mult;

	return CMD_SUCCESS;
}

DEFUN (no_isis_hello_multiplier,
       no_isis_hello_multiplier_cmd,
       "no " PROTO_NAME " hello-multiplier [(2-100)]",
       NO_STR
       PROTO_HELP
       "Set multiplier for Hello holding time\n"
       "Hello multiplier value\n")
{
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	if (!circuit)
		return CMD_ERR_NO_MATCH;

	circuit->hello_multiplier[0] = DEFAULT_HELLO_MULTIPLIER;
	circuit->hello_multiplier[1] = DEFAULT_HELLO_MULTIPLIER;

	return CMD_SUCCESS;
}

DEFUN (csnp_interval,
       csnp_interval_cmd,
       PROTO_NAME " csnp-interval (1-600)",
       PROTO_HELP
       "Set CSNP interval in seconds\n"
       "CSNP interval value\n")
{
	uint16_t interval = atoi(argv[2]->arg);
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	if (!circuit)
		return CMD_ERR_NO_MATCH;

	circuit->csnp_interval[0] = interval;
	circuit->csnp_interval[1] = interval;

	return CMD_SUCCESS;
}

DEFUN (no_csnp_interval,
       no_csnp_interval_cmd,
       "no " PROTO_NAME " csnp-interval [(1-600)]",
       NO_STR
       PROTO_HELP
       "Set CSNP interval in seconds\n"
       "CSNP interval value\n")
{
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	if (!circuit)
		return CMD_ERR_NO_MATCH;

	circuit->csnp_interval[0] = DEFAULT_CSNP_INTERVAL;
	circuit->csnp_interval[1] = DEFAULT_CSNP_INTERVAL;

	return CMD_SUCCESS;
}

DEFUN (psnp_interval,
       psnp_interval_cmd,
       PROTO_NAME " psnp-interval (1-120)",
       PROTO_HELP
       "Set PSNP interval in seconds\n"
       "PSNP interval value\n")
{
	uint16_t interval = atoi(argv[2]->arg);
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	if (!circuit)
		return CMD_ERR_NO_MATCH;

	circuit->psnp_interval[0] = interval;
	circuit->psnp_interval[1] = interval;

	return CMD_SUCCESS;
}

DEFUN (no_psnp_interval,
       no_psnp_interval_cmd,
       "no " PROTO_NAME " psnp-interval [(1-120)]",
       NO_STR
       PROTO_HELP
       "Set PSNP interval in seconds\n"
       "PSNP interval value\n")
{
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	if (!circuit)
		return CMD_ERR_NO_MATCH;

	circuit->psnp_interval[0] = DEFAULT_PSNP_INTERVAL;
	circuit->psnp_interval[1] = DEFAULT_PSNP_INTERVAL;

	return CMD_SUCCESS;
}

DEFUN (circuit_topology,
       circuit_topology_cmd,
       PROTO_NAME " topology " ISIS_MT_NAMES,
       PROTO_HELP
       "Configure interface IS-IS topologies\n"
       ISIS_MT_DESCRIPTIONS)
{
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	if (!circuit)
		return CMD_ERR_NO_MATCH;
	const char *arg = argv[2]->arg;
	uint16_t mtid = isis_str2mtid(arg);

	if (circuit->area && circuit->area->oldmetric) {
		vty_out(vty,
			"Multi topology IS-IS can only be used with wide metrics\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	if (mtid == (uint16_t)-1) {
		vty_out(vty, "Don't know topology '%s'\n", arg);
		return CMD_WARNING_CONFIG_FAILED;
	}

	return isis_circuit_mt_enabled_set(circuit, mtid, true);
}

DEFUN (no_circuit_topology,
       no_circuit_topology_cmd,
       "no " PROTO_NAME " topology " ISIS_MT_NAMES,
       NO_STR
       PROTO_HELP
       "Configure interface IS-IS topologies\n"
       ISIS_MT_DESCRIPTIONS)
{
	struct isis_circuit *circuit = isis_circuit_lookup(vty);
	if (!circuit)
		return CMD_ERR_NO_MATCH;
	const char *arg = argv[3]->arg;
	uint16_t mtid = isis_str2mtid(arg);

	if (circuit->area && circuit->area->oldmetric) {
		vty_out(vty,
			"Multi topology IS-IS can only be used with wide metrics\n");
		return CMD_WARNING_CONFIG_FAILED;
	}

	if (mtid == (uint16_t)-1) {
		vty_out(vty, "Don't know topology '%s'\n", arg);
		return CMD_WARNING_CONFIG_FAILED;
	}

	return isis_circuit_mt_enabled_set(circuit, mtid, false);
}

DEFUN (isis_bfd,
       isis_bfd_cmd,
       PROTO_NAME " bfd",
       PROTO_HELP
       "Enable BFD support\n")
{
	struct isis_circuit *circuit = isis_circuit_lookup(vty);

	if (!circuit)
		return CMD_ERR_NO_MATCH;

	if (circuit->bfd_info
	    && CHECK_FLAG(circuit->bfd_info->flags, BFD_FLAG_PARAM_CFG)) {
		return CMD_SUCCESS;
	}

	isis_bfd_circuit_param_set(circuit, BFD_DEF_MIN_RX,
				   BFD_DEF_MIN_TX, BFD_DEF_DETECT_MULT, true);

	return CMD_SUCCESS;
}

DEFUN (no_isis_bfd,
       no_isis_bfd_cmd,
       "no " PROTO_NAME " bfd",
       NO_STR
       PROTO_HELP
       "Disables BFD support\n"
)
{
	struct isis_circuit *circuit = isis_circuit_lookup(vty);

	if (!circuit)
		return CMD_ERR_NO_MATCH;

	if (!circuit->bfd_info)
		return CMD_SUCCESS;

	isis_bfd_circuit_cmd(circuit, ZEBRA_BFD_DEST_DEREGISTER);
	bfd_info_free(&circuit->bfd_info);
	return CMD_SUCCESS;
}

DEFUN (area_purge_originator,
       area_purge_originator_cmd,
       "[no] purge-originator",
       NO_STR
       "Use the RFC 6232 purge-originator\n")
{
	VTY_DECLVAR_CONTEXT(isis_area, area);

	area->purge_originator = !!strcmp(argv[0]->text, "no");
	return CMD_SUCCESS;
}

DEFUN (no_spf_delay_ietf,
       no_spf_delay_ietf_cmd,
       "no spf-delay-ietf",
       NO_STR
       "IETF SPF delay algorithm\n")
{
	VTY_DECLVAR_CONTEXT(isis_area, area);

	spf_backoff_free(area->spf_delay_ietf[0]);
	spf_backoff_free(area->spf_delay_ietf[1]);
	area->spf_delay_ietf[0] = NULL;
	area->spf_delay_ietf[1] = NULL;

	return CMD_SUCCESS;
}

DEFUN (spf_delay_ietf,
       spf_delay_ietf_cmd,
       "spf-delay-ietf init-delay (0-60000) short-delay (0-60000) long-delay (0-60000) holddown (0-60000) time-to-learn (0-60000)",
       "IETF SPF delay algorithm\n"
       "Delay used while in QUIET state\n"
       "Delay used while in QUIET state in milliseconds\n"
       "Delay used while in SHORT_WAIT state\n"
       "Delay used while in SHORT_WAIT state in milliseconds\n"
       "Delay used while in LONG_WAIT\n"
       "Delay used while in LONG_WAIT state in milliseconds\n"
       "Time with no received IGP events before considering IGP stable\n"
       "Time with no received IGP events before considering IGP stable (in milliseconds)\n"
       "Maximum duration needed to learn all the events related to a single failure\n"
       "Maximum duration needed to learn all the events related to a single failure (in milliseconds)\n")
{
	VTY_DECLVAR_CONTEXT(isis_area, area);

	long init_delay = atol(argv[2]->arg);
	long short_delay = atol(argv[4]->arg);
	long long_delay = atol(argv[6]->arg);
	long holddown = atol(argv[8]->arg);
	long timetolearn = atol(argv[10]->arg);

	size_t bufsiz = strlen(area->area_tag) + sizeof("IS-IS  Lx");
	char *buf = XCALLOC(MTYPE_TMP, bufsiz);

	snprintf(buf, bufsiz, "IS-IS %s L1", area->area_tag);
	spf_backoff_free(area->spf_delay_ietf[0]);
	area->spf_delay_ietf[0] =
		spf_backoff_new(master, buf, init_delay, short_delay,
				long_delay, holddown, timetolearn);

	snprintf(buf, bufsiz, "IS-IS %s L2", area->area_tag);
	spf_backoff_free(area->spf_delay_ietf[1]);
	area->spf_delay_ietf[1] =
		spf_backoff_new(master, buf, init_delay, short_delay,
				long_delay, holddown, timetolearn);

	XFREE(MTYPE_TMP, buf);
	return CMD_SUCCESS;
}

void isis_vty_init(void)
{
	install_element(INTERFACE_NODE, &isis_passive_cmd);
	install_element(INTERFACE_NODE, &no_isis_passive_cmd);

	install_element(INTERFACE_NODE, &isis_passwd_cmd);
	install_element(INTERFACE_NODE, &no_isis_passwd_cmd);

	install_element(INTERFACE_NODE, &isis_metric_cmd);
	install_element(INTERFACE_NODE, &no_isis_metric_cmd);

	install_element(INTERFACE_NODE, &isis_hello_interval_cmd);
	install_element(INTERFACE_NODE, &no_isis_hello_interval_cmd);

	install_element(INTERFACE_NODE, &isis_hello_multiplier_cmd);
	install_element(INTERFACE_NODE, &no_isis_hello_multiplier_cmd);

	install_element(INTERFACE_NODE, &csnp_interval_cmd);
	install_element(INTERFACE_NODE, &no_csnp_interval_cmd);

	install_element(INTERFACE_NODE, &psnp_interval_cmd);
	install_element(INTERFACE_NODE, &no_psnp_interval_cmd);

	install_element(INTERFACE_NODE, &circuit_topology_cmd);
	install_element(INTERFACE_NODE, &no_circuit_topology_cmd);

	install_element(INTERFACE_NODE, &isis_bfd_cmd);
	install_element(INTERFACE_NODE, &no_isis_bfd_cmd);

	install_element(ROUTER_NODE, &area_purge_originator_cmd);

	install_element(ROUTER_NODE, &spf_delay_ietf_cmd);
	install_element(ROUTER_NODE, &no_spf_delay_ietf_cmd);

	isis_vty_daemon_init();
}
