// random_search_optimized.cpp - 全力优化 I/O 版本
// 编译: g++ -std=c++17 -O3 -mavx2 -D_GNU_SOURCE -pthread -o random_search_opt random_search_optimized.cpp

#define _GNU_SOURCE         // 启用 posix_fadvise
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <immintrin.h>

// ------------------------------------------------------------
// 全局控制
// ------------------------------------------------------------
static volatile sig_atomic_t g_interrupt = 0;
void signal_handler(int) { g_interrupt = 1; }

// ------------------------------------------------------------
// AVX2 加速搜索（与之前相同，略作优化）
// ------------------------------------------------------------
static ssize_t find_pattern_avx2(const char* data, size_t len,
                                 const char* pattern, size_t pat_len) {
    if (pat_len == 0 || len < pat_len) return -1;

    if (pat_len > 32) {   // 退化
        for (size_t i = 0; i <= len - pat_len; ++i) {
            if (memcmp(data + i, pattern, pat_len) == 0) return i;
        }
        return -1;
    }

    const size_t stride = 32;
    size_t i = 0;
    while (i + stride <= len) {
        __m256i block = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
        __m256i first = _mm256_set1_epi8(pattern[0]);
        __m256i cmp = _mm256_cmpeq_epi8(block, first);
        int mask = _mm256_movemask_epi8(cmp);
        while (mask) {
            int bit = __builtin_ctz(mask);
            size_t pos = i + bit;
            if (memcmp(data + pos, pattern, pat_len) == 0) return pos;
            mask &= mask - 1;
        }
        i += stride;
    }
    for (size_t j = i; j <= len - pat_len; ++j) {
        if (memcmp(data + j, pattern, pat_len) == 0) return j;
    }
    return -1;
}

// ------------------------------------------------------------
// 主程序（优化 I/O）
// ------------------------------------------------------------
int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);

    std::string target_str = (argc > 1) ? argv[1] : "蔚蓝档案";
    std::vector<char> target_bytes(target_str.begin(), target_str.end());
    const char* pattern = target_bytes.data();
    size_t pat_len = target_bytes.size();

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1) {
        std::cerr << "Failed to open /dev/urandom\n";
        return 1;
    }

    // 1. 建议内核顺序读取（预读）
    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);

    std::cout << "Searching in /dev/urandom, target: '" << target_str << "'\n";
    std::cout << "Press Ctrl+C to stop.\n" << std::endl;

    // 2. 超大块：4MB（一次读取）
    const size_t CHUNK_SIZE = 4 * 1024 * 1024;   // 4 MB
    const size_t WINDOW = pat_len > 0 ? pat_len - 1 : 0;

    // 3. 预先分配缓冲区（避免反复分配）
    std::vector<char> buffer(CHUNK_SIZE);
    std::vector<char> tail;
    tail.reserve(WINDOW);

    size_t chunks_read = 0;
    size_t total_bytes = 0;
    size_t last_printed_bytes = 0;
    const size_t PRINT_INTERVAL = 200 * 1024 * 1024;   // 每 200MB 打印一次

    auto start_time = std::chrono::steady_clock::now();

    while (!g_interrupt) {
        ssize_t bytes_read = read(fd, buffer.data(), CHUNK_SIZE);
        if (bytes_read <= 0) break;

        chunks_read++;
        total_bytes += bytes_read;

        // 构建组合（避免拷贝，使用 vector 拼接）
        std::vector<char> combined;
        combined.reserve(tail.size() + bytes_read);
        combined.insert(combined.end(), tail.begin(), tail.end());
        combined.insert(combined.end(), buffer.data(), buffer.data() + bytes_read);

        // 搜索
        ssize_t found = find_pattern_avx2(combined.data(), combined.size(),
                                          pattern, pat_len);
        if (found != -1) {
            std::cout << "\n✅ Target found! Attempts: " << chunks_read
                      << " chunks (" << total_bytes << " bytes).\n";
            close(fd);
            return 0;
        }

        // 更新 tail（使用 assign 或直接拷贝）
        if (bytes_read >= WINDOW) {
            tail.assign(buffer.data() + bytes_read - WINDOW,
                        buffer.data() + bytes_read);
        } else {
            size_t keep = WINDOW - bytes_read;
            if (keep > tail.size()) keep = tail.size();
            std::vector<char> new_tail;
            new_tail.reserve(WINDOW);
            new_tail.insert(new_tail.end(), tail.end() - keep, tail.end());
            new_tail.insert(new_tail.end(), buffer.data(), buffer.data() + bytes_read);
            tail.swap(new_tail);
        }

        // 4. 降低进度打印频率（每 200MB 或每 5 秒）
        if (total_bytes - last_printed_bytes >= PRINT_INTERVAL) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start_time).count();
            double speed = total_bytes / (1024.0 * 1024.0) / elapsed;   // MB/s
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "\rRead: %.2f GB, chunks: %zu, speed: %.1f MB/s",
                     total_bytes / (1024.0 * 1024.0 * 1024.0),
                     chunks_read, speed);
            write(STDOUT_FILENO, buf, strlen(buf));
            last_printed_bytes = total_bytes;
        }
    }

    // 中断或结束
    std::cout << "\n\n⏹️  Interrupted. Total: " << chunks_read
              << " chunks (" << total_bytes << " bytes).\n";
    close(fd);
    return 0;
}
