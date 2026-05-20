#include "tool.h"
#include <chrono>

void save_binary(const char *filename, void *data, size_t byte_size)
{
    FILE *fp = fopen(filename, "wb");
    if (fp)
    {
        fwrite(data, 1, byte_size, fp);
        fclose(fp);
    }
}

double now_ms(void)
{
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(now - start).count();
}