// Standalone byte-identity and bounded-memory probe for generated PagedVector serialization.
#include <bdsg/internal/packed_structs.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr size_t TEST_PAGE_SIZE = 8;
using TestVector = bdsg::PagedVector<TEST_PAGE_SIZE>;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template<class Exception, class Function>
void require_throws(Function&& function, const std::string& label) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(label + ": expected exception");
}

std::string serialize_resident(const std::vector<uint64_t>& values) {
    TestVector resident;
    for (uint64_t value : values) {
        resident.push_back(value);
    }
    std::ostringstream out(std::ios::out | std::ios::binary);
    resident.serialize(out);
    require(static_cast<bool>(out), "resident serialization failed");
    return out.str();
}

std::string serialize_generated(const std::vector<uint64_t>& values,
                                size_t* generator_calls = nullptr) {
    size_t calls = 0;
    std::ostringstream out(std::ios::out | std::ios::binary);
    TestVector::serialize_generated(out, values.size(), [&](const auto& emit) {
        ++calls;
        for (uint64_t value : values) {
            emit(value);
        }
    });
    require(calls == 2, "generated serializer did not make exactly two passes");
    if (generator_calls != nullptr) {
        *generator_calls = calls;
    }
    return out.str();
}

void verify_case(const std::string& label, const std::vector<uint64_t>& values) {
    const std::string expected = serialize_resident(values);
    const std::string observed = serialize_generated(values);
    if (observed != expected) {
        const size_t common = std::min(observed.size(), expected.size());
        size_t offset = 0;
        while (offset < common && observed[offset] == expected[offset]) {
            ++offset;
        }
        throw std::runtime_error(label + ": wire bytes differ at offset " +
                                 std::to_string(offset));
    }

    std::istringstream in(observed, std::ios::in | std::ios::binary);
    TestVector loaded(in);
    require(loaded.size() == values.size(), label + ": deserialized size differs");
    for (size_t i = 0; i < values.size(); ++i) {
        require(loaded.get(i) == values[i],
                label + ": deserialized value differs at " + std::to_string(i));
    }
}

void check_wire_identity() {
    const std::vector<size_t> sizes = {
        0, 1, TEST_PAGE_SIZE - 1, TEST_PAGE_SIZE,
        TEST_PAGE_SIZE + 1, 5 * TEST_PAGE_SIZE + 3
    };
    for (size_t size : sizes) {
        std::vector<uint64_t> values(size, 0);
        for (size_t i = 0; i < size; ++i) {
            values[i] = (i % 5 == 0) ? 0 : 11 + 17 * i;
        }
        verify_case("boundary size " + std::to_string(size), values);
    }

    // Put the first nonzero anchor at a different offset on every page and
    // leave one complete page at anchor zero.
    std::vector<uint64_t> anchors(6 * TEST_PAGE_SIZE + 3, 0);
    const size_t first_nonzero[] = {0, 1, 7, 3, TEST_PAGE_SIZE, 5, 2};
    for (size_t page = 0; page < 7; ++page) {
        if (first_nonzero[page] < TEST_PAGE_SIZE) {
            const size_t index = page * TEST_PAGE_SIZE + first_nonzero[page];
            if (index < anchors.size()) {
                anchors[index] = 1000 + page;
            }
        }
    }
    verify_case("varied first nonzero anchors", anchors);

    // A wide first page followed by an all-zero page catches stale scratch
    // widths or anchors when the page allocation is reused.
    const uint64_t signed_max =
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    const uint64_t fifth = signed_max / 5;
    std::vector<uint64_t> extremes(4 * TEST_PAGE_SIZE, 0);
    extremes[0] = fifth;
    extremes[1] = 1;                    // Large negative to_diff, without overflow.
    extremes[2] = fifth + 17;
    extremes[2 * TEST_PAGE_SIZE] = 1;
    extremes[2 * TEST_PAGE_SIZE + 1] = 1 + 4 * fifth; // Large positive to_diff.
    extremes[3 * TEST_PAGE_SIZE + 7] = fifth - 1;
    verify_case("wide values then zeros and unsigned diff limits", extremes);

    std::mt19937_64 random(0x5041474544564543ULL);
    std::vector<uint64_t> random_values(4099);
    for (size_t i = 0; i < random_values.size(); ++i) {
        random_values[i] = (random() % 7 == 0)
            ? 0 : (random() & ((uint64_t(1) << 48) - 1));
    }
    verify_case("deterministic random multipage", random_values);
}

void check_count_validation() {
    {
        std::ostringstream out(std::ios::out | std::ios::binary);
        require_throws<std::length_error>([&] {
            TestVector::serialize_generated(out, 3, [](const auto& emit) {
                emit(1);
                emit(2);
            });
        }, "first-pass underrun");
        require(out.str().empty(), "first-pass underrun wrote output");
    }
    {
        std::ostringstream out(std::ios::out | std::ios::binary);
        require_throws<std::length_error>([&] {
            TestVector::serialize_generated(out, 2, [](const auto& emit) {
                emit(1);
                emit(2);
                emit(3);
            });
        }, "first-pass overrun");
        require(out.str().empty(), "first-pass overrun wrote output");
    }
    {
        size_t pass = 0;
        std::ostringstream out(std::ios::out | std::ios::binary);
        require_throws<std::length_error>([&] {
            TestVector::serialize_generated(out, 3, [&](const auto& emit) {
                emit(1);
                emit(2);
                if (pass++ == 0) {
                    emit(3);
                }
            });
        }, "second-pass underrun");
        require(!out.str().empty(), "second-pass underrun did not reach output");
    }
    {
        size_t pass = 0;
        std::ostringstream out(std::ios::out | std::ios::binary);
        require_throws<std::length_error>([&] {
            TestVector::serialize_generated(out, 2, [&](const auto& emit) {
                emit(1);
                emit(2);
                if (pass++ != 0) {
                    emit(3);
                }
            });
        }, "second-pass overrun");
    }
    {
        size_t pass = 0;
        std::ostringstream out(std::ios::out | std::ios::binary);
        require_throws<std::invalid_argument>([&] {
            TestVector::serialize_generated(out, 1, [&](const auto& emit) {
                emit(pass++ == 0 ? 1 : 2);
            });
        }, "changed replay anchor");
    }
}

uint64_t benchmark_value(size_t i) {
    if (i % 11 == 0) {
        return 0;
    }
    return 1 + (static_cast<uint64_t>(i) * 2654435761ULL) % 1000000007ULL;
}

uint64_t hash_bytes(const std::string& bytes) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void run_benchmark(const std::string& mode) {
    constexpr size_t count = 1000000;
    using BenchmarkVector = bdsg::PagedVector<64>;
    const auto started = std::chrono::steady_clock::now();
    std::ostringstream out(std::ios::out | std::ios::binary);
    size_t generator_calls = 0;
    size_t resident_bytes = 0;

    if (mode == "benchmark-resident") {
        BenchmarkVector resident;
        for (size_t i = 0; i < count; ++i) {
            resident.push_back(benchmark_value(i));
        }
        resident_bytes = resident.memory_usage();
        resident.serialize(out);
    } else if (mode == "benchmark-generated") {
        BenchmarkVector::serialize_generated(out, count, [&](const auto& emit) {
            ++generator_calls;
            for (size_t i = 0; i < count; ++i) {
                emit(benchmark_value(i));
            }
        });
        require(generator_calls == 2, "benchmark generator pass count differs");
    } else {
        throw std::invalid_argument("unknown benchmark mode");
    }

    const std::string bytes = out.str();
    const auto finished = std::chrono::steady_clock::now();
    std::cout << std::fixed << std::setprecision(6)
              << "{\"mode\":\"" << mode << "\",\"count\":" << count
              << ",\"wire_bytes\":" << bytes.size()
              << ",\"wire_hash\":" << hash_bytes(bytes)
              << ",\"resident_bytes\":" << resident_bytes
              << ",\"generator_calls\":" << generator_calls
              << ",\"seconds\":"
              << std::chrono::duration<double>(finished - started).count()
              << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2) {
            run_benchmark(argv[1]);
            return 0;
        }
        require(argc == 1,
                "usage: paged_stream_probe [benchmark-resident|benchmark-generated]");
        check_wire_identity();
        check_count_validation();
        std::cout << "paged stream probe: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "paged stream probe: FAIL: " << error.what() << '\n';
        return 1;
    }
}
