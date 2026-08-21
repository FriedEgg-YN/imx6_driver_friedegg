#ifndef SENSOR_SERVICE_H
#define SENSOR_SERVICE_H

#include "sensor_types.h"

#include <QObject>

namespace smartmonitor {

/**
 * @brief GUI 线程中的传感器服务门面。
 *
 * public API 同步返回命令是否受理；Worker 的最终启停状态和采样结果通过
 * 类型化信号异步发布。Service 不执行设备 I/O，也不拥有或直接调用 Worker。
 * CompositionRoot 负责将内部命令信号连接到 Worker 的槽函数。
 */
class SensorService final : public QObject
{
    Q_OBJECT

public:
    explicit SensorService(QObject *parent = nullptr);

    /** @brief 请求开始采样；返回值只表示命令是否受理。 */
    OperationResult requestStartSampling();

    /** @brief 请求停止采样；返回值只表示命令是否受理。 */
    OperationResult requestStopSampling();

signals:
    /** @brief 请求 SensorWorker 在其线程中开始采样。 */
    void workerStartRequested();

    /** @brief 请求 SensorWorker 在其线程中停止采样。 */
    void workerStopRequested();

    /** @brief 对订阅者发布新的传感器采样结果。 */
    void sampleUpdated(const FakeSample &sample);

    /** @brief 对订阅者发布采样已开始的最终状态。 */
    void samplingStarted();

    /** @brief 对订阅者发布采样已停止的最终状态。 */
    void samplingStopped();

public slots:
    /** @brief 处理 Worker 已开始采样的通知。 */
    void handleWorkerStarted();

    /** @brief 处理并转发 Worker 产生的采样结果。 */
    void handleWorkerSample(const FakeSample &sample);

    /** @brief 处理 Worker 已停止采样的通知。 */
    void handleWorkerStopped();

private:
    SamplingState m_samplingState = SamplingState::Idle;
};

} // namespace smartmonitor

#endif // SENSOR_SERVICE_H
