#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <omp.h>
#include <time.h>

#define BASE_STATION_RANK 0
#define CARTESIAN_DIMENSIONS 2
#define MAX_TIMESTAMP_DATAPOINTS 5
#define PORTS_PER_NODE 5
#define AVAILABILITY_THRESHOLD 2
#define SHIFT_ROW 0
#define SHIFT_COL 1
#define DISP 1
#define NEIGHBOURS 4

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
    int global_rank, worker_rank, grid_size, reorder = 1, n, m, processors;
    MPI_Comm worker_comm, cart_comm;
    int wrap_around[CARTESIAN_DIMENSIONS] = {0, 0}, dims[CARTESIAN_DIMENSIONS], coord[CARTESIAN_DIMENSIONS];

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &global_rank);
    MPI_Comm_split(MPI_COMM_WORLD, global_rank == BASE_STATION_RANK, 0, &worker_comm);

    // set-up
    if (global_rank == BASE_STATION_RANK)
    {
        // base station: read in command-line args
        if (argc != 3)
        {
            printf("Error: Invalid number of arguments.\n");
            return 1;
        }
        m = atoi(argv[1]);
        n = atoi(argv[2]);
        if (m == 0 || n == 0)
        {
            printf("Error: Invalid values of n and m. n and m must be positive integers.\n");
            return 1;
        }
        // ensure that processors can accommodate dimensions
        MPI_Comm_size(MPI_COMM_WORLD, &processors);
        if (processors != m * n + 1)
        {
            printf("Error: Dimensions cannot be accommodated by the number of processors. Number of processors must equal n * m + 1.\n");
            return 1;
        }
        dims[0] = m;
        dims[1] = n;
    }
    // broadcast dims to workers
    MPI_Bcast(&dims, CARTESIAN_DIMENSIONS, MPI_INT, BASE_STATION_RANK, MPI_COMM_WORLD);
    if (global_rank != BASE_STATION_RANK)
    {
        // nodes: set up cartesian grid
        grid_size = dims[0] * dims[1];
        // create cartesian grid of worker nodes
        MPI_Dims_create(grid_size, CARTESIAN_DIMENSIONS, dims);
        if (MPI_Cart_create(worker_comm, CARTESIAN_DIMENSIONS, dims, wrap_around, reorder, &cart_comm) != 0)
        {
            printf("Error: unable to create cartesian grid.");
            return 1;
        }
        // get node's coordinates
        MPI_Comm_rank(worker_comm, &worker_rank);
        MPI_Cart_coords(cart_comm, worker_rank, CARTESIAN_DIMENSIONS, coord);
        // printf("Node global rank: %d\nNode grid rank: %d\nNode cartesian coordinates: %d, %d\n\n", global_rank, worker_rank, coord[0], coord[1]);
    }

    // lifecycle loops
    if (global_rank == BASE_STATION_RANK)
    {
        printf("Base station doing its thang\n");
    }
    else
    {
        // set up availability array
        struct TimestampData timestamp_queue[MAX_TIMESTAMP_DATAPOINTS];
        int queue_index = 0;
        // each port gets one thread, one thread dedicated to communication and tallying
        omp_set_num_threads(PORTS_PER_NODE + 1);

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
            struct TimestampData new_entry = {year, month, day, hours, minutes, seconds, 0};
            timestamp_queue[queue_index] = new_entry;

#pragma omp parallel
            {
                int thread_id = omp_get_thread_num();
                if (thread_id == 0)
                {
                    // Thread 0 handles tallying and communication
                    if (second % 3 == 0)
                    // tally results every 3 iterations
                    {
                        printf("T: %d, I am node %d, thread %d\n", second, worker_rank, thread_id);
                        // get available ports
                        // if less than threshold, alert neighbours
                        // await response
                        // if all neighbours occupied, alert base station

                        // probe neighbours for alerts
                        // if alerts received, reply

                    }
                }
                else
                // Threads 1..N are ports. Each port updates its availability
                {
                    // a port has a 1/2 chance of being available at any timestamp
                    if (rand() % 2 == 0)
                    {
#pragma omp atomic
                        // update shared array
                        timestamp_queue[queue_index].available_ports++;
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