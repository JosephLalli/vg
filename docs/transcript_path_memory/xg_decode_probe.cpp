// Standalone probe for sequential decoding of sdsl::enc_vector handles.
// It intentionally does not include or link vg, XG, or libbdsg.

#include <sys/resource.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <sdsl/enc_vector.hpp>
#include <sdsl/util.hpp>

using EncodedHandles = sdsl::enc_vector<>;

struct Metrics {
    double wall_seconds = 0.0;
    double user_seconds = 0.0;
    double system_seconds = 0.0;
    long max_rss_kib = 0;
    uint64_t checksum = 0;
};

static double seconds(const timeval& value) {
    return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_usec) / 1e6;
}

static uint64_t mix(uint64_t state, uint64_t value) {
    return (state ^ value) * 0x9e3779b97f4a7c15ULL + 0x9e3779b97f4a7c15ULL;
}

static uint64_t block_value(const EncodedHandles& encoded, size_t index,
                            std::array<uint64_t, 128>& relative) {
    const size_t density = encoded.get_sample_dens();
    if (density != relative.size()) {
        throw std::runtime_error("probe assumes enc_vector density 128");
    }
    const size_t block = index / density;
    encoded.get_inter_sampled_values(block, relative.data());
    // get_inter_sampled_values() returns the first value as relative 0 and
    // later decoded prefix sums. Unsigned addition intentionally preserves
    // enc_vector's modulo-2^64 delta behavior for nonmonotonic inputs.
    return encoded.sample(block) + relative[index % density];
}

static uint64_t scan_operator(const EncodedHandles& encoded, size_t scans) {
    uint64_t checksum = 0x6a09e667f3bcc909ULL;
    for (size_t scan = 0; scan < scans; ++scan) {
        for (size_t i = 0; i < encoded.size(); ++i) {
            checksum = mix(checksum, encoded[i] + scan);
        }
    }
    return checksum;
}

static uint64_t scan_blocked(const EncodedHandles& encoded, size_t scans) {
    uint64_t checksum = 0x6a09e667f3bcc909ULL;
    const size_t density = encoded.get_sample_dens();
    std::array<uint64_t, 128> relative{};
    if (density != relative.size()) {
        throw std::runtime_error("probe assumes enc_vector density 128");
    }
    for (size_t scan = 0; scan < scans; ++scan) {
        for (size_t begin = 0; begin < encoded.size(); begin += density) {
            const size_t block = begin / density;
            encoded.get_inter_sampled_values(block, relative.data());
            const uint64_t sample = encoded.sample(block);
            const size_t end = std::min(begin + density, encoded.size());
            for (size_t i = begin; i < end; ++i) {
                checksum = mix(checksum, sample + relative[i - begin] + scan);
            }
        }
    }
    return checksum;
}

template<class Function>
static Metrics measure(const Function& function) {
    rusage before{}, after{};
    getrusage(RUSAGE_SELF, &before);
    const auto start = std::chrono::steady_clock::now();
    const uint64_t checksum = function();
    const auto finish = std::chrono::steady_clock::now();
    getrusage(RUSAGE_SELF, &after);
    return {std::chrono::duration<double>(finish - start).count(),
            seconds(after.ru_utime) - seconds(before.ru_utime),
            seconds(after.ru_stime) - seconds(before.ru_stime),
            after.ru_maxrss, checksum};
}

static void require_equivalent(const std::vector<uint64_t>& values, const std::string& label) {
    EncodedHandles encoded(values);
    if (encoded.size() != values.size()) {
        throw std::runtime_error(label + ": encoded size mismatch");
    }
    std::array<uint64_t, 128> relative{};
    for (size_t i = 0; i < values.size(); ++i) {
        const uint64_t direct = encoded[i];
        const uint64_t blocked = block_value(encoded, i, relative);
        if (direct != values[i] || blocked != values[i]) {
            throw std::runtime_error(label + ": disagreement at " + std::to_string(i));
        }
    }
}

static std::vector<uint64_t> realistic_handles(size_t count) {
    std::vector<uint64_t> values;
    values.reserve(count);
    std::mt19937_64 random(0x5eed1234ULL);
    uint64_t path_seed = 17;
    while (values.size() < count) {
        const size_t path_length = 3 + random() % 61;
        for (size_t step = 0; step < path_length && values.size() < count; ++step) {
            // Jagged, repeated, mixed-orientation local handles. Every 17th
            // step repeats the prior handle, as common in cyclic walks.
            uint64_t node = path_seed + ((step * 13 + random() % 7) % 4096);
            if (step != 0 && step % 17 == 0) {
                values.push_back(values.back());
            } else {
                values.push_back((node << 1) | ((step + path_seed) & 1));
            }
        }
        path_seed += 97;
    }
    return values;
}

static void print_metrics(const char* name, const Metrics& metrics) {
    std::cout << "\"" << name << "\":{"
              << "\"wall_seconds\":" << metrics.wall_seconds << ','
              << "\"user_seconds\":" << metrics.user_seconds << ','
              << "\"system_seconds\":" << metrics.system_seconds << ','
              << "\"max_rss_kib\":" << metrics.max_rss_kib << ','
              << "\"checksum\":" << metrics.checksum << '}';
}

int main() {
    try {
        const std::array<size_t, 8> lengths{{0, 1, 127, 128, 129, 255, 256, 257}};
        for (size_t length : lengths) {
            std::vector<uint64_t> values;
            for (size_t i = 0; i < length; ++i) {
                const uint64_t value = (i % 11 == 0) ? UINT64_MAX - i :
                    ((i % 5 == 0) ? (uint64_t(7) << 63) + i : (i * 37) ^ (i << 1));
                values.push_back(value);
            }
            require_equivalent(values, "boundary-" + std::to_string(length));
        }
        require_equivalent({0, 5, 5, 2, 9, 1, UINT64_MAX, 0, UINT64_MAX - 2}, "repeats-wrap");

        std::mt19937_64 random(0xdec0deULL);
        for (size_t trial = 0; trial < 256; ++trial) {
            std::vector<uint64_t> values(1 + random() % 513);
            for (size_t i = 0; i < values.size(); ++i) {
                values[i] = random();
                if (i != 0 && random() % 5 == 0) { values[i] = values[i - 1]; }
                if (i != 0 && random() % 7 == 0) { values[i] = ~values[i - 1]; }
            }
            require_equivalent(values, "random-" + std::to_string(trial));
        }

        const std::vector<uint64_t> values = realistic_handles(1000000);
        const EncodedHandles encoded(values);
        const Metrics direct = measure([&] { return scan_operator(encoded, 3); });
        const Metrics blocked = measure([&] { return scan_blocked(encoded, 3); });
        if (direct.checksum != blocked.checksum) {
            throw std::runtime_error("benchmark checksums differ");
        }
        std::cout << '{'
                  << "\"status\":\"ok\",\"values\":" << values.size() << ','
                  << "\"scans\":3,\"sample_density\":" << encoded.get_sample_dens() << ','
                  << "\"encoded_bytes\":" << sdsl::size_in_bytes(encoded) << ',';
        print_metrics("operator", direct);
        std::cout << ',';
        print_metrics("blocked", blocked);
        std::cout << "}\n";
    } catch (const std::exception& error) {
        std::cerr << "xg_decode_probe: " << error.what() << '\n';
        return 1;
    }
}
