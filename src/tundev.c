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
#include <linux/if.h>
#include <linux/if_tun.h>
#include <fcntl.h>
#include <sys/ioctl.h>


bool create_gre_tunnel_dev() {
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

    mnl_attr_put_str(nlh, IFLA_IFNAME, runtime.tunnel_interface_name);
    mnl_attr_put_u32(nlh, IFLA_MTU, runtime.tunnel_interface_mtu);

    struct nlattr *linkinfo = mnl_attr_nest_start(nlh, IFLA_LINKINFO);
    mnl_attr_put_str(nlh, IFLA_INFO_KIND, "ip6gre");

    struct nlattr *tunnelinfo = mnl_attr_nest_start(nlh, IFLA_INFO_DATA);
    struct sockaddr_in6 addr = {};
    addr.sin6_addr = get_primary_ip6(runtime.lte.interface_name);
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
            logger(LOG_ERROR, "Creation of tunnel interface '%s' failed: %s\n", runtime.tunnel_interface_name, strerror(-nlerr->error));
            return false;
        }
    }

    logger(LOG_INFO, "Tunnel interface '%s' created.\n", runtime.tunnel_interface_name);
    trigger_event("tunnelup");
    return true;
}

bool destroy_gre_tunnel_dev() {
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

    mnl_attr_put_str(nlh, IFLA_IFNAME, runtime.tunnel_interface_name);

    mnl_socket_sendto(nl_sock, nlh, nlh->nlmsg_len);
    mnl_socket_recvfrom(nl_sock, buf, sizeof(buf));

    nlh = (struct nlmsghdr*) buf;
    if (nlh->nlmsg_type == NLMSG_ERROR){
        struct nlmsgerr *nlerr = mnl_nlmsg_get_payload(nlh);
        if (nlerr->error) {
            logger(LOG_ERROR, "Destruction of Tunnel interface '%s' failed: %s\n", runtime.tunnel_interface_name, strerror(-nlerr->error));
            return false;
        }
    }

    logger(LOG_INFO, "Tunnel interface '%s' destroyed.\n", runtime.tunnel_interface_name);
    trigger_event("tunneldown");
    return true;
}

bool create_tun_tunnel_dev() {
    int fd;
    if ((fd = open("/dev/net/tun", O_RDWR)) < 0 ) {
        logger(LOG_ERROR, "Opening /dev/net/tun failed: %s\n", strerror(errno));
        return false;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN; // /* IFF_MULTI_QUEUE, maybe? */

    strncpy(ifr.ifr_name, runtime.tunnel_interface_name, strlen(runtime.tunnel_interface_name));

    if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0 ) {
        logger(LOG_ERROR, "Creation of Tunnel interface '%s' failed: %s\n", runtime.tunnel_interface_name, strerror(errno));
        close(fd);
        return false;
    }

    if (ioctl(fd, TUNSETPERSIST, true) < 0 ) {
        logger(LOG_ERROR, "Creation of Tunnel interface '%s' failed: %s\n", runtime.tunnel_interface_name, strerror(errno));
        close(fd);
        return false;
    }

    int gen_fd = socket(PF_INET, SOCK_DGRAM, 0);

    ifr.ifr_flags = 0;
    ifr.ifr_mtu = runtime.tunnel_interface_mtu;
    if (ioctl(gen_fd, SIOCSIFMTU, &ifr) < 0) {
        logger(LOG_ERROR, "Setting MTU for tunnel interface '%s' failed: %s\n", runtime.tunnel_interface_name, strerror(errno));
    }

    ifr.ifr_mtu = 0;
    ioctl(gen_fd, SIOCSIFFLAGS, &ifr);
    ifr.ifr_flags |= IFF_UP;
    if (ioctl(gen_fd, SIOCSIFFLAGS, &ifr) < 0) {
        logger(LOG_ERROR, "Bringing up tunnel interface '%s' failed: %s\n", runtime.tunnel_interface_name, strerror(errno));
    }

    close(gen_fd);
    close(fd);

    logger(LOG_INFO, "Tunnel interface '%s' created.\n", runtime.tunnel_interface_name);
    trigger_event("tunnelup");
    return true;
}

bool destroy_tun_tunnel_dev() {
    int fd;
    if ((fd = open("/dev/net/tun", O_RDWR)) < 0 ) {
        logger(LOG_ERROR, "Opening /dev/net/tun failed: %s\n", strerror(errno));
        return true;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN;

    strncpy(ifr.ifr_name, runtime.tunnel_interface_name, strlen(runtime.tunnel_interface_name));

    if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0 ) {
        logger(LOG_ERROR, "Destruction of Tunnel interface '%s' failed: %s\n", runtime.tunnel_interface_name, strerror(errno));
        close(fd);
        return false;
    }

    if (ioctl(fd, TUNSETPERSIST, false) < 0 ) {
        logger(LOG_ERROR, "Destruction of Tunnel interface '%s' failed: %s\n", runtime.tunnel_interface_name, strerror(errno));
        close(fd);
        return true;
    }

    close(fd);

    logger(LOG_INFO, "Tunnel interface '%s' destroyed.\n", runtime.tunnel_interface_name);
    trigger_event("tunneldown");
    return false;
}

bool create_tunnel_dev() {
    if (runtime.bonding)
        return create_tun_tunnel_dev();
    else
        return create_gre_tunnel_dev();
}

bool destroy_tunnel_dev() {
    if (runtime.bonding)
        return destroy_tun_tunnel_dev();
    else
        return destroy_gre_tunnel_dev();
}