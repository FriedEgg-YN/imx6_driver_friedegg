#ifndef AP3216C_PAGE_H
#define AP3216C_PAGE_H

#include "ap3216c_view_state.h"

#include <QWidget>

class QLabel;
class QPushButton;

namespace smartmonitor {

/**
 * @brief GUI 线程中的 AP3216C 页面。
 *
 * Page 只把按钮操作转换为用户意图信号，并根据 Ap3216cViewState 更新控件。
 * 它不创建 Controller，不访问 Service、Worker 或设备接口。
 */
class Ap3216cPage final : public QWidget
{
    Q_OBJECT

public:
    explicit Ap3216cPage(QWidget *parent = nullptr);

signals:
    /** @brief 用户请求开始采样。 */
    void startRequested();

    /** @brief 用户请求停止采样。 */
    void stopRequested();

public slots:
    /** @brief 根据完整 ViewState 更新页面控件。 */
    void renderViewState(const Ap3216cViewState &viewState);

private:
    QLabel *m_samplingStateLabel;
    QLabel *m_sampleLabel;
    QLabel *m_timeLabel;
    QPushButton *m_startButton;
    QPushButton *m_stopButton;
};

} // namespace smartmonitor

#endif // AP3216C_PAGE_H
