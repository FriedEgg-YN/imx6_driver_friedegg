#ifndef COMPOSITION_ROOT_H
#define COMPOSITION_ROOT_H

#include <QObject>

class QThread;

namespace smartmonitor {

class Ap3216cController;
class Ap3216cPage;
class SensorService;
class SensorWorker;

class CompositionRoot final : public QObject
{
    Q_OBJECT

public:
    explicit CompositionRoot(QObject *parent = nullptr);
    ~CompositionRoot() override;

    void start();

private:
    SensorService *m_sensorService = nullptr;
    QThread *m_sensorThread = nullptr;
    SensorWorker *m_sensorWorker = nullptr;
    Ap3216cController *m_controller = nullptr;
    Ap3216cPage *m_page = nullptr;
};

} // namespace smartmonitor

#endif // COMPOSITION_ROOT_H
