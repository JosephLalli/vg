/** \file ids_main.cpp
 *
 * Defines the "vg ids" subcommand, which modifies node IDs.
 */


#include <omp.h>
#include <unistd.h>
#include <getopt.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

#include "subcommand.hpp"

#include "../vg.hpp"
#include "../vg_set.hpp"
#include <vg/io/stream.hpp>
#include <vg/io/vpkg.hpp>
#include <handlegraph/mutable_path_mutable_handle_graph.hpp>
#include "bdsg/packed_graph.hpp"
#include "bdsg/hash_graph.hpp"
#include <bdsg/overlays/overlay_helper.hpp>
#include "../io/save_handle_graph.hpp"
#include <gcsa/support.h>

using namespace std;
using namespace vg;
using namespace vg::subcommand;

constexpr int JOIN_MAPPINGS_OPT = 1000;
constexpr int REBASE_PRUNED_OPT = 1001;
constexpr int CHECK_MAPPING_OPT = 1002;
constexpr int REFERENCE_OPT = 1003;

void help_ids(char** argv) {
    cerr << "usage: " << argv[0] << " ids [options] <graph1.vg> [graph2.vg ...] >new.vg" << endl
         << "   or: " << argv[0] << " ids --join-mappings FILE <part1.mapping> [...] >plan.tsv" << endl
         << "   or: " << argv[0] << " ids --rebase-pruned SPEC <pruned.vg> >rebased.vg" << endl
         << "   or: " << argv[0] << " ids --check-mapping FILE --reference FILE <graph.vg> [...]" << endl
         << "options:" << endl
         << "  -c, --compact             minimize the space of integers used by the ids" << endl
         << "  -i, --increment N         increase ids by N" << endl
         << "  -d, --decrement N         decrease ids by N" << endl
         << "  -j, --join                make a joint ID space for all supplied graphs by" << endl
         << "                            iterating through the supplied graphs and" << endl
         << "                            incrementing their ids to be non-conflicting" << endl
         << "                            (modifies original files)" << endl
         << "  -m, --mapping FILE        create an empty node mapping for vg prune" << endl
         << "  -s, --sort                assign new node IDs in generalized topological" << endl
         << "                            sort order" << endl
         << "joining graphs that were pruned in their own ID spaces:" << endl
         << "      --join-mappings FILE  concatenate the node mappings vg prune wrote, given" << endl
         << "                            in join order, into the joint mapping FILE and" << endl
         << "                            print each part's offsets as TSV" << endl
         << "      --rebase-pruned SPEC  move one pruned graph into the joint ID space;" << endl
         << "                            SPEC is FIRST_DUP:ORIG_OFFSET:DUP_OFFSET from" << endl
         << "                            the offsets: IDs below FIRST_DUP increase by" << endl
         << "                            ORIG_OFFSET and the rest by DUP_OFFSET" << endl
         << "      --check-mapping FILE  check the input graphs through the joint mapping" << endl
         << "                            FILE: each node's original ID must be in the" << endl
         << "                            reference graph with the same sequence" << endl
         << "      --reference FILE      graph in joint original IDs for --check-mapping" << endl
         << "  -h, --help                print this help message to stderr and exit" << endl;
}

/// Read and validate the header of a gcsa::NodeMapping file, which is
/// first_node and next_node followed by one original ID for each duplicate.
/// NodeMapping::load() does not notice a short read, so a truncated file would
/// otherwise load as one padded with zeros.
static pair<nid_t, nid_t> read_mapping_header(const Logger& logger, const string& filename) {
    constexpr uintmax_t WORD_BYTES = sizeof(gcsa::size_type);
    std::error_code error;
    uintmax_t bytes = std::filesystem::file_size(filename, error);
    if (error) {
        logger.error() << "cannot read node mapping " << filename << ": " << error.message() << endl;
    }
    if (bytes < 2 * WORD_BYTES) {
        logger.error() << "node mapping " << filename << " is " << bytes << " bytes, shorter than the "
                       << 2 * WORD_BYTES << "-byte header" << endl;
    }
    ifstream in(filename, ios_base::binary);
    gcsa::size_type header[2];
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!in) {
        logger.error() << "cannot read the header of node mapping " << filename << endl;
    }
    gcsa::size_type first_node = header[0], next_node = header[1];
    // Node IDs are signed, so anything larger cannot have come from a graph.
    constexpr gcsa::size_type MAX_ID = numeric_limits<nid_t>::max();
    if (first_node < 2 || next_node < first_node || next_node > MAX_ID) {
        logger.error() << "node mapping " << filename << " has a malformed header: first_node "
                       << first_node << ", next_node " << next_node << endl;
    }
    uintmax_t payload = bytes - 2 * WORD_BYTES;
    if (payload % WORD_BYTES != 0 || payload / WORD_BYTES != next_node - first_node) {
        logger.error() << "node mapping " << filename << " is " << bytes << " bytes, but its header ("
                       << first_node << ".." << next_node << ") describes " << (next_node - first_node)
                       << " duplicates, which take " << 2 * WORD_BYTES + (next_node - first_node) * WORD_BYTES
                       << " bytes" << endl;
    }
    return make_pair((nid_t) first_node, (nid_t) next_node);
}

static gcsa::NodeMapping load_mapping(const Logger& logger, const string& filename) {
    read_mapping_header(logger, filename);
    gcsa::NodeMapping mapping;
    ifstream in(filename, ios_base::binary);
    mapping.load(in);
    if (!in) {
        logger.error() << "cannot read node mapping " << filename << endl;
    }
    return mapping;
}

/// Concatenate the node mappings of graphs that were each pruned in their own
/// ID space, in join order, into one mapping over the joint ID space, and print
/// the offsets that move each part's pruned graph into that space.
///
/// Part c's original IDs are 1..L_c with L_c = first_node_c - 1, because vg
/// prune and vg ids -m both start the duplicates after the graph's max node ID.
/// Originals are packed first, in join order, the way vg ids -j packs graphs
/// whose IDs start at 1; all duplicates follow the last original, again in join
/// order.
static void join_mappings(const Logger& logger, const string& joint_name, const vector<string>& part_names) {
    // Writing the joint mapping truncates its file, so it cannot be a part, and
    // a part given twice would shadow the chromosome it replaced.
    for (size_t i = 0; i < part_names.size(); i++) {
        std::error_code error;
        if (std::filesystem::equivalent(joint_name, part_names[i], error)) {
            logger.error() << "the joint mapping " << joint_name << " is also given as part "
                           << part_names[i] << endl;
        }
        for (size_t j = 0; j < i; j++) {
            if (std::filesystem::equivalent(part_names[j], part_names[i], error)) {
                logger.error() << "node mapping " << part_names[i] << " is given twice, also as "
                               << part_names[j] << endl;
            }
        }
    }

    struct Part {
        nid_t local_max, first_dup, dup_count, orig_offset, dup_offset;
    };
    vector<Part> parts;
    parts.reserve(part_names.size());
    constexpr nid_t MAX_ID = numeric_limits<nid_t>::max();
    nid_t originals = 0, duplicates = 0;
    for (auto& name : part_names) {
        auto header = read_mapping_header(logger, name);
        Part part;
        part.first_dup = header.first;
        part.local_max = header.first - 1;
        part.dup_count = header.second - header.first;
        // vg ids -m writes a mapping with no duplicates as the seed that
        // vg prune -a appends to; it is not a prune's output.
        if (part.dup_count == 0) {
            logger.error() << "node mapping " << name << " is empty (" << header.first << ".."
                           << header.second << "); give the mapping vg prune -u wrote" << endl;
        }
        if (part.local_max > MAX_ID - originals || part.dup_count > MAX_ID - duplicates) {
            logger.error() << "node mapping " << name << " takes the joint ID space past "
                           << MAX_ID << endl;
        }
        originals += part.local_max;
        duplicates += part.dup_count;
        parts.push_back(part);
    }
    if (duplicates > MAX_ID - originals - 1) {
        logger.error() << "the joint ID space would pass " << MAX_ID << endl;
    }

    nid_t joint_first = originals + 1;
    nid_t orig_offset = 0, next_dup = joint_first;
    for (auto& part : parts) {
        part.orig_offset = orig_offset;
        part.dup_offset = next_dup - part.first_dup;
        orig_offset += part.local_max;
        next_dup += part.dup_count;
    }

    gcsa::NodeMapping joint(joint_first);
    joint.mapping.reserve(duplicates);
    for (size_t i = 0; i < parts.size(); i++) {
        gcsa::NodeMapping local = load_mapping(logger, part_names[i]);
        for (gcsa::size_type duplicate = local.begin(); duplicate < local.end(); duplicate++) {
            gcsa::size_type original = local(duplicate);
            if (original < 1 || original > (gcsa::size_type) parts[i].local_max) {
                logger.error() << "node mapping " << part_names[i] << " maps duplicate " << duplicate
                               << " to " << original << ", outside its original IDs 1.."
                               << parts[i].local_max << endl;
            }
            joint.insert(original + parts[i].orig_offset);
        }
    }

    ofstream out(joint_name, ios_base::binary);
    joint.serialize(out);
    out.close();
    if (!out) {
        std::remove(joint_name.c_str());
        logger.error() << "cannot write joint node mapping " << joint_name << endl;
    }

    cout << "part\tlocal_max\tfirst_dup\tdup_count\torig_offset\tdup_offset" << endl;
    for (size_t i = 0; i < parts.size(); i++) {
        cout << part_names[i] << "\t" << parts[i].local_max << "\t" << parts[i].first_dup << "\t"
             << parts[i].dup_count << "\t" << parts[i].orig_offset << "\t" << parts[i].dup_offset << endl;
    }
}

struct RebaseSpec {
    nid_t first_dup, orig_offset, dup_offset;
};

static RebaseSpec parse_rebase_spec(const Logger& logger, const string& arg) {
    vector<nid_t> values;
    size_t start = 0;
    while (true) {
        size_t end = arg.find(':', start);
        string field = arg.substr(start, end == string::npos ? string::npos : end - start);
        nid_t value = 0;
        bool parsed = false;
        try {
            parsed = parse<nid_t>(field, value);
        } catch (const exception&) {
            parsed = false;
        }
        if (!parsed) {
            logger.error() << "--rebase-pruned needs FIRST_DUP:ORIG_OFFSET:DUP_OFFSET as three integers, not \""
                           << arg << "\"" << endl;
        }
        values.push_back(value);
        if (end == string::npos) {
            break;
        }
        start = end + 1;
    }
    if (values.size() != 3) {
        logger.error() << "--rebase-pruned needs FIRST_DUP:ORIG_OFFSET:DUP_OFFSET as three integers, not \""
                       << arg << "\"" << endl;
    }
    RebaseSpec spec { values[0], values[1], values[2] };
    if (spec.first_dup < 1 || spec.orig_offset < 0) {
        logger.error() << "--rebase-pruned needs FIRST_DUP >= 1 and ORIG_OFFSET >= 0, not \"" << arg << "\"" << endl;
    }
    // Originals move to at most FIRST_DUP - 1 + ORIG_OFFSET and duplicates to at
    // least FIRST_DUP + DUP_OFFSET, so a smaller DUP_OFFSET could merge a
    // duplicate into an original.
    if (spec.dup_offset < spec.orig_offset) {
        logger.error() << "--rebase-pruned DUP_OFFSET " << spec.dup_offset << " is below ORIG_OFFSET "
                       << spec.orig_offset << ", which could give a duplicate an original's ID" << endl;
    }
    return spec;
}

/// Check graphs in the joint ID space against a reference graph in joint
/// original IDs. A wrong offset still gives a GCSA2 that builds without error,
/// so this is where it shows: a shifted node lands on another node's original,
/// whose sequence differs, or on none at all.
static void check_mapping(const Logger& logger, const string& mapping_name, const string& reference_name,
                          const vector<string>& graph_names) {
    gcsa::NodeMapping mapping = load_mapping(logger, mapping_name);
    const nid_t max_original = (nid_t) mapping.begin() - 1;
    unique_ptr<HandleGraph> reference = vg::io::VPKG::load_one<HandleGraph>(reference_name);

    // Enough examples to see the pattern of a bad offset without flooding.
    constexpr size_t EXAMPLES_PER_GRAPH = 10;
    size_t total_failures = 0;
    cout << "graph\tnodes\tduplicates\tmissing\tmismatched" << endl;
    for (auto& graph_name : graph_names) {
        unique_ptr<HandleGraph> graph = vg::io::VPKG::load_one<HandleGraph>(graph_name);
        size_t nodes = 0, duplicates = 0, missing = 0, mismatched = 0;
        auto report = [&](nid_t id, const string& problem) {
            if (missing + mismatched <= EXAMPLES_PER_GRAPH) {
                logger.warn() << graph_name << ": node " << id << " " << problem << endl;
            }
        };
        graph->for_each_handle([&](const handle_t& handle) {
            nodes++;
            nid_t id = graph->get_id(handle);
            nid_t original = id;
            if (id > max_original) {
                duplicates++;
                // NodeMapping passes IDs outside its range through unchanged,
                // which would pass off an unmapped duplicate as an original.
                if ((gcsa::size_type) id >= mapping.end()) {
                    missing++;
                    report(id, "is past the joint mapping's duplicates, which end before "
                           + std::to_string(mapping.end()));
                    return;
                }
                original = (nid_t) mapping(id);
            }
            if (!reference->has_node(original)) {
                missing++;
                report(id, "has original " + std::to_string(original) + ", which is not in the reference");
                return;
            }
            string sequence = graph->get_sequence(graph->forward(handle));
            string expected = reference->get_sequence(reference->get_handle(original));
            if (sequence != expected) {
                mismatched++;
                report(id, "has a " + std::to_string(sequence.size()) + " bp sequence that differs from its original "
                       + std::to_string(original) + " (" + std::to_string(expected.size()) + " bp) in the reference");
            }
        });
        cout << graph_name << "\t" << nodes << "\t" << duplicates << "\t" << missing << "\t" << mismatched << endl;
        total_failures += missing + mismatched;
    }
    if (total_failures > 0) {
        logger.error() << total_failures << " nodes are missing from the reference or differ from it through "
                       << mapping_name << endl;
    }
}

int main_ids(int argc, char** argv) {
    Logger logger("vg ids");

    if (argc == 2) {
        help_ids(argv);
        return 1;
    }

    bool join = false;
    bool compact = false;
    bool sort = false;
    int64_t increment = 0;
    int64_t decrement = 0;
    std::string mapping_name;
    std::string joint_mapping_name;
    std::string rebase_spec;
    std::string check_mapping_name;
    std::string reference_name;

    int c;
    optind = 2; // force optind past command positional argument
    while (true) {
        static struct option long_options[] =
        {
            {"compact", no_argument, 0, 'c'},
            {"increment", required_argument, 0, 'i'},
            {"decrement", required_argument, 0, 'd'},
            {"join", no_argument, 0, 'j'},
            {"mapping", required_argument, 0, 'm'},
            {"sort", no_argument, 0, 's'},
            {"join-mappings", required_argument, 0, JOIN_MAPPINGS_OPT},
            {"rebase-pruned", required_argument, 0, REBASE_PRUNED_OPT},
            {"check-mapping", required_argument, 0, CHECK_MAPPING_OPT},
            {"reference", required_argument, 0, REFERENCE_OPT},
            {"help", no_argument, 0, 'h'},
            {0, 0, 0, 0}
        };

        int option_index = 0;
        c = getopt_long (argc, argv, "h?ci:d:jm:s",
                         long_options, &option_index);

        // Detect the end of the options.
        if (c == -1)
            break;

        switch (c)
        {
            case 'c':
                compact = true;
                break;

            case 'i':
                increment = parse<int>(optarg);
                break;

            case 'd':
                decrement = parse<int>(optarg);
                break;

            case 'j':
                join = true;
                break;

            case 'm':
                mapping_name = ensure_writable(logger, optarg);
                break;

            case 's':
                sort = true;
                break;

            case JOIN_MAPPINGS_OPT:
                joint_mapping_name = ensure_writable(logger, optarg);
                break;

            case REBASE_PRUNED_OPT:
                rebase_spec = optarg;
                break;

            case CHECK_MAPPING_OPT:
                check_mapping_name = require_exists(logger, optarg);
                break;

            case REFERENCE_OPT:
                reference_name = require_exists(logger, optarg);
                break;

            case 'h':
            case '?':
                help_ids(argv);
                exit(1);
                break;

            default:
                abort ();
        }
    }

    int joint_modes = !joint_mapping_name.empty() + !rebase_spec.empty() + !check_mapping_name.empty();
    bool other_operations = compact || sort || join || increment != 0 || decrement != 0 || !mapping_name.empty();
    if (joint_modes > 1 || (joint_modes == 1 && other_operations)) {
        logger.error() << "--join-mappings, --rebase-pruned and --check-mapping each run alone, "
                       << "without any other operation" << endl;
    }
    if (check_mapping_name.empty() != reference_name.empty()) {
        logger.error() << "--check-mapping and --reference must be given together" << endl;
    }

    if (!joint_mapping_name.empty()) {
        vector<string> part_names;
        while (optind < argc) {
            part_names.push_back(get_input_file_name(optind, argc, argv));
        }
        if (part_names.empty()) {
            logger.error() << "--join-mappings needs the part mappings as input" << endl;
        }
        join_mappings(logger, joint_mapping_name, part_names);
        return 0;
    }

    if (!rebase_spec.empty()) {
        RebaseSpec spec = parse_rebase_spec(logger, rebase_spec);
        string graph_filename = get_input_file_name(optind, argc, argv);
        if (optind < argc) {
            logger.error() << "--rebase-pruned takes one input graph" << endl;
        }
        unique_ptr<MutablePathMutableHandleGraph> graph
            = vg::io::VPKG::load_one<MutablePathMutableHandleGraph>(graph_filename);
        graph->reassign_node_ids([&](const nid_t& id) {
            return id + (id < spec.first_dup ? spec.orig_offset : spec.dup_offset);
        });
        vg::io::save_handle_graph(graph.get(), cout);
        return 0;
    }

    if (!check_mapping_name.empty()) {
        vector<string> graph_names;
        while (optind < argc) {
            graph_names.push_back(get_input_file_name(optind, argc, argv));
        }
        if (graph_names.empty()) {
            logger.error() << "--check-mapping needs the graphs to check as input" << endl;
        }
        check_mapping(logger, check_mapping_name, reference_name, graph_names);
        return 0;
    }

    if (!join && mapping_name.empty()) {
        unique_ptr<MutablePathMutableHandleGraph> graph;
        string graph_filename = get_input_file_name(optind, argc, argv);
        graph = vg::io::VPKG::load_one<MutablePathMutableHandleGraph>(graph_filename);            
            
        if (sort || compact) {
            // We need to reassign IDs
            hash_map<nid_t, nid_t> new_ids;
            
            if (compact && !sort) {
                // We are compacting, but do not need to topologically sort
                
                // Loop over all the nodes in the graph's order and assign them new IDs in ID order.
                // This is slower than it needs to be, but gets us nice results even on graphs that don't preserve node order.
                // TODO: counting all the nodes may be an O(1) scan of the graph to save some vector copies.
                vector<nid_t> all_ids;
                all_ids.reserve(graph->get_node_count());
                graph->for_each_handle([&](const handle_t& h) {
                    all_ids.emplace_back(graph->get_id(h));
                });
                std::sort(all_ids.begin(), all_ids.end());
                
                // Now invert the vector's mapping
                new_ids.reserve(all_ids.size());
                for (nid_t i = 1; i < all_ids.size() + 1; i++) {
                    new_ids[all_ids[i - 1]] = i;
                }
            } else {
                // We are sorting to assign IDs, which inherently compacts.
                
                // We only need to sort the ID numbers, not the graph's iteration order (if any).
                auto handle_order = handlealgs::topological_order(graph.get());
                
                // Now invert the order's mapping
                new_ids.reserve(handle_order.size());
                for (nid_t i = 1; i < handle_order.size() + 1; i++) {
                    new_ids[graph->get_id(handle_order[i - 1])] = i;
                }
            }
            
            // Now assign the IDs. If we find any e.g. dangling paths or
            // edges with no nodes we will crash.
            graph->reassign_node_ids([&](const nid_t& old_id) {
                return new_ids.at(old_id);
            });
        }

        if (increment != 0) {
            graph->increment_node_ids(increment);
        }

        if (decrement != 0) {
            graph->increment_node_ids(-increment);
        }

        vg::io::save_handle_graph(graph.get(), cout);
    } else {

        vector<string> graph_file_names;
        while (optind < argc) {
            string file_name = get_input_file_name(optind, argc, argv);
            graph_file_names.push_back(file_name);
        }

        VGset graphs(graph_file_names);
        vg::id_t max_node_id = (join ? graphs.merge_id_space() : graphs.max_node_id());
        if (!mapping_name.empty()) {
            gcsa::NodeMapping mapping(max_node_id + 1);
            std::ofstream out(mapping_name, std::ios_base::binary);
            mapping.serialize(out);
            out.close();
        }
    }

    return 0;

}

// Register subcommand
static Subcommand vg_ids("ids", "manipulate node ids", TOOLKIT, main_ids);

