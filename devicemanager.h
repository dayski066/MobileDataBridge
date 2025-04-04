#ifndef DEVICEMANAGER_H
#define DEVICEMANAGER_H

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QMap>
#include "adbsocketclient.h"

// Estructura para almacenar información de un dispositivo
struct DeviceInfo {
    QString id;          // Identificador único (serial, UDID)
    QString type;        // "android" o "ios"
    QString model;       // Modelo del dispositivo
    QString name;        // Nombre asignado al dispositivo
    bool authorized;     // Si el dispositivo está autorizado/confiado
    QString osVersion;   // Versión del sistema operativo
};

class DeviceManager : public QObject
{
    Q_OBJECT
public:
    explicit DeviceManager(QObject *parent = nullptr);
    ~DeviceManager();

    // Iniciar/detener la detección de dispositivos
    bool startDeviceDetection();
    void stopDeviceDetection();

    // Obtener información de dispositivos
    QList<DeviceInfo> getConnectedDevices() const;
    DeviceInfo getDeviceInfo(const QString &deviceId) const;

    // Verificar estado de ADB
    bool isAdbAvailable() const;
    QString getAdbPath() const;
    bool setupAdb(const QString &customPath = "");

    // Verificar estado de libimobiledevice
    bool isLibimobiledeviceAvailable() const;
    QPair<QString, bool> getLibimobiledeviceInfo() const; // Ruta y disponibilidad

    // Integración con Bridge Client
    bool setupBridgeClient(const QString &deviceId);
    bool isBridgeClientConnected(const QString &deviceId) const;
    AdbSocketClient* getBridgeClient(const QString &deviceId);

signals:
    // Señales para comunicar cambios a la interfaz
    void deviceConnected(const DeviceInfo &device);
    void deviceDisconnected(const QString &deviceId);
    void deviceAuthorizationChanged(const QString &deviceId, bool authorized);
    void deviceListUpdated();
    void adbPathChanged(const QString &path);
    void error(const QString &message);

    // Señales para Bridge Client
    void bridgeClientConnected(const QString &deviceId);
    void bridgeClientDisconnected(const QString &deviceId);
    void bridgeClientError(const QString &deviceId, const QString &errorMessage);

public slots:
    // Acciones que se pueden invocar desde la interfaz
    void refreshDevices();
    bool authorizeAndroidDevice(const QString &deviceId);
    bool authorizeiOSDevice(const QString &deviceId);

private slots:
    void onAndroidDeviceListFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onIOSDeviceListFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onDeviceScanTimerTimeout();

private:
    // Métodos privados para manejo interno
    void scanForAndroidDevices();
    void scanForIOSDevices();
    bool checkAndroidPermissions(const QString &deviceId);
    bool checkIOSPermissions(const QString &deviceId);
    bool parseAndroidDeviceList(const QString &output);
    bool parseIOSDeviceList(const QString &output);
    QString findAdbPath();
    QString findLibimobiledevicePath();

    // Variables internas
    QProcess *adbProcess;
    QProcess *ideviceProcess;
    QTimer *scanTimer;
    QString adbPath;
    QString libimobiledevicePath;
    QMap<QString, DeviceInfo> connectedDevices;
    bool isScanning;
    int scanInterval; // en milisegundos

    // Bridge Client
    QMap<QString, AdbSocketClient*> m_bridgeClients;
};

#endif // DEVICEMANAGER_H
