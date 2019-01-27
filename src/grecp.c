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

 /* read one grecp attribute from buffer, save it in attr and return number of bytes read */
int read_grecpattribute(void *buffer, int size, struct grecpattr *attr) {
    if (size <= 0) {
        /* why do you tell me to read if there's nothing to read? */
        return 0;
    } else if (size < sizeof(attr->id) + sizeof(attr->length)) {
        logger(LOG_ERROR, "Malformed packet detected.\n");
        return 0;
    }

    memcpy(&attr->id, buffer, sizeof(attr->id));
    memcpy(&attr->length, buffer + sizeof(attr->id), sizeof(attr->length));
    attr->length = ntohs(attr->length);

    if (size - sizeof(attr->id) - sizeof(attr->length) < attr->length) {
        logger(LOG_ERROR, "Malformed packet detected.\n");
        return 0;
    }

    attr->value = buffer + sizeof(attr->id) + sizeof(attr->length);
    return sizeof(attr->id) + sizeof(attr->length) + attr->length;
}

/* write one grecp attribute to buffer and return number of bytes written */
int append_grecpattribute(void *buffer, uint8_t id, uint16_t length, void *value) {
    memcpy(buffer, &id, sizeof(id));
    uint16_t nlength = htons(length);
    memcpy(buffer + 1, &nlength, 2);
    memcpy(buffer + sizeof(id) + sizeof(length), value, length);
    return sizeof(id) + sizeof(length) + length;
}

/* validate gre message (type=grecp, key=correct, ...) and call message handlers */
void process_grecpmessage(void *buffer, int size) {
    struct grehdr *greh = (struct grehdr *)buffer;
    /* gre->proto is already checked by BPF, no need to check again */
    if (ntohs(greh->flags_and_version) != GRECP_FLAGSANDVERSION) {
        /* ignore messages with invalid flags & version */
        logger(LOG_WARNING, "Received packet with invalid gre flags/version mask, discarding.\n");
        return;
    } else {
        struct grecphdr *grecph = (struct grecphdr *)(buffer + sizeof(struct grehdr));
        if ((ntohl(greh->key) != runtime.haap.bonding_key) && (grecph->msgtype_and_tuntype >> 4 != GRECP_MSGTYPE_ACCEPT)) {
            /* ignore messages with invalid keys */
            logger(LOG_WARNING, "Received packet with invalid gre key, discarding.\n");
            return;
        } else if (((grecph->msgtype_and_tuntype & ~0xf0) != GRECP_TUNTYPE_LTE) && ((grecph->msgtype_and_tuntype & ~0xf0) != GRECP_TUNTYPE_DSL)) {
            /* ignore messages with invalid tunnel types */
            logger(LOG_WARNING, "Received packet with invalid tunnel type, discarding.\n");
            return;
        } else {
            switch (grecph->msgtype_and_tuntype >> 4) {
                case GRECP_MSGTYPE_ACCEPT:
                    handle_grecpaccept(grecph->msgtype_and_tuntype & ~0xf0, buffer + sizeof(struct grehdr) + sizeof(struct grecphdr), size - sizeof(struct grehdr) - sizeof(struct grecphdr));
                    break;
                case GRECP_MSGTYPE_DENY:
                    handle_grecpdeny(grecph->msgtype_and_tuntype & ~0xf0, buffer + sizeof(struct grehdr) + sizeof(struct grecphdr), size - sizeof(struct grehdr) - sizeof(struct grecphdr));
                    break;
                case GRECP_MSGTYPE_HELLO:
                    handle_grecphello(grecph->msgtype_and_tuntype & ~0xf0, buffer + sizeof(struct grehdr) + sizeof(struct grecphdr), size - sizeof(struct grehdr) - sizeof(struct grecphdr));
                    break;
                case GRECP_MSGTYPE_TEARDOWN:
                    handle_grecpteardown(buffer + sizeof(struct grehdr) + sizeof(struct grecphdr), size - sizeof(struct grehdr) - sizeof(struct grecphdr));
                    break;
                case GRECP_MSGTYPE_NOTIFY:
                    handle_grecpnotify(grecph->msgtype_and_tuntype & ~0xf0, buffer + sizeof(struct grehdr) + sizeof(struct grecphdr), size - sizeof(struct grehdr) - sizeof(struct grecphdr));
                    break;
                default:
                    logger(LOG_WARNING, "Ignoring packet with unknown message type '%u'.\n", grecph->msgtype_and_tuntype >> 4);
                    break;
            }
            return;
        }
    }
}

bool send_grecpmessage(uint8_t msgtype, uint8_t tuntype, void *attributes, int attributes_size) {
    void *buffer = calloc(1, MAX_PKT_SIZE);
    int size = 0;

    /* GRE header */
    struct grehdr *greh = (struct grehdr *)(buffer + size);
    greh->flags_and_version = htons(GRECP_FLAGSANDVERSION);
    greh->proto = htons(GRECP_PROTO);
    greh->key = htonl(runtime.haap.bonding_key);
    size += sizeof(struct grehdr);

    /* GRECP header */
    struct grecphdr *grecph = (struct grecphdr *)(buffer + size);
    grecph->msgtype_and_tuntype = (msgtype << 4) | tuntype;
    size += sizeof(struct grecphdr);

    /* Add GRECP attributes */
    memcpy(buffer + size, attributes, attributes_size);
    size += attributes_size;

    /* Source & Destination */
    struct sockaddr_in6 src;
    src.sin6_family = AF_INET6;
    if (tuntype == GRECP_TUNTYPE_LTE) {
        src.sin6_addr = runtime.lte.interface_ip;
    } else {
        src.sin6_addr = runtime.dsl.interface_ip;
    }
    struct sockaddr_in6 *dst = calloc(1, sizeof(struct sockaddr_in6));
    dst->sin6_family = AF_INET6;
    dst->sin6_addr = runtime.haap.ip;

    /* Construct control information */
    struct msghdr msgh;
    struct iovec msgiov;
    struct cmsghdr *c;
    struct unp_in_pktinfo {
        struct in6_addr ipi6_addr;
        int ipi6_ifindex;
    } *pi;
    msgh.msg_name = dst;
    msgh.msg_namelen = sizeof(struct sockaddr_in6);
    msgiov.iov_base = buffer;
    msgiov.iov_len = size;
    msgh.msg_iov = &msgiov;
    msgh.msg_iovlen = 1;
    msgh.msg_flags = 0;
    void *control_buf = calloc(1, CMSG_LEN(sizeof(struct unp_in_pktinfo)) + 5);  /* what do we need those 5 extra bytes for? alignment? */
    msgh.msg_control = control_buf;
    msgh.msg_controllen = CMSG_LEN(sizeof(struct unp_in_pktinfo)) + 5;
    c = CMSG_FIRSTHDR(&msgh);
    c->cmsg_level = IPPROTO_IPV6;
    c->cmsg_type = IPV6_PKTINFO;
    c->cmsg_len = CMSG_LEN(sizeof(struct unp_in_pktinfo));
    pi = (struct unp_in_pktinfo *)CMSG_DATA(c);
    pi->ipi6_addr = src.sin6_addr;
    msgh.msg_controllen = c->cmsg_len;

    bool res = true;
    if (memcmp(&src.sin6_addr, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) != 0) {
        if (sendmsg(sockfd, &msgh, 0) <= 0) {
            logger(LOG_ERROR, "Raw socket send failed: %s\n", strerror(errno));
            res = false;
        }
    } else {
        /* if we don't set a source ip, sendmsg() will use the ip of the outgoing interface
        ** and since the haap doesn't verify source ip's we would still get replies for our hellos
        */
        res = false;
    }

    /* TODO: check if sending failed due to a link failure and call send_grecpnotify_linkfailure if it did */

    free(control_buf);
    free(dst);
    free(buffer);

    return res;
}
