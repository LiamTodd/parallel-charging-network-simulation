#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <omp.h>
#include "node.h"

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

#pragma omp parallel shared(timestamp_queue, queue_index, neighbours, worker_rank, cart_comm)
    {
        int thread_num = omp_get_thread_num();
        if (thread_num == 0)
        {
            // thread 0 handles tallying and sending alerts
            for (int s = 0; s < 10; s++)
            {
                struct TimestampData current_data;
#pragma omp critical
                {
                    current_data = timestamp_queue[queue_index];
                }
                if (current_data.available_ports < AVAILABILITY_THRESHOLD)
                {
                    // alert neighbours
                    int alert_neighbour_signal = ALERT_NEIGHBOUR_SIGNAL;
                    int neighbour_availability[MAX_NEIGHBOURS] = {-1, -1, -1, -1};
                    int i;
                    for (i = 0; i < MAX_NEIGHBOURS; i++)
                    {
                        if (neighbours[i] != MPI_PROC_NULL)
                        {
                            MPI_Status recv_status;
                            printf("Node %d low availability. Sending message to node %d\n", worker_rank, neighbours[i]);
                            MPI_Send(&alert_neighbour_signal, 1, MPI_INT, neighbours[i], NEIGHBOUR_ALERT_TAG, *cart_comm);
                            MPI_Recv(&neighbour_availability[i], 1, MPI_INT, neighbours[i], NEIGHBOUR_AVAILABILITY_TAG, *cart_comm, &recv_status);
                        }
                    }
                    // if neighbour free, indicate somewhere
                    // if all neighbours occupied, alert base station
                    for (i = 0; i < MAX_NEIGHBOURS; i++)
                    {
                        if (neighbours[i] != MPI_PROC_NULL)
                        {
                            printf("Node %d Received message from %d: Available: %d\n", worker_rank, neighbours[i], neighbour_availability[i]);
                        }
                    }
                }
            }
        }
        else if (thread_num == 1)
        {
            for (int s2 = 0; s2 < 10; s2++)
            {
                // thread 1 periodically probes neighbours for alerts and responds to them
                struct TimestampData current_data;
#pragma omp critical
                {
                    current_data = timestamp_queue[queue_index];
                }
                int i;
                for (i = 0; i < MAX_NEIGHBOURS; i++)
                {
                    if (neighbours[i] != MPI_PROC_NULL)
                    {
                        int flag = 0;
                        MPI_Status probe_status;
                        MPI_Status recv_status;
                        MPI_Iprobe(neighbours[i], 0, *cart_comm, &flag, &probe_status);
                        if (flag)
                        {
                            int alert_signal;
                            MPI_Recv(&alert_signal, 1, MPI_INT, neighbours[i], NEIGHBOUR_ALERT_TAG, *cart_comm, &recv_status);
                            if (alert_signal == ALERT_NEIGHBOUR_SIGNAL)
                            {
                                printf("Node %d received message from %d and have %d available\n", worker_rank, neighbours[i], current_data.available_ports);
                                MPI_Send(&current_data.available_ports, 1, MPI_INT, neighbours[i], NEIGHBOUR_AVAILABILITY_TAG, *cart_comm);
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
            for (int a = 0; a < 10; a++)
            {
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
#pragma omp critical
                {
                    // circular queue behaviour
                    timestamp_queue[queue_index] = new_entry;
                    queue_index = (queue_index + 1) % MAX_TIMESTAMP_DATAPOINTS;
                }

            }

        }
        else
        {
            // threads 3..N represent charging nodes
            for (int s3 = 0; s3 < 10; s3++)
            {
                unsigned int seed = thread_num * (worker_rank + 1) + s3;
#pragma omp critical
                {
                    // a port has a 1/3 chance of being unavailable at any timestamp
                    if (rand_r(&seed) % 2 == 0 && worker_rank == 0 && timestamp_queue[queue_index].available_ports > 0)
                    {
                        timestamp_queue[queue_index].available_ports--;
                    }
                }
            }
        }
    }
    return 0;
}

// CURRENT ISSUE
// communication seems sus, but possibly is working.
// todo: alter the way that the loops run
// have one thread to poll the master for a 'stop' condition, at which point the whole function stops and returns