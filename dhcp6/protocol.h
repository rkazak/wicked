/*
 *	Build and parse DHCP6 packets
 *
 *	Copyright (C) 2010-2012, Olaf Kirch <okir@suse.de>
 *	Copyright (C) 2012 Marius Tomaschewski <mt@suse.de>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License along
 *	with this program; if not, see <http://www.gnu.org/licenses/> or write
 *	to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *	Boston, MA 02110-1301 USA.
 */

#ifndef __WICKED_DHCP6_PROTOCOL_H__
#define __WICKED_DHCP6_PROTOCOL_H__


/*
 * Socket buffer size defaults
 */
#define NI_DHCP6_RBUF_SIZE		65536		/* max. UDP packet  */
#define NI_DHCP6_WBUF_SIZE		1280		/* initial size     */

/*
 * Client/Server Message Formats, transaction-id
 * http://tools.ietf.org/html/rfc3315#section-6
 */
#define NI_DHCP6_XID_MASK		0x00ffffff	/* xid is 24 bit    */

/*
 * DHCPv6 Multicast Addresses
 * http://tools.ietf.org/html/rfc3315#section-5.1
 */
#define	NI_DHCP6_ALL_RAGENTS		"ff02::1:2"	/* relays & servers */
#define NI_DHCP6_ALL_SRVONLY		"ff05::1:3"	/* all servers only */

/*
 * DHCPv6 UDP Ports
 * http://tools.ietf.org/html/rfc3315#section-5.2
 */
#define NI_DHCP6_CLIENT_PORT		546
#define NI_DHCP6_SERVER_PORT		547
#define NI_DHCP6_CLIENT_SERVICE		"dhcpv6-client"
#define NI_DHCP6_SERVER_SERVICE		"dhcpv6-server"

/*
 * DHCPv6 Message Types
 *
 * http://tools.ietf.org/html/rfc3315#section-5.3
 *  and others from:
 * http://www.iana.org/assignments/dhcpv6-parameters/dhcpv6-parameters.xml
 */
enum NI_DHCP6_MSG_TYPE {
	NI_DHCP6_SOLICIT		= 1,	/* RFC3315 */
	NI_DHCP6_ADVERTISE		= 2,
	NI_DHCP6_REQUEST		= 3,
	NI_DHCP6_CONFIRM		= 4,
	NI_DHCP6_RENEW			= 5,
	NI_DHCP6_REBIND			= 6,
	NI_DHCP6_REPLY			= 7,
	NI_DHCP6_RELEASE		= 8,
	NI_DHCP6_DECLINE		= 9,
	NI_DHCP6_RECONFIGURE		= 10,
	NI_DHCP6_INFO_REQUEST		= 11,
	NI_DHCP6_RELAY_FORWARD		= 12,
	NI_DHCP6_RELAY_REPLY		= 13,
	NI_DHCP6_LEASEQUERY		= 14,	/* RFC5007 */
	NI_DHCP6_LEASEQUERY_REPLY	= 15,
	NI_DHCP6_LEASEQUERY_DONE	= 16,	/* RFC5460 */
	NI_DHCP6_LEASEQUERY_DATA	= 17,

	__NI_DHCP6_MSG_TYPE_MAX
};

/*
 * DHCPv6 Option Codes
 *
 * http://tools.ietf.org/html/rfc3315#section-24.3
 *
 * [Last Updated: 2012-03-29]
 * http://www.iana.org/assignments/dhcpv6-parameters/dhcpv6-parameters.xml
 */
enum NI_DHCP6_OPTION {
	/*					   0:	  Reserved   */
	NI_DHCP6_OPTION_CLIENTID		=  1,	/* [RFC3315] */
	NI_DHCP6_OPTION_SERVERID		=  2,
	NI_DHCP6_OPTION_IA_NA			=  3,
	NI_DHCP6_OPTION_IA_TA			=  4,
	NI_DHCP6_OPTION_IAADDR			=  5,
	NI_DHCP6_OPTION_ORO			=  6,
	NI_DHCP6_OPTION_PREFERENCE		=  7,
	NI_DHCP6_OPTION_ELAPSED_TIME		=  8,
	NI_DHCP6_OPTION_RELAY_MSG		=  9,
	/*					  10:	  Unassigned */
	NI_DHCP6_OPTION_AUTH			= 11,
	NI_DHCP6_OPTION_UNICAST			= 12,
	NI_DHCP6_OPTION_STATUS_CODE		= 13,
	NI_DHCP6_OPTION_RAPID_COMMIT		= 14,
	NI_DHCP6_OPTION_USER_CLASS		= 15,
	NI_DHCP6_OPTION_VENDOR_CLASS		= 16,
	NI_DHCP6_OPTION_VENDOR_OPTS		= 17,
	NI_DHCP6_OPTION_INTERFACE_ID		= 18,
	NI_DHCP6_OPTION_RECONF_MSG		= 19,
	NI_DHCP6_OPTION_RECONF_ACCEPT		= 20,
	NI_DHCP6_OPTION_SIP_SERVER_D		= 21,	/* [RFC3319] */
	NI_DHCP6_OPTION_SIP_SERVER_A		= 22,
	NI_DHCP6_OPTION_DNS_SERVERS		= 23,	/* [RFC3646] */
	NI_DHCP6_OPTION_DNS_DOMAINS		= 24,
	NI_DHCP6_OPTION_IA_PD			= 25,	/* [RFC3633] */
	NI_DHCP6_OPTION_IA_PREFIX		= 26,
	NI_DHCP6_OPTION_NIS_SERVERS		= 27,	/* [RFC3898] */
	NI_DHCP6_OPTION_NISP_SERVERS		= 28,
	NI_DHCP6_OPTION_NIS_DOMAIN_NAME		= 29,
	NI_DHCP6_OPTION_NISP_DOMAIN_NAME	= 30,
	NI_DHCP6_OPTION_SNTP_SERVERS		= 31,	/* [RFC4075] */
	NI_DHCP6_OPTION_INFO_REFRESH_TIME	= 32,	/* [RFC4242] */
	NI_DHCP6_OPTION_BCMCS_SERVER_D		= 33,	/* [RFC4280] */
	NI_DHCP6_OPTION_BCMCS_SERVER_A		= 34,
	/*					  35:	  Unassigned */
	NI_DHCP6_OPTION_GEOCONF_CIVIC		= 36,	/* [RFC4776] */
	NI_DHCP6_OPTION_REMOTE_ID		= 37,	/* [RFC4649] */
	NI_DHCP6_OPTION_SUBSCRIBER_ID		= 38,	/* [RFC4580] */
	NI_DHCP6_OPTION_CLIENT_FQDN		= 39,	/* [RFC4704] */
	NI_DHCP6_OPTION_PANA_AGENT		= 40,	/* [RFC5192] */
	NI_DHCP6_OPTION_POSIX_TIMEZONE		= 41,	/* [RFC4833] */
	NI_DHCP6_OPTION_POSIX_TIMEZONEDB	= 42,	/* [RFC4833] */
	NI_DHCP6_OPTION_ERO			= 43,	/* [RFC4994] */
	NI_DHCP6_OPTION_LQ_QUERY		= 44,	/* [RFC5007] */
	NI_DHCP6_OPTION_CLIENT_DATA		= 45,
	NI_DHCP6_OPTION_CLT_TIME		= 46,
	NI_DHCP6_OPTION_LQ_RELAY_DATA		= 47,
	NI_DHCP6_OPTION_LQ_CLIENT_LINK		= 48,
	NI_DHCP6_OPTION_MIP6_HNINF		= 49,	/* [RFC-ietf-mip6-hiopt-17] */
	NI_DHCP6_OPTION_MIP6_RELAY		= 50,	/* [RFC-ietf-mip6-hiopt-17] */
	NI_DHCP6_OPTION_V6_LOST			= 51,	/* [RFC5223] */
	NI_DHCP6_OPTION_CAPWAP_AC_V6		= 52,	/* [RFC5417] */
	NI_DHCP6_OPTION_RELAY_ID		= 53,	/* [RFC5460] */
	NI_DHCP6_OPTION_MOS_ADDRESSES		= 54,	/* [RFC5678] */
	NI_DHCP6_OPTION_MOS_DOMAINS		= 55,
	NI_DHCP6_OPTION_NTP_SERVER		= 56,	/* [RFC5908] */
	NI_DHCP6_OPTION_V6_ACCESS_DOMAIN	= 57,	/* [RFC5986] */
	NI_DHCP6_OPTION_SIP_UA_CS_LIST		= 58,	/* [RFC6011] */
	NI_DHCP6_OPTION_BOOTFILE_URL		= 59,	/* [RFC5970] */
	NI_DHCP6_OPTION_BOOTFILE_PARAM		= 60,	/* [RFC5970] */
	NI_DHCP6_OPTION_CLIENT_ARCH_TYPE	= 61,	/* [RFC5970] */
	NI_DHCP6_OPTION_NII			= 62,	/* [RFC5970] */
	NI_DHCP6_OPTION_GEOLOCATION		= 63,	/* [RFC6225] */
	NI_DHCP6_OPTION_AFTR_NAME		= 64,	/* [RFC6334] */
	NI_DHCP6_OPTION_ERP_LOCAL_DOMAIN	= 65,	/* [RFC6440] */
	NI_DHCP6_OPTION_RSOO			= 66,	/* [RFC6422] */
	NI_DHCP6_OPTION_PD_EXCLUDE		= 67,	/* [RFC-ietf-dhc-pd-exclude-04] */
	NI_DHCP6_OPTION_VSS			= 68,	/* [RFC-ietf-dhc-vpn-option-15] */
	/*					69-255:	  Unassigned */

	__NI_DHCP6_OPTION_MAX
};

/*
 * DHCPv6 Status Codes
 * http://tools.ietf.org/html/rfc3315#section-24.4
 */
enum NI_DHCP6_STATUS_CODE {
	NI_DHCP6_STATUS_SUCCESS			= 0,
	NI_DHCP6_STATUS_FAILURE			= 1,
	NI_DHCP6_STATUS_NOADDRS			= 2,
	NI_DHCP6_STATUS_NOBINDING		= 3,
	NI_DHCP6_STATUS_NOTONLINK		= 4,
	NI_DHCP6_STATUS_USEMULTICAST		= 5,
};

enum ni_dhcp6_ia_type {
	NI_DHCP6_IA_NA_TYPE = 0x03,
	NI_DHCP6_IA_TA_TYPE = 0x04,
	NI_DHCP6_IA_PD_TYPE = 0x19,
};

/*
 * DHCPv6 Timings
 *
 * http://tools.ietf.org/html/rfc3315#section-5.5
 *
 *      Parameter		  Value    Description
 * ---------------------------------------------------------------------------
 */
#define	NI_DHCP6_SOL_MAX_DELAY	   1000	/* Max delay of first Solicit  */
#define NI_DHCP6_SOL_TIMEOUT	   1000	/* Initial Solicit timeout     */
#define NI_DHCP6_SOL_MAX_RT	 120000	/* Max Solicit timeout value   */
#define NI_DHCP6_REQ_TIMEOUT	   1000	/* Initial Request timeout     */
#define NI_DHCP6_REQ_MAX_RC	     10	/* Max Request retry attempts  */
#define NI_DHCP6_REQ_MAX_RT	  30000	/* Max Request timeout value   */
#define NI_DHCP6_CNF_MAX_DELAY	   1000	/* Max delay of first Confirm  */
#define NI_DHCP6_CNF_TIMEOUT	   1000	/* Initial Confirm timeout     */
#define NI_DHCP6_CNF_MAX_RT	   4000	/* Max Confirm timeout         */
#define NI_DHCP6_CNF_MAX_RD	  10000	/* Max Confirm duration        */
#define NI_DHCP6_REN_TIMEOUT	  10000	/* Initial Renew timeout       */
#define NI_DHCP6_REN_MAX_RT	 600000	/* Max Renew timeout value     */
#define NI_DHCP6_REB_TIMEOUT	  10000	/* Initial Rebind timeout      */
#define NI_DHCP6_REB_MAX_RT	 600000	/* Max Rebind timeout value    */
#define NI_DHCP6_INF_MAX_DELAY	   1000	/* Max delay of first Info-req */
#define NI_DHCP6_INF_TIMEOUT	   1000	/* Initial Info-req timeout    */
#define NI_DHCP6_INF_MAX_RT	 120000	/* Max Info-req timeout value  */
#define NI_DHCP6_REL_TIMEOUT	   1000	/* Initial Release timeout     */
#define NI_DHCP6_REL_MAX_RC	      5	/* Max Release attempts        */
#define NI_DHCP6_DEC_TIMEOUT	   1000	/* Initial Decline timeout     */
#define NI_DHCP6_DEC_MAX_RC	      5	/* Max Decline attempts        */
#define NI_DHCP6_REC_TIMEOUT	   2000	/* Initial Reconfigure timeout */
#define NI_DHCP6_REC_MAX_RC	      8	/* Max Reconfigure attempts    */
#define NI_DHCP6_HOP_COUNT_LIMIT     32	/* Max hop count in Relay-fwd  */
#define NI_DHCP6_MAX_JITTER	    100	/* Randomization factor [± 0.1]*/

/*
 * Option Format
 * http://tools.ietf.org/html/rfc3315#section-22.1
 */
typedef struct ni_dhcp6_option_header {
	uint16_t			code;
	uint16_t			len;
	unsigned char			data[];
} ni_dhcp6_option_header_t;

/*
 * Client/Server Message Formats
 * http://tools.ietf.org/html/rfc3315#section-6
 */
typedef union ni_dhcp6_client_header {
	uint8_t				type;
	uint32_t			xid;
} ni_dhcp6_client_header_t;

/*
 * Relay Agent/Server Message Formats
 *
 * http://tools.ietf.org/html/rfc3315#section-7
 */
typedef struct ni_dhcp6_relay_header {
	uint8_t				type;
	uint8_t				hop_count;
	struct in6_addr 		link_addr;
	struct in6_addr 		peer_addr;
} ni_dhcp6_relay_header_t;


/*
 * Union of Client/Server and Relay/Agent Messages
 */
typedef union ni_dhcp6_packet_header {
	uint8_t				type;
	ni_dhcp6_client_header_t	client;
	ni_dhcp6_relay_header_t		relay;
} ni_dhcp6_packet_header_t;

/*
typedef struct ni_dhcp6_option {
	uint16_t			code;
	uint16_t			len;
	unsigned char			data[];
} ni_dhcp6_option_t;

typedef struct ni_dhcp6_option_array {
	unsigned int			count;
	ni_dhcp6_option_t *		data;
} ni_dhcp6_option_array_t;
*/


/*
 * functions used in device.c and fsm.c
 */
extern const char *	ni_dhcp6_message_name(unsigned int);
extern const char *	ni_dhcp6_option_name(unsigned int);

extern int		ni_dhcp6_socket_open(ni_dhcp6_device_t *);

extern int		ni_dhcp6_build_message( const ni_dhcp6_device_t *, unsigned int,
		 				const ni_addrconf_lease_t *, ni_buffer_t *);

extern ni_int_range_t	ni_dhcp6_jitter_rebase(unsigned int msec, int lower, int upper);
extern ni_bool_t	ni_dhcp6_set_message_timing(ni_dhcp6_device_t *dev, unsigned int msg_type);

extern int		ni_dhcp6_client_parse_response(ni_dhcp6_device_t *, ni_buffer_t *,
					const struct in6_addr *, ni_addrconf_lease_t **);

/* FIXME: */
extern void		ni_dhcp6_status_free(struct ni_dhcp6_status *status);
extern void		ni_dhcp6_ia_list_destroy(struct ni_dhcp6_ia **list);
void			ni_addrconf_dhcp6_lease_free(ni_addrconf_lease_t *);

const char *		__ni_dhcp6_format_time(const struct timeval *tv);

#endif /* __WICKED_DHCP6_PROTOCOL_H__ */