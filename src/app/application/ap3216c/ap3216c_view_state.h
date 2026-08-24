#ifndef AP3216C_VIEW_STATE_H
#define AP3216C_VIEW_STATE_H

#include "device/sensor/sensor_types.h"
#include "device/sensor/ap3216c_backend.h"

#include <QString>

namespace smartmonitor {

/**
 * @brief AP3216C 页面一次完整渲染所需的只读状态。
 *
 * Controller 负责组装该值类型，Page 只根据字段更新控件，不访问 Service
 * 或 Worker。hasSample 用于区分尚未采样和一次无效采样。
 */
struct Ap3216cViewState
{
    SamplingState samplingState = SamplingState::Idle;
    bool hasSample = false;
    Ap3216cSample sample;
};

} // namespace smartmonitor

#endif // AP3216C_VIEW_STATE_H
