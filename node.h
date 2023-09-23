#define SHIFT_ROW 0
#define SHIFT_COL 1
#define DISP 1
#define CARTESIAN_DIMENSIONS 2

int node_set_up(MPI_Comm *worker_comm, MPI_Comm *cart_comm, int *dims, int *coord, int *neighbours, int *worker_rank);