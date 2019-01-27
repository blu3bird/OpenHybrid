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
    uint32_t sequence_flushed = 0;

    uint8_t payload_offset;
    struct grehdr *greh;
    struct sockaddr_in6 saddr = {};
    socklen_t saddr_size = sizeof(saddr);

    bool flushed_something;
    bool needs_flush;
    uint16_t reorder_buffer_freeable;

    struct timeval now;
    struct timeval age;
    struct timeval maxage;

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
                memcpy(&sequence, buffer + 8, 4);
                sequence = ntohl(sequence);
                payload_offset = 12;
            } else {
                payload_offset = 8;
            }

            if (payload_offset == 8) {
                /* no sequence? skip reorder buffer and flush directly */
                memset(buffer + payload_offset - 4, 0, 2); /* tun pi flags */
                memcpy(buffer + payload_offset - 2, buffer + 2, 2); /* tun pi proto */
                if (write(sockfd_tun, buffer + payload_offset - 4, size - payload_offset + 4) != size - payload_offset + 4) {
                    logger(LOG_ERROR, "Tun device write failed: %s\n", strerror(errno));
                }
            } else {
                /* add packet to reorder buffer */
                reorder_buffer.packets = realloc(reorder_buffer.packets, sizeof(struct reorder_buffer_element) * (reorder_buffer.size + 1));
                reorder_buffer.packets[reorder_buffer.size].sequence = sequence;
                reorder_buffer.packets[reorder_buffer.size].timestamp = get_uptime();
                reorder_buffer.packets[reorder_buffer.size].size = size - payload_offset + 4;
                reorder_buffer.packets[reorder_buffer.size].packet = malloc(reorder_buffer.packets[reorder_buffer.size].size);
                memset(reorder_buffer.packets[reorder_buffer.size].packet, 0, 2); /* tun pi flags */
                memcpy(reorder_buffer.packets[reorder_buffer.size].packet + 2, &greh->proto, 2); /* tun pi proto */
                memcpy(reorder_buffer.packets[reorder_buffer.size].packet + 4, buffer + payload_offset, size - payload_offset);
                reorder_buffer.size++;
            }
        }

        /* calculate the max time we wait for a packet based on the rtt difference of both links */
        if (timercmp(&runtime.dsl.round_trip_time, &runtime.lte.round_trip_time, <)) {
            timersub(&runtime.lte.round_trip_time, &runtime.dsl.round_trip_time, &maxage);
        } else {
            timersub(&runtime.dsl.round_trip_time, &runtime.lte.round_trip_time, &maxage);
        }
        /* cap at 100 ms */
        if ((maxage.tv_sec > 0) || (maxage.tv_usec > 100000)) {
            maxage.tv_sec = 0;
            maxage.tv_usec = 100000;
        }
        logger(LOG_CRAZYDEBUG, "Max reorder buffer: %u.%06us\n", maxage.tv_sec, maxage.tv_usec);
        now = get_uptime();

        /* flush reorder buffer, if possible */
        flushed_something = true;
        while (flushed_something) {
            flushed_something = false;
            for (int i=0; i<reorder_buffer.size; i++) {
                needs_flush = false;
                if (reorder_buffer.packets[i].size > 0) {
                    if ((reorder_buffer.packets[i].sequence) == sequence_flushed +1) {
                        /* this one is next in order */
                        logger(LOG_CRAZYDEBUG, "Reorder buffer: Flushed packet %u.\n", reorder_buffer.packets[i].sequence);
                        needs_flush = true;
                    } else if (reorder_buffer.packets[i].sequence <= sequence_flushed) {
                        /* this one's already out of order...just flush it and hope for the best */
                        logger(LOG_CRAZYDEBUG, "Reorder buffer: Flushed out-of-order packet %u.\n", reorder_buffer.packets[i].sequence);
                        needs_flush = true;
                    } else {
                        timersub(&now, &reorder_buffer.packets[i].timestamp, &age);
                        if (timercmp(&age, &maxage, >=)) {
                            /* timeout reached */
                            logger(LOG_CRAZYDEBUG, "Reorder buffer: Flushed timed-out packet %u.\n", reorder_buffer.packets[i].sequence);
                            needs_flush = true;
                        }
                    }

                    if (needs_flush) {
                        if (write(sockfd_tun, reorder_buffer.packets[i].packet, reorder_buffer.packets[i].size) != reorder_buffer.packets[i].size) {
                            logger(LOG_ERROR, "Tun device write failed: %s\n", strerror(errno));
                        }
                        free(reorder_buffer.packets[i].packet);
                        reorder_buffer.packets[i].size = 0; /* mark as flushed */
                        flushed_something = true;
                        if (reorder_buffer.packets[i].sequence > sequence_flushed)
                            sequence_flushed = reorder_buffer.packets[i].sequence;
                    }
                }
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