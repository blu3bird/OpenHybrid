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
#include <ifaddrs.h>
#include <netdb.h>

bool isvalueinarray(uint8_t val, uint8_t *arr, uint8_t size) {
    for (int i=0; i < size; i++) {
        if (arr[i] == val)
            return true;
    }
    return false;
}

inline struct timeval get_uptime() {
    struct timespec t = {};
    clock_gettime(CLOCK_MONOTONIC, &t);
    struct timeval ret = { .tv_sec = t.tv_sec, .tv_usec = t.tv_nsec / 1000 };
    return ret;
}

struct in6_addr get_primary_ip6(char *interface) {
    struct in6_addr ip = {};

    if ((strlen(interface) >= 7) && (interface[4] == ':')) {
       /* dirty workaround to allow people to pass an ip instead of an interface */
        inet_pton(AF_INET6, interface, &ip);
        return ip;
    }

    /* getifaddrs(3) corrups my memory :-/ */

    FILE *fd = fopen("/proc/net/if_inet6", "r");
    if (fd == NULL) {
        logger(LOG_ERROR, "Failed to read ip of interface %s: %s\n", interface, strerror(errno));
        return ip;
    }

    struct in6_addr this_ip = {};
    char this_ifname[IF_NAMESIZE] = {};
    int this_scope = 0;
    int this_prefix = 0;
    bool found = false;
    while (19 == fscanf(fd,
                        " %2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx %*x %x %x %*x %s",
                        &this_ip.__in6_u.__u6_addr8[0],
                        &this_ip.__in6_u.__u6_addr8[1],
                        &this_ip.__in6_u.__u6_addr8[2],
                        &this_ip.__in6_u.__u6_addr8[3],
                        &this_ip.__in6_u.__u6_addr8[4],
                        &this_ip.__in6_u.__u6_addr8[5],
                        &this_ip.__in6_u.__u6_addr8[6],
                        &this_ip.__in6_u.__u6_addr8[7],
                        &this_ip.__in6_u.__u6_addr8[8],
                        &this_ip.__in6_u.__u6_addr8[9],
                        &this_ip.__in6_u.__u6_addr8[10],
                        &this_ip.__in6_u.__u6_addr8[11],
                        &this_ip.__in6_u.__u6_addr8[12],
                        &this_ip.__in6_u.__u6_addr8[13],
                        &this_ip.__in6_u.__u6_addr8[14],
                        &this_ip.__in6_u.__u6_addr8[15],
                        &this_prefix,
                        &this_scope,
                        this_ifname)) {

                            if ((strcmp(interface, this_ifname) == 0) && (this_scope == 0)) { /* global */
                                ip = this_ip;
                                found = true;
                            }

                        }

    fclose(fd);

    if (!found) {
        logger(LOG_ERROR, "Unable to determine primary ip6 address of interface '%s'.\n", interface);
    }

    return ip;
}
