#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <time.h>
#include "base_station.h"
#include "node.h"
#include "shared_constants.h"
#include "shared_structs.h"
#include "helpers.h"

int main(int argc, char *argv[])
{
    int global_rank, worker_rank, provided, simulation_seconds, availability_threshold;
    MPI_Comm worker_comm, cart_comm;
    MPI_Datatype alert_report_type;
    int dims[CARTESIAN_DIMENSIONS], coord[CARTESIAN_DIMENSIONS], neighbours[MAX_NEIGHBOURS], second_order_neighbours[MAX_SECOND_ORDER_NEIGHBOURS];
    FILE *fp;
    time_t clock_time;
    struct tm *tm_info;
    char time_str[20];

    time(&clock_time);
    tm_info = localtime(&clock_time);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    fp = fopen(LOG_FILE_NAME, "w");
    fprintf(fp, "Simulation start: %s\n", time_str);
    fclose(fp);

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE)
    {
        printf("The threading support level is lesser than that demanded.\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }
    MPI_Wtime();
    if (set_up(argc, argv, dims, &simulation_seconds, &availability_threshold, &alert_report_type) != 0){
        printf("Error while setting up.\n");
        return 1;
    }

    MPI_Comm_rank(MPI_COMM_WORLD, &global_rank);
    MPI_Comm_split(MPI_COMM_WORLD, global_rank == BASE_STATION_RANK, 0, &worker_comm);

    if (global_rank == BASE_STATION_RANK)
    {
        fp = fopen(LOG_FILE_NAME, "a");
        fprintf(fp, "Node grid dimensions: %d X %d\nTotal ports per station: %d\nNumber of available ports to be considered insufficient: %d\n", dims[0], dims[1], PORTS_PER_NODE, availability_threshold);
        fclose(fp);
    }

    // node set-up
    if (global_rank != BASE_STATION_RANK)
    {
        if (node_set_up(&worker_comm, &cart_comm, dims, coord, neighbours, second_order_neighbours, &worker_rank) != 0)
        {
            printf("Error setting up node.\n");
            return 1;
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // lifecycle loops
    if (global_rank == BASE_STATION_RANK)
    {
        if (base_station_lifecycle(dims[0] * dims[1], simulation_seconds, alert_report_type, dims[1], availability_threshold) != 0)
        {
            printf("Error in base station lifecycle.\n");
            return 1;
        }
        time(&clock_time);
        tm_info = localtime(&clock_time);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
        fp = fopen(LOG_FILE_NAME, "a");
        fprintf(fp, "\nSimulation end: %s\n", time_str);
        fclose(fp);
    }
    else
    {
        if (node_lifecycle(neighbours, second_order_neighbours, &cart_comm, worker_rank, alert_report_type, availability_threshold) != 0)
        {
            printf("Error in node lifecycle.\n");
            return 1;
        }
    }

    // clean up
    if (global_rank != BASE_STATION_RANK)
    {
        MPI_Comm_free(&cart_comm);
        MPI_Comm_free(&worker_comm);
    }
    MPI_Type_free(&alert_report_type);
    MPI_Finalize();

    return 0;
}