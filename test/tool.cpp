#include "tool.h"
void save_binary(const char *filename, void *data, size_t byte_size)
{
    FILE *fp = fopen(filename, "wb");
    if (fp)
    {
        fwrite(data, 1, byte_size, fp);
        fclose(fp);
    }
}