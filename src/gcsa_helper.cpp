#include "gcsa_helper.hpp"

#include <vg/io/vpkg.hpp>

#include <atomic>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>

namespace vg {

namespace {

std::atomic<std::uint64_t> gcsa_store_counter(0);

template<class IndexType>
bool atomic_store_gcsa_component(const IndexType& index, const std::string& filename) {
    std::string partial = filename + "." +
        std::to_string(static_cast<std::uint64_t>(::getpid())) + "." +
        std::to_string(gcsa_store_counter.fetch_add(1)) + ".partial";
    if (!sdsl::store_to_file(index, partial)) {
        std::filesystem::remove(partial);
        return false;
    }
    int descriptor = ::open(partial.c_str(), O_RDONLY);
    if (descriptor < 0 || ::fdatasync(descriptor) != 0) {
        if (descriptor >= 0) { ::close(descriptor); }
        std::filesystem::remove(partial);
        return false;
    }
    if (::close(descriptor) != 0 || ::rename(partial.c_str(), filename.c_str()) != 0) {
        std::filesystem::remove(partial);
        return false;
    }
    std::filesystem::path parent = std::filesystem::path(filename).parent_path();
    if (parent.empty()) { parent = "."; }
    descriptor = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (descriptor < 0 || ::fsync(descriptor) != 0) {
        if (descriptor >= 0) { ::close(descriptor); }
        return false;
    }
    return (::close(descriptor) == 0);
}

} // namespace

//------------------------------------------------------------------------------

void load_gcsa(gcsa::GCSA& index, const std::string& filename, bool show_progress) {
    if (show_progress) {
        std::cerr << "Loading GCSA from " << filename << std::endl;
    }
    std::unique_ptr<gcsa::GCSA> loaded = vg::io::VPKG::load_one<gcsa::GCSA>(filename);
    if (loaded.get() == nullptr) {
        std::cerr << "error: [load_gcsa()] cannot load GCSA " << filename << std::endl;
        std::exit(EXIT_FAILURE);
    }
    index = std::move(*loaded);
}

void load_lcp(gcsa::LCPArray& lcp, const std::string& filename, bool show_progress) {
    if (show_progress) {
        std::cerr << "Loading LCP from " << filename << std::endl;
    }
    std::unique_ptr<gcsa::LCPArray> loaded = vg::io::VPKG::load_one<gcsa::LCPArray>(filename);
    if (loaded.get() == nullptr) {
        std::cerr << "error: [load_lcp()] cannot load LCP " << filename << std::endl;
        std::exit(EXIT_FAILURE);
    }
    lcp = std::move(*loaded);
}

void save_gcsa(const gcsa::GCSA& index, const std::string& filename, bool show_progress) {
    if (show_progress) {
        std::cerr << "Saving GCSA to " << filename << std::endl;
    }
    if (!atomic_store_gcsa_component(index, filename)) {
        std::cerr << "error: [save_gcsa()] cannot write GCSA to " << filename << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void save_lcp(const gcsa::LCPArray& lcp, const std::string& filename, bool show_progress) {
    if (show_progress) {
        std::cerr << "Saving LCP to " << filename << std::endl;
    }
    if (!atomic_store_gcsa_component(lcp, filename)) {
        std::cerr << "error: [save_gcsa()] cannot write LCP to " << filename << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

//------------------------------------------------------------------------------

} // namespace vg
