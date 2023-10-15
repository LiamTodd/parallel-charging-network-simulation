/*
This file implements node-specific functions
*/

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
#include "base_station.h"

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

int node_lifecycle(int *neighbours, int *second_order_neighbours, MPI_Comm *cart_comm, int worker_rank, MPI_Datatype alert_report_type, int availability_threshold)
{
    // set up shared struct for current timestamp
    struct TimestampData timestamp_queue[MAX_TIMESTAMP_DATAPOINTS];
    int queue_index = 0, thread_num, i, exit_flag = 0;
    time_t current_time;
    struct tm *time_info;
    char time_str[20];

    // initialise first timestamp data
    time(&current_time);
    time_info = localtime(&current_time);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_info);
    struct TimestampData new_entry;
    // initialise all ports to be free
    for (i = 0; i < PORTS_PER_NODE; i++)
    {
        new_entry.ports[i] = 1;
    }
    strcpy(new_entry.time_str, time_str);
    timestamp_queue[queue_index] = new_entry;

    // each port gets one thread, one thread dedicated to sending alerts, one dedicated to receiving alerts, one as a master clock
    omp_set_num_threads(PORTS_PER_NODE + EXTRA_THREADS);

#pragma omp parallel shared(timestamp_queue, queue_index, exit_flag) private(thread_num)
    {
        thread_num = omp_get_thread_num();
        if (thread_num == 0)
        {
            // thread 0 handles tallying its own availability
            // if insufficient availability, it probes the direct neighbours for theirs
            // reports and alerts are sent to the base station based on this
            while (exit_flag == 0)
            {
                reporter_iteration(timestamp_queue, &queue_index, neighbours, cart_comm, availability_threshold, &exit_flag, worker_rank, second_order_neighbours, alert_report_type);
            }
        }
        else if (thread_num == 1)
        {
            // thread 1 periodically probes neighbours for alerts and responds to them
            while (exit_flag == 0)
            {
                responder_iteration(timestamp_queue, &queue_index, neighbours, cart_comm, &exit_flag);
            }
        }
        else if (thread_num == 2)
        {
            // thread 2 is the 'time keeper'
            // it sets the current time period for all threads by initialising a new entry and updating the queue index
            // it also polls the base station for the termination signal
            while (exit_flag == 0)
            {
                node_time_keeper_iteration(timestamp_queue, &queue_index, &exit_flag);
            }
        }
        else
        {
            // threads 3..N represent charging ports
            // they periodically update the port availability of the node
            while (exit_flag == 0)
            {
                charging_port_iteration(thread_num, worker_rank, timestamp_queue, &queue_index);
            }
        }
    }
    return 0;
}

void reporter_iteration(struct TimestampData timestamp_queue[MAX_TIMESTAMP_DATAPOINTS], int *queue_index, int *neighbours, MPI_Comm *cart_comm, int availability_threshold, int *exit_flag, int worker_rank, int *second_order_neighbours, MPI_Datatype alert_report_type)
{
    sleep(TALLY_INTERVAL);

    int alert_neighbour_signal = ALERT_NEIGHBOUR_SIGNAL, current_availability = 0, i, flag, expect_reply = 1;
    char current_time_str[20];
    int neighbour_availability[MAX_NEIGHBOURS] = {-1, -1, -1, -1};
    int available_so_neighbours[MAX_SECOND_ORDER_NEIGHBOURS];
    int coord[CARTESIAN_DIMENSIONS];
    time_t wait_start, wait_current;
    struct AlertReport alert_report;
    double start_node_comm_time, end_node_comm_time;

#pragma omp critical
    {
        for (i = 0; i < PORTS_PER_NODE; i++)
        {
            if (timestamp_queue[*queue_index].ports[i] == 1)
            {
                current_availability++;
            }
        }
        strcpy(current_time_str, timestamp_queue[*queue_index].time_str);
    }

    // prepare report for base station
    MPI_Cart_coords(*cart_comm, worker_rank, CARTESIAN_DIMENSIONS, coord);
    alert_report.messages_exchanged_between_nodes = 0;
    alert_report.neighbours_count = 0;
    alert_report.row = coord[0];
    alert_report.col = coord[1];
    alert_report.type = ALERT_TYPE;
    alert_report.reporting_node = worker_rank;
    alert_report.reporting_node_availability = current_availability;
    strcpy(alert_report.time_str, current_time_str);

    if (current_availability <= availability_threshold)
    {
        // alert neighbours
        start_node_comm_time = MPI_Wtime();
        for (i = 0; i < MAX_NEIGHBOURS; i++)
        {
            alert_report.neighbours[i] = neighbours[i];

            if (neighbours[i] != MPI_PROC_NULL)
            {
                MPI_Status recv_status;
                MPI_Send(&alert_neighbour_signal, 1, MPI_INT, neighbours[i], NEIGHBOUR_ALERT_TAG, *cart_comm);
                time(&wait_start);
                time(&wait_current);
                MPI_Status probe_status;
                flag = 0;
                while (wait_current - wait_start < MAX_WAIT_TIME && *exit_flag == 0)
                {
                    MPI_Iprobe(neighbours[i], NEIGHBOUR_AVAILABILITY_TAG, *cart_comm, &flag, &probe_status);
                    if (flag)
                    {
                        MPI_Recv(&neighbour_availability[i], 1, MPI_INT, neighbours[i], NEIGHBOUR_AVAILABILITY_TAG, *cart_comm, &recv_status);
                        // 2 messages exchanged with each neighbour
                        alert_report.messages_exchanged_between_nodes += 2;
                        alert_report.neighbours_count++;
                        alert_report.neighbours_availability[i] = neighbour_availability[i];
                        if (neighbour_availability[i] > availability_threshold)
                        {
                            // node has at least one available neighbour, don't need response from base station
                            alert_report.type = REPORT_TYPE;
                            expect_reply = 0;
                        }
                        break;
                    }
                    else
                    {
                        time(&wait_current);
                    }
                }
            }
            else
            {
                // -1 indicates that there is no neighbour for this index
                alert_report.neighbours_availability[i] = -1;
            }
        }
        end_node_comm_time = MPI_Wtime();
        alert_report.node_comm_time = (end_node_comm_time - start_node_comm_time) * 1000.0;

        for (i = 0; i < MAX_SECOND_ORDER_NEIGHBOURS; i++)
        {
            alert_report.second_order_neighbours[i] = second_order_neighbours[i];
        }

        if (*exit_flag == 0)
        {
            // indicate to base station that the node is occupied, but its neighbours have availability
            alert_report.node_base_station_comm_start = MPI_Wtime();
            MPI_Send(&alert_report, 1, alert_report_type, BASE_STATION_RANK, ALERT_TAG, MPI_COMM_WORLD);
            if (expect_reply)
            {
                time(&wait_start);
                time(&wait_current);
                MPI_Status probe_status;
                flag = 0;
                while (wait_current - wait_start < MAX_WAIT_TIME && *exit_flag == 0)
                {
                    MPI_Iprobe(BASE_STATION_RANK, BASE_STATION_REPLY_TAG, MPI_COMM_WORLD, &flag, &probe_status);
                    if (flag)
                    {
                        MPI_Status recv_status;
                        MPI_Recv(&available_so_neighbours, MAX_SECOND_ORDER_NEIGHBOURS, MPI_INT, BASE_STATION_RANK, BASE_STATION_REPLY_TAG, MPI_COMM_WORLD, &recv_status);
                    }
                    else
                    {
                        time(&wait_current);
                    }
                }
            }
        }
    }
}

void responder_iteration(struct TimestampData timestamp_queue[MAX_TIMESTAMP_DATAPOINTS], int *queue_index, int *neighbours, MPI_Comm *cart_comm, int *exit_flag)
{
    usleep(LATENCY_THOUSANDTH_OF_SECOND);
    int current_availability = 0, i, flag, alert_signal;
    MPI_Status probe_status;
    MPI_Status recv_status;

#pragma omp critical
    {
        for (i = 0; i < PORTS_PER_NODE; i++)
        {
            if (timestamp_queue[*queue_index].ports[i] == 1)
            {
                current_availability++;
            }
        }
    }

    // probe neighbours for alerts, reply with avaialbility
    for (i = 0; i < MAX_NEIGHBOURS; i++)
    {
        if (neighbours[i] != MPI_PROC_NULL)
        {
            flag = 0;
            MPI_Iprobe(neighbours[i], NEIGHBOUR_ALERT_TAG, *cart_comm, &flag, &probe_status);
            if (flag && *exit_flag == 0)
            {
                alert_signal = -1;
                MPI_Recv(&alert_signal, 1, MPI_INT, neighbours[i], NEIGHBOUR_ALERT_TAG, *cart_comm, &recv_status);
                if (alert_signal == ALERT_NEIGHBOUR_SIGNAL)
                {
                    MPI_Send(&current_availability, 1, MPI_INT, neighbours[i], NEIGHBOUR_AVAILABILITY_TAG, *cart_comm);
                }
            }
        }
    }
}

void node_time_keeper_iteration(struct TimestampData timestamp_queue[MAX_TIMESTAMP_DATAPOINTS], int *queue_index, int *exit_flag)
{
    time_t current_time;
    struct TimestampData new_entry;
    char time_str[20];
    struct tm *time_info;
    int flag = 0, i;
    MPI_Status probe_status;
    MPI_Status recv_status;
    int termination_signal;

    sleep(QUEUE_INTERVAL);

    time(&current_time);
    time_info = localtime(&current_time);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", time_info);

    // initialise all ports to be free
    for (i = 0; i < PORTS_PER_NODE; i++)
    {
        new_entry.ports[i] = 1;
    }

    strcpy(new_entry.time_str, time_str);

    // update circular queue
#pragma omp critical
    {
        *queue_index = (*queue_index + 1) % MAX_TIMESTAMP_DATAPOINTS;
        timestamp_queue[*queue_index] = new_entry;
    }

    // probe base station for termination signal
    MPI_Iprobe(BASE_STATION_RANK, TERMINATION_TAG, MPI_COMM_WORLD, &flag, &probe_status);
    if (flag)
    {

        MPI_Recv(&termination_signal, 1, MPI_INT, BASE_STATION_RANK, TERMINATION_TAG, MPI_COMM_WORLD, &recv_status);

        if (termination_signal == TERMINATION_SIGNAL)
        {
#pragma omp atomic write
            *exit_flag = 1;
        }
    }
}

void charging_port_iteration(int thread_num, int worker_rank, struct TimestampData timestamp_queue[MAX_TIMESTAMP_DATAPOINTS], int *queue_index)
{
    sleep(AVAILABILITY_INTERVAL);
    unsigned int seed = thread_num * (worker_rank + 1) + time(NULL);
#pragma omp critical
    {
        // a port has a 2/3 chance of being unavailable upon an update
        if (rand_r(&seed) % 3 != 0)
        {
            timestamp_queue[*queue_index].ports[thread_num - EXTRA_THREADS] = 0;
        }
        else
        {
            timestamp_queue[*queue_index].ports[thread_num - EXTRA_THREADS] = 1;
        }
    }
}
