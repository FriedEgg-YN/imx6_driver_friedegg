#ifndef SENSOR_WORKER_H
#define SENSOR_WORKER_H

#include "sensor_types.h"

#include <QObject>

class QTimer;

namespace smartmonitor {

/**
 * @brief 在 Sensor I/O 线程中执行周期采样的 Worker 对象。
 *
 * 对象由 CompositionRoot 在 GUI 线程创建且不设置 parent，随后移动到
 * Sensor I/O 线程。采样定时器在 startSampling() 中创建，确保定时器与
 * Worker 具有相同的线程 affinity。所有公开槽均应通过 queued connection 调用。
 */
class SensorWorker final : public QObject
{
    Q_OBJECT

public:
    explicit SensorWorker(QObject *parent = nullptr);

public slots:
    /** @brief 在 Worker 线程中创建并启动周期采样定时器。 */
    void startSampling();

    /** @brief 在 Worker 线程中停止定时器并释放采样资源。 */
    void stopSampling();

private slots:
    /** @brief 执行一次有界模拟采样并立即返回事件循环。 */
    void sampleOnce();

signals:
    /** @brief Worker 已进入采样状态。 */
    void samplingStarted();

    /** @brief Worker 已停止采样并完成资源清理。 */
    void samplingStopped();

    /** @brief Worker 产生了一份拥有自身数据的采样结果。 */
    void sampleProduced(const FakeSample &sample);

private:
    QTimer *m_timer = nullptr;
    int m_sequence = 0;
};

} // namespace smartmonitor

#endif // SENSOR_WORKER_H
