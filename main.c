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
    int global_rank, worker_rank, grid_size, reorder = 1, n, m, processors, provided;
    MPI_Comm worker_comm, cart_comm;
    int wrap_around[CARTESIAN_DIMENSIONS] = {0, 0}, dims[CARTESIAN_DIMENSIONS], coord[CARTESIAN_DIMENSIONS], neighbours[MAX_NEIGHBOURS];

    // MPI_Init(&argc, &argv);
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE)
    {
        printf("The threading support level is lesser than that demanded.\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

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
        // find neighbours
        MPI_Cart_shift(cart_comm, SHIFT_ROW, DISP, &neighbours[0], &neighbours[1]);
        MPI_Cart_shift(cart_comm, SHIFT_COL, DISP, &neighbours[2], &neighbours[3]);
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
            struct TimestampData new_entry = {year, month, day, hours, minutes, seconds, PORTS_PER_NODE}; // initialise all ports to be free
            timestamp_queue[queue_index] = new_entry;

#pragma omp parallel
            {
                int thread_num = omp_get_thread_num();

                if (thread_num == 0)
                {
                    // Thread 0 handles tallying and communication
                    if (second % 3 == 0)
                    {
                        // Check available ports
                        struct TimestampData current_data;
#pragma omp critical
                        {
                            current_data = timestamp_queue[queue_index];
                        }
                        if (current_data.available_ports < AVAILABILITY_THRESHOLD)
                        {
                            printf("T: %d, Node: %d, Low availability.\n", second, worker_rank);
                            // alert neighbours
                            MPI_Request send_request[MAX_NEIGHBOURS];
                            MPI_Request receive_request[MAX_NEIGHBOURS];
                            MPI_Status send_status[MAX_NEIGHBOURS];
                            MPI_Status receive_status[MAX_NEIGHBOURS];
                            int alert_neighbour_signal = ALERT_NEIGHBOUR_SIGNAL;
                            int neighbour_availability[MAX_NEIGHBOURS] = {-1, -1, -1, -1};
                            int i;
                            for (i = 0; i < MAX_NEIGHBOURS; i++)
                            {
                                if (neighbours[i] != MPI_PROC_NULL)
                                {
                                    // USE TAG TO SPECIFY MESSAGE TYPE
                                    MPI_Isend(&alert_neighbour_signal, 1, MPI_INT, neighbours[i], 0, cart_comm, &send_request[i]);
                                    MPI_Irecv(&neighbour_availability[i], 1, MPI_INT, neighbours[i], 0, cart_comm, &receive_request[i]);
                                }
                            }
                            MPI_Waitall(MAX_NEIGHBOURS, send_request, send_status);
                            MPI_Waitall(MAX_NEIGHBOURS, receive_request, receive_status);
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
                        // probe neighbours for alerts, respond with availability
                        int i;
                        for (i = 0; i < MAX_NEIGHBOURS; i++)
                        {
                            if (neighbours[i] != MPI_PROC_NULL)
                            {
                                int flag = 0;
                                MPI_Status probe_status;
                                MPI_Iprobe(neighbours[i], 0, cart_comm, &flag, &probe_status);
                                if (flag)
                                {
                                    int alert_signal;
                                    MPI_Recv(&alert_signal, 1, MPI_INT, neighbours[i], 0, cart_comm, MPI_STATUS_IGNORE); // TODO replace with IRecv
                                    if (alert_signal == ALERT_NEIGHBOUR_SIGNAL)
                                    {
                                        // reply to neighbour
                                    }
                                }
                            }
                        }
                    }
                }
                else
                // Threads 1..N are ports. Each port updates its availability
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