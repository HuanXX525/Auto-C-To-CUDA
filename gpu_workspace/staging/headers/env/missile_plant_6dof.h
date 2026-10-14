/** @file missile_plant_6dof.h
 *  @brief 导弹 6DOF 真实状态容器。
 *
 *  该结构承载环境程序中导弹动力学积分所需的完整状态，包括位置、速度、
 *  姿态、角速度、力矩、质量和执行机构状态。它是环境模型最核心的数据结构。
 */
#ifndef ENV_MISSILE_PLANT_6DOF_H
#define ENV_MISSILE_PLANT_6DOF_H

#include "common/matrix3.h"
#include "common/protocol.h"
#include "common/quaternion.h"
#include "common/vec3.h"
#include "common/status.h"
#include "common/device_qualifiers.h"

/** @brief 六自由度导弹状态。 */
typedef struct PlantState6Dof {
    /** @brief 仿真时间，单位秒。 */
    double time;
    /** @brief 地心地固坐标位置，单位米。 */
    Vec3 pos_ecef;
    /** @brief 地心地固坐标速度，单位米每秒。 */
    Vec3 vel_ecef;
    /** @brief 地心地固坐标加速度，单位米每二次方秒。 */
    Vec3 accel_ecef;
    /** @brief 纬度，单位弧度。 */
    double lat_rad;
    /** @brief 经度，单位弧度。 */
    double lon_rad;
    /** @brief 椭球高或球高，单位米。 */
    double height_m;
    /** @brief 离地高度，单位米。 */
    double height_agl_m;
    /** @brief 机体系相对惯性系的姿态四元数。 */
    Quat q_bi;
    /** @brief 机体系角速度，单位弧度每秒。 */
    Vec3 omega_b;
    /** @brief 机体系角加速度，单位弧度每二次方秒。 */
    Vec3 alpha_b;
    /** @brief 当前质量，单位千克。 */
    double mass;
    /** @brief 机体系惯性矩阵。 */
    Matrix3 inertia_b;
    /** @brief 机体系合外力。 */
    Vec3 force_b;
    /** @brief 机体系合外力矩。 */
    Vec3 moment_b;
    /** @brief 各执行机构当前偏角。 */
    double actuator_pos[SIM_MAX_ACTUATORS];
    /** @brief 各执行机构偏角速率。 */
    double actuator_rate[SIM_MAX_ACTUATORS];
} PlantState6Dof;

/** @brief 数值积分器类型。 */
typedef enum IntegratorType {
    INTEGRATOR_EULER = 1,
    INTEGRATOR_RK2 = 2,
    INTEGRATOR_RK4 = 3
} IntegratorType;

/** @brief 单步积分期间保持常值的六自由度外部输入。 */
typedef struct PlantInput6Dof {
    Vec3 force_b_n;
    Vec3 moment_b_nm;
    Vec3 gravity_ecef_mps2;
    Vec3 earth_rotation_ecef_radps;
    int enable_earth_rotation;
} PlantInput6Dof;

/** @brief 校验六自由度状态、质量、惯量和姿态。 */
C2CUDA_HOST_DEVICE SimStatus missile_plant_validate(const PlantState6Dof *state);
/** @brief 使用指定积分器推进六自由度刚体状态。 */
C2CUDA_HOST_DEVICE SimStatus missile_plant_step(
    PlantState6Dof *state,
    const PlantInput6Dof *input,
    double dt,
    IntegratorType integrator);

#endif
