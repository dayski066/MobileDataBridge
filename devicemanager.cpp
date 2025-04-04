#include "devicemanager.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>
#include <QRegularExpression>
#include <QCoreApplication>

DeviceManager::DeviceManager(QObject *parent) : QObject(parent),
    adbProcess(nullptr),
    ideviceProcess(nullptr),
    scanTimer(nullptr),
    isScanning(false),
    scanInterval(3000) // Escanear cada 3 segundos
{
    // Inicializar los procesos
    adbProcess = new QProcess(this);
    ideviceProcess = new QProcess(this);

    // Configurar timer para escaneo periódico
    scanTimer = new QTimer(this);
    connect(scanTimer, &QTimer::timeout, this, &DeviceManager::onDeviceScanTimerTimeout);

    // Buscar rutas de herramientas
    adbPath = findAdbPath();
    libimobiledevicePath = findLibimobiledevicePath();

    // Conectar señales de procesos
    connect(adbProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &DeviceManager::onAndroidDeviceListFinished);

    connect(ideviceProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &DeviceManager::onIOSDeviceListFinished);

    // En el constructor de DeviceManager
    connect(adbProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &DeviceManager::onAndroidDeviceListFinished);
}

bool DeviceManager::setupBridgeClient(const QString &deviceId)
{
    if (!connectedDevices.contains(deviceId) ||
        connectedDevices[deviceId].type != "android") {
        qWarning() << "Cannot setup Bridge Client for non-Android or non-connected device:" << deviceId;
        return false;
    }

    // Crear cliente si no existe
    if (!m_bridgeClients.contains(deviceId)) {
        m_bridgeClients[deviceId] = new AdbSocketClient(this);

        // Conectar señales
        connect(m_bridgeClients[deviceId], &AdbSocketClient::connected, this, [this, deviceId]() {
            qDebug() << "Bridge Client connected for device:" << deviceId;
            emit bridgeClientConnected(deviceId);
        });

        connect(m_bridgeClients[deviceId], &AdbSocketClient::disconnected, this, [this, deviceId]() {
            qDebug() << "Bridge Client disconnected for device:" << deviceId;
            emit bridgeClientDisconnected(deviceId);
        });

        connect(m_bridgeClients[deviceId], &AdbSocketClient::errorOccurred, this, [this, deviceId](const QString &errorMessage) {
            qDebug() << "Bridge Client error for device:" << deviceId << "-" << errorMessage;
            emit bridgeClientError(deviceId, errorMessage);
        });
    }

    // Configurar y conectar
    return m_bridgeClients[deviceId]->setupBridgeClient(deviceId, adbPath);
}

bool DeviceManager::isBridgeClientConnected(const QString &deviceId) const
{
    return m_bridgeClients.contains(deviceId) && m_bridgeClients[deviceId]->isConnected();
}

AdbSocketClient* DeviceManager::getBridgeClient(const QString &deviceId)
{
    if (m_bridgeClients.contains(deviceId)) {
        return m_bridgeClients[deviceId];
    }
    return nullptr;
}

DeviceManager::~DeviceManager()
{
    stopDeviceDetection();

    if (adbProcess) {
        adbProcess->close();
        delete adbProcess;
    }

    if (ideviceProcess) {
        ideviceProcess->close();
        delete ideviceProcess;
    }

    // Limpiar clientes de Bridge
    qDeleteAll(m_bridgeClients);
    m_bridgeClients.clear();
}

bool DeviceManager::startDeviceDetection()
{
    if (isScanning)
        return true; // Ya está escaneando

    // Verificar si tenemos ADB disponible
    if (!isAdbAvailable()) {
        emit error("ADB no encontrado. La detección de dispositivos Android no estará disponible.");
        // Continuamos para al menos intentar detectar dispositivos iOS
    }

    // Verificar si tenemos libimobiledevice disponible
    if (!isLibimobiledeviceAvailable()) {
        emit error("libimobiledevice no encontrado. La detección de dispositivos iOS no estará disponible.");
        // Continuamos para al menos intentar detectar dispositivos Android
    }

    // Si no tenemos ninguna herramienta disponible, fallamos
    if (!isAdbAvailable() && !isLibimobiledeviceAvailable()) {
        emit error("No se encontraron herramientas para detectar dispositivos. Por favor, instale ADB y/o libimobiledevice.");
        return false;
    }

    // Comenzar el escaneo periódico
    isScanning = true;
    refreshDevices(); // Primer escaneo inmediato
    scanTimer->start(scanInterval);

    return true;
}

void DeviceManager::stopDeviceDetection()
{
    if (!isScanning)
        return;

    scanTimer->stop();

    if (adbProcess->state() != QProcess::NotRunning) {
        adbProcess->terminate();
        adbProcess->waitForFinished(1000);
    }

    if (ideviceProcess->state() != QProcess::NotRunning) {
        ideviceProcess->terminate();
        ideviceProcess->waitForFinished(1000);
    }

    isScanning = false;
}

void DeviceManager::refreshDevices()
{
    if (isAdbAvailable()) {
        scanForAndroidDevices();
    }

    if (isLibimobiledeviceAvailable()) {
        scanForIOSDevices();
    }
}

void DeviceManager::scanForAndroidDevices()
{
    if (adbProcess->state() != QProcess::NotRunning) {
        return; // Proceso ya en ejecución
    }

    QStringList arguments;
    arguments << "devices" << "-l";

    adbProcess->start(adbPath, arguments);
}

void DeviceManager::scanForIOSDevices()
{
    if (ideviceProcess->state() != QProcess::NotRunning) {
        return; // Proceso ya en ejecución
    }

    // En Windows, el comando sería idevice_id -l
    QStringList arguments;
    arguments << "-l";

    ideviceProcess->start(libimobiledevicePath + "/idevice_id", arguments);
}

void DeviceManager::onAndroidDeviceListFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        qWarning() << "Error al ejecutar adb devices:" << adbProcess->errorString();
        return;
    }

    QString output = adbProcess->readAllStandardOutput();
    parseAndroidDeviceList(output);
}

void DeviceManager::onIOSDeviceListFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        qWarning() << "Error al ejecutar idevice_id:" << ideviceProcess->errorString();
        return;
    }

    QString output = ideviceProcess->readAllStandardOutput();
    parseIOSDeviceList(output);
}

bool DeviceManager::parseAndroidDeviceList(const QString &output)
{
    // Guardar los IDs actuales para determinar dispositivos desconectados
    QStringList currentIds = connectedDevices.keys();
    QStringList foundIds;

    QStringList lines = output.split('\n', Qt::SkipEmptyParts);

    // Saltamos la primera línea que es el encabezado "List of devices attached"
    for (int i = 1; i < lines.size(); ++i) {
        QString line = lines[i].trimmed();
        if (line.isEmpty())
            continue;

        // Formato típico:
        // XXXXXXXX       device product:modelo device:nombre model:nombre_modelo
        QRegularExpression re("^([^\\s]+)\\s+(\\w+)(.*)$");
        QRegularExpressionMatch match = re.match(line);

        if (match.hasMatch()) {
            QString deviceId = match.captured(1);
            QString deviceState = match.captured(2);
            QString deviceProps = match.captured(3).trimmed();

            foundIds.append(deviceId);

            // Si el dispositivo ya está en nuestra lista, actualizamos su estado
            if (connectedDevices.contains(deviceId)) {
                DeviceInfo &device = connectedDevices[deviceId];
                bool wasAuthorized = device.authorized;
                device.authorized = (deviceState == "device");

                // Emitir señal si el estado de autorización cambió
                if (wasAuthorized != device.authorized) {
                    emit deviceAuthorizationChanged(deviceId, device.authorized);
                }
            } else {
                // Nuevo dispositivo, extraer información
                DeviceInfo newDevice;
                newDevice.id = deviceId;
                newDevice.type = "android";
                newDevice.authorized = (deviceState == "device");

                // Extraer más información de las propiedades
                QRegularExpression modelRe("model:([^\\s]+)");
                QRegularExpressionMatch modelMatch = modelRe.match(deviceProps);
                if (modelMatch.hasMatch()) {
                    newDevice.model = modelMatch.captured(1);
                } else {
                    newDevice.model = "Android Device";
                }

                QRegularExpression deviceRe("device:([^\\s]+)");
                QRegularExpressionMatch deviceMatch = deviceRe.match(deviceProps);
                if (deviceMatch.hasMatch()) {
                    newDevice.name = deviceMatch.captured(1);
                } else {
                    newDevice.name = newDevice.model;
                }

                connectedDevices[deviceId] = newDevice;
                emit deviceConnected(newDevice);
            }
        }
    }

    // Verificar dispositivos desconectados
    for (const QString &id : currentIds) {
        if (!foundIds.contains(id) && connectedDevices[id].type == "android") {
            emit deviceDisconnected(id);
            connectedDevices.remove(id);
        }
    }

    emit deviceListUpdated();
    return true;
}

bool DeviceManager::parseIOSDeviceList(const QString &output)
{
    // Implementación básica para iOS - expandir según sea necesario
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    QStringList foundIds;
    QStringList currentIds = connectedDevices.keys();

    for (const QString &line : lines) {
        QString deviceId = line.trimmed();
        if (deviceId.isEmpty())
            continue;

        foundIds.append(deviceId);

        if (!connectedDevices.contains(deviceId)) {
            // Nuevo dispositivo iOS detectado
            DeviceInfo newDevice;
            newDevice.id = deviceId;
            newDevice.type = "ios";
            newDevice.model = "iPhone/iPad"; // Necesitaríamos ejecutar ideviceinfo para obtener más detalles
            newDevice.name = "iOS Device";
            newDevice.authorized = checkIOSPermissions(deviceId); // Verificar si está autorizado

            connectedDevices[deviceId] = newDevice;
            emit deviceConnected(newDevice);
        }
    }

    // Verificar dispositivos desconectados
    for (const QString &id : currentIds) {
        if (!foundIds.contains(id) && connectedDevices[id].type == "ios") {
            emit deviceDisconnected(id);
            connectedDevices.remove(id);
        }
    }

    emit deviceListUpdated();
    return true;
}

void DeviceManager::onDeviceScanTimerTimeout()
{
    refreshDevices();
}

bool DeviceManager::checkAndroidPermissions(const QString &deviceId)
{
    if (!connectedDevices.contains(deviceId))
        return false;

    DeviceInfo device = connectedDevices[deviceId];
    return device.authorized;
}

bool DeviceManager::checkIOSPermissions(const QString &deviceId)
{
    // En una implementación real, usaríamos ideviceinfo para verificar si podemos acceder al dispositivo
    QProcess process;
    QStringList args;
    args << "-u" << deviceId << "-k" << "DeviceName";

    process.start(libimobiledevicePath + "/ideviceinfo", args);
    process.waitForFinished(3000);

    return (process.exitCode() == 0);
}

QString DeviceManager::findAdbPath()
{
    // Buscar en el directorio de la aplicación
    QString appDir = QCoreApplication::applicationDirPath();
    QString internalPath = QDir::cleanPath(appDir + "/tools/adb/adb");

    if (QSysInfo::productType() == "windows") {
        internalPath += ".exe";
    }

    if (QFileInfo::exists(internalPath)) {
        qDebug() << "ADB encontrado en el directorio de la aplicación:" << internalPath;
        return internalPath;
    }

    // Probar con la ruta relativa al directorio del proyecto
    QString projectPath = QDir::currentPath();
    internalPath = QDir::cleanPath(projectPath + "/tools/adb/adb");

    if (QSysInfo::productType() == "windows") {
        internalPath += ".exe";
    }

    if (QFileInfo::exists(internalPath)) {
        qDebug() << "ADB encontrado en el directorio del proyecto:" << internalPath;
        return internalPath;
    }

    // Si no se encuentra, emitir advertencia y devolver vacío
    qWarning() << "ADB no encontrado en el directorio interno de herramientas (esperado en tools/adb/)";
    return QString();
}

QString DeviceManager::findLibimobiledevicePath()
{
    // Determinar si estamos en un sistema de 32 o 64 bits
    QString archDir = QSysInfo::currentCpuArchitecture().contains("64") ? "x64" : "x32";

    // Buscar en el directorio de la aplicación
    QString appDir = QCoreApplication::applicationDirPath();
    QString internalPath = QDir::cleanPath(appDir + "/tools/libimobiledevice/" + archDir);

    if (QDir(internalPath).exists()) {
        // Comprobar si existe el ejecutable clave (idevice_id)
        QString ideviceIdPath = QDir::cleanPath(internalPath + "/idevice_id");
        if (QSysInfo::productType() == "windows") {
            ideviceIdPath += ".exe";
        }

        if (QFileInfo::exists(ideviceIdPath)) {
            qDebug() << "libimobiledevice encontrado en el directorio de la aplicación:" << internalPath;
            return internalPath;
        }
    }

    // Probar con la ruta relativa al directorio del proyecto
    QString projectPath = QDir::currentPath();
    internalPath = QDir::cleanPath(projectPath + "/tools/libimobiledevice/" + archDir);

    if (QDir(internalPath).exists()) {
        // Comprobar si existe el ejecutable clave (idevice_id)
        QString ideviceIdPath = QDir::cleanPath(internalPath + "/idevice_id");
        if (QSysInfo::productType() == "windows") {
            ideviceIdPath += ".exe";
        }

        if (QFileInfo::exists(ideviceIdPath)) {
            qDebug() << "libimobiledevice encontrado en el directorio del proyecto:" << internalPath;
            return internalPath;
        }
    }

    // Si no se encuentra, emitir advertencia y devolver vacío
    qWarning() << "Herramientas libimobiledevice no encontradas en el directorio interno (esperado en tools/libimobiledevice/" << archDir << ")";
    return QString();
}

QPair<QString, bool> DeviceManager::getLibimobiledeviceInfo() const
{
    // Determinar si estamos en un sistema de 32 o 64 bits
    QString archDir = QSysInfo::currentCpuArchitecture().contains("64") ? "x64" : "x32";

    // Buscar en el directorio de la aplicación
    QString appDir = QCoreApplication::applicationDirPath();
    QString internalPath = QDir::cleanPath(appDir + "/tools/libimobiledevice/" + archDir);

    if (QDir(internalPath).exists()) {
        // Comprobar si existe el ejecutable clave (idevice_id)
        QString ideviceIdPath = QDir::cleanPath(internalPath + "/idevice_id");
        if (QSysInfo::productType() == "windows") {
            ideviceIdPath += ".exe";
        }

        if (QFileInfo::exists(ideviceIdPath)) {
            qDebug() << "libimobiledevice encontrado en el directorio de la aplicación:" << internalPath;
            return QPair<QString, bool>(internalPath, true);
        }
    }

    // Probar con la ruta relativa al directorio del proyecto
    QString projectPath = QDir::currentPath();
    internalPath = QDir::cleanPath(projectPath + "/tools/libimobiledevice/" + archDir);

    if (QDir(internalPath).exists()) {
        // Comprobar si existe el ejecutable clave (idevice_id)
        QString ideviceIdPath = QDir::cleanPath(internalPath + "/idevice_id");
        if (QSysInfo::productType() == "windows") {
            ideviceIdPath += ".exe";
        }

        if (QFileInfo::exists(ideviceIdPath)) {
            qDebug() << "libimobiledevice encontrado en el directorio del proyecto:" << internalPath;
            return QPair<QString, bool>(internalPath, true);
        }
    }

    // Si no se encuentra, emitir advertencia y devolver no disponible
    qWarning() << "Herramientas libimobiledevice no encontradas en el directorio interno (esperado en tools/libimobiledevice/" << archDir << ")";
    return QPair<QString, bool>("", false);
}

bool DeviceManager::isAdbAvailable() const
{
    return !adbPath.isEmpty();
}

bool DeviceManager::isLibimobiledeviceAvailable() const
{
    return !libimobiledevicePath.isEmpty();
}

QString DeviceManager::getAdbPath() const
{
    return adbPath;
}

bool DeviceManager::setupAdb(const QString &customPath)
{
    if (!customPath.isEmpty()) {
        QFileInfo fileInfo(customPath);
        if (fileInfo.exists() && fileInfo.isExecutable()) {
            adbPath = customPath;
            emit adbPathChanged(adbPath);
            return true;
        }
        return false;
    }

    // Intentar buscar automáticamente
    QString foundPath = findAdbPath();
    if (!foundPath.isEmpty()) {
        adbPath = foundPath;
        emit adbPathChanged(adbPath);
        return true;
    }

    return false;
}

QList<DeviceInfo> DeviceManager::getConnectedDevices() const
{
    return connectedDevices.values();
}

DeviceInfo DeviceManager::getDeviceInfo(const QString &deviceId) const
{
    if (connectedDevices.contains(deviceId)) {
        return connectedDevices[deviceId];
    }

    return DeviceInfo(); // Devolver un objeto vacío si no se encuentra
}

bool DeviceManager::authorizeAndroidDevice(const QString &deviceId)
{
    // En realidad, no podemos forzar la autorización desde el PC
    // Solo podemos mostrar instrucciones al usuario

    if (!connectedDevices.contains(deviceId))
        return false;

    DeviceInfo device = connectedDevices[deviceId];
    if (device.authorized)
        return true; // Ya está autorizado

    // Emitir error/mensaje con instrucciones para el usuario
    emit error("Por favor, desbloquee su dispositivo Android y acepte el diálogo de 'Permitir depuración USB' cuando aparezca.");

    return false;
}

bool DeviceManager::authorizeiOSDevice(const QString &deviceId)
{
    // Para iOS, también debemos guiar al usuario
    if (!connectedDevices.contains(deviceId))
        return false;

    DeviceInfo device = connectedDevices[deviceId];
    if (device.authorized)
        return true; // Ya está autorizado

    // Emitir error/mensaje con instrucciones para el usuario
    emit error("Por favor, desbloquee su dispositivo iOS y toque 'Confiar' cuando se le pregunte si desea confiar en este ordenador.");

    return false;
}

bool DeviceManager::setupBridgeClient(const QString &deviceId)
{
    if (!connectedDevices.contains(deviceId) ||
        connectedDevices[deviceId].type != "android") {
        qWarning() << "Cannot setup Bridge Client for non-Android or non-connected device:" << deviceId;
        return false;
    }

    // Crear cliente si no existe
    if (!m_bridgeClients.contains(deviceId)) {
        m_bridgeClients[deviceId] = new AdbSocketClient(this);

        // Conectar señales
        connect(m_bridgeClients[deviceId], &AdbSocketClient::connected, this, [this, deviceId]() {
            qDebug() << "Bridge Client connected for device:" << deviceId;
            emit bridgeClientConnected(deviceId);
        });

        connect(m_bridgeClients[deviceId], &AdbSocketClient::disconnected, this, [this, deviceId]() {
            qDebug() << "Bridge Client disconnected for device:" << deviceId;
            emit bridgeClientDisconnected(deviceId);
        });

        connect(m_bridgeClients[deviceId], &AdbSocketClient::errorOccurred, this, [this, deviceId](const QString &errorMessage) {
            qDebug() << "Bridge Client error for device:" << deviceId << "-" << errorMessage;
            emit bridgeClientError(deviceId, errorMessage);
        });
    }

    // Configurar y conectar
    return m_bridgeClients[deviceId]->setupBridgeClient(deviceId, adbPath);
}

bool DeviceManager::isBridgeClientConnected(const QString &deviceId) const
{
    return m_bridgeClients.contains(deviceId) && m_bridgeClients[deviceId]->isConnected();
}

AdbSocketClient* DeviceManager::getBridgeClient(const QString &deviceId)
{
    if (m_bridgeClients.contains(deviceId)) {
        return m_bridgeClients[deviceId];
    }
    return nullptr;
}
