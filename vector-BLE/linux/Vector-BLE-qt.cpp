#include "Vector-BLE.h"

#include <QCoreApplication>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothLocalDevice>
#include <QBluetoothUuid>
#include <QLowEnergyController>
#include <QLowEnergyConnectionParameters>
#include <QDebug>
#include <QPointer>
#include <signal.h>
#include <QTimer>

extern "C" {

void bleRecv(uint8_t const* bytes, size_t length);

extern int _argc;
extern char** _argv;


static QPointer<QBluetoothDeviceDiscoveryAgent> agent;
static QPointer<QLowEnergyController> controller;
static QPointer<QLowEnergyService> m_service;

static void onDeviceConnected()
{
    qDebug() << "Device discovered";
    static const QBluetoothUuid vectorService(QStringLiteral("{0000fee3-0000-1000-8000-00805f9b34fb}"));
    m_service = controller->createServiceObject(vectorService, controller);
    if (!m_service) {
        qWarning() << "FAiled to create service";
        qApp->quit();
        return;
    }

    static const QBluetoothUuid readUuid(QStringLiteral("{30619F2D-0F54-41BD-A65A-7588D8C85B45}"));
    QObject::connect(m_service, &QLowEnergyService::characteristicChanged, [](const QLowEnergyCharacteristic &characteristic, const QByteArray &data){
        if (characteristic.uuid() != readUuid) {
            qWarning() << "Unexpected data from characteristic" << characteristic.name() << characteristic.uuid();
        }
        qDebug() << "Received" << data;
        bleRecv(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    });

    QObject::connect(m_service, &QLowEnergyService::stateChanged, [](QLowEnergyService::ServiceState newState) {
        qDebug() << "State changeD" << newState;
        if (newState != QLowEnergyService::ServiceDiscovered) {
            return;
        }

        for (const QLowEnergyCharacteristic &c : m_service->characteristics()) {
            qDebug() << c.name() << c.uuid() << c.properties();
        }
        qDebug() << "read charactierstic is valid?" << m_service->characteristic(readUuid).isValid();

        QList<QLowEnergyDescriptor> readDescriptors = m_service->characteristic(readUuid).descriptors();
        if (readDescriptors.count() != 1) {
            qDebug() << "Read descriptors wrong count" << readDescriptors.count();
            return;
        }
        m_service->writeDescriptor(readDescriptors.first(), QByteArray::fromHex("0100"));
    });
    m_service->discoverDetails();
}

static void onDeviceDiscovered(const QBluetoothDeviceInfo &device)
{
    if (controller) {
        return;
    }


    static const QBluetoothUuid vectorService(QStringLiteral("{0000fee3-0000-1000-8000-00805f9b34fb}"));
    if (!device.serviceUuids().contains(vectorService)) {
        return;
    }

    qDebug() << "Device discovered" << device.name();
    qDebug() << "Device uuid" << device.address();

    if (!device.isValid()) {
        qWarning() << "Invalid device";
        return;
    }
    for (const QBluetoothUuid &uuid : device.serviceUuids()) {
        qDebug() << "service:" << uuid;
    }
    qDebug() << device.isValid();
    controller = QLowEnergyController::createCentral(device, agent);

    QObject::connect(controller, &QLowEnergyController::connected, controller, &QLowEnergyController::discoverServices);
    QObject::connect(controller, &QLowEnergyController::connected, controller, []() {
        qDebug() << "Connected";
    });
    QObject::connect(controller, &QLowEnergyController::connectionUpdated, [](const QLowEnergyConnectionParameters &parms) {
        qDebug() << " - controller connection updated, latency" << parms.latency() << "maxinterval:" << parms.maximumInterval() << "mininterval:" << parms.minimumInterval() << "supervision timeout" << parms.supervisionTimeout(
                        );
    });
    QObject::connect(controller, &QLowEnergyController::connected, []() {
        qDebug() << " - controller connected";
    });
    QObject::connect(controller, QOverload<QLowEnergyController::Error>::of(&QLowEnergyController::error), [](QLowEnergyController::Error newError) {
        qWarning() << "Controller error" << newError;
    });
    QObject::connect(controller, &QLowEnergyController::disconnected, []() {
        qDebug() << " - controller disconnected";
    });
    QObject::connect(controller, &QLowEnergyController::discoveryFinished, []() {
        qDebug() << " - controller discovery finished";
    });
    QObject::connect(controller, &QLowEnergyController::connected, []() { qDebug() << "ConnecteD"; });
//    QObject::connect(controller, &QLowEnergyController::connected, []() { onDeviceConnected(); });
    QObject::connect(controller, &QLowEnergyController::serviceDiscovered, [](const QBluetoothUuid &newService) {
        qDebug() << "Service discovered" << newService;
        if (newService == vectorService) {
            onDeviceConnected();
        }
    });
    qDebug() << "Discovering services";
//    controller->discoverServices();
    controller->connectToDevice();
}

static void quit(int)
{
    qApp->quit();
}

void bleSend(void const* bytes, size_t length)
{
    if (!controller || !m_service) {
        qWarning() << "Can't send without a controller and service";
        return;
    }
    static const QBluetoothUuid writeUuid(QStringLiteral("{7D2A4BDA-D29B-4152-B725-2491478C5CD7}"));
    m_service->writeCharacteristic(m_service->characteristic(writeUuid), QByteArray(reinterpret_cast<const char *>(bytes), length));
}

void bleScan()
{
    QCoreApplication app(_argc, _argv);

    agent = new QBluetoothDeviceDiscoveryAgent(&app);
    QObject::connect(agent, QOverload<QBluetoothDeviceDiscoveryAgent::Error>::of(&QBluetoothDeviceDiscoveryAgent::error), [](){
        qDebug() << "Agent error:" << agent->errorString();
    });
    QObject::connect(agent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, [](const QBluetoothDeviceInfo &device) {
        onDeviceDiscovered(device);
    });
    agent->setLowEnergyDiscoveryTimeout(30000);

    // Wait for event loop to start
    QTimer::singleShot(0, [&app](){
        QBluetoothLocalDevice *adapter = new QBluetoothLocalDevice(&app);
        adapter->powerOn(); // Justin Case™
        agent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
    });

    signal(SIGINT, &quit);
    signal(SIGTERM, &quit);

    app.exec();

    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    agent->stop();

    return;
}
}
