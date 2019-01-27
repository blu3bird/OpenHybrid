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

#define _GNU_SOURCE

/* Common includes */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/ip6.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <string.h>
#ifndef __USE_POSIX199309
  #define __USE_POSIX199309
#endif
#include <time.h>
#include <net/if.h>
#include <pthread.h>

/* Custom includes */
#include "grecp.h"
#include "grecp_request.h"
#include "grecp_accept.h"
#include "grecp_deny.h"
#include "grecp_hello.h"
#include "grecp_notify.h"
#include "grecp_teardown.h"
#include "helpers.h"
#include "tundev.h"
#include "config.h"
#include "logging.h"
#include "dhcp.h"
#include "event.h"
#include "tun2gre.h"
#include "gre2tun.h"

/* GRECP already supports fragmentation of large message, we shouldn't need IP fragmentation */
#define MAX_PKT_SIZE 1500

/* Global structs to hold and statuses and configs */
struct {
    /* shared with haap */
    struct {
        struct in6_addr anycast_ip;
        struct in6_addr ip;
        uint32_t session_id;
        uint32_t bonding_key;
        uint32_t active_hello_interval;
        uint32_t hello_retry_times;
        struct {
            uint32_t commit_count;
            /* TODO: hold actual filer list */
        } filter_list;
        uint32_t bypass_bandwidth_check_interval;
    } haap;
    /* local stuff */
    bool bonding;
    bool filter_list_acked;
    uint8_t log_level;
    uint16_t tunnel_interface_mtu;
    bool tunnel_interface_created;
    char tunnel_interface_name[IF_NAMESIZE];
    pthread_t gre2tun_thread;
    pthread_t tun2gre_thread;
    volatile int signal;
    char event_script_path[128];
    struct {
        pid_t udhcpc_pid;
        struct in_addr ip;
        uint32_t lease_time;
        time_t lease_obtained;
    } dhcp;
    struct {
        pid_t udhcpc6_pid;
        struct in6_addr prefix_address;
        uint8_t prefix_length;
        uint32_t lease_time;
        time_t lease_obtained;
    } dhcp6;
    struct {
        char interface_name[IF_NAMESIZE];
        bool tunnel_established;
        time_t last_hello_sent;
        time_t last_hello_received;
        uint8_t missed_hellos;
        bool tunnel_verification_required;
        struct in6_addr interface_ip;
        struct timeval round_trip_time;
    } lte;
    struct {
        char interface_name[IF_NAMESIZE];
        bool tunnel_established;
        time_t last_hello_sent;
        time_t last_hello_received;
        uint8_t missed_hellos;
        time_t last_bypass_traffic_sent;
        struct in6_addr interface_ip;
        struct timeval round_trip_time;
    } dsl;
} runtime;

/* Raw socket */
int sockfd;
int sockfd_gre;
int sockfd_tun;