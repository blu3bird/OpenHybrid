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
#include <stdarg.h>
#include <unistd.h>

void print_log_header(uint8_t log_level) {
    switch (log_level) {
        case LOG_CRAZYDEBUG: printf("CRAZYDEBUG: "); break;
        case LOG_DEBUG: printf("DEBUG: "); break;
        case LOG_INFO: printf("INFO: "); break;
        case LOG_WARNING: printf("WARNING: "); break;
        case LOG_ERROR: printf("ERROR: "); break;
        case LOG_FATAL: printf("FATAL: "); break;
    }
}

void logger(uint8_t log_level, const char* format, ...) {
    if (runtime.log_level >= log_level) {
        print_log_header(log_level);

        va_list arglist;
        va_start(arglist, format);
        vprintf(format, arglist);
        va_end(arglist);

        fflush(stdout);
    }

    if (log_level == LOG_FATAL) {
        exit(EXIT_FAILURE);
    }
}

void logger_hexdump(int8_t log_level, void *buffer, int size, const char* format, ...) {
    if (runtime.log_level >= log_level) {
        print_log_header(log_level);

        va_list arglist;
        va_start(arglist, format);
        vprintf(format, arglist);
        va_end(arglist);

        unsigned char asciibuffer[17] = {};
        unsigned char *charbuffer = (unsigned char*)buffer;
        int i;
        for (i = 0; i < size; i++) {
            if ((i % 16) == 0) {
                if (i != 0) {
                    printf(" %s\n", asciibuffer);
                }
                printf("  %04x", i);
            }

            printf(" %02x", charbuffer[i]);

            if ((charbuffer[i] < 0x20) || (charbuffer[i] > 0x7e))
                asciibuffer[i % 16] = '.';
            else
                asciibuffer[i % 16] = charbuffer[i];
            asciibuffer[(i % 16) + 1] = '\0';
        }

        while ((i % 16) != 0) {
            printf("   ");
            i++;
        }

        printf(" %s\n", asciibuffer);
    }

    if (log_level == LOG_FATAL) {
        exit(EXIT_FAILURE);
    }
}

void logger_bitdump(int8_t log_level, uint8_t byte, const char* format, ...) {
    if (runtime.log_level >= log_level) {
        print_log_header(log_level);

        va_list arglist;
        va_start(arglist, format);
        vprintf(format, arglist);
        va_end(arglist);
        printf("  ");
        for (int i = 0; i < 8; i++) {
            printf("%d", !!((byte << i) & 0x80));
        }
        printf("\n");
    }

    if (log_level == LOG_FATAL) {
        exit(EXIT_FAILURE);
    }    
}