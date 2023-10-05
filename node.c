#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <omp.h>
#include <string.h>
#include "node.h"
#include "shared_constants.h"
#include "shared_structs.h"

int node_set_up(MPI_Comm *worker_comm, MPI_Comm *cart_comm, int *dims, int *coord, int *neighbours, int *second_order_neighbours, int *worker_rank)
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

    // Calculate second order neighbour ranks
    int top_top[2] = {coord[0] - 2, coord[1]};          // Top-top neighbor
    int top_right[2] = {coord[0] - 1, coord[1] + 1};    // Top-right neighbor
    int right_right[2] = {coord[0], coord[1] + 2};      // Right-right neighbor
    int bottom_right[2] = {coord[0] + 1, coord[1] + 1}; // Bottom-right neighbor
    int bottom_bottom[2] = {coord[0] + 2, coord[1]};    // Bottom-bottom neighbor
    int bottom_left[2] = {coord[0] + 1, coord[1] - 1};  // Bottom-left neighbor
    int left_left[2] = {coord[0], coord[1] - 2};        // Left-left neighbor
    int top_left[2] = {coord[0] - 1, coord[1] - 1};     // Top-left neighbor

    // Initialize neighbors with invalid ranks
    for (int i = 0; i < 8; i++)
    {
        second_order_neighbours[i] = MPI_PROC_NULL;
    }

    // Check if the calculated coordinates are within the boundaries
    if (coord[0] >= 2)
    {
        MPI_Cart_rank(*cart_comm, top_top, &second_order_neighbours[0]); // Top-top neighbor
    }
    if (coord[0] >= 1 && coord[1] < dims[1] - 1)
    {
        MPI_Cart_rank(*cart_comm, top_right, &second_order_neighbours[1]); // Top-right neighbor
    }
    if (coord[1] < dims[1] - 2)
    {
        MPI_Cart_rank(*cart_comm, right_right, &second_order_neighbours[2]); // Right-right neighbor
    }
    if (coord[0] < dims[0] - 1 && coord[1] < dims[1] - 1)
    {
        MPI_Cart_rank(*cart_comm, bottom_right, &second_order_neighbours[3]); // Bottom-right neighbor
    }
    if (coord[0] < dims[0] - 2)
    {
        MPI_Cart_rank(*cart_comm, bottom_bottom, &second_order_neighbours[4]); // Bottom-bottom neighbor
    }
    if (coord[0] < dims[0] - 1 && coord[1] >= 1)
    {
        MPI_Cart_rank(*cart_comm, bottom_left, &second_order_neighbours[5]); // Bottom-left neighbor
    }
    if (coord[1] >= 2)
    {
        MPI_Cart_rank(*cart_comm, left_left, &second_order_neighbours[6]); // Left-left neighbor
    }
    if (coord[0] >= 1 && coord[1] >= 1)
    {
        MPI_Cart_rank(*cart_comm, top_left, &second_order_neighbours[7]); // Top-left neighbor
    }

    return 0;
}

int node_lifecycle(int *neighbours, int *second_order_neighbours, MPI_Comm *cart_comm, int worker_rank, MPI_Datatype alert_report_type)
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
                            while (wait_current - wait_start < MAX_WAIT_TIME && exit_flag == 0)
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
                        }
                    }
                    // prepare report for base station
                    struct AlertReport alert_report;
                    alert_report.reporting_node = worker_rank;
                    alert_report.reporting_node_availability = current_availability;
                    alert_report.messages_exchanged_between_nodes = 0;
                    for (i = 0; i < MAX_NEIGHBOURS; i++)
                    {

                        alert_report.neighbours[i] = neighbours[i];

                        if (neighbours[i] != MPI_PROC_NULL)
                        {
                            // 2 messages exchanged with each neighbour
                            alert_report.messages_exchanged_between_nodes += 2;
                            alert_report.neighbours_availability[i] = neighbour_availability[i];
                        }
                        else
                        {
                            // -1 indicates that there is no neighbour for this index
                            alert_report.neighbours_availability[i] = -1;
                        }
                    }
                    for (i = 0; i < MAX_SECOND_ORDER_NEIGHBOURS; i++)
                    {
                        alert_report.second_order_neighbours[i] = second_order_neighbours[i];
                    }
                    if (exit_flag == 0)
                    {
                        printf("Node %d alerting base station\n", worker_rank);
                        // indicate to base station that the node is occupied, but its neighbours have availability
                        MPI_Send(&alert_report, 1, alert_report_type, BASE_STATION_RANK, ALERT_TAG, MPI_COMM_WORLD);
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
                    if (rand_r(&seed) % 2 == 0 && timestamp_queue[queue_index].available_ports > 0)
                    {
                        timestamp_queue[queue_index].available_ports--;
                    }
                }
            }
        }
    }
    return 0;
}
