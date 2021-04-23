#include <mpi/mpi.h>
#include <stdlib.h>
#include "app.h"

int main(int argc, char** argv)
{
    app_t app;
    arguments_t arguments;

    // Initialize MPI
    MPI_Init(NULL, NULL);

    // Parse input
    parse_input(argc, argv, &arguments);

    // Initialize app
    init_app(&app, &arguments);

    if (arguments.list_devices)
        print_available_devices();
    else if (arguments.sz_text != NULL)
        test_pattern(arguments.sz_text, &app);
    else if (arguments.sz_filename != NULL)
        test_file(arguments.sz_filename, &app);
    else
        capture_network(&app);

    // Destroy app
    destroy_app(&app);

    // Shutdown MPI
    MPI_Finalize();

    return 0;
}
