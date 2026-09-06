// Hidden, fork-free child-process entry point for external GCSA2 join tasks.
// The parent passes one versioned task file; no graph semantics are encoded in
// argv and only the parent publishes the completed PathGraph generation.

#include "subcommand.hpp"

#include <gcsa/path_graph.h>
#include <gcsa/path_graph_external.h>

#include <iostream>

using namespace vg::subcommand;

int
main_gcsa_worker_task(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " gcsa-worker-task TASK" << std::endl;
        return 1;
    }
    return gcsa::externalPathJoinWorker(argv[2]);
}

static Subcommand vg_gcsa_worker_task(
    "gcsa-worker-task",
    "run one internal external-memory GCSA2 join partition",
    DEVELOPMENT,
    main_gcsa_worker_task);
