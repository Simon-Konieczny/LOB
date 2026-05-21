//
// Created by Simon Konieczny on 21/05/2026.
//

#ifndef LOB_MMAPREADER_HPP
#define LOB_MMAPREADER_HPP

#endif

#pragma once
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>
#include <string>

class MmapReader {
    int fd;
    size_t fileSize;
    const char* mappedData;

public:
    MmapReader(const std::string& filepath) {
        fd = open(filepath.c_str(), O_RDONLY);
        if (fd < 0) throw std::runtime_error("Failed to open ITCH file");

        struct stat sb;
        if (fstat(fd, &sb) < 0) throw std::runtime_error("Failed to stat file");
        fileSize = sb.st_size;

        // Map the file into memory
        mappedData = static_cast<const char*>(mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0));
        if (mappedData == MAP_FAILED) throw std::runtime_error("mmap failed");
    }

    ~MmapReader() {
        if (mappedData != MAP_FAILED) munmap((void*)mappedData, fileSize);
        if (fd >= 0) close(fd);
    }

    const char* data() const { return mappedData; }
    size_t size() const { return fileSize; }
};