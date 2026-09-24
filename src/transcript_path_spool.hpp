#ifndef VG_TRANSCRIPT_PATH_SPOOL_HPP_INCLUDED
#define VG_TRANSCRIPT_PATH_SPOOL_HPP_INCLUDED

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <type_traits>
#include <unistd.h>
#include <vector>

namespace vg {

/// An append-only, disk-backed arena for one trivially-copyable path-step type.
/// Appends are externally serialized and reads begin after writers finish. The
/// caller owns path descriptors and the file name; this class never deletes the
/// file, including after a failed write.
template<typename Record>
class TranscriptPathSpool {
    static_assert(std::is_trivially_copyable<Record>::value,
                  "TranscriptPathSpool records must be trivially copyable");

public:
    struct Span {
        uint64_t begin = 0;
        uint64_t count = 0;
    };

    explicit TranscriptPathSpool(const std::string& filename) : filename_(filename) {
        fd_ = ::open(filename.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd_ < 0) {
            throw system_error("create");
        }
        try {
            const uint64_t header[] = { magic, sizeof(Record) };
            write_exact(header, sizeof(header), 0);
        }
        catch (...) {
            ::close(fd_);
            fd_ = -1;
            throw;
        }
    }

    ~TranscriptPathSpool() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    TranscriptPathSpool(const TranscriptPathSpool&) = delete;
    TranscriptPathSpool& operator=(const TranscriptPathSpool&) = delete;

    Span append(const Record* records, uint64_t count) {
        if (count != 0 && records == nullptr) {
            throw std::invalid_argument("cannot append null records");
        }
        if (count > std::numeric_limits<uint64_t>::max() - record_count_) {
            throw_overflow("record count overflows uint64_t");
        }
        const Span result{ record_count_, count };
        const uint64_t bytes = checked_record_bytes(count);
        checked_offset(result.begin);
        checked_offset(record_count_ + count);
        if (count != 0) {
            write_exact(records, bytes, byte_offset(result.begin));
        }
        record_count_ += count;
        return result;
    }

    void read(const Span& span, uint64_t relative_begin, uint64_t count, Record* output) const {
        validate_span(span);
        if (relative_begin > span.count || count > span.count - relative_begin) {
            throw std::out_of_range("record range is outside its spool span");
        }
        if (count != 0 && output == nullptr) {
            throw std::invalid_argument("cannot read into null records");
        }
        if (count != 0) {
            read_exact(output, checked_record_bytes(count),
                       byte_offset(span.begin + relative_begin));
        }
    }

    template<typename Callback>
    void for_each_chunk(const Span& span, uint64_t max_records, Callback&& callback) const {
        validate_span(span);
        if (max_records == 0) {
            throw std::invalid_argument("chunk size must be nonzero");
        }
        const uint64_t chunk_records = std::min(max_records, span.count);
        if (chunk_records == 0) {
            return;
        }
        std::vector<Record> buffer(checked_vector_size(chunk_records));
        for (uint64_t relative_begin = 0; relative_begin < span.count;) {
            const uint64_t count = std::min(chunk_records, span.count - relative_begin);
            read(span, relative_begin, count, buffer.data());
            callback(buffer.data(), count);
            relative_begin += count;
        }
    }

    /// Direct writes are immediately visible to this file descriptor; sync makes
    /// their durable completion explicit for a caller that needs a receipt.
    void sync() const {
        if (::fsync(fd_) != 0) {
            throw system_error("fsync");
        }
    }

    void flush() const {
        sync();
    }

    uint64_t record_count() const {
        return record_count_;
    }

    const std::string& filename() const {
        return filename_;
    }

private:
    static constexpr uint64_t magic = UINT64_C(0x545053504f4f4c31); // "TPSPOOL1"
    static constexpr uint64_t header_bytes = 2 * sizeof(uint64_t);

    std::string filename_;
    int fd_ = -1;
    uint64_t record_count_ = 0;

    [[noreturn]] static void throw_overflow(const char* message) {
        throw std::overflow_error(message);
    }

    static size_t checked_vector_size(uint64_t count) {
        if (count > std::numeric_limits<size_t>::max()) {
            throw_overflow("record count exceeds addressable memory");
        }
        return static_cast<size_t>(count);
    }

    static uint64_t checked_record_bytes(uint64_t count) {
        if (count > std::numeric_limits<uint64_t>::max() / sizeof(Record)) {
            throw_overflow("record byte count overflows uint64_t");
        }
        const uint64_t bytes = count * sizeof(Record);
        if (bytes > std::numeric_limits<size_t>::max()) {
            throw_overflow("record byte count exceeds addressable memory");
        }
        return bytes;
    }

    static ::off_t checked_offset(uint64_t record_begin) {
        if (record_begin > (std::numeric_limits<uint64_t>::max() - header_bytes) / sizeof(Record)) {
            throw_overflow("record offset overflows uint64_t");
        }
        const uint64_t offset = header_bytes + record_begin * sizeof(Record);
        if (offset > static_cast<uint64_t>(std::numeric_limits<::off_t>::max())) {
            throw_overflow("record offset exceeds off_t");
        }
        return static_cast<::off_t>(offset);
    }

    static ::off_t byte_offset(uint64_t record_begin) {
        return checked_offset(record_begin);
    }

    void validate_span(const Span& span) const {
        if (span.begin > record_count_ || span.count > record_count_ - span.begin) {
            throw std::out_of_range("spool span is outside appended records");
        }
    }

    void write_exact(const void* data, uint64_t bytes, ::off_t offset) {
        const char* cursor = static_cast<const char*>(data);
        uint64_t done = 0;
        while (done < bytes) {
            const size_t remaining = static_cast<size_t>(bytes - done);
            const ssize_t written = ::pwrite(fd_, cursor + done, remaining, offset + static_cast<::off_t>(done));
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw system_error("pwrite");
            }
            if (written == 0) {
                throw std::runtime_error("pwrite made no progress");
            }
            done += static_cast<uint64_t>(written);
        }
    }

    void read_exact(void* data, uint64_t bytes, ::off_t offset) const {
        char* cursor = static_cast<char*>(data);
        uint64_t done = 0;
        while (done < bytes) {
            const size_t remaining = static_cast<size_t>(bytes - done);
            const ssize_t read_count = ::pread(fd_, cursor + done, remaining, offset + static_cast<::off_t>(done));
            if (read_count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw system_error("pread");
            }
            if (read_count == 0) {
                throw std::runtime_error("truncated transcript path spool");
            }
            done += static_cast<uint64_t>(read_count);
        }
    }

    std::runtime_error system_error(const char* operation) const {
        return std::runtime_error(std::string(operation) + " " + filename_ + ": " + std::strerror(errno));
    }
};

} // namespace vg

#endif
