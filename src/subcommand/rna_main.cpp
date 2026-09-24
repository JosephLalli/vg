/** \file rna_main.cpp
 *
 * Defines the "vg rna" subcommand.
 */

#include <unistd.h>
#include <getopt.h>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>

#include "subcommand.hpp"

#include "../transcriptome.hpp"
#include <vg/io/vpkg.hpp>
#include <vg/io/stream.hpp>
#include "../gbwt_helper.hpp"
#include "bdsg/packed_graph.hpp"
#include <gbwtgraph/gbz.h>
#include <gbwtgraph/utils.h>

using namespace std;
using namespace vg;
using namespace vg::subcommand;

namespace {

constexpr int PATH_WORKSPACE_OPT = 1000;
constexpr int PATH_WORKSPACE_RESERVE_OPT = 1001;
constexpr uint64_t MAPPED_COPY_CHECKPOINT_BYTES = 256ull * 1024 * 1024;

string path_workspace_file(const string & workspace, const string & filename) {
    return workspace + (workspace.back() == '/' ? "" : "/") + filename;
}

}

void help_rna(char** argv) {
    cerr << "usage: " << argv[0] << " rna [options] graph.[vg|pg|hg|gbz] > splicing_graph.[vg|pg|hg]" << endl

         << endl 
         << "General options:" << endl

         << "  -t, --threads INT               number of compute threads to use [1]" << endl
         << "  -p, --progress                  show progress" << endl
         << "      --path-workspace DIR        use new disk-backed graph/path workspace" << endl
         << "      --path-workspace-reserve N  reserve N GiB in one initial sparse mapping" << endl
         << "  -h, --help                      print this help message to stderr and exit" << endl

         << endl
         << "Input options:" << endl

         << "  -n, --transcripts FILE          transcript file(s) in gtf/gff format (may" << endl
         << "                                   repeat)" << endl
         << "  -m, --introns FILE              intron file(s) in bed format (may repeat)" << endl
         << "  -y, --feature-type NAME         parse only this feature type in the GTF/GFF" << endl
         << "                                   (parses all if empty) [exon]" << endl
         << "  -s, --transcript-tag NAME       use this attribute tag in the GTF/GFf file(s)" << endl
         << "                                   as ID" << endl
         << "                                   to group exons and name paths [transcript_id]" << endl
         << "  -l, --haplotypes FILE           project transcripts onto haplotypes in GBWT" << endl
         << "                                   index" << endl
         << "  -z, --gbz-format                input graph is GBZ format (has graph & GBWT" << endl
         << "                                   index)" << endl

         << endl
         << "Construction options:" << endl

         << "  -j, --use-hap-ref               use haplotype paths in GBWT index as" << endl
         << "                                   references" << endl
         << "                                   (disables projection)" << endl
         << "  -e, --proj-embed-paths          project transcripts onto embedded haplotype" << endl
         << "                                   paths" << endl
         << "  -c, --path-collapse TYPE        collapse identical transcript paths across" << endl
         << "                                   no|haplotype|all paths [haplotype]" << endl
         << "  -k, --max-node-length INT       chop nodes longer than INT (disable with 0)" << endl
         << "                                   [0]" << endl
         << "  -d, --remove-non-gene           remove intergenic and intronic regions" << endl
         << "                                   (deletes all paths in the graph)" << endl
         << "  -o, --do-not-sort               do not topological sort and compact the graph" << endl
         << "DON'T FORGET TO EMBED PATHS:" << endl
         << "  -r, --add-ref-paths             add reference transcripts as embedded paths" << endl
         << "  -a, --add-hap-paths             add projected transcripts as embedded paths" << endl
         << "  -B, --add-tx-bodies             also embed per-transcript unspliced BODY" << endl
         << "                                   paths" << endl
         << "                                   (exons+introns) for intron/intergenic read" << endl
         << "                                   assignment" << endl

         << endl
         << "Output options:" << endl

         << "  -b, --write-gbwt FILE           write pantranscriptome transcript paths as" << endl
         << "                                   GBWT" << endl
         << "  -v, --write-hap-gbwt FILE       write input haplotypes as a GBWT" << endl
         << "                                   with node IDs matching the output graph" << endl
         << "  -f, --write-fasta FILE          write pantranscriptome transcript sequences" << endl
         << "                                   to here" << endl
         << "  -i, --write-info FILE           write pantranscriptome transcript info table" << endl
         << "                                   as TSV" << endl
         << "  -q, --out-exclude-ref           exclude reference transcripts from" << endl
         << "                                   pantranscriptome" << endl
         << "  -g, --gbwt-bidirectional        use bidirectional paths in GBWT index" << endl
         << "                                   construction" << endl

         << endl;
}

int32_t main_rna(int32_t argc, char** argv) {
    Logger logger("vg rna");

    if (argc == 2) {
        help_rna(argv);
        return 1;
    }
    
    vector<string> transcript_filenames;
    vector<string> intron_filenames;
    string feature_type = "exon";
    string transcript_tag = "transcript_id";
    string haplotypes_filename;
    bool gbz_format = false;
    bool use_hap_ref = false;
    bool proj_emded_paths = false;
    string path_collapse_type = "haplotype";
    uint32_t max_node_length = 0;
    bool remove_non_transcribed_nodes = false;
    bool sort_collapse_graph = true;
    bool add_reference_transcript_paths = false;
    bool add_projected_transcript_paths = false;
    bool add_transcript_body_paths = false;
    bool exclude_reference_transcripts = false;
    string gbwt_out_filename = "";
    bool gbwt_add_bidirectional = false;
    string fasta_out_filename = "";
    string info_out_filename = "";
    string hap_gbwt_out_filename = "";
    int32_t num_threads = 1;
    bool show_progress = false;
    string path_workspace;
    bool path_workspace_requested = false;
    bool path_workspace_reserve_requested = false;
    size_t path_workspace_initial_bytes = 0;
    bool output_filter_option_seen = false;

    int32_t c;
    optind = 2;

    while (true) {
        static struct option long_options[] =
            {
                {"transcripts",  required_argument, 0, 'n'},
                {"introns",  required_argument, 0, 'm'},
                {"feature-type",  required_argument, 0, 'y'},
                {"transcript-tag",  required_argument, 0, 's'},
                {"haplotypes",  required_argument, 0, 'l'},
                {"gbz-format",  no_argument, 0, 'z'},
                {"use-hap-ref",  no_argument, 0, 'j'},
                {"proj-embed-paths",  no_argument, 0, 'e'},
                {"path-collapse",  required_argument, 0, 'c'},
                {"max-node-length",  required_argument, 0, 'k'},
                {"remove-non-gene",  no_argument, 0, 'd'},
                {"do-not-sort",  no_argument, 0, 'o'},
                {"add-ref-paths",  no_argument, 0, 'r'},
                {"add-hap-paths",  no_argument, 0, 'a'},
                {"add-tx-bodies",  no_argument, 0, 'B'},
                {"write-gbwt",  required_argument, 0, 'b'},
                {"write-hap-gbwt",  required_argument, 0, 'v'},
                {"write-fasta",  required_argument, 0, 'f'},
                {"write-info",  required_argument, 0, 'i'},
                {"out-ref-paths",  no_argument, 0, 'u'},
                {"out-exclude-ref",  no_argument, 0, 'q'},
                {"gbwt-bidirectional",  no_argument, 0, 'g'},   
                {"threads",  required_argument, 0, 't'},
                {"progress",  no_argument, 0, 'p'},
                {"path-workspace", required_argument, 0, PATH_WORKSPACE_OPT},
                {"path-workspace-reserve", required_argument, 0, PATH_WORKSPACE_RESERVE_OPT},
                {"help", no_argument, 0, 'h'},
                {0, 0, 0, 0}
            };

        int32_t option_index = 0;
        c = getopt_long(argc, argv, "n:m:y:s:l:zjec:k:doraBb:v:f:i:uqgt:ph?", long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c)
        {

        case 'n':
            transcript_filenames.push_back(require_exists(logger, optarg));
            require_non_gzipped(logger, transcript_filenames.back());
            break;

        case 'm':
            intron_filenames.push_back(require_exists(logger, optarg));
            require_non_gzipped(logger, intron_filenames.back());
            break;

        case 'y':
            feature_type = optarg;
            break;

        case 's':
            transcript_tag = optarg;
            break;

        case 'l':
            haplotypes_filename = require_exists(logger, optarg);
            break;

        case 'z':
            gbz_format = true;
            break;

        case 'j':
            use_hap_ref = true;
            break;

        case 'e':
            proj_emded_paths = true;
            break;

        case 'c':
            path_collapse_type = optarg;
            break;

        case 'k':
            max_node_length = stoi(optarg);
            break;

        case 'd':
            remove_non_transcribed_nodes = true;
            break;

        case 'o':
            sort_collapse_graph = false;
            break;

        case 'r':
            add_reference_transcript_paths = true;
            break;

        case 'a':
            add_projected_transcript_paths = true;
            break;

        case 'B':
            add_transcript_body_paths = true;
            break;

        case 'b':
            gbwt_out_filename = optarg;
            break;
            
        case 'v':
            hap_gbwt_out_filename = optarg;
            break;

        case 'f':
            fasta_out_filename = optarg;
            break;

        case 'i':
            info_out_filename = optarg;
            break;

        case 'u':
            exclude_reference_transcripts = false;
            output_filter_option_seen = true;
            break;

        case 'q':
            exclude_reference_transcripts = true;
            output_filter_option_seen = true;
            break;

        case 'g':
            gbwt_add_bidirectional = true;
            break;

        case 't':
            num_threads = set_thread_count(logger, optarg);
            break;

        case 'p':
            show_progress = true;
            break;

        case PATH_WORKSPACE_OPT:
            path_workspace = optarg;
            path_workspace_requested = true;
            break;

        case PATH_WORKSPACE_RESERVE_OPT: {
            const uint64_t gib = parse<uint64_t>(optarg);
            const uint64_t maximum = std::min<uint64_t>(
                std::numeric_limits<size_t>::max(),
                std::numeric_limits<::off_t>::max());
            if (gib == 0 || gib > (maximum >> 30)) {
                logger.error() << "--path-workspace-reserve requires a positive GiB size "
                               << "representable by size_t and off_t" << endl;
            }
            path_workspace_initial_bytes = static_cast<size_t>(gib << 30);
            path_workspace_reserve_requested = true;
            break;
        }

        case 'h':
        case '?':
            help_rna(argv);
            exit(1);
            break;

        default:
            abort();
        }
    }

    if (argc < optind + 1) {
        help_rna(argv);
        return 1;
    }

    if (transcript_filenames.empty() && intron_filenames.empty()) {
        logger.error() << "No transcripts or introns were given. "
                       << "Use --transcripts FILE and/or --introns FILE." << endl;
    }

    if (!haplotypes_filename.empty() && gbz_format) {
        logger.error() << "Only one set of haplotypes can be provided "
                       << "(GBZ file contains both a graph and haplotypes). "
                       << "Use either --haplotypes or --gbz-format." << endl;
    }

    if (remove_non_transcribed_nodes && !add_reference_transcript_paths && !add_projected_transcript_paths) {
        logger.warn() << "Reference paths are deleted when removing intergenic and intronic regions. "
                      << "Consider adding transcripts as embedded paths "
                      << "using --add-ref-paths and/or --add-hap-paths." << endl;
    }

    if (path_collapse_type != "no" && path_collapse_type != "haplotype" && path_collapse_type != "all") {
        logger.error() << "Path collapse type (--path-collapse) provided not supported. "
                       << "Options: no, haplotype or all." << endl;
    }

    if (path_workspace_reserve_requested && !path_workspace_requested) {
        logger.error() << "--path-workspace-reserve requires --path-workspace" << endl;
    }

    if (path_workspace_requested) {
        if (path_workspace.empty()) {
            logger.error() << "--path-workspace requires a nonempty directory name" << endl;
        }
        const bool supported_workspace_recipe =
            gbz_format && use_hap_ref && path_collapse_type == "no" &&
            remove_non_transcribed_nodes && sort_collapse_graph &&
            add_reference_transcript_paths && !transcript_filenames.empty() &&
            intron_filenames.empty() && haplotypes_filename.empty() &&
            !proj_emded_paths && max_node_length == 0 &&
            !add_projected_transcript_paths && !add_transcript_body_paths &&
            gbwt_out_filename.empty() && hap_gbwt_out_filename.empty() &&
            fasta_out_filename.empty() && !info_out_filename.empty() &&
            !exclude_reference_transcripts && !output_filter_option_seen &&
            !gbwt_add_bidirectional;
        if (!supported_workspace_recipe) {
            logger.error() << "--path-workspace currently requires the exact bounded route: "
                           << "--gbz-format --use-hap-ref --path-collapse no "
                           << "--remove-non-gene --add-ref-paths --write-info, transcript input, "
                           << "sorting enabled, and no other construction or optional output flags." << endl;
        }
        if (mkdir(path_workspace.c_str(), 0700) != 0) {
            logger.error() << "Cannot create new path workspace \"" << path_workspace
                           << "\": " << strerror(errno) << endl;
        }
    }

    if (!gbwt_out_filename.empty()) {
        gbwt_out_filename = ensure_writable(logger, gbwt_out_filename);
    }
    if (!hap_gbwt_out_filename.empty()) {
        hap_gbwt_out_filename = ensure_writable(logger, hap_gbwt_out_filename);
    }
    if (!fasta_out_filename.empty()) {
        fasta_out_filename = ensure_writable(logger, fasta_out_filename);
    }
    if (!info_out_filename.empty()) {
        info_out_filename = ensure_writable(logger, info_out_filename);
    }

    double time_parsing_start = gcsa::readTimer();
    if (show_progress) { logger.info() << "Parsing graph file ..." << endl; }

    string graph_filename = get_input_file_name(optind, argc, argv);

    unique_ptr<MutablePathDeletableHandleGraph> graph(nullptr);
    unique_ptr<gbwt::GBWT> haplotype_index;

    if (!gbz_format) {

        // Load pangenome graph.
        graph = std::move(vg::io::VPKG::load_one<MutablePathDeletableHandleGraph>(graph_filename));
    
        if (!haplotypes_filename.empty()) {

            // Load haplotype GBWT index.
            if (show_progress) { logger.info() << "Parsing haplotype GBWT index file ..." << endl; }
            haplotype_index = vg::io::VPKG::load_one<gbwt::GBWT>(haplotypes_filename);
            assert(haplotype_index->bidirectional());

        } else {

            // Construct empty GBWT index if non is given. 
            haplotype_index = unique_ptr<gbwt::GBWT>(new gbwt::GBWT());
        }

    } else {

        bdsg::MappedPackedGraph * mapped_graph = nullptr;
        if (path_workspace.empty()) {
            graph = unique_ptr<MutablePathDeletableHandleGraph>(new bdsg::PackedGraph());
        } else {
            const string graph_arena = path_workspace_file(path_workspace, "graph.arena");
            const int graph_fd = open(graph_arena.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
            if (graph_fd < 0) {
                logger.error() << "Cannot create mapped graph arena \"" << graph_arena
                               << "\": " << strerror(errno) << endl;
            }
            unique_ptr<bdsg::MappedPackedGraph> mapped_owner;
            try {
                mapped_owner = make_unique<bdsg::MappedPackedGraph>(
                    graph_fd, path_workspace_initial_bytes);
            } catch (...) {
                (void) close(graph_fd);
                throw;
            }
            if (close(graph_fd) != 0) {
                const int saved_errno = errno;
                mapped_owner.reset();
                logger.error() << "Cannot close mapped graph arena descriptor for \""
                               << graph_arena << "\": " << strerror(saved_errno) << endl;
            }
            mapped_graph = mapped_owner.get();
            graph = std::move(mapped_owner);
        }

        // Load GBZ file 
        unique_ptr<gbwtgraph::GBZ> gbz = vg::io::VPKG::load_one<gbwtgraph::GBZ>(graph_filename);
        
        if (show_progress) { logger.info() << "Converting graph format ..." << endl; }

        // Convert GBWTGraph to the selected mutable graph type.
        graph->set_id_increment(gbz->graph.min_node_id());
        if (mapped_graph == nullptr) {
            handlealgs::copy_handle_graph(&(gbz->graph), graph.get());

            // Copy reference and generic paths to new graph.
            gbz->graph.for_each_path_matching({PathSense::GENERIC, PathSense::REFERENCE}, {}, {},
                [&](const path_handle_t& path) {
                handlegraph::algorithms::copy_path(&(gbz->graph), path, graph.get());
            });
        } else {
            uint64_t work_since_checkpoint = 0;
            auto note_copy_work = [&](uint64_t bytes) {
                if (bytes >= MAPPED_COPY_CHECKPOINT_BYTES ||
                    work_since_checkpoint >= MAPPED_COPY_CHECKPOINT_BYTES - bytes) {
                    mapped_graph->checkpoint_and_evict();
                    work_since_checkpoint = 0;
                } else {
                    work_since_checkpoint += bytes;
                }
            };

            gbz->graph.for_each_handle([&](const handle_t & handle) {
                const string sequence = gbz->graph.get_sequence(handle);
                graph->create_handle(sequence, gbz->graph.get_id(handle));
                note_copy_work(sequence.size() + 64);
            });
            gbz->graph.for_each_edge([&](const edge_t & edge) {
                graph->create_edge(
                    graph->get_handle(gbz->graph.get_id(edge.first),
                                      gbz->graph.get_is_reverse(edge.first)),
                    graph->get_handle(gbz->graph.get_id(edge.second),
                                      gbz->graph.get_is_reverse(edge.second)));
                note_copy_work(64);
            });

            gbz->graph.for_each_path_matching({PathSense::GENERIC, PathSense::REFERENCE}, {}, {},
                [&](const path_handle_t & source_path) {
                    const path_handle_t destination_path = graph->create_path(
                        gbz->graph.get_sense(source_path),
                        gbz->graph.get_sample_name(source_path),
                        gbz->graph.get_locus_name(source_path),
                        gbz->graph.get_haplotype(source_path),
                        gbz->graph.get_phase_block(source_path),
                        gbz->graph.get_subrange(source_path),
                        gbz->graph.get_is_circular(source_path));
                    for (const handle_t & handle : gbz->graph.scan_path(source_path)) {
                        graph->append_step(destination_path,
                            graph->get_handle(gbz->graph.get_id(handle),
                                              gbz->graph.get_is_reverse(handle)));
                        note_copy_work(sizeof(handle_t));
                    }
                });
            mapped_graph->checkpoint_and_evict();
        }

        haplotype_index = make_unique<gbwt::GBWT>(std::move(gbz->index));
    }

    if (graph == nullptr) {
        logger.error() << "Could not load graph." << endl;
    }

    // Construct transcriptome and parse graph.
    Transcriptome transcriptome(std::move(graph), path_workspace);
    assert(graph == nullptr);

    transcriptome.show_progress = show_progress;
    transcriptome.num_threads = num_threads;
    transcriptome.feature_type = feature_type;
    transcriptome.transcript_tag = transcript_tag;
    transcriptome.path_collapse_type = path_collapse_type;
    // On this output-only route no later operation consumes the embedded paths.
    // Generate their ordinary PackedGraph records at serialization time.
    transcriptome.use_streaming_path_output =
        path_workspace.empty() && gbz_format && use_hap_ref &&
        path_collapse_type == "no" && remove_non_transcribed_nodes &&
        add_reference_transcript_paths && !add_projected_transcript_paths &&
        !add_transcript_body_paths && !proj_emded_paths;
    
    if (show_progress) {
        logger.info() << "Graph " << ((!haplotype_index->empty()) ? "and GBWT index " : "")
                      << "parsed in " << gcsa::readTimer() - time_parsing_start << " seconds, "
                      << gcsa::inGigabytes(gcsa::memoryUsage()) << " GB" << endl;
    };


    if (!intron_filenames.empty()) {

        double time_intron_start = gcsa::readTimer();
        if (show_progress) { logger.info() << "Adding intron splice-junctions to graph ..." << endl; }

        vector<istream *> intron_streams;
        intron_streams.reserve(intron_filenames.size());

        for (auto & filename: intron_filenames) {

            auto intron_stream = new ifstream(filename);
            intron_streams.emplace_back(intron_stream);
        }

        // Add introns as novel splice-junctions to graph.
        transcriptome.add_intron_splice_junctions(intron_streams, haplotype_index, true);

        for (auto & intron_stream: intron_streams) {

            delete intron_stream;
        }

        if (show_progress) {
            logger.info() << "Introns parsed and graph updated in "
                          << gcsa::readTimer() - time_intron_start << " seconds, "
                          << gcsa::inGigabytes(gcsa::memoryUsage()) << " GB" << endl;
        };
    }

    vector<istream *> transcript_streams;

    if (!transcript_filenames.empty()) {

        double time_transcript_start = gcsa::readTimer();
        if (show_progress) { 
            logger.info() << "Adding transcript splice-junctions and exon boundaries to graph ..." << endl;
        }

        transcript_streams.reserve(transcript_filenames.size());

        for (auto & filename: transcript_filenames) {

            auto transcript_stream = new ifstream(filename);
            transcript_streams.emplace_back(transcript_stream);
        }

        // Add transcripts as novel exon boundaries and splice-junctions to graph.
        transcriptome.add_reference_transcripts(transcript_streams, haplotype_index, use_hap_ref, !use_hap_ref);

        if (show_progress) {
            logger.info() << "Transcripts parsed and graph updated in "
                          << gcsa::readTimer() - time_transcript_start << " seconds, "
                          << gcsa::inGigabytes(gcsa::memoryUsage()) << " GB" << endl;
        };
    }

    if (!transcript_streams.empty() && (!haplotype_index->empty() || proj_emded_paths) && !use_hap_ref) {

        double time_project_start = gcsa::readTimer();
        if (show_progress) { logger.info() << "Projecting transcripts to haplotypes ..." << endl; }

        for (auto & transcript_stream: transcript_streams) {

            // Reset transcript file streams.
            transcript_stream->clear();
            transcript_stream->seekg(0);
        }

        // Add transcripts to transcriptome by projecting them onto embedded paths 
        // in a graph and/or haplotypes in a GBWT index.
        transcriptome.add_haplotype_transcripts(transcript_streams, *haplotype_index, proj_emded_paths);

        if (show_progress) {
            logger.info() << "Haplotype-specific transcripts constructed in "
                          << gcsa::readTimer() - time_project_start << " seconds, "
                          << gcsa::inGigabytes(gcsa::memoryUsage()) << " GB" << endl;
        };
    }

    for (auto & transcript_stream: transcript_streams) {

        delete transcript_stream;
    }


    if (remove_non_transcribed_nodes) {

        double time_remove_start = gcsa::readTimer();
        if (show_progress) { logger.info() << "Removing non-transcribed regions ..." << endl; }

        transcriptome.remove_non_transcribed_nodes();

        if (show_progress) {
            logger.info() << "Regions removed in " << gcsa::readTimer() - time_remove_start
                          << " seconds, " << gcsa::inGigabytes(gcsa::memoryUsage()) << " GB" << endl;
        };
    }


    if (max_node_length > 0) {

        double time_chop_start = gcsa::readTimer();
        if (show_progress) { logger.info() << "Chopping long nodes ..." << endl; }

        transcriptome.chop_nodes(max_node_length);

        if (show_progress) {
            logger.info() << "Nodes chopped in " << gcsa::readTimer() - time_chop_start 
                          << " seconds, " << gcsa::inGigabytes(gcsa::memoryUsage()) << " GB" << endl;
        };
    }


    if (sort_collapse_graph) {
    
        double time_sort_start = gcsa::readTimer();
        if (show_progress) {
            logger.info() << "Topological sorting graph and compacting node ids ..." << endl;
        }
        
        if (transcriptome.sort_compact_nodes()) {

            if (show_progress) { 
                logger.info() << "Graph sorted and compacted in " 
                              << gcsa::readTimer() - time_sort_start << " seconds, " 
                              << gcsa::inGigabytes(gcsa::memoryUsage()) << " GB" << endl;
            };

        } else {

            if (show_progress) {
                logger.warn() << "Can only sort and compact node ids for a graph in the PackedGraph format" << endl;
            };            
        }        
    }


    if (add_reference_transcript_paths || add_projected_transcript_paths) {

        double time_add_start = gcsa::readTimer();

        if (add_reference_transcript_paths && add_projected_transcript_paths) {

            if (show_progress) {
                logger.info() << "Adding reference and projected transcripts "
                              << "as embedded paths in the graph ..." << endl;
            }

        } else {

            if (show_progress) { 
                logger.info() << "Adding " << ((add_reference_transcript_paths) ? "reference" : "projected")
                              << " transcripts as embedded paths in the graph ..." << endl;
            }
        }

        transcriptome.embed_transcript_paths(add_reference_transcript_paths, add_projected_transcript_paths);

        if (show_progress) {
            logger.info() << "Transcript paths added in " << gcsa::readTimer() - time_add_start
                          << " seconds, " << gcsa::inGigabytes(gcsa::memoryUsage()) << " GB" << endl;
        };
    }

    if (add_transcript_body_paths) {

        double time_body_start = gcsa::readTimer();

        if (show_progress) {
            logger.info() << "Embedding per-transcript unspliced body paths in the graph ..." << endl;
        }

        // Walk both reference/embedded assemblies and GBWT haplotype threads that
        // carry each transcript; bodies are deduplicated on the unspliced node-walk.
        transcriptome.embed_transcript_body_paths(*haplotype_index, true, true);

        if (show_progress) {
            logger.info() << "Transcript body paths added in " << gcsa::readTimer() - time_body_start
                          << " seconds, " << gcsa::inGigabytes(gcsa::memoryUsage()) << " GB" << endl;
        };
    }


    double time_writing_start = gcsa::readTimer();

    bool write_pantranscriptome = (!gbwt_out_filename.empty() || !fasta_out_filename.empty() || !info_out_filename.empty());

    if (write_pantranscriptome) {

        if (show_progress) {
            logger.info() << "Writing pantranscriptome transcripts to file(s) ..." << endl;
        }
    }

    // Write transcript paths in transcriptome as GBWT index.
    if (!gbwt_out_filename.empty()) {

        // Silence GBWT index construction. 
        gbwt::Verbosity::set(gbwt::Verbosity::SILENT); 
        gbwt::GBWTBuilder gbwt_builder(gbwt::bit_length(gbwt::Node::encode(transcriptome.graph().max_node_id(), true)),
                                       gbwt::DynamicGBWT::INSERT_BATCH_SIZE, gbwt::DynamicGBWT::SAMPLE_INTERVAL);

        transcriptome.add_transcripts_to_gbwt(&gbwt_builder, gbwt_add_bidirectional, exclude_reference_transcripts);

        assert(gbwt_builder.index.hasMetadata());

        // Finish contruction and recode index.
        gbwt_builder.finish();
        save_gbwt(gbwt_builder.index, gbwt_out_filename);
    }
    
    // Write a haplotype GBWT with node IDs updated to match the spliced graph.
    if (!hap_gbwt_out_filename.empty()) {
        if (!haplotype_index.get()) {
            logger.warn() << "not saving updated haplotypes to " << hap_gbwt_out_filename 
                          << " because haplotypes were not provided as input" << endl;
        }
        else {
            ofstream hap_gbwt_ostream;
            hap_gbwt_ostream.open(hap_gbwt_out_filename);
            
            haplotype_index->serialize(hap_gbwt_ostream);
        }
    }

    // Write transcript sequences in transcriptome as fasta file.
    if (!fasta_out_filename.empty()) {

        ofstream fasta_ostream;
        fasta_ostream.open(fasta_out_filename);

        transcriptome.write_transcript_sequences(&fasta_ostream, exclude_reference_transcripts);
     
        fasta_ostream.close();
    }

    // Write transcript info in transcriptome as tsv file.
    if (!info_out_filename.empty()) {

        ofstream info_ostream;
        info_ostream.open(info_out_filename);

        transcriptome.write_transcript_info(&info_ostream, *haplotype_index, exclude_reference_transcripts);

        info_ostream.close();
    }    

    if (show_progress) { logger.info() << "Writing splicing graph to stdout ..." << endl; }

    // Write splicing graph to stdout 
    transcriptome.write_graph(&cout);

    if (show_progress) {
        logger.info() << "Graph " << (write_pantranscriptome ? "and pantranscriptome " : "")
                      << "written in " << gcsa::readTimer() - time_writing_start << " seconds, " 
                      << gcsa::inGigabytes(gcsa::memoryUsage()) << " GB" << endl;
    };

    return 0;
}

// Register subcommand
static Subcommand vg_rna("rna", "construct splicing graphs and pantranscriptomes", PIPELINE, 3, main_rna);
