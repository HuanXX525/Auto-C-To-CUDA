#include <cstdio>
#include <cstdlib>

#include "common/ring_buffer.h"

int main() {
    int storage[3] = {0, 0, 0};
    RingBufferView buffer{};
    int value = 0;
    if (ring_buffer_init(&buffer, storage, 3u, sizeof(storage[0])) != SIM_OK) return 1;
    const int inputs[] = {10, 20, 30, 40};
    for (int index = 0; index < 4; ++index) {
        if (ring_buffer_push(&buffer, &inputs[index]) != SIM_OK) return 2;
    }
    if (ring_buffer_count(&buffer) != 3u ||
        ring_buffer_get(&buffer, 0u, &value) != SIM_OK || value != 20 ||
        ring_buffer_get_delayed(&buffer, 1u, &value) != SIM_OK || value != 30) {
        return 3;
    }
    std::printf("ring_buffer_probe: PASS offset=%lld\n",
                static_cast<long long>(buffer.data_offset));
    return EXIT_SUCCESS;
}
