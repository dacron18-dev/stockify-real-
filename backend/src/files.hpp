// File handling: text log (ofstream/ifstream) and binary audit file with random access.
#pragma once
#include <cstring>
#include <fstream>
#include "core.hpp"

class FileLogger {
    std::string path_;
    std::ofstream out_;
    std::mutex m_;
    RingBuffer<std::string, 50> recent_;                    // template with non-type parameter
public:
    explicit FileLogger(std::string path) : path_(std::move(path)), out_(path_, std::ios::app) {}
    ~FileLogger() { if (out_.is_open()) out_.close(); }
    void write(const std::string& line) {
        std::lock_guard<std::mutex> g(m_);
        recent_.push(line);
        if (out_) out_ << line << '\n' << std::flush;       // appending
    }
    std::vector<std::string> recent() const { return recent_.items(); }
    std::vector<std::string> tail(std::size_t n) const {    // reading the file back
        std::ifstream in(path_);
        std::vector<std::string> lines;
        for (std::string l; std::getline(in, l);) lines.push_back(l);
        if (lines.size() > n) lines.erase(lines.begin(), lines.end() - static_cast<std::ptrdiff_t>(n));
        return lines;
    }
};

struct AuditRecord {                                        // fixed-size record for binary I/O
    char symbol[16];
    char side;
    int qty;
    double price;
    long long ts;
};

class AuditFile {
    std::string path_;
    mutable std::mutex m_;
public:
    explicit AuditFile(std::string path) : path_(std::move(path)) {}
    void append(const AuditRecord& r) {
        std::lock_guard<std::mutex> g(m_);
        std::ofstream f(path_, std::ios::binary | std::ios::app);
        f.write(reinterpret_cast<const char*>(&r), sizeof r);          // reinterpret_cast
    }
    std::size_t count() const {                             // tellg() on end-of-file = size
        std::lock_guard<std::mutex> g(m_);
        std::ifstream f(path_, std::ios::binary | std::ios::ate);
        if (!f) return 0;
        return static_cast<std::size_t>(f.tellg()) / sizeof(AuditRecord);
    }
    std::optional<AuditRecord> read(std::size_t i) const {  // random access with seekg()
        std::lock_guard<std::mutex> g(m_);
        std::ifstream f(path_, std::ios::binary);
        f.seekg(static_cast<std::streamoff>(i * sizeof(AuditRecord)));
        AuditRecord r{};
        if (!f.read(reinterpret_cast<char*>(&r), sizeof r)) return std::nullopt;
        return r;
    }
};
