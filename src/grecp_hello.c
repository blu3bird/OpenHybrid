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

bool send_grecphello(uint8_t tuntype) {
    unsigned char buffer[MAX_PKT_SIZE];
    int size = 0;

    struct {
        uint32_t seconds;
        uint32_t milliseconds;
    } timestamp;
    timestamp.seconds = htonl(get_uptime().tv_sec);
    timestamp.milliseconds = htonl(get_uptime().tv_usec / 1000);
    size += append_grecpattribute(buffer + size, GRECP_MSGATTR_TIMESTAMP, sizeof(timestamp), &timestamp);
    size += append_grecpattribute(buffer + size, GRECP_MSGATTR_PADDING, 0, NULL);

    bool res;
    if (send_grecpmessage(GRECP_MSGTYPE_HELLO, tuntype, buffer, size))  {
        if (tuntype == GRECP_TUNTYPE_LTE) {
            runtime.lte.last_hello_sent = ntohl(timestamp.seconds);
            logger(LOG_DEBUG, "Sent hello message for LTE tunnel.\n");
        } else {
            runtime.dsl.last_hello_sent = ntohl(timestamp.seconds);
            logger(LOG_DEBUG, "Sent hello message for DSL tunnel.\n");
        }
        logger_hexdump(LOG_CRAZYDEBUG, buffer, size, "Contents of hello message:\n");
        res = true;
    } else {
        /* set last_hello_sent even on failure, otherwise the timeout logic wouldn't work in case of a local issue */
        if (tuntype == GRECP_TUNTYPE_LTE) {
            runtime.lte.last_hello_sent = ntohl(timestamp.seconds);
            logger(LOG_ERROR, "Sending hello message for LTE tunnel failed.\n");
        } else {
            runtime.dsl.last_hello_sent = ntohl(timestamp.seconds);
            logger(LOG_ERROR, "Sending hello message for DSL tunnel failed.\n");
        }
        res = false;
    }

    return res;
}

void handle_grecphello(uint8_t tuntype, void *buffer, int size) {
    if (tuntype == GRECP_TUNTYPE_LTE) {
        logger(LOG_DEBUG, "Received hello message for LTE tunnel.\n");
    }  else {
        logger(LOG_DEBUG, "Received hello message for DSL tunnel.\n");
    }
    logger_hexdump(LOG_CRAZYDEBUG, buffer, size, "Contents of hello message:\n");

    struct grecpattr attr = {};
    int bytes_read = 0;
    while ((bytes_read = read_grecpattribute(buffer += bytes_read, size -= bytes_read, &attr)) > 0) {
        switch (attr.id) {
            case GRECP_MSGATTR_TIMESTAMP:
                true;
                struct {
                    uint32_t seconds;
                    uint32_t milliseconds;
                } timestamp;
                memcpy(&timestamp.seconds, attr.value, sizeof(timestamp.seconds));
                timestamp.seconds = ntohl(timestamp.seconds);
                memcpy(&timestamp.milliseconds, attr.value + sizeof(timestamp.seconds), sizeof(timestamp.milliseconds));
                timestamp.milliseconds = ntohl(timestamp.milliseconds);

                struct timeval now = get_uptime();
                struct timeval sent = { .tv_sec = timestamp.seconds, .tv_usec = timestamp.milliseconds * 1000 };

                if (tuntype == GRECP_TUNTYPE_LTE ) {
                    timersub(&now, &sent, &runtime.lte.round_trip_time);
                    runtime.lte.last_hello_received = timestamp.seconds;
                    runtime.lte.missed_hellos = 0;
                    logger(LOG_DEBUG, "Round trip time for LTE: %u.%03us\n", runtime.lte.round_trip_time.tv_sec, runtime.lte.round_trip_time.tv_usec / 1000);
                } else {
                    timersub(&now, &sent, &runtime.dsl.round_trip_time);
                    runtime.dsl.last_hello_received = timestamp.seconds;
                    runtime.dsl.missed_hellos = 0;
                    logger(LOG_DEBUG, "Round trip time for DSL: %u.%03us\n", runtime.dsl.round_trip_time.tv_sec, runtime.dsl.round_trip_time.tv_usec / 1000);
                }

            case GRECP_MSGATTR_PADDING:
                break;
            default:
                logger(LOG_DEBUG, "Unimplemented attribute in hello message received: %u\n", attr.id);
                break;
        }
    }
}