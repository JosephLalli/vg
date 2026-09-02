#ifndef VG_GCSA_WORKSPACE_HPP_INCLUDED
#define VG_GCSA_WORKSPACE_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vg
{

// Result of committing or restoring the durable k-mer inputs that vg creates
// before handing control to GCSA2. Each entry remains one semantic GCSA input;
// files are never split into extra logical graphs merely to obtain spill files.
struct PersistentGcsaKmers
{
    std::vector<std::string> filenames;
    std::uint64_t bytes = 0;
};

bool persistent_gcsa_kmers_exist(const std::string& work_directory);

// Atomically move/copy generated temporary k-mer files into the workspace and
// commit a versioned manifest only after all payloads are durable.
PersistentGcsaKmers persist_gcsa_kmers(
    const std::string& work_directory,
    const std::vector<std::string>& semantic_sources,
    std::size_t kmer_length,
    const std::vector<std::string>& generated_files);

// Validate source and k-mer sizes/checksums before returning reusable inputs.
// Operational construction settings are intentionally absent from this API.
PersistentGcsaKmers restore_gcsa_kmers(
    const std::string& work_directory,
    const std::vector<std::string>& semantic_sources,
    std::size_t kmer_length);

} // namespace vg

#endif // VG_GCSA_WORKSPACE_HPP_INCLUDED
