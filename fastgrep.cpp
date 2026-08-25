// fastgrep.cpp - 使用 AVX2 加速的 grep 替代品
// 编译: g++ -std=c++17 -O3 -mavx2 -D_GNU_SOURCE -pthread -o fastgrep fastgrep.cpp

#define _GNU_SOURCE
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <immintrin.h>

// ------------------------------------------------------------
// AVX2 加速搜索（与之前相同）
// ------------------------------------------------------------
static ssize_t find_pattern_avx2(const char* data, size_t len,
                                 const char* pattern, size_t pat_len) {
    if (pat_len == 0 || len < pat_len) return -1;

    if (pat_len > 32) {
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
// 主程序
// ------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <pattern> [<file>]\n"
                  << "If file is omitted, reads from stdin.\n";
        return 2;
    }

    const std::string pattern_str = argv[1];
    const std::vector<char> pattern_bytes(pattern_str.begin(), pattern_str.end());
    const char* pattern = pattern_bytes.data();
    size_t pat_len = pattern_bytes.size();

    int fd = 0;   // 默认 stdin
    bool use_stdin = (argc == 2);
    if (!use_stdin) {
        fd = open(argv[2], O_RDONLY);
        if (fd == -1) {
            std::cerr << "Cannot open file: " << argv[2] << "\n";
            return 2;
        }
        // 建议顺序读取
        posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    }

    // 大块 I/O
    const size_t CHUNK_SIZE = 16 * 1024 * 1024;   // 16 MB
    const size_t WINDOW = pat_len > 0 ? pat_len - 1 : 0;

    std::vector<char> buffer(CHUNK_SIZE);
    std::vector<char> tail;
    tail.reserve(WINDOW);

    size_t total_bytes = 0;
    bool found = false;

    while (true) {
        ssize_t bytes_read = read(fd, buffer.data(), CHUNK_SIZE);
        if (bytes_read <= 0) break;

        total_bytes += bytes_read;

        // 构建组合
        std::vector<char> combined;
        combined.reserve(tail.size() + bytes_read);
        combined.insert(combined.end(), tail.begin(), tail.end());
        combined.insert(combined.end(), buffer.data(), buffer.data() + bytes_read);

        ssize_t pos = find_pattern_avx2(combined.data(), combined.size(),
                                        pattern, pat_len);
        if (pos != -1) {
            // 计算在原始文件中的绝对偏移
            size_t abs_offset = total_bytes - bytes_read + pos - tail.size();
            std::cout << "Found at byte offset " << abs_offset << "\n";
            found = true;
            break;
        }

        // 更新 tail
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

        // 可选进度（对于大文件）
        // 每 1GB 打印一次进度
        if (total_bytes % (1024 * 1024 * 1024) == 0) {
            std::cerr << "\rProcessed: " << (total_bytes >> 30) << " GB" << std::flush;
        }
    }

    if (!use_stdin) close(fd);

    if (!found) {
        std::cerr << "Pattern not found.\n";
        return 1;
    }
    return 0;
}
