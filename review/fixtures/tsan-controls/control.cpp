#include <numeric>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "race") {
        volatile unsigned long value = 0;
        #pragma omp parallel for num_threads(4)
        for (int i = 0; i < 10000; ++i) { ++value; }
        return 0;
    }
    std::vector<int> values(1000);
    #pragma omp parallel for num_threads(4)
    for (int i = 0; i < 1000; ++i) { values[i] = i; }
    return std::accumulate(values.begin(), values.end(), 0) != 499500;
}
