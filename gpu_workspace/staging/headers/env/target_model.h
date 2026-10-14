/** @file target_model.h
 *  @brief 目标运动模型配置。
 */
#ifndef ENV_TARGET_MODEL_H
#define ENV_TARGET_MODEL_H

/** @brief 目标模型类型选择。 */
typedef struct TargetModel {
    /** @brief 模型编号，供后续扩展不同目标机动模式。 */
    int model_type;
} TargetModel;

#endif
