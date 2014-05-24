/*
 * Network playback synchronization
 * Copyright (C) 2009 Google Inc.
 *
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.    See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _BSD_SOURCE

#ifdef __MINGW__
#define HAVE_WINSOCK2_H 1
#else
#define HAVE_WINSOCK2_H 0
#endif

#if !HAVE_WINSOCK2_H
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <netdb.h>
#include <signal.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif /* HAVE_WINSOCK2_H */

#include <math.h>
#include <pthread.h>

#include "libmpv/client.h"

// for struct mp_scripting only
#include "player/core.h"
// for mp_client_get_log() only
#include "player/client.h"

#include "common/msg.h"

// config options for UDP sync
static const int udp_port   = 23867;
static const char *udp_ip = "127.0.0.1"; // where the master sends datagrams
                                         // (can be a broadcast address)

static const float udp_seek_threshold = 1.0;   // how far off before we seek

// how far off is still considered equal
#define UDP_TIMING_TOLERANCE 0.02

static pthread_once_t socket_init_once = PTHREAD_ONCE_INIT;

static void socket_init(void)
{
#if HAVE_WINSOCK2_H
    WSADATA wd;
    WSAStartup(0x0202, &wd);
#endif
}

static void set_blocking(int fd, int blocking)
{
    long sock_flags;
#if HAVE_WINSOCK2_H
    sock_flags = !blocking;
    ioctlsocket(fd, FIONBIO, &sock_flags);
#else
    sock_flags = fcntl(fd, F_GETFL, 0);
    sock_flags = blocking ? sock_flags & ~O_NONBLOCK : sock_flags | O_NONBLOCK;
    fcntl(fd, F_SETFL, sock_flags);
#endif /* HAVE_WINSOCK2_H */
}

static void send_udp(int fd, struct sockaddr_in *addr, char *mesg)
{
    sendto(fd, mesg, strlen(mesg), 0, (struct sockaddr *)addr, sizeof(*addr));
}

static int run_master(struct mpv_handle *client, struct mp_log *log)
{
    pthread_once(&socket_init_once, socket_init);
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1)
        return -1;

    // Enable broadcast
    static const int one = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));

    int ip_valid = 0;
    struct sockaddr_in socketinfo;
#if HAVE_WINSOCK2_H
    socketinfo.sin_addr.s_addr = inet_addr(udp_ip);
    ip_valid = socketinfo.sin_addr.s_addr != INADDR_NONE;
#else
    ip_valid = inet_aton(udp_ip, &socketinfo.sin_addr);
#endif

    if (!ip_valid) {
        mp_fatal(log, "Invalid IP.\n");
        return -1;
    }

    socketinfo.sin_family = AF_INET;
    socketinfo.sin_port   = htons(udp_port);

    const uint64_t time_pos_id = 1;
    mpv_observe_property(client, time_pos_id, "time-pos", MPV_FORMAT_DOUBLE);

    while (1) {
        mpv_event *e = mpv_wait_event(client, 1e20);
        switch (e->event_id) {
        case MPV_EVENT_SHUTDOWN:
        case MPV_EVENT_END_FILE: // assume this is just the same as total quit
            send_udp(sockfd, &socketinfo, "bye");
            goto done;
        case MPV_EVENT_PROPERTY_CHANGE: {
            mpv_event_property *pe = e->data;
            if (e->reply_userdata == time_pos_id &&
                pe->format == MPV_FORMAT_DOUBLE)
            {
                double position = *(double *)pe->data;
                char current_time[256];
                snprintf(current_time, sizeof(current_time), "%f", position);
                send_udp(sockfd, &socketinfo, current_time);
            }
            break;
        }
        default:
            break;
        }
    }
done:
    // TODO: close sockfd
    return 0;
}

// gets a datagram from the master with or without blocking.  updates
// master_position if successful.  if the master has exited, returns 1.
// returns -1 on error or if no message received.
// otherwise, returns 0.
static int get_udp(struct mp_log *log, int sockfd, int blocking,
                   double *master_position)
{
    char mesg[100];

    int chars_received = -1;
    int n;

    set_blocking(sockfd, blocking);

    while (-1 != (n = recvfrom(sockfd, mesg, sizeof(mesg)-1, 0,
                               NULL, NULL)))
    {
        char *end;
        // flush out any further messages so we don't get behind
        if (chars_received == -1)
            set_blocking(sockfd, 0);

        chars_received = n;
        mesg[chars_received] = 0;
        if (strcmp(mesg, "bye") == 0)
            return 1;
        *master_position = strtod(mesg, &end);
        if (*end) {
            mp_warn(log, "Could not parse udp string!\n");
            return -1;
        }
    }
    if (chars_received == -1)
        return -1;

    return 0;
}

static int run_slave(struct mpv_handle *client, struct mp_log *log)
{
#if HAVE_WINSOCK2_H
    DWORD tv = 30000;
#else
    struct timeval tv = {.tv_sec = 30};
#endif
    struct sockaddr_in servaddr = {0};

    pthread_once(&socket_init_once, socket_init);
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1)
        return -1;

    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port        = htons(udp_port);
    bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));

    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

    const uint64_t time_pos_id = 1;
    mpv_observe_property(client, time_pos_id, "time-pos", MPV_FORMAT_DOUBLE);

    // remember where the master is in the file
    double udp_master_position = 0;
    // whether we timed out before waiting for a master message
    int timed_out = -1;
    // last time we received a valid master message
    int64_t last_success = 0;

    bool file_loaded = false;
    bool seeking = false;
    while (1) {
        mpv_event *e = mpv_wait_event(client, 1e20);
        switch (e->event_id) {
        case MPV_EVENT_SHUTDOWN:
            goto done;
        case MPV_EVENT_FILE_LOADED: {
            // initialize
            double pts = 0;
            mpv_get_property(client, "time-pos", MPV_FORMAT_DOUBLE, &pts);
            udp_master_position = pts - udp_seek_threshold / 2;
            timed_out = 0;
            last_success = mpv_get_time_us(client);
            file_loaded = true;
            break;
        }
        case MPV_EVENT_END_FILE:
            file_loaded = false;
            break;
        case MPV_EVENT_SEEK:
            seeking = true;
            break;
        case MPV_EVENT_PLAYBACK_RESTART:
            seeking = false;
            break;
        case MPV_EVENT_PROPERTY_CHANGE: {
            if (e->reply_userdata == time_pos_id) {
                if (file_loaded && !seeking)
                    goto handle_sync;
            }
            break;
        }
        default:
            break;
        }

        continue;

    handle_sync: ;
        // Sync here.

        // grab any waiting datagrams without blocking
        int master_exited = get_udp(log, sockfd, 0, &udp_master_position);

        while (!master_exited || (!timed_out && master_exited < 0)) {
            double my_position = 0;
            mpv_get_property(client, "time-pos", MPV_FORMAT_DOUBLE, &my_position);

            // if we're way off, seek to catch up
            if (fabs(my_position - udp_master_position) > udp_seek_threshold) {
                mpv_set_property(client, "time-pos", MPV_FORMAT_DOUBLE,
                                 &udp_master_position);
                // We hope to receive a MPV_EVENT_SEEK
                seeking = true;
                break;
            }

            // normally we expect that the master will have just played the
            // frame we're ready to play.  break out and play it, and we'll be
            // right in sync.
            // or, the master might be up to a few seconds ahead of us, in
            // which case we also want to play the current frame immediately,
            // without waiting.
            // UDP_TIMING_TOLERANCE is a small value that lets us consider
            // the master equal to us even if it's very slightly ahead.
            if (udp_master_position + UDP_TIMING_TOLERANCE > my_position)
                break;

            // the remaining case is that we're slightly ahead of the master.
            // usually, it just means we called get_udp() before the datagram
            // arrived.  call get_udp again, but this time block until we receive
            // a datagram.
            master_exited = get_udp(log, sockfd, 1, &udp_master_position);
            if (master_exited < 0)
                timed_out = 1;
        }

        if (master_exited >= 0) {
            last_success = mpv_get_time_us(client);
            timed_out = 0;
        } else {
            master_exited = 0;
            timed_out |= mpv_get_time_us(client) - last_success > 30 * 1e6;
        }

        if (timed_out || master_exited > 0)
            break;
    }
done:
    mp_warn(log, "Exiting.\n");
    // TODO: close the socket (MPlayer doesn't do it)
    return 0;
}

static int load_udpsync(struct mpv_handle *client, const char *fname)
{
    struct mp_log *log = mp_client_get_log(client);
    if (strcmp(fname, "slave.udpsync") == 0) {
        return run_slave(client, log);
    } else if (strcmp(fname, "master.udpsync") == 0) {
        return run_master(client, log);
    }
    mp_fatal(log, "Must use 'slave.udpsync' or 'master.udpsync'.\n");
    return -1;
}

// This is not really a scripting wrapper, but let's hack it.
const struct mp_scripting mp_scripting_udp_sync = {
    .file_ext = "udpsync",
    .load = load_udpsync,
};
