#define MAX_REPORTS 10000

int base_station_set_up(int argc, char *argv[], int *dims, int *simulation_seconds, int *availability_threshold);
int base_station_lifecycle(int num_nodes, int simulation_seconds, MPI_Datatype alert_report_type, int cols, int availability_threshold);