/** @file environment_force_model.h
 *  @brief 环境力、力矩和重力的统一汇总接口。
 *
 *  该模块位于执行机构与六自由度积分器之间。它将飞控加速度级虚拟接口、
 *  气动力、推进力和重力组合为 PlantInput6Dof，避免各物理模型直接修改
 *  刚体状态。所有输入输出均为单个仿真实例私有状态，不包含跨实例共享状态。
 */
#ifndef ENV_ENVIRONMENT_FORCE_MODEL_H
#define ENV_ENVIRONMENT_FORCE_MODEL_H

#include "common/status.h"
#include "common/vec3.h"
#include "env/aero_model.h"
#include "env/atmosphere_model.h"
#include "env/gravity_model.h"
#include "env/missile_plant_6dof.h"
#include "env/propulsion_model.h"

/** @brief WGS-84 地球自转角速度，单位 rad/s。 */
#define ENV_WGS84_EARTH_ROTATION_RADPS 7.2921150e-5

/** @brief 环境力模型的只读配置快照。 */
typedef struct EnvironmentForceModel {
    /** @brief 中心引力模型。 */
    GravityModel gravity;
    /** @brief 标准大气模型。 */
    AtmosphereModel atmosphere;
    /** @brief 推进系统模型。 */
    PropulsionModel propulsion;
    /** @brief 气动力和气动力矩模型。 */
    AeroModel aerodynamics;
    /** @brief ECEF 风速，单位 m/s。 */
    Vec3 wind_velocity_ecef_mps;
    /** @brief 是否在积分器中启用科里奥利和离心项。 */
    int enable_earth_rotation;
    /** @brief 地球自转角速度，单位 rad/s。 */
    double earth_rotation_rate_radps;
} EnvironmentForceModel;

/** @brief 单步力模型计算所需的状态和执行机构输出。 */
typedef struct EnvironmentForceInput {
    /** @brief 当前六自由度刚体真值，只读。 */
    const PlantState6Dof *plant;
    /** @brief 当前椭球高，单位 m。 */
    double height_m;
    /** @brief 虚拟执行机构输出的 ECEF 加速度，单位 m/s^2。 */
    Vec3 virtual_acceleration_ecef_mps2;
    /** @brief 俯仰控制面等效偏角，单位 rad。 */
    double pitch_actuator_rad;
    /** @brief 偏航控制面等效偏角，单位 rad。 */
    double yaw_actuator_rad;
    /** @brief 当前可用推进剂质量，单位 kg。 */
    double propellant_mass_kg;
    /** @brief 当前积分步长，单位 s。 */
    double dt_s;
} EnvironmentForceInput;

/** @brief 力模型计算结果和诊断分量。 */
typedef struct EnvironmentForceOutput {
    /** @brief 直接提供给六自由度积分器的输入。 */
    PlantInput6Dof plant_input;
    /** @brief 当前大气状态。 */
    AtmosphereState atmosphere;
    /** @brief 虚拟加速度对应的机体系等效力，单位 N。 */
    Vec3 virtual_force_b_n;
    /** @brief 气动力，单位 N。 */
    Vec3 aerodynamic_force_b_n;
    /** @brief 气动力矩，单位 N*m。 */
    Vec3 aerodynamic_moment_b_nm;
    /** @brief 气动模型诊断标志，例如表格外推或钳制。 */
    uint32_t aerodynamic_model_flags;
    /** @brief 推进力，单位 N。 */
    Vec3 propulsion_force_b_n;
    /** @brief 本步有效推进剂质量流量，单位 kg/s。 */
    double mass_flow_kgps;
} EnvironmentForceOutput;

/** @brief 校验力模型配置中的有限值、范围和方向向量。 */
SimStatus environment_force_model_validate(const EnvironmentForceModel *model);

/** @brief 汇总当前仿真步的机体系力、力矩和 ECEF 重力输入。 */
SimStatus environment_force_model_evaluate(
    const EnvironmentForceModel *model,
    const EnvironmentForceInput *input,
    EnvironmentForceOutput *output);

#endif
