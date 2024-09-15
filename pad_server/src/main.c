#include <getopt.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "controller.h"
#include "helptext/helptext.h"
#include "state.h"
#include "telemetry.h"

#define TELEMETRY_PORT 50002
#define CONTROL_PORT 50001

padstate_t state;

controller_t controller;
pthread_t controller_thread;
controller_args_t controller_args = {.port = CONTROL_PORT, .state = &state};

telemetry_t telem;
pthread_t telem_thread;
telemetry_args_t telemetry_args = {.port = TELEMETRY_PORT, .state = &state, .data_file = NULL};

void int_handler(int sig) {

    (void)(sig);
    int err;

    /* Tell threads to die */

    sem_post(&controller_args.die);
    sem_post(&telemetry_args.die);

    printf("Terminating server...\n");

    /* Wait for control thread to end */
    err = pthread_join(controller_thread, NULL);
    if (err) {
        fprintf(stderr, "Controller thread exited with error: %s\n", strerror(err));
    }
    printf("Controller thread terminated.\n");

    /* Wait for telemetry thread to end */
    err = pthread_join(telem_thread, NULL);
    if (err) {
        fprintf(stderr, "Telemetry thread exited with error: %s\n", strerror(err));
    }
    printf("Telemetry thread terminated.\n");

    exit(EXIT_SUCCESS);
}

/*
 * The pad server has two tasks:
 * - Handle requests from a single control input client to change arming states or actuate actuators
 * - Send telemetry (state changes or sensor measurements) to zero or more telemetry clients
 *
 * All telemetry being sent must be sent to all active telemetry clients at the same time.
 * There must always be at least one telemetry client.
 *
 * Control commands which change state should cause that state change to be broadcasted over the telemetry channel.
 *
 * Sending telemetry should not take processing time away from handling control commands, since those are more
 * important. This requires a multi-threaded model.
 *
 * One thread handles incoming control commands.
 * The other thread sends telemetry data.
 *
 * The control command thread must have some way to signal state changes to the telemetry data thread. Some shared state
 * object for the overall pad control system state can accomplish this. This state object must be synchronized. The
 * telemetry thread can poll it for changes for now.
 */

int main(int argc, char **argv) {

    /* Parse command line options. */

    int c;
    while ((c = getopt(argc, argv, ":ht:")) != -1) {
        switch (c) {
        case 'h':
            puts(HELP_TEXT);
            exit(EXIT_SUCCESS);
            break;
        case 't':
            telemetry_args.data_file = optarg;
            break;
        case '?':
            fprintf(stderr, "Unknown option -%c\n", optopt);
            exit(EXIT_FAILURE);
            break;
        }
    }

    /* Set up the state to be shared */
    padstate_t state;
    padstate_init(&state);

    int err;

    /* Start controller thread */
    sem_init(&controller_args.die, 0, 0);
    err = pthread_create(&controller_thread, NULL, controller_run, &controller_args);
    if (err) {
        fprintf(stderr, "Could not start controller thread: %s\n", strerror(err));
        exit(EXIT_FAILURE);
    }

    /* Start telemetry thread */
    sem_init(&telemetry_args.die, 0, 0);
    err = pthread_create(&telem_thread, NULL, telemetry_run, &telemetry_args);
    if (err) {
        fprintf(stderr, "Could not start telemetry thread: %s\n", strerror(err));
        exit(EXIT_FAILURE);
    }

    /* Attach signal handler */
    signal(SIGINT, int_handler);

    /* Wait for control thread to end */
    err = pthread_join(controller_thread, NULL);
    if (err) {
        fprintf(stderr, "Controller thread exited with error: %s\n", strerror(err));
    }

    /* Wait for telemetry thread to end */
    err = pthread_join(telem_thread, NULL);
    if (err) {
        fprintf(stderr, "Telemetry thread exited with error: %s\n", strerror(err));
    }

    return EXIT_SUCCESS;
}
