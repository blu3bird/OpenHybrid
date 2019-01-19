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

void handle_grecpteardown(void *buffer, int size) {
    logger(LOG_DEBUG, "Received tear down message.\n");
    logger_hexdump(LOG_CRAZYDEBUG, buffer, size, "Contents of tear down message:\n");

    uint32_t errorcode = 0;
    struct grecpattr attr = {};
    int bytes_read = 0;
    while ((bytes_read = read_grecpattribute(buffer += bytes_read, size -= bytes_read, &attr)) > 0) {
        switch (attr.id) {
            case 17:
                memcpy(&errorcode, attr.value, sizeof(errorcode));
                errorcode = ntohl(errorcode);
                break;
            case 255:
                break;
            default:
                logger(LOG_DEBUG, "Unimplemented attribute in tear down message received: %u\n", attr.id);
                break;
        }
    }

    runtime.lte.tunnel_established = false;
    runtime.dsl.tunnel_established = false;
    logger(LOG_ERROR, "Tunnel(s) forcefully terminated by HAAP with error code %u.\n", errorcode);
}