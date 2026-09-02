#include "gcsa_workspace.hpp"

#include "utility.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace vg
{

namespace
{

constexpr std::uint32_t MANIFEST_VERSION = 1;
constexpr std::size_t COPY_BUFFER_BYTES = 1024 * 1024;
constexpr std::uint64_t FNV_OFFSET = 1469598103934665603ULL;
constexpr std::uint64_t FNV_PRIME = 1099511628211ULL;

std::atomic<std::uint64_t> partial_counter(0);

struct FileIdentity
{
    std::string path;
    std::uint64_t bytes = 0;
    std::uint64_t checksum = FNV_OFFSET;
};

std::runtime_error
workspaceError(const std::string& message, const std::string& path)
{
    return std::runtime_error(message + ": " + path);
}

std::string
partialSuffix()
{
    return "." + std::to_string(static_cast<std::uint64_t>(::getpid())) + "." +
        std::to_string(partial_counter.fetch_add(1)) + ".partial";
}

std::string
normalizedPath(const std::string& path)
{
    if(path == "-") { return path; }
    return std::filesystem::absolute(path).lexically_normal().string();
}

void
validateManifestPath(const std::string& path)
{
    if(path.find('\t') != std::string::npos || path.find('\n') != std::string::npos ||
       path.find('\r') != std::string::npos)
    {
        throw workspaceError("GCSA workspace path contains a tab or newline", path);
    }
}

void
writeAll(int descriptor, const void* data, std::size_t bytes, const std::string& path)
{
    const char* cursor = static_cast<const char*>(data);
    while(bytes > 0)
    {
        ssize_t written = ::write(descriptor, cursor, bytes);
        if(written < 0 && errno == EINTR) { continue; }
        if(written <= 0) { throw workspaceError("write failed", path); }
        cursor += written;
        bytes -= static_cast<std::size_t>(written);
    }
}

void
syncFile(int descriptor, const std::string& path)
{
    if(::fdatasync(descriptor) != 0)
    {
        int saved_errno = errno;
        ::close(descriptor);
        errno = saved_errno;
        throw workspaceError("fdatasync failed", path);
    }
    if(::close(descriptor) != 0) { throw workspaceError("close failed", path); }
}

void
syncDirectory(const std::string& path)
{
    int descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if(descriptor < 0) { throw workspaceError("cannot open directory for fsync", path); }
    if(::fsync(descriptor) != 0)
    {
        int saved_errno = errno;
        ::close(descriptor);
        errno = saved_errno;
        throw workspaceError("directory fsync failed", path);
    }
    if(::close(descriptor) != 0) { throw workspaceError("directory close failed", path); }
}

FileIdentity
identifyFile(const std::string& input_path)
{
    FileIdentity identity;
    identity.path = normalizedPath(input_path);
    validateManifestPath(identity.path);
    if(identity.path == "-")
    {
        throw std::runtime_error(
            "cannot create a resumable GCSA workspace from standard input; use a named graph file");
    }

    int descriptor = ::open(identity.path.c_str(), O_RDONLY);
    if(descriptor < 0) { throw workspaceError("cannot open semantic input", identity.path); }
    std::vector<std::uint8_t> buffer(COPY_BUFFER_BYTES);
    try
    {
        while(true)
        {
            ssize_t bytes = ::read(descriptor, buffer.data(), buffer.size());
            if(bytes < 0 && errno == EINTR) { continue; }
            if(bytes < 0) { throw workspaceError("read failed", identity.path); }
            if(bytes == 0) { break; }
            identity.bytes += static_cast<std::uint64_t>(bytes);
            for(ssize_t i = 0; i < bytes; ++i)
            {
                identity.checksum ^= buffer[static_cast<std::size_t>(i)];
                identity.checksum *= FNV_PRIME;
            }
        }
        if(::close(descriptor) != 0) { throw workspaceError("close failed", identity.path); }
    }
    catch(...)
    {
        ::close(descriptor);
        throw;
    }
    return identity;
}

std::vector<FileIdentity>
identifySources(const std::vector<std::string>& sources)
{
    if(sources.empty())
    {
        throw std::runtime_error("cannot identify resumable GCSA construction without a source graph");
    }
    std::vector<FileIdentity> result;
    result.reserve(sources.size());
    for(const std::string& source : sources) { result.push_back(identifyFile(source)); }
    return result;
}

std::string
manifestPath(const std::string& work_directory)
{
    return (std::filesystem::path(work_directory) / "inputs" / "kmers.manifest").string();
}

std::string
kmerPath(const std::string& work_directory, std::size_t index)
{
    std::ostringstream name;
    name << "kmer-";
    name.width(6);
    name.fill('0');
    name << index << ".graph";
    return std::filesystem::absolute(
        std::filesystem::path(work_directory) / "inputs" / name.str()).lexically_normal().string();
}

void
removeInputPartials(const std::filesystem::path& input_directory)
{
    std::error_code error;
    bool removed = false;
    for(std::filesystem::directory_iterator iterator(input_directory, error), end;
        !error && iterator != end; iterator.increment(error))
    {
        const std::filesystem::path& path = iterator->path();
        if(iterator->is_regular_file(error) && !error && path.extension() == ".partial")
        {
            if(!std::filesystem::remove(path, error) || error)
            {
                throw workspaceError("cannot remove orphan partial k-mer artifact", path.string());
            }
            removed = true;
        }
    }
    if(error)
    {
        throw workspaceError("cannot inspect GCSA input artifact directory", input_directory.string());
    }
    if(removed) { syncDirectory(input_directory.string()); }
}

void
copyFile(const std::string& source, const std::string& destination)
{
    int input = ::open(source.c_str(), O_RDONLY);
    if(input < 0) { throw workspaceError("cannot open generated k-mer file", source); }
    int output = ::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if(output < 0)
    {
        ::close(input);
        throw workspaceError("cannot create partial k-mer artifact", destination);
    }
    std::vector<std::uint8_t> buffer(COPY_BUFFER_BYTES);
    try
    {
        while(true)
        {
            ssize_t bytes = ::read(input, buffer.data(), buffer.size());
            if(bytes < 0 && errno == EINTR) { continue; }
            if(bytes < 0) { throw workspaceError("read failed", source); }
            if(bytes == 0) { break; }
            writeAll(output, buffer.data(), static_cast<std::size_t>(bytes), destination);
        }
        if(::close(input) != 0) { throw workspaceError("close failed", source); }
        input = -1;
        syncFile(output, destination);
        output = -1;
    }
    catch(...)
    {
        if(input >= 0) { ::close(input); }
        if(output >= 0) { ::close(output); }
        ::unlink(destination.c_str());
        throw;
    }
}

void
installGeneratedFile(const std::string& source, const std::string& destination)
{
    const std::string partial = destination + partialSuffix();
    if(::rename(source.c_str(), partial.c_str()) != 0)
    {
        if(errno != EXDEV) { throw workspaceError("cannot stage generated k-mer file", source); }
        copyFile(source, partial);
    }

    // The source name was registered with vg's temporary-file handler. Remove
    // the stale registration (and the original after a cross-device copy).
    temp_file::remove(source);

    int descriptor = ::open(partial.c_str(), O_RDONLY);
    if(descriptor < 0) { throw workspaceError("cannot open staged k-mer artifact", partial); }
    syncFile(descriptor, partial);
    if(::rename(partial.c_str(), destination.c_str()) != 0)
    {
        ::unlink(partial.c_str());
        throw workspaceError("cannot publish k-mer artifact", destination);
    }
}

void
commitManifest(const std::string& path, const std::string& contents)
{
    const std::string partial = path + partialSuffix();
    int descriptor = ::open(partial.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if(descriptor < 0) { throw workspaceError("cannot create k-mer manifest", partial); }
    try
    {
        writeAll(descriptor, contents.data(), contents.size(), partial);
        syncFile(descriptor, partial);
        descriptor = -1;
        if(::rename(partial.c_str(), path.c_str()) != 0)
        {
            throw workspaceError("cannot publish k-mer manifest", path);
        }
        syncDirectory(std::filesystem::path(path).parent_path().string());
    }
    catch(...)
    {
        if(descriptor >= 0) { ::close(descriptor); }
        ::unlink(partial.c_str());
        throw;
    }
}

std::vector<std::string>
splitTabs(const std::string& line)
{
    std::vector<std::string> fields;
    std::size_t first = 0;
    while(true)
    {
        std::size_t next = line.find('\t', first);
        fields.push_back(line.substr(first, next - first));
        if(next == std::string::npos) { break; }
        first = next + 1;
    }
    return fields;
}

std::uint64_t
parseUnsigned(const std::string& value, const std::string& field,
    const std::string& manifest)
{
    std::size_t parsed = 0;
    std::uint64_t result = 0;
    try { result = std::stoull(value, &parsed); }
    catch(const std::exception&)
    {
        throw workspaceError("invalid " + field + " in k-mer manifest", manifest);
    }
    if(parsed != value.size())
    {
        throw workspaceError("invalid " + field + " in k-mer manifest", manifest);
    }
    return result;
}

} // namespace

bool
persistent_gcsa_kmers_exist(const std::string& work_directory)
{
    return std::filesystem::is_regular_file(manifestPath(work_directory));
}

PersistentGcsaKmers
persist_gcsa_kmers(const std::string& work_directory,
    const std::vector<std::string>& semantic_sources, std::size_t kmer_length,
    const std::vector<std::string>& generated_files)
{
    if(generated_files.empty())
    {
        throw std::runtime_error("GCSA k-mer generation produced no logical inputs");
    }
    std::vector<FileIdentity> sources = identifySources(semantic_sources);
    const std::filesystem::path input_directory =
        std::filesystem::path(work_directory) / "inputs";
    std::error_code error;
    std::filesystem::create_directories(input_directory, error);
    if(error) { throw workspaceError("cannot create GCSA input artifact directory", input_directory.string()); }
    removeInputPartials(input_directory);

    PersistentGcsaKmers result;
    std::vector<FileIdentity> kmers;
    kmers.reserve(generated_files.size());
    try
    {
        for(std::size_t i = 0; i < generated_files.size(); ++i)
        {
            std::string destination = kmerPath(work_directory, i);
            installGeneratedFile(generated_files[i], destination);
            syncDirectory(input_directory.string());
            FileIdentity identity = identifyFile(destination);
            result.filenames.push_back(identity.path);
            result.bytes += identity.bytes;
            kmers.push_back(std::move(identity));
        }

        std::ostringstream manifest;
        manifest << "version\t" << MANIFEST_VERSION << "\n";
        manifest << "kmer_length\t" << kmer_length << "\n";
        manifest << "source_count\t" << sources.size() << "\n";
        for(const FileIdentity& source : sources)
        {
            manifest << "source\t" << source.bytes << "\t" << source.checksum
                     << "\t" << source.path << "\n";
        }
        manifest << "kmer_count\t" << kmers.size() << "\n";
        for(const FileIdentity& kmer : kmers)
        {
            manifest << "kmer\t" << kmer.bytes << "\t" << kmer.checksum
                     << "\t" << kmer.path << "\n";
        }
        commitManifest(manifestPath(work_directory), manifest.str());
    }
    catch(...)
    {
        // Payloads without the final manifest are uncommitted orphans. They
        // remain available for diagnosis and are safely replaced on retry.
        throw;
    }
    return result;
}

PersistentGcsaKmers
restore_gcsa_kmers(const std::string& work_directory,
    const std::vector<std::string>& semantic_sources, std::size_t kmer_length)
{
    const std::filesystem::path input_directory =
        std::filesystem::path(work_directory) / "inputs";
    removeInputPartials(input_directory);
    const std::string manifest_name = manifestPath(work_directory);
    std::ifstream manifest(manifest_name.c_str());
    if(!manifest) { throw workspaceError("cannot resume without committed k-mer manifest", manifest_name); }
    std::vector<std::string> lines;
    for(std::string line; std::getline(manifest, line);) { lines.push_back(line); }
    if(!manifest.eof()) { throw workspaceError("cannot read k-mer manifest", manifest_name); }

    std::size_t cursor = 0;
    auto nextFields = [&]()
    {
        if(cursor >= lines.size()) { throw workspaceError("truncated k-mer manifest", manifest_name); }
        return splitTabs(lines[cursor++]);
    };
    std::vector<std::string> fields = nextFields();
    if(fields.size() != 2 || fields[0] != "version" ||
       parseUnsigned(fields[1], "version", manifest_name) != MANIFEST_VERSION)
    {
        throw workspaceError("incompatible k-mer manifest version", manifest_name);
    }
    fields = nextFields();
    if(fields.size() != 2 || fields[0] != "kmer_length" ||
       parseUnsigned(fields[1], "k-mer length", manifest_name) != kmer_length)
    {
        throw workspaceError("k-mer length changed while resuming", manifest_name);
    }

    std::vector<FileIdentity> current_sources = identifySources(semantic_sources);
    fields = nextFields();
    if(fields.size() != 2 || fields[0] != "source_count" ||
       parseUnsigned(fields[1], "source count", manifest_name) != current_sources.size())
    {
        throw workspaceError("source graph count changed while resuming", manifest_name);
    }
    for(std::size_t i = 0; i < current_sources.size(); ++i)
    {
        fields = nextFields();
        if(fields.size() != 4 || fields[0] != "source")
        {
            throw workspaceError("invalid source record in k-mer manifest", manifest_name);
        }
        const FileIdentity& source = current_sources[i];
        if(parseUnsigned(fields[1], "source size", manifest_name) != source.bytes ||
           parseUnsigned(fields[2], "source checksum", manifest_name) != source.checksum ||
           normalizedPath(fields[3]) != source.path)
        {
            throw workspaceError("semantic source graph changed while resuming", source.path);
        }
    }

    fields = nextFields();
    if(fields.size() != 2 || fields[0] != "kmer_count")
    {
        throw workspaceError("missing k-mer count in manifest", manifest_name);
    }
    std::uint64_t kmer_count = parseUnsigned(fields[1], "k-mer count", manifest_name);
    if(kmer_count == 0 || kmer_count > static_cast<std::uint64_t>(lines.size()))
    {
        throw workspaceError("invalid k-mer count in manifest", manifest_name);
    }

    PersistentGcsaKmers result;
    result.filenames.reserve(static_cast<std::size_t>(kmer_count));
    for(std::uint64_t i = 0; i < kmer_count; ++i)
    {
        fields = nextFields();
        if(fields.size() != 4 || fields[0] != "kmer")
        {
            throw workspaceError("invalid k-mer record in manifest", manifest_name);
        }
        FileIdentity actual = identifyFile(fields[3]);
        if(parseUnsigned(fields[1], "k-mer size", manifest_name) != actual.bytes ||
           parseUnsigned(fields[2], "k-mer checksum", manifest_name) != actual.checksum)
        {
            throw workspaceError("committed k-mer artifact is corrupt", actual.path);
        }
        result.filenames.push_back(actual.path);
        result.bytes += actual.bytes;
    }
    if(cursor != lines.size()) { throw workspaceError("trailing data in k-mer manifest", manifest_name); }
    return result;
}

} // namespace vg
