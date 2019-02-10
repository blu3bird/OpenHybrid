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
    char trimifname[IF_NAMESIZE-6];
    char threadname[IF_NAMESIZE];
    strncpy(trimifname, runtime.tunnel_interface_name, IF_NAMESIZE-7);
    sprintf(threadname, "%s-recv", trimifname);
    pthread_setname_np(pthread_self(), threadname);

    struct reorder_buffer_element {
        uint32_t sequence;
        struct timeval timestamp;
        void *packet;
        uint16_t size;
    };
    struct {
        struct reorder_buffer_element *packets;
        struct reorder_buffer_element *packets_old;
        uint32_t size;
    } reorder_buffer = {};

    unsigned char buffer[MAX_PKT_SIZE];
    ssize_t size;
    uint32_t sequence;
    uint32_t sequence_flushed = UINT32_MAX;

    uint8_t payload_offset;
    struct grehdr *greh;
    struct sockaddr_in6 saddr = {};
    socklen_t saddr_size = sizeof(saddr);

    bool flushed_something;
    uint16_t reorder_buffer_freeable;

    struct timeval now;
    struct timeval age;

    while (true) {
        size = recvfrom(sockfd_gre, buffer, MAX_PKT_SIZE, 0, (struct sockaddr *)&saddr, &saddr_size);

        if ((size <= 0) && (errno != EAGAIN)) {
            logger(LOG_ERROR, "Raw socket receive failed: %s\n", strerror(errno));
            continue;
        }

        if (size > 0) {
            /* ignore packets with invalid source ips */
            if (memcmp(&saddr.sin6_addr, &runtime.haap.ip, sizeof(struct in6_addr)) != 0) {
                continue;
            }

            /* extract sequence number and payload offset */
            greh = (struct grehdr *)buffer;
            if (greh->flags_and_version == htons(GRECP_FLAGSANDVERSION_WITH_SEQ)) {
                memcpy(&sequence, buffer + sizeof(struct grehdr), sizeof(sequence));
                sequence = ntohl(sequence);
                payload_offset = 12;
            } else if (greh->flags_and_version == htons(GRECP_FLAGSANDVERSION)) {
                payload_offset = 8;
            } else {
                logger(LOG_ERROR, "Received packet with invalid gre flags.\n");
                continue;
            }

            if ((payload_offset == 8) || ((runtime.reorder_buffer_timeout.tv_sec == 0) && (runtime.reorder_buffer_timeout.tv_usec == 0))) {
                /* no sequence or reordering diabled? flush directly */
                if (write(sockfd_tun, buffer + payload_offset, size - payload_offset) != size - payload_offset) {
                    logger(LOG_ERROR, "Tun device write failed: %s\n", strerror(errno));
                }
            } else {
                /* add packet to reorder buffer */
                reorder_buffer.packets = realloc(reorder_buffer.packets, sizeof(struct reorder_buffer_element) * (reorder_buffer.size + 1));
                reorder_buffer.packets[reorder_buffer.size].sequence = sequence;
                reorder_buffer.packets[reorder_buffer.size].timestamp = get_uptime();
                reorder_buffer.packets[reorder_buffer.size].size = size - payload_offset;
                reorder_buffer.packets[reorder_buffer.size].packet = malloc(reorder_buffer.packets[reorder_buffer.size].size);
                memcpy(reorder_buffer.packets[reorder_buffer.size].packet, buffer + payload_offset, reorder_buffer.packets[reorder_buffer.size].size);
                reorder_buffer.size++;
            }
        }

        now = get_uptime();

        /* flush reorder buffer, in-order */
        restartflushing:
        flushed_something = true;
        while (flushed_something) {
            flushed_something = false;
            for (int i=0; i<reorder_buffer.size; i++) {
                if (reorder_buffer.packets[i].sequence == sequence_flushed +1) {
                    logger(LOG_CRAZYDEBUG, "Reorder buffer: Packet %u arrived in-order.\n", reorder_buffer.packets[i].sequence);
                    if (write(sockfd_tun, reorder_buffer.packets[i].packet, reorder_buffer.packets[i].size) != reorder_buffer.packets[i].size) {
                        logger(LOG_ERROR, "Tun device write failed: %s\n", strerror(errno));
                    }
                    free(reorder_buffer.packets[i].packet);
                    flushed_something = true;
                    sequence_flushed++;
                    reorder_buffer.packets[i].size = 0; /* mark as flushed */
                }
            }
        }

        /* check for timed-out packets */
        for (int i=0; i<reorder_buffer.size; i++) {
            if (reorder_buffer.packets[i].size > 0) {
                timersub(&now, &reorder_buffer.packets[i].timestamp, &age);
                if (timercmp(&age, &runtime.reorder_buffer_timeout, >=)) {
                    logger(LOG_DEBUG, "Reorder buffer: Packet %u timed out while waiting for packet %u to arrive.\n", reorder_buffer.packets[i].sequence, sequence_flushed + 1);
                    sequence_flushed++;
                    goto restartflushing;
                }
            }
        }

        /* and drop packets arriving after the deadline */
        for (int i=0; i<reorder_buffer.size; i++) {
            if ((reorder_buffer.packets[i].size > 0) && (reorder_buffer.packets[i].sequence <= sequence_flushed)) {
                logger(LOG_DEBUG, "Reorder buffer: Packet %u arrived after deadline of %u.%03u seconds. Discarding.\n", reorder_buffer.packets[i].sequence, runtime.reorder_buffer_timeout.tv_sec, runtime.reorder_buffer_timeout.tv_usec / 1000);
                free(reorder_buffer.packets[i].packet);
                reorder_buffer.packets[i].size = 0; /* mark as flushed */
            }
        }

        /* remove flushed packets from reorder buffer */
        reorder_buffer_freeable = 0;
        for (int i=0; i<reorder_buffer.size; i++) {
            if (reorder_buffer.packets[i].size == 0)
                reorder_buffer_freeable++;
            else
                break;
        }
        if (reorder_buffer.size == reorder_buffer_freeable) {
            reorder_buffer.packets = realloc(reorder_buffer.packets, 0);
            reorder_buffer.size = 0;
        } else if (reorder_buffer_freeable > 0) {
            reorder_buffer.packets_old = reorder_buffer.packets;
            reorder_buffer.packets = malloc(sizeof(struct reorder_buffer_element) * (reorder_buffer.size - reorder_buffer_freeable));
            memcpy(reorder_buffer.packets, reorder_buffer.packets_old + reorder_buffer_freeable, sizeof(struct reorder_buffer_element) * (reorder_buffer.size - reorder_buffer_freeable));
            free(reorder_buffer.packets_old);
            reorder_buffer.size -= reorder_buffer_freeable;
        }
    }

    /* TODO: fix memory leak on thread cancel */
}