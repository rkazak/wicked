/*
 * Addrconf stubs for the DHCP helper
 *
 * Copyright (C) 2010 Olaf Kirch <okir@suse.de>
 */

#include <sys/poll.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include <wicked/netinfo.h>
#include <wicked/logging.h>
#include <wicked/wicked.h>
#include <wicked/xml.h>
#include <wicked/addrconf.h>

extern int	ni_dhcp_xml_from_lease(const ni_addrconf_t *, const ni_addrconf_lease_t *, xml_node_t *);
extern int	ni_dhcp_xml_to_lease(const ni_addrconf_t *, ni_addrconf_lease_t *, const xml_node_t *);

static int
ni_dhcp_addrconf_request(const ni_addrconf_t *acm, ni_interface_t *ifp, const xml_node_t *cfg_xml)
{
	int rv;

	if (!ni_afinfo_addrconf_test(&ifp->ipv4, NI_ADDRCONF_DHCP)) {
		ni_warn("%s: DHCP not enabled", __FUNCTION__);
		ni_afinfo_addrconf_enable(&ifp->ipv4, NI_ADDRCONF_DHCP);
	}

	rv = ni_objectmodel_dhcp4_acquire(ifp, ifp->ipv4.request[NI_ADDRCONF_DHCP]);
	if (rv < 0)
		ni_error("dhcp4_acquire call failed: %s", ni_strerror(rv));
	return rv;
}

static int
ni_dhcp_addrconf_release(const ni_addrconf_t *acm, ni_interface_t *ifp, ni_addrconf_lease_t *lease)
{
	int rv;

	if (ni_afinfo_addrconf_test(&ifp->ipv4, NI_ADDRCONF_DHCP)) {
		ni_warn("%s: DHCP still marked enabled", __FUNCTION__);
		ni_afinfo_addrconf_disable(&ifp->ipv4, NI_ADDRCONF_DHCP);
	}

	rv = ni_objectmodel_dhcp4_release(ifp, lease);
	if (rv < 0)
		ni_error("dhcp4_release call failed: %s", ni_strerror(rv));
	return rv;
}

static int
ni_dhcp_is_valid(const ni_addrconf_t *acm, const ni_addrconf_lease_t *lease)
{
	time_t now = time(NULL);

	if (lease->state != NI_ADDRCONF_STATE_GRANTED)
		return 0;
	if (lease->time_acquired + lease->dhcp.lease_time <= now)
		return 0;
	return 1;
}

ni_addrconf_t ni_dhcp_addrconf = {
	.type = NI_ADDRCONF_DHCP,
	.supported_af = NI_AF_MASK_IPV4,

	.request = ni_dhcp_addrconf_request,
	.release = ni_dhcp_addrconf_release,
	.is_valid = ni_dhcp_is_valid,
	.xml_from_lease = ni_dhcp_xml_from_lease,
	.xml_to_lease = ni_dhcp_xml_to_lease,
};
