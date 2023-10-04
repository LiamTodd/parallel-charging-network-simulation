struct NeighbourAvailableReport
{
    int reporting_node;
    int reporting_node_availability;
    int neighbours[4];
    int available_neighbours[4];
    int messages_exchanged_between_nodes;
};

struct AlertReport
{
    int reporting_node;
    int reporting_node_availability;
    int neighbours[4];
    int neighbours_availability[4];
    int nearby[8];
};