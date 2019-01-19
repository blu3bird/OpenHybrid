/* OpenHybrid - an open GRE tunnel bonding implemantion
 * Copyright (C) 2019  Friedrich Oslage <friedrich@oslage.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "openhybrid.h"
#include <libmnl/libmnl.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>
#include <linux/if_tunnel.h>
#include <linux/ip6_tunnel.h>

bool create_tunnel_dev(uint8_t tuntype) {
    struct mnl_socket *nl_sock = NULL;
    if ((nl_sock = mnl_socket_open(NETLINK_ROUTE)) == NULL) {
        logger(LOG_ERROR, "Opening netlink socket failed: %s\n", strerror(errno));
        return false;
    }

    uint8_t buf[MNL_SOCKET_BUFFER_SIZE];
    memset(buf, 0, MNL_SOCKET_BUFFER_SIZE);
    struct nlmsghdr *nlh = mnl_nlmsg_put_header(buf);
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_ACK;
    nlh->nlmsg_type = RTM_NEWLINK;

    struct ifinfomsg *ifinfo = mnl_nlmsg_put_extra_header(nlh, sizeof(struct ifinfomsg));
    ifinfo->ifi_family = AF_UNSPEC;
    ifinfo->ifi_change = IFF_UP;
    ifinfo->ifi_flags = IFF_UP;

    if (tuntype == GRECP_TUNTYPE_LTE) {
        mnl_attr_put_str(nlh, IFLA_IFNAME, runtime.lte.gre_interface_name);
    } else {
        mnl_attr_put_str(nlh, IFLA_IFNAME, runtime.dsl.gre_interface_name);
    }
    mnl_attr_put_u32(nlh, IFLA_MTU, runtime.gre_interface_mtu);

    struct nlattr *linkinfo = mnl_attr_nest_start(nlh, IFLA_LINKINFO);
    mnl_attr_put_str(nlh, IFLA_INFO_KIND, "ip6gre");

    struct nlattr *tunnelinfo = mnl_attr_nest_start(nlh, IFLA_INFO_DATA);
    struct sockaddr_in6 addr = {};
    if (tuntype == GRECP_TUNTYPE_LTE) {
        addr.sin6_addr = get_primary_ip6(runtime.lte.interface_name);
    } else {
        addr.sin6_addr = get_primary_ip6(runtime.dsl.interface_name);
    }
    mnl_attr_put(nlh, IFLA_GRE_LOCAL, sizeof(addr.sin6_addr), &addr.sin6_addr);
    mnl_attr_put(nlh, IFLA_GRE_REMOTE, sizeof(runtime.haap.ip), &runtime.haap.ip);
    mnl_attr_put_u32(nlh, IFLA_GRE_FLAGS, IP6_TNL_F_IGN_ENCAP_LIMIT);
    mnl_attr_put_u8(nlh, IFLA_GRE_TTL, 64);
    mnl_attr_put_u32(nlh, IFLA_GRE_IKEY, htonl(runtime.haap.bonding_key));
    mnl_attr_put_u32(nlh, IFLA_GRE_OKEY, htonl(runtime.haap.bonding_key));
    mnl_attr_put_u16(nlh, IFLA_GRE_IFLAGS, GRE_KEY);
    mnl_attr_put_u16(nlh, IFLA_GRE_OFLAGS, GRE_KEY);
    /* TODO: implement reordering and set GRE_SEQ flag */

    mnl_attr_nest_end(nlh, tunnelinfo);
    mnl_attr_nest_end(nlh, linkinfo);

    mnl_socket_sendto(nl_sock, nlh, nlh->nlmsg_len);
    mnl_socket_recvfrom(nl_sock, buf, sizeof(buf));

    nlh = (struct nlmsghdr*) buf;
    if (nlh->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *nlerr = mnl_nlmsg_get_payload(nlh);
        if (nlerr->error) {
            if (tuntype == GRECP_TUNTYPE_LTE) {
                logger(LOG_ERROR, "Creation of LTE Tunnel interface '%s' failed: %s\n", runtime.lte.gre_interface_name, strerror(-nlerr->error));
            } else {
                logger(LOG_ERROR, "Creation of DSL Tunnel interface '%s' failed: %s\n", runtime.dsl.gre_interface_name, strerror(-nlerr->error));
            }
            return false;
        }
    }

    if (tuntype == GRECP_TUNTYPE_LTE) {
        logger(LOG_INFO, "LTE Tunnel interface '%s' created.\n", runtime.lte.gre_interface_name);
        trigger_event("tunnelup_lte");
    } else {
        logger(LOG_INFO, "DSL Tunnel interface '%s' created.\n", runtime.dsl.gre_interface_name);
        trigger_event("tunnelup_dsl");
    }
    return true;
}

bool destroy_tunnel_dev(uint8_t tuntype) {
    struct mnl_socket *nl_sock;
    if ((nl_sock = mnl_socket_open(NETLINK_ROUTE)) == NULL) {
        logger(LOG_ERROR, "Opening netlink socket failed: %s\n", strerror(errno));
        return true;
    }

    uint8_t buf[MNL_SOCKET_BUFFER_SIZE];
    struct nlmsghdr *nlh = mnl_nlmsg_put_header(buf);
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_type = RTM_DELLINK;

    struct ifinfomsg *ifinfo = mnl_nlmsg_put_extra_header(nlh, sizeof(struct ifinfomsg));
    ifinfo->ifi_family = AF_UNSPEC;

    if (tuntype == GRECP_TUNTYPE_LTE) {
        mnl_attr_put_str(nlh, IFLA_IFNAME, runtime.lte.gre_interface_name);
    } else {
        mnl_attr_put_str(nlh, IFLA_IFNAME, runtime.dsl.gre_interface_name);
    }

    mnl_socket_sendto(nl_sock, nlh, nlh->nlmsg_len);
    mnl_socket_recvfrom(nl_sock, buf, sizeof(buf));

    nlh = (struct nlmsghdr*) buf;
    if (nlh->nlmsg_type == NLMSG_ERROR){
        struct nlmsgerr *nlerr = mnl_nlmsg_get_payload(nlh);
        if (nlerr->error) {
            if (tuntype == GRECP_TUNTYPE_LTE) {
                logger(LOG_ERROR, "Destruction of LTE Tunnel interface '%s' failed: %s\n", runtime.lte.gre_interface_name, strerror(-nlerr->error));
            } else {
                logger(LOG_ERROR, "Destruction of DSL Tunnel interface '%s' failed: %s\n", runtime.dsl.gre_interface_name, strerror(-nlerr->error));
            }
            return false;
        }
    }

    if (tuntype == GRECP_TUNTYPE_LTE) {
        logger(LOG_INFO, "LTE Tunnel interface '%s' destroyed.\n", runtime.lte.gre_interface_name);
        trigger_event("tunneldown_lte");
    } else {
        logger(LOG_INFO, "DSL Tunnel interface '%s' destroyed.\n", runtime.dsl.gre_interface_name);
        trigger_event("tunneldown_dsl");
    }
    return true;
}
