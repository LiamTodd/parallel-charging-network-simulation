#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <omp.h>
#include <time.h>
#include "base_station.h"
#include "node.h"

#define BASE_STATION_RANK 0
#define MAX_TIMESTAMP_DATAPOINTS 5
#define PORTS_PER_NODE 5
#define AVAILABILITY_THRESHOLD 2
#define MAX_NEIGHBOURS 4
#define ALERT_NEIGHBOUR_SIGNAL 100
#define AVAILABLE 1
#define UNAVAILABLE 0

struct TimestampData
{
    int year;
    int month;
    int day;
    int hours;
    int minutes;
    int seconds;
    int available_ports;
};

int main(int argc, char *argv[])
{
    int global_rank, worker_rank, provided;
    MPI_Comm worker_comm, cart_comm;
    int dims[CARTESIAN_DIMENSIONS], coord[CARTESIAN_DIMENSIONS], neighbours[MAX_NEIGHBOURS];

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE)
    {
        printf("The threading support level is lesser than that demanded.\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    MPI_Comm_rank(MPI_COMM_WORLD, &global_rank);
    MPI_Comm_split(MPI_COMM_WORLD, global_rank == BASE_STATION_RANK, 0, &worker_comm);

    // base station set-up
    if (global_rank == BASE_STATION_RANK)
    {
        if (base_station_set_up(argc, argv, dims) != 0)
        {
            printf("Error setting up base station\n");
            return 1;
        };
    }
    // broadcast dims to workers
    MPI_Bcast(&dims, CARTESIAN_DIMENSIONS, MPI_INT, BASE_STATION_RANK, MPI_COMM_WORLD);
    // node set-up
    if (global_rank != BASE_STATION_RANK)
    {
        if (node_set_up(&worker_comm, &cart_comm, dims, coord, neighbours, &worker_rank) != 0)
        {
            printf("Error setting up node\n");
            return 1;
        }
        // printf("Node global rank: %d\nNode grid rank: %d\nNode cartesian coordinates: %d, %d\nNeighbours: %d %d %d %d\n\n", global_rank, worker_rank, coord[0], coord[1], neighbours[0], neighbours[1], neighbours[2], neighbours[3]);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    // lifecycle loops
    if (global_rank == BASE_STATION_RANK)
    {
        if (base_station_loop() != 0)
        {
            printf("Error in base station lifecycle loop\n");
            return 1;
        }
    }
    else
    {
        // set up availability array
        struct TimestampData timestamp_queue[MAX_TIMESTAMP_DATAPOINTS];
        int queue_index = 0;
        // each port gets one thread, one thread dedicated to sending alerts, one dedicated to receiving alerts
        omp_set_num_threads(PORTS_PER_NODE + 2);

        // simulate node over time
        for (int second = 0; second < 10; second++) // TODO: replace this condition
        {
            // set up shared struct for current timestamp
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

#pragma omp parallel
            {
                int thread_num = omp_get_thread_num();
                // Check available ports
                struct TimestampData current_data;
#pragma omp critical
                {
                    current_data = timestamp_queue[queue_index];
                }
                if (thread_num == 0)
                {
                    // Thread 0 handles tallying and sending alerts
                    if (second % 3 == 0)
                    {
                        if (current_data.available_ports < AVAILABILITY_THRESHOLD)
                        {
                            printf("T: %d, Node: %d, Low availability.\n", second, worker_rank);
                            // alert neighbours
                            int alert_neighbour_signal = ALERT_NEIGHBOUR_SIGNAL;
                            int neighbour_availability[MAX_NEIGHBOURS] = {-1, -1, -1, -1};
                            int i;
                            for (i = 0; i < MAX_NEIGHBOURS; i++)
                            {
                                if (neighbours[i] != MPI_PROC_NULL)
                                {
                                    MPI_Status recv_status;
                                    printf("Node %d sending message to node %d\n", worker_rank, neighbours[i]);
                                    MPI_Send(&alert_neighbour_signal, 1, MPI_INT, neighbours[i], 0, cart_comm);
                                    MPI_Recv(&neighbour_availability[i], 1, MPI_INT, neighbours[i], 0, cart_comm, &recv_status);
                                }
                            }
                            // if neighbour free, indicate somewhere
                            // if all neighbours occupied, alert base station
                            for (i = 0; i < MAX_NEIGHBOURS; i++)
                            {
                                if (neighbours[i] != MPI_PROC_NULL)
                                {
                                    printf("Sender: %d Neighbour: %d Available %d\n", worker_rank, neighbours[i], neighbour_availability[i]);
                                }
                            }
                        }
                    }
                }
                else if (thread_num == 1)
                {
                    // thread 1 periodically probes neighbours for alerts, responds with availability
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
                            MPI_Iprobe(neighbours[i], 0, cart_comm, &flag, &probe_status);
                            if (flag)
                            {
                                int alert_signal;
                                MPI_Recv(&alert_signal, 1, MPI_INT, neighbours[i], 0, cart_comm, &recv_status);
                                if (alert_signal == ALERT_NEIGHBOUR_SIGNAL)
                                {
                                    printf("I am %d and received a message from %d and have %d available\n", worker_rank, neighbours[i], current_data.available_ports);

                                    MPI_Send(&current_data.available_ports, 1, MPI_INT, neighbours[i], 0, cart_comm);
                                }
                            }
                        }
                    }
                }
                else
                // Threads 2..N are ports. Each port updates its availability
                {
                    // a port has a 1/2 chance of being available at any timestamp
                    unsigned int seed = thread_num * worker_rank + second;
                    if (rand_r(&seed) % 2 == 0)
                    {
#pragma omp atomic
                        timestamp_queue[queue_index].available_ports--;
                    }
                }
            }
            // circular queue behaviour
            queue_index = (queue_index + 1) % MAX_TIMESTAMP_DATAPOINTS;
        }
    }

    // clean up
    if (global_rank != BASE_STATION_RANK)
    {
        MPI_Comm_free(&cart_comm);
        MPI_Comm_free(&worker_comm);
    }
    MPI_Finalize();

    return 0;
}