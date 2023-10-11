#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <time.h>
#include "base_station.h"
#include "node.h"
#include "shared_constants.h"
#include "shared_structs.h"

int main(int argc, char *argv[])
{
    int global_rank, worker_rank, provided, simulation_seconds, availability_threshold;
    MPI_Comm worker_comm, cart_comm;
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
    MPI_Wtime();
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
        if (base_station_set_up(argc, argv, dims, &simulation_seconds, &availability_threshold) != 0)
        {
            printf("Error setting up base station.\n");
            return 1;
        };
        fp = fopen(LOG_FILE_NAME, "a");
        fprintf(fp, "Node grid dimensions: %d X %d\nTotal ports per station: %d\nNumber of available ports to be considered insufficient: %d\n", dims[0], dims[1], PORTS_PER_NODE, availability_threshold);
        fclose(fp);
    }
    // broadcast dims to workers
    MPI_Bcast(&dims, CARTESIAN_DIMENSIONS, MPI_INT, BASE_STATION_RANK, MPI_COMM_WORLD);
    MPI_Bcast(&availability_threshold, 1, MPI_INT, BASE_STATION_RANK, MPI_COMM_WORLD);
    // node set-up
    if (global_rank != BASE_STATION_RANK)
    {
        if (node_set_up(&worker_comm, &cart_comm, dims, coord, neighbours, second_order_neighbours, &worker_rank) != 0)
        {
            printf("Error setting up node.\n");
            return 1;
        }
    }

    // define data types
    MPI_Datatype alert_report_type;
    int block_lengths[17] = {1, 1, 4, 4, 1, 8, 1, 20, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    MPI_Aint offsets[17];
    MPI_Datatype types[17] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_CHAR, MPI_INT, MPI_INT, MPI_INT, MPI_DOUBLE, MPI_INT, MPI_DOUBLE, MPI_DOUBLE, MPI_DOUBLE, MPI_DOUBLE};
    offsets[0] = offsetof(struct AlertReport, reporting_node);
    offsets[1] = offsetof(struct AlertReport, reporting_node_availability);
    offsets[2] = offsetof(struct AlertReport, neighbours);
    offsets[3] = offsetof(struct AlertReport, neighbours_availability);
    offsets[4] = offsetof(struct AlertReport, messages_exchanged_between_nodes);
    offsets[5] = offsetof(struct AlertReport, second_order_neighbours);
    offsets[6] = offsetof(struct AlertReport, iteration);
    offsets[7] = offsetof(struct AlertReport, time_str);
    offsets[8] = offsetof(struct AlertReport, neighbours_count);
    offsets[9] = offsetof(struct AlertReport, row);
    offsets[10] = offsetof(struct AlertReport, col);
    offsets[11] = offsetof(struct AlertReport, node_comm_time);
    offsets[12] = offsetof(struct AlertReport, type);
    offsets[13] = offsetof(struct AlertReport, report_received);
    offsets[14] = offsetof(struct AlertReport, report_processed);
    offsets[15] = offsetof(struct AlertReport, node_base_station_comm_start);
    offsets[16] = offsetof(struct AlertReport, node_base_station_comm_end);

    MPI_Type_create_struct(17, block_lengths, offsets, types, &alert_report_type);
    MPI_Type_commit(&alert_report_type);

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