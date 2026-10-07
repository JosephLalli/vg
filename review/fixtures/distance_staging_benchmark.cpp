#include "bdsg/snarl_distance_index.hpp"
#include <chrono>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) { return 1; }
    const bool dense = std::string(argv[1]) == "dense";
    const bool sparse = std::string(argv[2]) == "sparse";
    using Record = bdsg::SnarlDistanceIndex::TemporaryDistanceIndex::TemporarySnarlRecord;
    Record record;
    record.node_count = 1000;
    const auto start = std::chrono::steady_clock::now();
    if (dense) {
        record.allocate_staged_distances();
    } else {
        // Match the unchanged vg producer's reservation before filling rows.
        record.distances.reserve(record.node_count * record.node_count);
    }
    size_t pairs = 0, digest = 0;
    for (size_t first = 0; first < record.node_count; ++first) {
        for (size_t second = first + 1; second < record.node_count; ++second) {
            if (sparse && (first + second) % 128 != 0) { continue; }
            for (bool left : {false, true}) {
                for (bool right : {false, true}) {
                    const size_t distance = 1 + (first + 3 * second + left + right) % 65536;
                    if (dense) {
                        record.stage_distance(first + 2, left, second + 2, right, distance);
                    } else {
                        record.distances.emplace(std::make_pair(std::make_pair(first + 2, left),
                                                               std::make_pair(second + 2, right)), distance);
                    }
                    ++pairs;
                    digest += distance;
                }
            }
        }
    }
    std::cout << "seconds=" << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
              << " records=" << pairs << " input_digest=" << digest << '\n';
}
