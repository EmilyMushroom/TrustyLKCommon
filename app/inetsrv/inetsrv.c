/*
 * Copyright (c) 2014 Travis Geiselbrecht
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <app.h>
#include <debug.h>
#include <trace.h>
#include <stdio.h>
#include <stdlib.h>
#include <compiler.h>
#include <kernel/thread.h>
#include <lib/minip.h>
#include <lib/cksum.h>

#include "inetsrv.h"

static int chargen_worker(void *socket)
{
    uint64_t count = 0;
#define CHARGEN_BUFSIZE (64*1024)

    /* allocate a large buffer to flood the socket layer */
    uint8_t *buf = malloc(CHARGEN_BUFSIZE);
    if (!buf)
        return -1;

    uint8_t c = '!';
    for (size_t i = 0; i < CHARGEN_BUFSIZE; i++) {
        buf[i] = c++;
        if (c == 0x7f)
            c = ' ';
    }

    for (;;) {
        ssize_t ret = tcp_write(socket, buf, CHARGEN_BUFSIZE);
        //TRACEF("tcp_write returns %d\n", ret);
        if (ret < 0)
            break;

        count += ret;
    }

    TRACEF("chargen worker exiting, wrote %llu bytes\n", count);
    free(buf);
    tcp_close(socket);

    return 0;
}

static int chargen_server(void *arg)
{
    status_t err;
    void *listen_socket;

    err = tcp_open_listen(&listen_socket, 19);
    if (err < 0) {
        TRACEF("error opening chargen listen socket\n");
        return -1;
    }

    for (;;) {
        void *accept_socket;

        err = tcp_accept(listen_socket, &accept_socket);
        TRACEF("tcp_accept returns returns %d, handle %p\n", err, accept_socket);
        if (err < 0) {
            TRACEF("error accepting socket, retrying\n");
            continue;
        }

        TRACEF("starting chargen worker\n");
        thread_detach_and_resume(thread_create("chargen_worker", &chargen_worker, accept_socket, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
    }
}

static int discard_worker(void *socket)
{
    uint64_t count = 0;
    uint32_t crc = 0;

    for (;;) {
        uint8_t buf[32];

        ssize_t ret = tcp_read(socket, buf, sizeof(buf));
        if (ret <= 0)
            break;

        crc = crc32(crc, buf, ret);

        count += ret;
    }

    TRACEF("discard worker exiting, read %llu bytes, crc32 0x%x\n", count, crc);
    tcp_close(socket);

    return 0;
}

static int discard_server(void *arg)
{
    status_t err;
    void *listen_socket;

    err = tcp_open_listen(&listen_socket, 9);
    if (err < 0) {
        TRACEF("error opening discard listen socket\n");
        return -1;
    }

    for (;;) {
        void *accept_socket;

        err = tcp_accept(listen_socket, &accept_socket);
        TRACEF("tcp_accept returns returns %d, handle %p\n", err, accept_socket);
        if (err < 0) {
            TRACEF("error accepting socket, retrying\n");
            continue;
        }

        TRACEF("starting discard worker\n");
        thread_detach_and_resume(thread_create("discard_worker", &discard_worker, accept_socket, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
    }

}

static void inetsrv_init(const struct app_descriptor *app)
{
}

static void inetsrv_entry(const struct app_descriptor *app, void *args)
{
    /* XXX wait for the stack to initialize */

    printf("starting internet servers\n");

    thread_detach_and_resume(thread_create("chargen", &chargen_server, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
    thread_detach_and_resume(thread_create("discard", &discard_server, NULL, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
}

APP_START(inetsrv)
    .init = inetsrv_init,
    .entry = inetsrv_entry,
    .flags = 0,
APP_END