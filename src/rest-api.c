/*
 * No REST for the wicked!
 *
 * Copyright (C) 2010 Olaf Kirch <okir@suse.de>
 */

#include <sys/poll.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>

#include <wicked/netinfo.h>
#include <wicked/wicked.h>
#include <wicked/logging.h>
#include <wicked/xml.h>
#include <wicked/xpath.h>
#include "netinfo_priv.h"


static ni_rest_node_t *	ni_rest_node_lookup(ni_rest_node_t *, const char *, const char **);
static void		ni_rest_generate_meta(ni_rest_node_t *, xml_node_t *);

/*
 * construct and destroy wicked request object
 */
void
ni_wicked_request_init(ni_wicked_request_t *req)
{
	memset(req, 0, sizeof(*req));
	req->cmd = -1;
}

void
ni_wicked_request_destroy(ni_wicked_request_t *req)
{
	ni_string_free(&req->path);
	xml_node_free(req->xml_out);

	ni_var_array_destroy(&req->options);
	ni_string_free(&req->error_msg);
	memset(req, 0, sizeof(*req));
}

/*
 * Pass "options" along with a wicked request.
 * The main motivation for having this kludge is to pass
 * in the "root" parameter, i.e. the directory relative
 * to which we should look for sysconfig files.
 * Needed to support netcf.
 */
int
ni_wicked_request_add_option(ni_wicked_request_t *req,
		const char *name, const char *value)
{
	if (!name || !value)
		return 0;
	if (strchr(name, '\n')) {
		ni_error("bad option name \"%s\"", name);
		return -1;
	}
	if (strchr(value, '\n')) {
		ni_error("Bad value for option %s", name);
		return -1;
	}
	ni_var_array_set(&req->options, name, value);
	return 0;
}

const char *
ni_wicked_request_get_option(ni_wicked_request_t *req, const char *name)
{
	ni_var_t *var;

	var = ni_var_array_get(&req->options, name);
	if (var && var->value && var->value[0])
		return var->value;
	return NULL;
}

/*
 * Map GET/PUT/POST/DELETE strings
 */
int
ni_wicked_rest_op_parse(const char *cmd)
{
	if (!strcasecmp(cmd, "get"))
		return NI_REST_OP_GET;
	if (!strcasecmp(cmd, "put"))
		return NI_REST_OP_PUT;
	if (!strcasecmp(cmd, "post"))
		return NI_REST_OP_POST;
	if (!strcasecmp(cmd, "delete"))
		return NI_REST_OP_DELETE;
	return -1;
}

const char *
ni_wicked_rest_op_print(int cmd)
{
	static const char *op_name[__NI_REST_OP_MAX] = {
		[NI_REST_OP_GET] = "get",
		[NI_REST_OP_PUT] = "put",
		[NI_REST_OP_POST] = "post",
		[NI_REST_OP_DELETE] = "delete",
	};

	if (cmd < 0 || cmd >= __NI_REST_OP_MAX)
		return "unknown";

	return op_name[cmd];
}

/*
 * Parse a WICKED request, usually reading from a socket.
 */
int
ni_wicked_request_parse(ni_wicked_request_t *req, FILE *in)
{
	char buffer[1024];
	char *cmd, *s;

	ni_wicked_request_init(req);

	if (fgets(buffer, sizeof(buffer), in) == NULL) {
		werror(req, "unable to read request from socket");
		return - 1;
	}

	for (cmd = s = buffer; *s && !isspace(*s); ++s)
		;

	while (isspace(*s))
		*s++ = '\0';
	ni_string_dup(&req->path, s);

	s = req->path + strlen(req->path);
	while (s > req->path && isspace(s[-1]))
		*--s = '\0';

	if (cmd[0] == '\0' || req->path[0] == '\0') {
		werror(req, "cannot parse REST request");
		return -1;
	}

	req->cmd = ni_wicked_rest_op_parse(cmd);
	if (req->cmd < 0) {
		werror(req, "unknown command \"%s\"", cmd);
		return -1;
	}

	/* Get options */
	while (fgets(buffer, sizeof(buffer), in) != NULL) {
		int len = strlen(buffer);
		char *s;

		while (len && isspace(buffer[len-1]))
			buffer[--len] = '\0';

		if (buffer[0] == '\0')
			break;

		for (s = buffer; isalpha(*s); ++s)
			*s = tolower(*s);
		while (*s == ':' || isspace(*s))
			*s++ = '\0';

		ni_wicked_request_add_option(req, buffer, s);
	}

	/* Now get the XML document, if any */
	req->xml_in = xml_node_scan(in);
	if (req->xml_in == NULL) {
		werror(req, "unable to parse xml document");
		return -1;
	}

	return 0;
}

/*
 * Call the local wicked server to process a REST call
 * This is what wicked clients usually call.
 */
int
ni_wicked_call_indirect(ni_wicked_request_t *req)
{
	char respbuf[256];
	unsigned int i;
	FILE *sock;
	int fd;

	fd = ni_server_connect();
	if (fd < 0)
		return -1;

	if (!(sock = fdopen(fd, "w+"))) {
		werror(req, "cannot fdopen local socket: %m");
		return -1;
	}

	fprintf(sock, "%s %s\n", ni_wicked_rest_op_print(req->cmd), req->path);
	for (i = 0; i < req->options.count; ++i) {
		ni_var_t *var = &req->options.data[i];

		fprintf(sock, "%s: %s\n", var->name, var->value);
	}
	fprintf(sock, "\n");
	fflush(sock);

	if (req->xml_in) {
		if (xml_node_print(req->xml_in, sock) < 0) {
			werror(req, "write error on socket: %m");
			goto error;
		}
	}

	fflush(sock);

	/* Tell the server we're done sending */
	shutdown(fd, SHUT_WR);

	if (fgets(respbuf, sizeof(respbuf), sock) == NULL) {
		werror(req, "error receiving response from server: EOF");
		goto error;
	}
	respbuf[strcspn(respbuf, "\r\n")] = '\0';
	if (strcmp(respbuf, "OK")) {
		ni_string_dup(&req->error_msg, respbuf);
		goto error;
	}

	if ((req->xml_out = xml_node_scan(sock)) == NULL) {
		werror(req, "error receiving response from server: %m");
		goto error;
	}

	fclose(sock);
	return 0;

error:
	fclose(sock);
	return -1;
}

/*
 * Process a REST call directly.
 * This is what the wicked server calls to handle an incoming request.
 */
int
ni_wicked_call_direct(ni_wicked_request_t *req)
{
	return __ni_wicked_call_direct(req, &ni_rest_root_node);
}

int
__ni_wicked_call_direct(ni_wicked_request_t *req, ni_rest_node_t *root_node)
{
	ni_rest_node_t *node;
	const char *remainder = NULL;

	if (ni_debug & NI_TRACE_WICKED) {
		unsigned int i;
		ni_trace("Processing REST request %s \"%s\"",
				ni_wicked_rest_op_print(req->cmd), req->path);
		if (req->options.count)
			ni_trace("Options:");
		for (i = 0; i < req->options.count; ++i) {
			ni_var_t *var = &req->options.data[i];

			ni_trace("  %s=\"%s\"", var->name, var->value);
		}
	}

	node = ni_rest_node_lookup(root_node, req->path, &remainder);
	if (!node) {
		werror(req, "unknown path \"%s\"", req->path);
		return -1;
	}

	if (node->ops.fn[req->cmd] == NULL) {
		werror(req, "%s command not supported for this path",
				ni_wicked_rest_op_print(req->cmd));
		return -1;
	}

	return node->ops.fn[req->cmd](remainder, req);
}

static ni_handle_t *
system_handle(ni_wicked_request_t *req)
{
	ni_handle_t *nih;

	nih = ni_state_open();
	if (nih == NULL) {
		werror(req, "unable to obtain netinfo handle");
		return NULL;
	}
	if (ni_refresh(nih) < 0) {
		werror(req, "cannot refresh interface list!");
		ni_close(nih);
		return NULL;
	}

	return nih;
}

static ni_handle_t *
config_handle(ni_wicked_request_t *req)
{
	const char *root_dir;
	ni_handle_t *nih;

	root_dir = ni_wicked_request_get_option(req, "root");
	nih = ni_netconfig_open(ni_netconfig_default_syntax(root_dir));
	if (nih == NULL) {
		werror(req, "unable to obtain netinfo handle");
		return NULL;
	}
	if (ni_refresh(nih) < 0) {
		werror(req, "cannot refresh interface list!");
		ni_close(nih);
		return NULL;
	}

	return nih;
}

static int
generic_interface_response(ni_handle_t *nih, ni_interface_t *ifp, ni_wicked_request_t *req)
{
	ni_syntax_t *xmlsyntax = ni_default_xml_syntax();

	if (ifp == NULL) {
		xml_document_t *doc;

		doc = ni_syntax_xml_from_all(xmlsyntax, nih);
		if (doc) {
			req->xml_out = xml_document_take_root(doc);
			xml_document_free(doc);
		}
	} else {
		req->xml_out = ni_syntax_xml_from_interface(xmlsyntax, nih, ifp);
	}

	if (req->xml_out == NULL) {
		werror(req, "cannot render interface information");
		return -1;
	}

	return 0;
}

static int
generic_interface_get(ni_handle_t *nih, const char *path, ni_wicked_request_t *req)
{
	ni_interface_t *ifp = NULL;

	if (nih == NULL)
		return -1;

	if (path != NULL) {
		/* select interface and display only that */
		if (!(ifp = ni_interface_by_name(nih, path))) {
			werror(req, "interface %s not known", path);
			return -1;
		}
	}

	return generic_interface_response(nih, ifp, req);
}

static int
system_interface_get(const char *path, ni_wicked_request_t *req)
{
	return generic_interface_get(system_handle(req), path, req);
}

static int
config_interface_get(const char *path, ni_wicked_request_t *req)
{
	return generic_interface_get(config_handle(req), path, req);
}

static int
generic_interface_put(ni_handle_t *nih, const char *ifname, ni_wicked_request_t *req)
{
	ni_interface_t *ifp = NULL;
	ni_handle_t *cnih = NULL;
	xml_node_t *cfg_xml;
	int rv = -1;

	if (nih == NULL)
		return -1;

	if (ifname == NULL) {
		werror(req, "no interface name given");
		return -1;
	}

	cnih = ni_dummy_open();
	if (cnih == NULL) {
		werror(req, "unable to create netinfo dummy handle");
		goto failed;
	}

	if (__ni_syntax_xml_to_all(ni_default_xml_syntax(), cnih, req->xml_in) < 0) {
		werror(req, "unable to parse interface configuration");
		goto failed;
	}

	if (!(ifp = ni_interface_by_name(cnih, ifname))) {
		werror(req, "cannot find configuration for interface %s", ifname);
		goto failed;
	}

	/* Find the XML intrface element - we want to pass it to the configure
	 * routine. This helps us write flexible extensions */
	for (cfg_xml = req->xml_in->children; cfg_xml; cfg_xml = cfg_xml->next) {
		const char *name;

		if (strcmp(cfg_xml->name, "interface"))
			continue;
		if ((name = xml_node_get_attr(cfg_xml, "name")) && !strcmp(name, ifname))
			break;
	}
	if (cfg_xml == NULL) {
		werror(req, "surprising, found interface %s but no xml?!", ifname);
		goto failed;
	}

	if (ni_interface_configure(nih, ifp, cfg_xml) < 0) {
		werror(req, "error configuring interface %s", ifname);
		goto failed;
	}

	if (!(ifp = ni_interface_by_name(nih, ifname))) {
		werror(req, "cannot find current status for interface %s", ifname);
		goto failed;
	}

	rv = generic_interface_response(nih, ifp, req);

failed:
	if (cnih)
		ni_close(cnih);
	return rv;
}

static int
system_interface_put(const char *path, ni_wicked_request_t *req)
{
	return generic_interface_put(system_handle(req), path, req);
}

static int
config_interface_put(const char *path, ni_wicked_request_t *req)
{
	return generic_interface_put(config_handle(req), path, req);
}

static int
generic_interface_delete(ni_handle_t *nih, const char *ifname, ni_wicked_request_t *req)
{
	if (nih == NULL)
		return -1;

	if (ifname == NULL) {
		werror(req, "DELETE: no interface name given");
		return -1;
	}

	if (ni_interface_delete(nih, ifname) < 0) {
		werror(req, "unable to delete %s", ifname);
		return -1;
	}

	return 0;
}

static int
system_interface_delete(const char *path, ni_wicked_request_t *req)
{
	return generic_interface_delete(system_handle(req), path, req);
}

static int
config_interface_delete(const char *path, ni_wicked_request_t *req)
{
	return generic_interface_delete(config_handle(req), path, req);
}

static ni_rest_node_t	ni_rest_system_interface_node = {
	.name		= "interface",
	.ops = {
	    .byname = {
		.get	= system_interface_get,
		.put	= system_interface_put,
		.delete	= system_interface_delete,
	    },
	},
};

static ni_rest_node_t	ni_rest_config_interface_node = {
	.name		= "interface",
	.ops = {
	    .byname = {
		.get	= config_interface_get,
		.put	= config_interface_put,
		.delete	= config_interface_delete,
	    },
	},
};

static int
system_hostname_get(const char *path, ni_wicked_request_t *req)
{
	char hostname[256];

	if (path && *path) {
		werror(req, "excess elements in path");
		return -1;
	}

	if (gethostname(hostname, sizeof(hostname)) < 0) {
		werror(req, "error getting hostname");
		return -1;
	}

	req->xml_out = xml_node_new("hostname", NULL);
	xml_node_set_cdata(req->xml_out, hostname);
	return 0;
}

static int
system_hostname_put(const char *path, ni_wicked_request_t *req)
{
	char *hostname, *sp;
	xml_node_t *hnode;
	unsigned int n;

	if (path && *path) {
		werror(req, "excess elements in path");
		return -1;
	}

	if (!req->xml_in
	 || !(hnode = xml_node_get_child(req->xml_in, "hostname"))
	 || !(sp = hnode->cdata)) {
		werror(req, "bad or missing XML document");
		return -1;
	}

	while (isspace(*sp))
		++sp;
	hostname = sp;

	n = strlen(hostname);
	while (n && isspace(hostname[n-1]))
		hostname[--n] = '\0';

	/* Be strict - do not accept garbage in hostnames. Note that
	 * this also excludes UTF8 encoded names */
	for (n = 0; hostname[n]; ++n) {
		unsigned char cc = hostname[n];

		if (cc <= 0x20 || cc >= 0x7f) {
			werror(req, "illegal character in hostname");
			return -1;
		}
	}

	if (sethostname(hostname, n) < 0) {
		werror(req, "error setting hostname");
		return -1;
	}

	req->xml_out = xml_node_new("hostname", NULL);
	xml_node_set_cdata(req->xml_out, hostname);
	return 0;
}

static ni_rest_node_t	ni_rest_system_hostname_node = {
	.name		= "hostname",
	.ops = {
	    .byname = {
		.get	= system_hostname_get,
		.put	= system_hostname_put,
	    },
	},
};

static int
system_meta_get(const char *path, ni_wicked_request_t *req)
{
	if (path && *path) {
		werror(req, "excess elements in path");
		return -1;
	}

	req->xml_out = xml_node_new("meta", NULL);
	ni_rest_generate_meta(NULL, req->xml_out);
	return 0;
}

static ni_rest_node_t	ni_rest_meta = {
	.name		= "meta",
	.ops = {
	    .byname = {
		.get	= system_meta_get,
	    },
	},
};

static ni_rest_node_t	ni_rest_system_node = {
	.name		= "system",
	.children = {
		&ni_rest_system_interface_node,
		&ni_rest_system_hostname_node,
	},
};

static ni_rest_node_t	ni_rest_config_node = {
	.name		= "config",
	.children = {
		&ni_rest_config_interface_node,
	},
};

ni_rest_node_t	ni_rest_root_node = {
	.name		= "/",
	.children = {
		&ni_rest_config_node,
		&ni_rest_system_node,
		&ni_rest_meta,
	},
};

static ni_rest_node_t *
ni_rest_node_find_child(ni_rest_node_t *node, const char *name)
{
	unsigned int i;

	for (i = 0; i < __NI_REST_CHILD_MAX; ++i) {
		ni_rest_node_t *child = node->children[i];

		if (child == NULL)
			break;
		if (!strcmp(child->name, name))
			return child;
	}
	return NULL;
}

ni_rest_node_t *
ni_rest_node_lookup(ni_rest_node_t *root, const char *path, const char **remainder)
{
	ni_rest_node_t *node = root;
	char *copy, *pos;

	copy = pos = strdup(path);
	while (*pos) {
		char *comp;

		while (*pos == '/')
			++pos;
		comp = pos;

		/* No more children; remainder of path is interpreted by node */
		if (node->children[0] == NULL)
			break;

		/* Find end of component, and NUL terminate it */
		pos += strcspn(pos, "/");
		if (*pos)
			*pos++ = '\0';

		node = ni_rest_node_find_child(node, comp);
		if (!node)
			return NULL;
	}

	if (*pos == '\0')
		*remainder = NULL;
	else
		*remainder = path + (pos - copy);
	free(copy);
	return node;
}

static void
ni_rest_generate_meta(ni_rest_node_t *node, xml_node_t *xml_parent)
{
	unsigned int i, j;

	if (node == NULL)
		node = &ni_rest_root_node;

	for (j = 0; j < __NI_REST_OP_MAX; j++ ) {
		if (node->ops.fn[j] != NULL) {
			switch (j) {
			case NI_REST_OP_GET:
				xml_node_add_attr(xml_parent, "get", NULL);
				continue;
			case NI_REST_OP_PUT:
				xml_node_add_attr(xml_parent, "put", NULL);
				continue;
			case NI_REST_OP_POST:
				xml_node_add_attr(xml_parent, "post", NULL);
				continue;
			case NI_REST_OP_DELETE:
				xml_node_add_attr(xml_parent, "delete", NULL);
				continue;
			}

		}		
	}

	for (i = 0; i < __NI_REST_CHILD_MAX; ++i) {
		ni_rest_node_t *child = node->children[i];
		xml_node_t *child_xml;

		if (child == NULL)
			break;
		child_xml = xml_node_new(child->name, xml_parent);
		ni_rest_generate_meta(child, child_xml);
	}
}

void
werror(ni_wicked_request_t *req, const char *fmt, ...)
{
	char buffer[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, ap);
	ni_string_dup(&req->error_msg, buffer);
	va_end(ap);
}
