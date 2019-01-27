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

void *gre2tun_main() {
    static unsigned char buffer[MAX_PKT_SIZE];
    uint16_t size;
    uint32_t sequence;
    uint8_t payload_offset;
    struct grehdr *greh;
    struct sockaddr_in6 saddr = {};
    socklen_t saddr_size = sizeof(saddr);
    while (true) {
        size = recvfrom(sockfd_gre, buffer, MAX_PKT_SIZE, 0, (struct sockaddr *)&saddr, &saddr_size);

        if (size <= 0) {
            logger(LOG_ERROR, "Raw socket receive failed: %s\n", strerror(errno));
            continue;
        }

        /* ignore packets with invalid source ips */
        if (memcmp(&saddr.sin6_addr, &runtime.haap.ip, sizeof(struct in6_addr)) != 0) {
            continue;
        }

        greh = (struct grehdr *)buffer;
        if (greh->flags_and_version == htons(GRECP_FLAGSANDVERSION_WITH_SEQ)) {
            memcpy(&sequence, buffer + 8, 4);
            sequence = ntohl(sequence);
            payload_offset = 12;
        } else {
            sequence = 0;
            payload_offset = 8;
        }

        /* set tun packet info */
        memset(buffer + payload_offset - 4, 0, 2);
        memcpy(buffer + payload_offset - 2, buffer + 2, 2);

        if (sequence) {
            /* TODO: implement reorder buffer */
        }

        logger(LOG_CRAZYDEBUG, "gre2run: Received %u bytes\n", size);
        if (write(sockfd_tun, buffer + payload_offset - 4, size - payload_offset + 4) < 0) {
            logger(LOG_ERROR, "Tun device write failed: %s\n", strerror(errno));
        }
    }
}