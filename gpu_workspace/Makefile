CUDA_ARCH ?= sm_120
NVCC ?= /usr/local/cuda-13.2/bin/nvcc
NVCCFLAGS ?= -std=c++14 -arch=$(CUDA_ARCH)
INCLUDES := -Igpu_campaign/include -Istaging/headers -Iconverted

COMMON_SRCS := converted/common/vec3.cu \
               converted/common/matrix3.cu \
               converted/common/quaternion.cu \
               converted/common/random.cu \
               converted/common/ring_buffer.cu
ENV_SRCS := converted/environment/missile_plant_6dof.cu \
            converted/environment/actuator_model.cu \
            converted/environment/aero_surrogate_device.cu \
            converted/environment/atmosphere_model.cu \
            converted/environment/fault_injection.cu \
            converted/environment/geo_coordinate.cu \
            converted/environment/earth_model.cu \
            converted/environment/gravity_model.cu \
            converted/environment/mass_model.cu \
            converted/environment/propulsion_model.cu \
            converted/environment/sensor_noise.cu \
            converted/environment/sensor_vector3.cu \
            converted/environment/sensor_imu.cu \
            converted/environment/sensor_accel.cu \
            converted/environment/sensor_speed.cu \
            converted/environment/sensor_seeker.cu
FC_SRCS := converted/flight_control/autopilot.cu \
           converted/flight_control/command_manager.cu \
           converted/flight_control/estimator.cu \
           converted/flight_control/fc_health.cu \
           converted/flight_control/fc_modes.cu \
           converted/flight_control/fc_scheduler.cu \
           converted/flight_control/fc_state.cu \
           converted/flight_control/guidance_manager.cu \
           converted/flight_control/guidance_png.cu \
           converted/flight_control/navigation.cu \
           converted/flight_control/safety_monitor.cu
CAMPAIGN_SRCS := gpu_campaign/src/simulate_step.cu \
                 gpu_campaign/src/production_step.cu \
                 gpu_campaign/src/production_runner.cu \
                 gpu_campaign/src/main.cu
SRCS := $(COMMON_SRCS) $(ENV_SRCS) $(FC_SRCS) $(CAMPAIGN_SRCS)
OBJS := $(patsubst %.cu,build/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)
TARGET := build/bin/gpu_campaign
HOST_OBJS := build/host_environment_packer.o \
             build/host_scenario_mapper.o build/host_campaign_config.o \
             build/production_output.o \
             build/config_host.o \
             build/fault_injection_loader_host.o \
             build/aero_database_host.o build/map_tile_host.o build/crc32_host.o \
             build/protocol_host.o build/packet_host.o build/vec3_c_host.o

.PHONY: all generate clean

all: $(TARGET)

generate:
	./tools/convert_minimal.sh
	./tools/convert_environment_probe.sh
	./tools/convert_flight_control.sh

$(TARGET): $(OBJS) $(HOST_OBJS)
	@mkdir -p $(dir $@)
	$(NVCC) $(NVCCFLAGS) $(OBJS) $(HOST_OBJS) -o $@

build/%.o: %.cu
	@mkdir -p $(dir $@)
	$(NVCC) $(NVCCFLAGS) $(INCLUDES) -dc $< -o $@ -MD -MP -MF $(@:.o=.d)

build/host_environment_packer.o: gpu_campaign/src/host_environment_packer.cpp
	@mkdir -p $(dir $@)
	$(NVCC) $(NVCCFLAGS) $(INCLUDES) -dc $< -o $@

build/host_scenario_mapper.o: gpu_campaign/src/host_scenario_mapper.cpp
	@mkdir -p $(dir $@)
	g++ -std=c++14 -Igpu_campaign/include -Istaging/headers -c $< -o $@

build/host_campaign_config.o: gpu_campaign/src/host_campaign_config.cpp
	@mkdir -p $(dir $@)
	g++ -std=c++14 -Igpu_campaign/include -Istaging/headers -c $< -o $@

build/production_output.o: gpu_campaign/src/production_output.cpp
	@mkdir -p $(dir $@)
	g++ -std=c++14 -Igpu_campaign/include -Istaging/headers -c $< -o $@

build/config_host.o: ../external/Aircraft-Simu/common/src/config.c
	@mkdir -p $(dir $@)
	gcc -std=c11 -Istaging/headers -c $< -o $@

build/fault_injection_loader_host.o: staging/environment/fault_injection_loader.c
	@mkdir -p $(dir $@)
	g++ -std=c++14 -x c++ -Istaging/headers -c $< -o $@

build/aero_database_host.o: staging/environment_hostonly/aero_database.c
	@mkdir -p $(dir $@)
	gcc -std=c11 -Istaging/headers -c $< -o $@

build/map_tile_host.o: ../external/Aircraft-Simu/environment_sim/src/map_tile.c
	@mkdir -p $(dir $@)
	gcc -std=c11 -Istaging/headers -c $< -o $@

build/crc32_host.o: ../external/Aircraft-Simu/common/src/crc32.c
	@mkdir -p $(dir $@)
	gcc -std=c11 -Istaging/headers -c $< -o $@

build/protocol_host.o: ../external/Aircraft-Simu/common/src/protocol.c
	@mkdir -p $(dir $@)
	gcc -std=c11 -Istaging/headers -c $< -o $@

build/packet_host.o: ../external/Aircraft-Simu/common/src/packet.c
	@mkdir -p $(dir $@)
	gcc -std=c11 -Istaging/headers -c $< -o $@

# packet.c is intentionally compiled as C to preserve the CPU wire protocol ABI.
# The CUDA vec3 object exports C++ symbols, so provide the C ABI dependency too.
build/vec3_c_host.o: ../external/Aircraft-Simu/common/src/vec3.c
	@mkdir -p $(dir $@)
	gcc -std=c11 -Istaging/headers -c $< -o $@

clean:
	rm -rf build/bin/gpu_campaign build/converted build/gpu_campaign \
		build/host_environment_packer.o build/host_scenario_mapper.o \
		build/host_campaign_config.o \
		build/production_output.o \
		build/config_host.o build/fault_injection_loader_host.o \
		build/aero_database_host.o build/map_tile_host.o build/crc32_host.o \
		build/protocol_host.o build/packet_host.o build/vec3_c_host.o

-include $(DEPS)
