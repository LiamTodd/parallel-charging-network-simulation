#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <omp.h>
#include "node.h"
#include "shared_constants.h"

int node_set_up(MPI_Comm *worker_comm, MPI_Comm *cart_comm, int *dims, int *coord, int *neighbours, int *worker_rank)
{
    int wrap_around[CARTESIAN_DIMENSIONS] = {0, 0};
    int grid_size = dims[0] * dims[1];
    int reorder = 1;
    // create cartesian grid of worker nodes
    MPI_Dims_create(grid_size, CARTESIAN_DIMENSIONS, dims);
    if (MPI_Cart_create(*worker_comm, CARTESIAN_DIMENSIONS, dims, wrap_around, reorder, cart_comm) != 0)
    {
        printf("Error: unable to create cartesian grid.\n");
        return 1;
    }
    // get node's coordinates
    MPI_Comm_rank(*worker_comm, worker_rank);
    MPI_Cart_coords(*cart_comm, *worker_rank, CARTESIAN_DIMENSIONS, coord);

    // find neighbours
    MPI_Cart_shift(*cart_comm, SHIFT_ROW, DISP, &neighbours[0], &neighbours[1]);
    MPI_Cart_shift(*cart_comm, SHIFT_COL, DISP, &neighbours[2], &neighbours[3]);
    return 0;
}

int node_lifecycle(int *neighbours, MPI_Comm *cart_comm, int worker_rank)
{
    // set up shared struct for current timestamp
    struct TimestampData timestamp_queue[MAX_TIMESTAMP_DATAPOINTS];
    int queue_index = 0;
    time_t current_time;
    struct tm *time_info;
    time(&current_time);
    time_info = localtime(&current_time);
    int year = time_info->tm_year + 1900; // Year starts from 1900, add 1900
    int month = time_info->tm_mon + 1;    // Months are 0-based, add 1
    int day = time_info->tm_mday;
    int hours = time_info->tm_hour;
    int minutes = time_info->tm_min;
    int seconds = time_info->tm_sec;
    struct TimestampData new_entry = {year, month, day, hours, minutes, seconds, PORTS_PER_NODE}; // initialise all ports to be free
    timestamp_queue[queue_index] = new_entry;

    // each port gets one thread, one thread dedicated to sending alerts, one dedicated to receiving alerts, one as a master clock
    omp_set_num_threads(PORTS_PER_NODE + 3);
    int exit_flag = 0;

#pragma omp parallel shared(timestamp_queue, queue_index, neighbours, worker_rank, cart_comm, exit_flag)
    {
        int thread_num = omp_get_thread_num();
        if (thread_num == 0)
        {
            // thread 0 handles tallying and sending alerts
            while (exit_flag == 0)
            {
                sleep(1);
                int current_availability;
#pragma omp critical
                {
                    current_availability = timestamp_queue[queue_index].available_ports;
                }
                if (current_availability < AVAILABILITY_THRESHOLD)
                {
                    printf("ALERT: Node %d low availability. Sending message to %d, %d, %d, %d\n", worker_rank, neighbours[0], neighbours[1], neighbours[2], neighbours[3]);
                    // alert neighbours
                    int alert_neighbour_signal = ALERT_NEIGHBOUR_SIGNAL;
                    int neighbour_availability[MAX_NEIGHBOURS] = {-1, -1, -1, -1};
                    int i;
                    for (i = 0; i < MAX_NEIGHBOURS; i++)
                    {
                        if (neighbours[i] != MPI_PROC_NULL)
                        {
                            MPI_Status recv_status;
                            MPI_Send(&alert_neighbour_signal, 1, MPI_INT, neighbours[i], NEIGHBOUR_ALERT_TAG, *cart_comm);
                            time_t wait_start, wait_current;
                            time(&wait_start);
                            time(&wait_current);
                            MPI_Status probe_status;
                            int flag = 0;
                            while (wait_current - wait_start < MAX_WAIT_TIME)
                            {
                                MPI_Iprobe(neighbours[i], NEIGHBOUR_AVAILABILITY_TAG, *cart_comm, &flag, &probe_status);
                                if (flag)
                                {
                                    MPI_Recv(&neighbour_availability[i], 1, MPI_INT, neighbours[i], NEIGHBOUR_AVAILABILITY_TAG, *cart_comm, &recv_status);
                                    break;
                                }
                                else
                                {
                                    time(&wait_current);
                                }
                            }
                            printf("Node %d: Time waited for %d: %li\n", worker_rank, neighbours[i], wait_current - wait_start);
                        }
                    }
                    printf("RECV: Node %d received availability from neighbours: %d, %d, %d, %d\n", worker_rank, neighbour_availability[0], neighbour_availability[1], neighbour_availability[2], neighbour_availability[3]);
                    // if neighbour free, indicate somewhere
                    // if all neighbours occupied, alert base station
                    for (i = 0; i < MAX_NEIGHBOURS; i++)
                    {
                        if (neighbours[i] != MPI_PROC_NULL)
                        {
                            // printf("RECV: Node %d Received message from %d: Available: %d\n", worker_rank, neighbours[i], neighbour_availability[i]);
                        }
                    }
                }
            }
        }
        else if (thread_num == 1)
        {
            while (exit_flag == 0)
            {
                sleep(1);
                // thread 1 periodically probes neighbours for alerts and responds to them
                int current_availability;
#pragma omp critical
                {
                    current_availability = timestamp_queue[queue_index].available_ports;
                }
                int i;
                for (i = 0; i < MAX_NEIGHBOURS; i++)
                {
                    if (neighbours[i] != MPI_PROC_NULL)
                    {
                        int flag = 0;
                        MPI_Status probe_status;
                        MPI_Status recv_status;
                        MPI_Iprobe(neighbours[i], NEIGHBOUR_ALERT_TAG, *cart_comm, &flag, &probe_status);
                        if (flag)
                        {
                            int alert_signal;
                            MPI_Recv(&alert_signal, 1, MPI_INT, neighbours[i], NEIGHBOUR_ALERT_TAG, *cart_comm, &recv_status);
                            if (alert_signal == ALERT_NEIGHBOUR_SIGNAL)
                            {
                                printf("REPLY: Node %d received message from %d and has %d available\n", worker_rank, neighbours[i], current_availability);
                                MPI_Send(&current_availability, 1, MPI_INT, neighbours[i], NEIGHBOUR_AVAILABILITY_TAG, *cart_comm);
                            }
                        }
                    }
                }
            }
        }
        else if (thread_num == 2)
        {
            // thread 2 is the 'master clock'
            // it sets the current time period for all threads by initialising a new entry and updating the queue index
            // it also polls the base station for the termination signal
            while (exit_flag == 0)
            {
                sleep(1);

                // update circular queue
                time_t current_time;
                struct tm *time_info;
                time(&current_time);
                time_info = localtime(&current_time);
                int year = time_info->tm_year + 1900; // Year starts from 1900, add 1900
                int month = time_info->tm_mon + 1;    // Months are 0-based, add 1
                int day = time_info->tm_mday;
                int hours = time_info->tm_hour;
                int minutes = time_info->tm_min;
                int seconds = time_info->tm_sec;
                struct TimestampData new_entry = {year, month, day, hours, minutes, seconds, 5}; // initialise all ports to be free
#pragma omp critical
                {
                    queue_index = (queue_index + 1) % MAX_TIMESTAMP_DATAPOINTS;
                    timestamp_queue[queue_index] = new_entry;
                }

                int flag = 0;
                MPI_Status probe_status;
                MPI_Status recv_status;
                MPI_Iprobe(BASE_STATION_RANK, 0, MPI_COMM_WORLD, &flag, &probe_status);
                if (flag)
                {
                    int termination_signal;
                    MPI_Recv(&termination_signal, 1, MPI_INT, BASE_STATION_RANK, TERMINATION_TAG, MPI_COMM_WORLD, &recv_status);
                    if (termination_signal == TERMINATION_SIGNAL)
                    {
                        printf("TERMINATION: Node %d Received termination signal from base station\n", worker_rank);
#pragma omp atomic write
                        exit_flag = 1;
                    }
                }
            }
        }
        else
        {
            // threads 3..N represent charging nodes
            while (exit_flag == 0)
            {
                sleep(1);
                unsigned int seed = thread_num * (worker_rank + 1) + time(NULL);
#pragma omp critical
                {
                    // a port has a 1/3 chance of being unavailable at any timestamp
                    if (rand_r(&seed) % 1 == 0 && timestamp_queue[queue_index].available_ports > 0 && worker_rank == 1)
                    {
                        timestamp_queue[queue_index].available_ports--;
                    }
                }
            }
        }
    }
    return 0;
}

// have one thread to poll the master for a 'stop' condition, at which point the whole function stops and returns