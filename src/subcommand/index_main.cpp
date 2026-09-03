// index.cpp: define the "vg index" subcommand, which makes XG, GCSA2, and distance indexes

#include <omp.h>
#include <unistd.h>
#include <getopt.h>

#include <algorithm>
#include <random>
#include <filesystem>
#include <string>
#include <vector>

#include "subcommand.hpp"

#include "../vg.hpp"
#include "xg.hpp"
#include <vg/io/stream.hpp>
#include <vg/io/vpkg.hpp>
#include "../io/save_handle_graph.hpp"
#include "../stream_index.hpp"
#include "../vg_set.hpp"
#include "../utility.hpp"
#include "../region.hpp"
#include "../integrated_snarl_finder.hpp"
#include "../snarl_distance_index.hpp"
#include "../source_sink_overlay.hpp"
#include "../gbwtgraph_helper.hpp"
#include "../gcsa_helper.hpp"
#include "../gcsa_workspace.hpp"

#include <gcsa/algorithms.h>
#include <bdsg/overlays/packed_subgraph_overlay.hpp>
#include <handlegraph/algorithms/weakly_connected_components.hpp>

using namespace std;
using namespace vg;
using namespace vg::subcommand;

const size_t DEFAULT_SNARL_LIMIT = 50000;

void help_index(char** argv) {
    cerr << "usage: " << argv[0] << " index [options] <graph1.vg> [graph2.vg ...]" << endl
         << "Creates an index on the specified graph or graphs. All graphs indexed must " << endl
         << "already be in a joint ID space." << endl
         << "general options:" << endl
         << "  -h, --help                print this help message to stderr and exit" << endl
         << "  -b, --temp-dir DIR        use DIR for temporary files" << endl
         << "  -t, --threads N           number of threads to use" << endl
         << "  -p, --progress            show progress" << endl
         << "xg options:" << endl
         << "  -x, --xg-name FILE        use this file to store a succinct, queryable version" << endl
         << "                            of graph(s), or read for GCSA or distance indexing" << endl
         << "  -L, --xg-alts             include alt paths in XG" << endl
         << "gcsa options:" << endl
         << "  -g, --gcsa-out FILE       output GCSA2 (FILE) & LCP (FILE.lcp) indexes" << endl
       //<< "  -i, --dbg-in FILE         use kmers from FILE instead of input VG (may repeat)" << endl
         << "  -f, --mapping FILE        use this node mapping in GCSA2 construction" << endl
         << "  -k, --kmer-size N         index kmers of size N in the graph [" << gcsa::Key::MAX_LENGTH << "]" << endl
         << "  -X, --doubling-steps N    use N doubling steps for GCSA2 construction "
                                     << "[" << gcsa::ConstructionParameters::DOUBLING_STEPS << "]" << endl
         << "  -Z, --size-limit N        limit temp disk space usage to N GB "
                                     << "[" << gcsa::ConstructionParameters::SIZE_LIMIT << "]" << endl
         << "  -V, --verify-index        validate the GCSA2 index using the input kmers" << endl
         << "                            (important for testing)" << endl
         << "      --gcsa-work-dir DIR   durable disk-first construction workspace" << endl
         << "      --gcsa-resume         resume committed GCSA2 workspace phases" << endl
         << "      --gcsa-memory-limit S aggregate external-construction working-set target, not a hard whole-process cap" << endl
         << "      --gcsa-disk-limit S   spill-generation disk budget (for example 4T)" << endl
         << "      --gcsa-sort-run-size S max workspace for one label-sort run" << endl
         << "      --gcsa-join-partition-size S max workspace for one join sorter" << endl
         << "      --gcsa-process-workers N fork-free parallel join worker processes" << endl
         << "GAM indexing options:" << endl
         << "  -l, --index-sorted-gam    input is sorted .gam format alignments," << endl
         << "                            store a GAI index of the sorted GAM in INPUT.gam.gai" << endl
         << "vg in-place indexing options:" << endl
         << "      --index-sorted-vg     input is ID-sorted .vg format graph chunks" << endl
         << "                            store a VGI index of the sorted vg in INPUT.vg.vgi" << endl
         << "snarl distance index options" << endl
         << "  -j, --dist-name FILE      use this file to store a snarl-based distance index" << endl
         << "      --snarl-limit N       don't store distances for snarls > N nodes "
                                     << "[" << DEFAULT_SNARL_LIMIT << "]" << endl
         << "                            if 0 then don't store distances, only the snarl tree" << endl
         << "      --no-nested-distance  only store distances along the top-level chain" << endl
         << "  -w, --upweight-node N     upweight the node with ID N to push it to be part" << endl
         << "                            of a top-level chain (may repeat)" << endl
         << "  -P, --path-prefix NAME    upweight tips of paths with given prefix to orient" << endl
         << "                            snarl tree. often necessary when running vg" << endl
         << "                            haplotypes downstream" << endl;
}

int main_index(int argc, char** argv) {
    Logger logger("vg index");

    if (argc == 2) {
        help_index(argv);
        return 1;
    }

    constexpr int OPT_BUILD_VGI_INDEX = 1000;
    constexpr int OPT_RENAME_VARIANTS = 1001;
    constexpr int OPT_DISTANCE_SNARL_LIMIT = 1002;
    constexpr int OPT_DISTANCE_NESTING = 1003;
    constexpr int OPT_GCSA_WORK_DIR = 1004;
    constexpr int OPT_GCSA_RESUME = 1005;
    constexpr int OPT_GCSA_MEMORY_LIMIT = 1006;
    constexpr int OPT_GCSA_DISK_LIMIT = 1007;
    constexpr int OPT_GCSA_SORT_RUN_SIZE = 1008;
    constexpr int OPT_GCSA_JOIN_PARTITION_SIZE = 1009;
    constexpr int OPT_GCSA_PROCESS_WORKERS = 1010;

    // Which indexes to build.
    bool build_xg = false, build_gcsa = false, build_dist = false;
    bool gcsa_resume_requested = false;

    // Files we should read.
    string vcf_name, mapping_name;
    vector<string> dbg_names;

    // Files we should write.
    string xg_name, gcsa_name, dist_name;

    // General
    bool show_progress = false;

    // GCSA
    gcsa::size_type kmer_size = gcsa::Key::MAX_LENGTH;
    gcsa::ConstructionParameters params;
    params.setWorkerExecutable(argv[0]);
    bool verify_gcsa = false;
    
    // Gam index (GAI)
    bool build_gai_index = false;
    
    // VG in-place index (VGI)
    bool build_vgi_index = false;

    // Include alt paths in xg
    bool xg_alts = false;

    //Distance index
    size_t snarl_limit = DEFAULT_SNARL_LIMIT;
    bool only_top_level_chain_distances = false;
    std::unordered_map<nid_t, size_t> extra_node_weight;
    // We will put this amount of extra weight on upweighted nodes. It should
    // be longer than the maximum plausible distracting path or spurious bridge
    // edge cycle, but small enough that several of it fit in a size_t.
    // TODO: Expose to command line.
    constexpr size_t EXTRA_WEIGHT = 10000000000;
    string ref_prefix;

    int c;
    optind = 2; // force optind past command positional argument
    while (true) {
        static struct option long_options[] =
        {
            // General
            {"temp-dir", required_argument, 0, 'b'},
            {"threads", required_argument, 0, 't'},
            {"progress",  no_argument, 0, 'p'},
            {"help",  no_argument, 0, 'h'},

            // XG
            {"xg-name", required_argument, 0, 'x'},
            {"xg-alts", no_argument, 0, 'L'},

            // GBWT. These have been removed and will return an error.
            {"vcf-phasing", required_argument, 0, 'v'},
            {"ignore-missing", no_argument, 0, 'W'},
            {"store-threads", no_argument, 0, 'T'},
            {"store-gam", required_argument, 0, 'M'},
            {"store-gaf", required_argument, 0, 'F'},
            {"gbwt-name", required_argument, 0, 'G'},
            {"actual-phasing", no_argument, 0, 'z'},
            {"discard-overlaps", no_argument, 0, 'o'},
            {"batch-size", required_argument, 0, 'B'},
            {"buffer-size", required_argument, 0, 'u'},
            {"id-interval", required_argument, 0, 'n'},
            {"range", required_argument, 0, 'R'},
            {"rename", required_argument, 0, 'r'},
            {"rename-variants", no_argument, 0, OPT_RENAME_VARIANTS},
            {"region", required_argument, 0, 'I'},
            {"exclude", required_argument, 0, 'E'},

            // GCSA
            {"gcsa-out", required_argument, 0, 'g'},
            {"dbg-in", required_argument, 0, 'i'},
            {"mapping", required_argument, 0, 'f'},
            {"kmer-size", required_argument, 0, 'k'},
            {"doubling-steps", required_argument, 0, 'X'},
            {"size-limit", required_argument, 0, 'Z'},
            {"verify-index", no_argument, 0, 'V'},
            {"gcsa-work-dir", required_argument, 0, OPT_GCSA_WORK_DIR},
            {"gcsa-resume", no_argument, 0, OPT_GCSA_RESUME},
            {"gcsa-memory-limit", required_argument, 0, OPT_GCSA_MEMORY_LIMIT},
            {"gcsa-disk-limit", required_argument, 0, OPT_GCSA_DISK_LIMIT},
            {"gcsa-sort-run-size", required_argument, 0, OPT_GCSA_SORT_RUN_SIZE},
            {"gcsa-join-partition-size", required_argument, 0, OPT_GCSA_JOIN_PARTITION_SIZE},
            {"gcsa-process-workers", required_argument, 0, OPT_GCSA_PROCESS_WORKERS},
            
            // GAM index (GAI)
            {"index-sorted-gam", no_argument, 0, 'l'},
            
            // VG in-place index (VGI)
            {"index-sorted-vg", no_argument, 0, OPT_BUILD_VGI_INDEX},

            //Snarl distance index
            {"snarl-limit", required_argument, 0, OPT_DISTANCE_SNARL_LIMIT},
            {"dist-name", required_argument, 0, 'j'},
            {"no-nested-distance", no_argument, 0, OPT_DISTANCE_NESTING},
            {"upweight-node", required_argument, 0, 'w'},
            {"path-prefix", required_argument, 0, 'P'},
            {0, 0, 0, 0}
        };

        int option_index = 0;
        c = getopt_long (argc, argv, "b:t:px:Lv:WTM:F:G:zoB:u:n:R:r:I:E:g:i:f:k:X:Z:Vlj:w:P:h?",
                         long_options, &option_index);

        // Detect the end of the options.
        if (c == -1)
            break;

        switch (c)
        {
        // General
        case 'b':
            temp_file::set_dir(optarg);
            break;
        case 't':
            set_thread_count(logger, optarg);
            break;
        case 'p':
            show_progress = true;
            break;

        // XG
        case 'x':
            build_xg = true;
            // This may be an input *or* output
            xg_name = optarg;
            break;
        case 'L':
            xg_alts = true;
            break;

        // GBWT. The options remain, but they are no longer supported.
        case 'v': // Fall through
        case 'W': // Fall through
        case 'T': // Fall through
        case 'M': // Fall through
        case 'F': // Fall through
        case 'G': // Fall through
        case 'z': // Fall through
        case 'o': // Fall through
        case 'B': // Fall through
        case 'u': // Fall through
        case 'n': // Fall through
        case 'R': // Fall through
        case 'r': // Fall through
        case OPT_RENAME_VARIANTS: // Fall through
        case 'I': // Fall through
        case 'E':
            logger.error() << "GBWT construction options have been removed; use vg gbwt instead" << endl;
            break;

        // GCSA
        case 'g':
            build_gcsa = true;
            gcsa_name = ensure_writable(logger, optarg);
            // We also write to gcsa_name + ".lcp"
            ensure_writable(logger, gcsa_name + ".lcp");
            break;
        case 'i':
            logger.warn() << "-i option is deprecated" << endl;
            dbg_names.push_back(optarg);
            break;
        case 'f':
            mapping_name = require_exists(logger, optarg);
            break;
        case 'k':
            kmer_size = std::max(parse<size_t>(optarg), 1ul);
            break;
        case 'X':
            params.setSteps(parse<size_t>(optarg));
            break;
        case 'Z':
            params.setLimit(parse<size_t>(optarg));
            break;
        case 'V':
            verify_gcsa = true;
            break;
        case OPT_GCSA_WORK_DIR:
            params.setWorkDirectory(optarg);
            break;
        case OPT_GCSA_RESUME:
            gcsa_resume_requested = true;
            params.setResume();
            break;
        case OPT_GCSA_MEMORY_LIMIT:
            params.setMemoryLimitBytes(gcsa::parseBytes(optarg));
            break;
        case OPT_GCSA_DISK_LIMIT:
            params.setLimitBytes(gcsa::parseBytes(optarg));
            break;
        case OPT_GCSA_SORT_RUN_SIZE:
            params.setSortRunSize(gcsa::parseBytes(optarg));
            break;
        case OPT_GCSA_JOIN_PARTITION_SIZE:
            params.setJoinPartitionSize(gcsa::parseBytes(optarg));
            break;
        case OPT_GCSA_PROCESS_WORKERS:
            params.setProcessWorkers(parse<size_t>(optarg));
            break;
            
        // Gam index (GAI)
        case 'l':
            build_gai_index = true;
            break;
            
        // VGI index
        case OPT_BUILD_VGI_INDEX:
            build_vgi_index = true;
            break;

        //Snarl distance index
        case 'j':
            build_dist = true;
            dist_name = ensure_writable(logger, optarg);
            break;
        case OPT_DISTANCE_SNARL_LIMIT:
            snarl_limit = parse<int>(optarg);
            break;
        case OPT_DISTANCE_NESTING:
            only_top_level_chain_distances = true;
            break;
        case 'w':
            // We use += so you can repeat a node and make it even more
            // heavier.
            extra_node_weight[parse<nid_t>(optarg)] += EXTRA_WEIGHT;
            break;
        case 'P':
            ref_prefix = optarg;
            break;

        case 'h':
        case '?':
            help_index(argv);
            exit(1);
            break;
        default:
            abort ();
        }
    }

    vector<string> file_names;
    while (optind < argc) {
        string file_name = get_input_file_name(optind, argc, argv);
        file_names.push_back(file_name);
    }


    if (xg_name.empty() && gcsa_name.empty() && !build_gai_index && !build_vgi_index && dist_name.empty()) {
        logger.error() << "index type not specified" << endl;
    }

    if (file_names.size() <= 0 && dbg_names.empty()){
        //logger.error() << "No graph provided for indexing. "
        //               << "Please provide a .vg file or GCSA2-format deBruijn graph to index." << endl;
    }
    
    if (file_names.size() != 1 && build_gai_index) {
        logger.error() << "can only index exactly one sorted GAM file at a time" << endl;
    }
    
    if (file_names.size() != 1 && build_vgi_index) {
        logger.error() << "can only index exactly one sorted VG file at a time" << endl;
    }
    
    if (file_names.size() > 1 && build_dist) {
        // Allow zero filenames for the index-from-xg mode
        logger.error() << "can only create one distance index at a time" << endl;
    }
    
    if (build_gcsa && kmer_size > gcsa::Key::MAX_LENGTH) {
        logger.error() << "GCSA2 cannot index with kmer size greater than "
                       << gcsa::Key::MAX_LENGTH << endl;
    }
    if (params.getResume() && params.getWorkDirectory().empty()) {
        logger.error() << "--gcsa-resume requires --gcsa-work-dir" << endl;
    }
    if (params.externalMemory()) {
        std::error_code error;
        std::filesystem::create_directories(params.getWorkDirectory(), error);
        if (error) {
            logger.error() << "cannot create GCSA2 workspace " << params.getWorkDirectory()
                           << ": " << error.message() << endl;
        }
        params.setWorkDirectory(std::filesystem::absolute(params.getWorkDirectory())
                                    .lexically_normal().string());
        // A workspace explicitly selects disk-first construction. Path growth
        // is limited by the disk budget, not by the RAM ceiling.
        params.setAllowPathExplosion();
        bool has_construction_manifest = std::filesystem::is_regular_file(
            std::filesystem::path(params.getWorkDirectory()) / "build.json");
        if (!gcsa_resume_requested && has_construction_manifest) {
            logger.error() << "GCSA2 workspace already contains a build; use --gcsa-resume: "
                           << params.getWorkDirectory() << endl;
        }
        if (gcsa_resume_requested && !has_construction_manifest) {
            // A crash may occur after vg commits durable k-mers but before
            // GCSA2 creates build.json. Continue as a new GCSA2 frontier while
            // still reusing a compatible vg input manifest below.
            params.setResume(false);
            if (show_progress) {
                logger.info() << "No GCSA2 build manifest exists; resuming from durable input generation" << endl;
            }
        }
        gcsa::TempFile::setDirectory(params.getWorkDirectory());
    }

    if (!build_dist && !extra_node_weight.empty()) {
        logger.error() << "cannot up-weight nodes for snarl finding if not building distance index" << endl;
    }

    if (!build_dist && !ref_prefix.empty()) {
        logger.error() << "cannot set reference prefix for snarl finding if not building distance index" << endl;
    }
    
    if (build_xg && build_gcsa && file_names.empty()) {
        // Really we want to build a GCSA by *reading* an XG
        build_xg = false;
        // We'll continue in the build_gcsa section
        logger.warn() << "providing input XG with option -x is deprecated" << endl;
    }
    if (build_dist && file_names.empty()) {
        //If we want to build the distance index from the xg
        build_xg = false;
        logger.warn() << "providing input XG with option -x is deprecated" << endl;
    }


    // Build XG. Include alt paths in the XG if requested with -L.
    if (build_xg) {
        ensure_writable(logger, xg_name);
        if (file_names.empty()) {
            // VGset or something segfaults when we feed it no graphs.
            logger.error() << "at least one graph is required to build an XG index" << endl;
        }
        if (show_progress) {
            logger.info() << "Building XG index" << endl;
        }
        xg::XG xg_index;
        VGset graphs(file_names);
        graphs.to_xg(xg_index, (xg_alts ? [](const string&) {return false;} : Paths::is_alt), nullptr);
        if (show_progress) {
            logger.info() << "Saving XG index to " << xg_name << endl;
        }
        // Save the XG.
        vg::io::save_handle_graph(&xg_index, xg_name);
    }

    // Build GCSA
    if (build_gcsa) {

        // Configure GCSA2 verbosity so it doesn't spit out loads of extra info
        gcsa::Verbosity::set(show_progress
            ? gcsa::Verbosity::EXTENDED
            : gcsa::Verbosity::SILENT);

        double start = gcsa::readTimer();

        // Generate temporary kmer files
        bool delete_kmer_files = false;
        bool generated_kmer_inputs = dbg_names.empty();
        vector<string> semantic_sources;
        if (!file_names.empty()) {
            semantic_sources = file_names;
        } else if (!xg_name.empty()) {
            semantic_sources.push_back(xg_name);
        }

        if (generated_kmer_inputs && params.externalMemory() &&
            persistent_gcsa_kmers_exist(params.getWorkDirectory())) {
            if (!gcsa_resume_requested) {
                logger.error() << "GCSA2 workspace already contains durable k-mer inputs; "
                               << "use --gcsa-resume: " << params.getWorkDirectory() << endl;
            }
            if (show_progress) {
                logger.info() << "Validating and restoring durable kmer files..." << endl;
            }
            PersistentGcsaKmers persistent = restore_gcsa_kmers(
                params.getWorkDirectory(), semantic_sources, kmer_size);
            dbg_names = std::move(persistent.filenames);
            params.reduceLimit(persistent.bytes);
        }
        if (generated_kmer_inputs && params.getResume() && dbg_names.empty()) {
            logger.error() << "GCSA2 build manifest exists but its durable k-mer input manifest is missing: "
                           << params.getWorkDirectory() << endl;
        }

        if (dbg_names.empty()) {
            if (show_progress) {
                logger.info() << "Generating kmer files..." << endl;
            }
            
            if (!file_names.empty()) {
                // Get the kmers from a VGset.
                VGset graphs(file_names);
                size_t kmer_bytes = params.getLimitBytes();
                dbg_names = graphs.write_gcsa_kmers_binary(kmer_size, kmer_bytes);
                params.reduceLimit(kmer_bytes);
                delete_kmer_files = true;
            } else if (!xg_name.empty()) {
                // Get the kmers from an XG or other single graph
                
                // Load the graph
                require_exists(logger, xg_name);
                auto single_graph = vg::io::VPKG::load_one<HandleGraph>(xg_name);
                
                auto make_kmers_for_component = [&](const HandleGraph* g) {
                    // Make an overlay on it to add source and sink nodes
                    // TODO: Don't use this directly; unify this code with VGset's code.
                    SourceSinkOverlay overlay(g, kmer_size);
                    
                    // Get the size limit
                    size_t kmer_bytes = params.getLimitBytes();
                    
                    // Write the kmer temp file
                    dbg_names.push_back(write_gcsa_kmers_to_tmpfile(overlay, kmer_size, kmer_bytes,
                        overlay.get_id(overlay.get_source_handle()),
                        overlay.get_id(overlay.get_sink_handle())));
                        
                    // Feed back into the size limit
                    params.reduceLimit(kmer_bytes);
                    delete_kmer_files = true;
                };
                
                if (show_progress) {
                    logger.info() << "Finding connected components..." << endl;
                }
                
                // Get all the components in the graph, which we can process separately to save memory.
                std::vector<std::unordered_set<nid_t>> components = \
                    handlealgs::weakly_connected_components(single_graph.get());
                
                if (components.size() == 1) {
                    // Only one component
                    if (show_progress) {
                        logger.info() << "Processing single component graph..." << endl;
                    }
                    make_kmers_for_component(single_graph.get());
                } else {
                    for (size_t i = 0; i < components.size(); i++) {
                        // Run separately on each component.
                        // Don't run in parallel or size limit tracking won't work.
                
                        if (show_progress) {
                            logger.info() << "Selecting component "
                                          << i << "/" << components.size() << "..." << endl;
                        }
                        
                        bdsg::PackedSubgraphOverlay component_graph(single_graph.get());
                        for (auto& id : components[i]) {
                            // Add each node to the subgraph.
                            // TODO: use a handle-returning component
                            // finder so we don't need to get_handle here.
                            component_graph.add_node(single_graph->get_handle(id, false));
                        }
                        
                        if (show_progress) {
                            logger.info() << "Processing component " << i << "/" << components.size() << "..." << endl;
                        }
                        
                        make_kmers_for_component(&component_graph);
                    }
                }
            } else {
                logger.error() << "cannot generate GCSA index without either a VG or an XG" << endl;
            }
        }

        if (generated_kmer_inputs && params.externalMemory() && delete_kmer_files) {
            if (show_progress) {
                logger.info() << "Committing durable kmer inputs..." << endl;
            }
            PersistentGcsaKmers persistent = persist_gcsa_kmers(
                params.getWorkDirectory(), semantic_sources, kmer_size, dbg_names);
            dbg_names = std::move(persistent.filenames);
            // persist_gcsa_kmers() unregisters/removes the anonymous temporary
            // names. The committed workspace inputs must survive this process.
            delete_kmer_files = false;
        }

        // Build the index
        if (show_progress) {
            logger.info() << "Building the GCSA2 index..." << endl;
        }
        gcsa::InputGraph input_graph(dbg_names, true, params, gcsa::Alphabet(), mapping_name);
        gcsa::GCSA gcsa_index;
        bool gcsa_stored_directly = params.externalMemory();
        if (gcsa_stored_directly) {
            gcsa::GCSA::buildAndStore(input_graph, params, gcsa_name);
        } else {
            gcsa_index = gcsa::GCSA(input_graph, params);
        }
        gcsa::LCPArray lcp_array;
        if (params.externalMemory()) {
            // Stream the LCP hierarchy directly to its final representation so
            // external construction does not retain it in memory.
            gcsa::LCPArray::buildAndStore(input_graph, params, gcsa_name + ".lcp");
        } else {
            lcp_array = gcsa::LCPArray(input_graph, params);
        }
        if (show_progress) {
            double seconds = gcsa::readTimer() - start;
            logger.info() << "GCSA2 index built in " << seconds << " seconds, "
                          << gcsa::inGigabytes(gcsa::memoryUsage()) << " GB" << endl;
            logger.info() << "I/O volume:" << gcsa::inGigabytes(gcsa::readVolume()) << " GB read, "
                          << gcsa::inGigabytes(gcsa::writeVolume()) << " GB write" << endl;
        }

        // Save the indexes
        if (!gcsa_stored_directly) {
            save_gcsa(gcsa_index, gcsa_name, show_progress);
        } else if (show_progress) {
            logger.info() << "GCSA2 components were published directly to "
                          << gcsa_name << endl;
        }
        if (!params.externalMemory()) {
            save_lcp(lcp_array, gcsa_name + ".lcp", show_progress);
        } else if (show_progress) {
            logger.info() << "LCP components were published directly to "
                          << gcsa_name + ".lcp" << endl;
        }

        // Verify the index
        if (verify_gcsa) {
            if (show_progress) {
                logger.info() << "Verifying the index..." << endl;
            }
            bool verified = false;
            if (params.externalMemory()) {
                // External verification bounds its occurrence streams, but
                // querying still requires the final succinct index itself.
                // Reload only when the user explicitly requested -V.
                if (!sdsl::load_from_file(gcsa_index, gcsa_name)) {
                    throw runtime_error("cannot reload the staged GCSA2 index for verification: " +
                                        gcsa_name);
                }
                if (!sdsl::load_from_file(lcp_array, gcsa_name + ".lcp")) {
                    throw runtime_error("cannot reload the staged LCP array for verification: " +
                                        gcsa_name + ".lcp");
                }
                const gcsa::size_type verification_budget = std::max(
                    gcsa::verifyIndexMinimumBudget(),
                    std::min(params.getMemoryLimitBytes(), static_cast<gcsa::size_type>(64 * gcsa::MEGABYTE)));
                verified = gcsa::verifyIndex(gcsa_index, &lcp_array, input_graph,
                    verification_budget, params.getMergeFanIn());
            } else {
                verified = gcsa::verifyIndex(gcsa_index, &lcp_array, input_graph);
            }
            if (!verified) {
                logger.warn() << "GCSA2 index verification failed" << endl;
            }
        }

        // Delete the temporary kmer files
        if (delete_kmer_files) {
            for (auto& filename : dbg_names) {
                temp_file::remove(filename);
            }
        }
    }
    
    if (build_gai_index) {
        // Index a sorted GAM file.
        
        get_input_file(file_names.at(0), [&](istream& in) {
            // Grab the input GAM stream and wrap it in a cursor
            vg::io::ProtobufIterator<Alignment> cursor(in);
            
            // Index the file
            StreamIndex<Alignment> index;
            index.index(cursor);
 
            // Save the GAM index in the appropriate place.
            // TODO: Do we really like this enforced naming convention just beacuse samtools does it?
            ofstream index_out(file_names.at(0) + ".gai");
            if (!index_out.good()) {
                logger.error() << "could not open " << file_names.at(0) << ".gai for writing" << endl;
            }
            index.save(index_out);
        });
    }
    
    if (build_vgi_index) {
        // Index an ID-sorted VG file.
        get_input_file(file_names.at(0), [&](istream& in) {
            // Grab the input VG stream and wrap it in a cursor
            vg::io::ProtobufIterator<Graph> cursor(in);
            
            // Index the file
            StreamIndex<Graph> index;
            index.index(cursor);
 
            // Save the index in the appropriate place.
            // TODO: Do we really like this enforced naming convention just beacuse samtools does it?
            string index_name = file_names.at(0) + ".vgi";
            ensure_writable(logger, index_name);
            ofstream index_out(index_name);
            index.save(index_out);
        });
        
    }

    //Build a snarl-based minimum distance index
    if (build_dist) {

        // upweight the tips of reference paths (important for vg haplotypes)
        function<void(const HandleGraph&)> add_ref_weights =
            [&](const HandleGraph& hgraph) {
                const PathHandleGraph* graph = dynamic_cast<const PathHandleGraph*>(&hgraph);
                if (!ref_prefix.empty()) {
                    if (graph == nullptr) {
                        logger.error() << "-P cannot be used because graph format does not support paths" << endl;
                    }
                    graph->for_each_path_of_sense({PathSense::REFERENCE, PathSense::GENERIC}, [&](const path_handle_t& path_handle) {
                        string path_name = graph->get_path_name(path_handle);
                        if (path_name.compare(0, ref_prefix.size(), ref_prefix) == 0 && !graph->is_empty(path_handle)) {
                            extra_node_weight[graph->get_id(graph->get_handle_of_step(graph->path_begin(path_handle)))] += EXTRA_WEIGHT;
                            extra_node_weight[graph->get_id(graph->get_handle_of_step(graph->path_back(path_handle)))] += EXTRA_WEIGHT;
                        }
                    });
                }
            };

        if (file_names.empty() && xg_name.empty()) {
            logger.error() << "one graph is required to build a distance index" << endl;
        } else if (file_names.size() > 1 || (file_names.size() == 1 && !xg_name.empty())) {
            logger.error() << "only one graph at a time can be used to build a distance index" << endl;
        } else if (dist_name.empty()) {
            logger.error() << "distance index requires an output file" << endl;
            
        } else  {
            //Get graph and build dist index

            if (file_names.empty() && !xg_name.empty()) {
                // We were given a -x specifically to read as XG
                
                auto xg = vg::io::VPKG::load_one<xg::XG>(xg_name);

                // Create the SnarlDistanceIndex
                add_ref_weights(*xg.get());
                IntegratedSnarlFinder snarl_finder(*xg.get(), extra_node_weight);
                SnarlDistanceIndex distance_index;

                //Fill it in
                fill_in_distance_index(&distance_index, xg.get(), &snarl_finder, snarl_limit, only_top_level_chain_distances, false);
                // Save it
                distance_index.serialize(dist_name);
            } else {
                // May be GBZ or a HandleGraph.
                auto options = vg::io::VPKG::try_load_first<gbwtgraph::GBZ, handlegraph::HandleGraph>(file_names.at(0));
                
                if (get<0>(options)) {
                    // We have a GBZ graph
                    auto& gbz = get<0>(options);
                    
                    // Create the SnarlDistanceIndex
                    add_ref_weights(gbz->graph);
                    IntegratedSnarlFinder snarl_finder(gbz->graph, extra_node_weight);

                    //Make a distance index and fill it in
                    SnarlDistanceIndex distance_index;
                    fill_in_distance_index(&distance_index, &(gbz->graph), &snarl_finder, snarl_limit, only_top_level_chain_distances, false);
                    // Save it
                    distance_index.serialize(dist_name);
                } else if (get<1>(options)) {
                    // We were given a graph generically
                    auto& graph = get<1>(options);
                    
                    // Create the SnarlDistanceIndex
                    add_ref_weights(*graph.get());
                    IntegratedSnarlFinder snarl_finder(*graph.get(), extra_node_weight);

                    //Make a distance index and fill it in
                    SnarlDistanceIndex distance_index;
                    fill_in_distance_index(&distance_index, graph.get(), &snarl_finder, snarl_limit, only_top_level_chain_distances, false);
                    // Save it
                    distance_index.serialize(dist_name);
                } else {
                    logger.error() << "input is not a graph or GBZ" << endl;
                }
            }
        }

    }
    if (show_progress) {
        logger.info() << "Memory usage: " << gcsa::inGigabytes(gcsa::memoryUsage()) << " GB" << endl;
    }
    return 0;
}

// Register subcommand
static Subcommand vg_construct("index", "index graphs or alignments for random access or mapping", PIPELINE, 4, main_index);
