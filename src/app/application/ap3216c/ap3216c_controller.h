#ifndef AP3216C_CONTROLLER_H
#define AP3216C_CONTROLLER_H

#include "ap3216c_view_state.h"
#include "device/sensor/sensor_service.h"

#include <QObject>

namespace smartmonitor {

/**
 * @brief GUI 线程中的 AP3216C 页面控制器。
 *
 * Controller 接收 Page 的用户意图，调用进程级 SensorService，并根据同步
 * 受理结果和异步采样事件组装 Ap3216cViewState。Controller 不执行设备 I/O，
 * 也不拥有 SensorService；调用方必须保证 Service 的生命周期更长。
 */
class Ap3216cController final : public QObject
{
    Q_OBJECT

public:
    explicit Ap3216cController(SensorService *sensorService,
                               QObject *parent = nullptr);

    /** @brief 发布当前完整 ViewState，用于连接完成后的首次渲染。 */
    void publishCurrentViewState();

public slots:
    /** @brief 处理 Page 的开始采样请求。 */
    void requestStart();

    /** @brief 处理 Page 的停止采样请求。 */
    void requestStop();

    /** @brief 处理 Service 已开始采样的异步通知。 */
    void handleSamplingStarted();

    /** @brief 处理 Service 发布的新采样结果。 */
    void handleSampleUpdated(const FakeSample &sample);

    /** @brief 处理 Service 已停止采样的异步通知。 */
    void handleSamplingStopped();

signals:
    /** @brief 发布 Page 一次渲染所需的完整状态。 */
    void viewStateChanged(const Ap3216cViewState &viewState);

private:
    void publishViewState();

    Ap3216cViewState m_viewState;
    SensorService *m_sensorService;
};

} // namespace smartmonitor

#endif // AP3216C_CONTROLLER_H
