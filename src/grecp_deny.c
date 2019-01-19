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

void handle_grecpdeny(uint8_t tuntype, void *buffer, int size) {
    if (tuntype == GRECP_TUNTYPE_LTE) {
        logger(LOG_DEBUG, "Received deny message for LTE tunnel.\n");
    }  else {
        logger(LOG_DEBUG, "Received deny message for DSL tunnel.\n");
    }
    logger_hexdump(LOG_CRAZYDEBUG, buffer, size, "Contents of deny message:\n");

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
                logger(LOG_DEBUG, "Unimplemented attribute in deny message received: %u\n", attr.id);
                break;
        }
    }

    logger(LOG_ERROR, "HAAP rejected our connect reques with error code %u.\n", errorcode);

    /* RFC says we have to terminate our connection attempt now.
    ** It doesn't say anything about whether we may retry at a later time.
    ** We'll just retry during our next main loop run, which is about 1 second in the future... ;-)
    */
}