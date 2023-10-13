#define MAX_REPORTS 10000

int base_station_lifecycle(int num_nodes, int simulation_seconds, MPI_Datatype alert_report_type, int cols, int availability_threshold, char *log_file_name);