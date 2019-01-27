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
#include <linux/if.h>
#include <linux/if_tun.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>

void send_gre(uint8_t tuntype, uint16_t proto, uint32_t sequence, bool include_sequence, void *payload, uint16_t payload_size) {
    unsigned char buffer[MAX_PKT_SIZE] = {};
    int size = 0;

    /* GRE header */
    struct grehdr *greh = (struct grehdr *)(buffer + size);
    greh->flags_and_version = htons(GRECP_FLAGSANDVERSION);
    greh->proto = htons(proto);
    greh->key = htonl(runtime.haap.bonding_key);
    size += sizeof(struct grehdr);
    /* Sequence */
    if (include_sequence) {
        greh->flags_and_version = htons(GRECP_FLAGSANDVERSION_WITH_SEQ);
        sequence = htonl(sequence);
        memcpy(buffer + size, &sequence, sizeof(sequence));
        size += sizeof(sequence);
    }

    /* Payload */
    memcpy(buffer + size, payload, payload_size);
    size += payload_size;

    /* Source & Destination */
    struct sockaddr_in6 src = {};
    src.sin6_family = AF_INET6;
    if (tuntype == GRECP_TUNTYPE_LTE) {
        src.sin6_addr = runtime.lte.interface_ip;
    } else {
        src.sin6_addr = runtime.dsl.interface_ip;
    }
    struct sockaddr_in6 dst = {};
    dst.sin6_family = AF_INET6;
    dst.sin6_addr = runtime.haap.ip;

    /* Construct control information */
    struct msghdr msgh = {};
    struct iovec msgiov = {};
    struct cmsghdr *c;
    struct unp_in_pktinfo {
        struct in6_addr ipi6_addr;
        int ipi6_ifindex;
    } *pi;
    msgh.msg_name = &dst;
    msgh.msg_namelen = sizeof(struct sockaddr_in6);
    msgiov.iov_base = buffer;
    msgiov.iov_len = size;
    msgh.msg_iov = &msgiov;
    msgh.msg_iovlen = 1;
    unsigned char control_buf[CMSG_LEN(sizeof(struct unp_in_pktinfo))] = {};
    msgh.msg_control = &control_buf;
    msgh.msg_controllen = CMSG_LEN(sizeof(struct unp_in_pktinfo));
    c = CMSG_FIRSTHDR(&msgh);
    c->cmsg_level = IPPROTO_IPV6;
    c->cmsg_type = IPV6_PKTINFO;
    c->cmsg_len = CMSG_LEN(sizeof(struct unp_in_pktinfo));
    pi = (struct unp_in_pktinfo *)CMSG_DATA(c);
    pi->ipi6_addr = src.sin6_addr;
    msgh.msg_controllen = c->cmsg_len;

    if (sendmsg(sockfd_gre, &msgh, 0) <= 0) {
        logger(LOG_ERROR, "Raw socket send failed: %s\n", strerror(errno));
    }
}

void *tun2gre_main() {
    char trimifname[IF_NAMESIZE-6];
    char threadname[IF_NAMESIZE];
    strncpy(trimifname, runtime.tunnel_interface_name, IF_NAMESIZE-7);
    sprintf(threadname, "%s-send", trimifname);
    pthread_setname_np(pthread_self(), threadname);

    unsigned char buffer[MAX_PKT_SIZE];
    ssize_t size;
    uint16_t etherproto;
    uint32_t sequence = 0;
    struct iphdr *iph;
    struct ip6_hdr *ip6h;
    struct udphdr *udph;
    bool is_dhcp;
    while (true) {
        is_dhcp = false;

        size = read(sockfd_tun, buffer, MAX_PKT_SIZE);
        if (size > 0) {
            //logger_hexdump(LOG_DEBUG, buffer, size, "buffer:");

            /* extract ether proto from tun packet info */
            memcpy(&etherproto, buffer + 2, 2);
            etherproto = ntohs(etherproto);

            /* ignore unsupported protocols */
            if ((etherproto != ETHERTYPE_IP) && (etherproto != ETHERTYPE_IPV6))
                continue;

            /* check if it's a dhcp packet */
            if (etherproto == ETHERTYPE_IP) {
                iph = (struct iphdr *)(buffer + 4);
                if (iph->protocol == IPPROTO_UDP) {
                    udph = (struct udphdr *)(buffer + 4 + sizeof(struct iphdr));
                    if ((ntohs(udph->uh_sport) == 68) && (ntohs(udph->uh_dport) == 67))
                        is_dhcp = true;
                }
            } else if (etherproto == ETHERTYPE_IPV6) {
                ip6h = (struct ip6_hdr *)(buffer + 4);
                if (ip6h->ip6_ctlun.ip6_un1.ip6_un1_nxt == IPPROTO_UDP) {
                    udph = (struct udphdr *)(buffer + 4 + sizeof(struct ip6_hdr));
                    if ((ntohs(udph->uh_sport) == 546) && (ntohs(udph->uh_dport) == 547))
                        is_dhcp = true;
                }
            }

            if (is_dhcp) {
                logger(LOG_CRAZYDEBUG, "tun2gre: Sending %u bytes via LTE (forced)\n", size);
                send_gre(GRECP_TUNTYPE_LTE, etherproto, 0, false, buffer + 4, size - 4);
            } else if (runtime.dsl.tunnel_established) {
                /* TODO: implement overflow to LTE */
                logger(LOG_CRAZYDEBUG, "tun2gre: Sending %u bytes via DSL\n", size);
                send_gre(GRECP_TUNTYPE_DSL, etherproto, sequence++, true, buffer + 4, size - 4);
            } else if (runtime.lte.tunnel_established) {
                logger(LOG_CRAZYDEBUG, "tun2gre: Sending %u bytes via LTE\n", size);
                send_gre(GRECP_TUNTYPE_LTE, etherproto, sequence++, true, buffer + 4, size - 4);
            } else {
                logger(LOG_ERROR, "Sending packet faiked: All tunnels are down");
            }
        } else {
            logger(LOG_ERROR, "Tun device read failed: %s\n", strerror(errno));
        }
    }
}