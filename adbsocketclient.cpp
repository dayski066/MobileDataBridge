#include "adbsocketclient.h"
#include <QDebug>
#include <QHostAddress>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QMutexLocker>

/**
 * Constructor de la clase AdbSocketClient
 */
AdbSocketClient::AdbSocketClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_connected(false)
    , m_reconnectTimer(new QTimer(this))
    , m_reconnectAttempts(0)
    , m_connectionState(Disconnected)
    , m_transferRole(Unknown)
    , m_connectionCheckTimer(new QTimer(this))
    , m_isProcessingCommands(false)
{
    // Conexiones para eventos del socket
    connect(m_socket, &QTcpSocket::readyRead, this, &AdbSocketClient::readFromSocket);

    connect(m_socket, &QTcpSocket::connected, this, [this]() {
        m_connected = true;
        m_reconnectAttempts = 0;
        qDebug() << "Socket conectado a Bridge Client";
        setConnectionState(Connected);
        emit connected();
    });

    connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
        m_connected = false;
        qDebug() << "Socket desconectado de Bridge Client";
        setConnectionState(Disconnected);
        emit disconnected();

        // Intentar reconectar automáticamente
        if (m_reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
            m_reconnectTimer->start(RECONNECT_INTERVAL);
        }
    });

    connect(m_socket, static_cast<void(QTcpSocket::*)(QAbstractSocket::SocketError)>(&QTcpSocket::error),
            this, &AdbSocketClient::handleSocketError);

    // Configurar timers
    connect(m_reconnectTimer, &QTimer::timeout, this, &AdbSocketClient::reconnectTimer);

    m_connectionCheckTimer->setInterval(CONNECTION_CHECK_INTERVAL);
    connect(m_connectionCheckTimer, &QTimer::timeout, this, &AdbSocketClient::checkConnectionState);
    m_connectionCheckTimer->start();

    // Conectar señales del proceso ADB
    connect(&m_adbProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &AdbSocketClient::onAdbForwardFinished);
}

/**
 * Destructor
 */
AdbSocketClient::~AdbSocketClient()
{
    disconnectFromDevice();
}

/**
 * Establece conexión con un dispositivo
 */
bool AdbSocketClient::connectToDevice(const QString &deviceId)
{
    if (m_connected) {
        disconnectFromDevice();
    }

    m_deviceId = deviceId;
    setConnectionState(Connecting);

    // Conectar al socket local con puerto redirigido por ADB
    m_socket->connectToHost(QHostAddress::LocalHost, PORT);
    if (!m_socket->waitForConnected(5000)) {
        qWarning() << "Failed to connect to Bridge Client socket:" << m_socket->errorString();
        setConnectionState(Error);
        return false;
    }

    return true;
}

/**
 * Desconecta del dispositivo actual
 */
void AdbSocketClient::disconnectFromDevice()
{
    if (m_socket->state() != QTcpSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
        if (m_socket->state() != QTcpSocket::UnconnectedState)
            m_socket->waitForDisconnected(1000);
    }

    m_connected = false;
    m_reconnectTimer->stop();
    m_reconnectAttempts = 0;

    setConnectionState(Disconnected);

    // Limpiar cola de comandos
    {
        QMutexLocker locker(&m_commandMutex);
        m_commandQueue.clear();
        m_isProcessingCommands = false;
    }
}

/**
 * Verifica si hay una conexión activa
 */
bool AdbSocketClient::isConnected() const
{
    return m_connected;
}

/**
 * Establece el rol del dispositivo (origen o destino)
 */
bool AdbSocketClient::setRole(const QString &role)
{
    // Guardar rol internamente también
    if (role.toLower() == "source") {
        m_transferRole = Source;
    } else if (role.toLower() == "destination") {
        m_transferRole = Destination;
    } else {
        m_transferRole = Unknown;
    }

    return sendCommand(QString("SET_ROLE:%1").arg(role));
}

/**
 * Inicia un escaneo en el dispositivo
 */
bool AdbSocketClient::startScan()
{
    return sendCommand("START_SCAN");
}

/**
 * Solicita información del dispositivo
 */
bool AdbSocketClient::getDeviceInfo()
{
    return sendCommand("GET_DEVICE_INFO");
}

/**
 * Solicita un archivo específico del dispositivo
 */
bool AdbSocketClient::requestFile(const QString &filePath)
{
    return sendCommand(QString("GET_FILE:%1").arg(filePath));
}

/**
 * Guarda un archivo en el dispositivo
 */
bool AdbSocketClient::saveFile(const QString &fileInfo)
{
    return sendCommand(QString("SAVE_FILE:%1").arg(fileInfo));
}

/**
 * Envía un ping para verificar la conexión
 */
bool AdbSocketClient::ping()
{
    return sendCommand("PING");
}

/**
 * Configura automáticamente el Bridge Client
 */
bool AdbSocketClient::setupBridgeClient(const QString &deviceId, const QString &adbPath)
{
    m_deviceId = deviceId;
    m_adbPath = adbPath;

    // 1. Verificar si la app está instalada
    if (!isAppInstalled(deviceId, adbPath)) {
        // 2. Instalar la app si no está presente
        if (!installApp(deviceId, adbPath)) {
            emit errorOccurred("Failed to install Bridge Client app");
            return false;
        }
    }

    // 3. Configurar el reenvío de puertos
    if (!forwardTcpPort(deviceId, adbPath)) {
        emit errorOccurred("Failed to forward TCP port");
        return false;
    }

    // 4. Lanzar la app
    if (!launchApp(deviceId, adbPath, "source")) {
        emit errorOccurred("Failed to launch Bridge Client app");
        return false;
    }

    // 5. Conectar al socket
    return connectToDevice(deviceId);
}

/**
 * Obtiene el estado actual de la conexión
 */
AdbSocketClient::ConnectionState AdbSocketClient::getConnectionState() const
{
    return m_connectionState;
}

/**
 * Obtiene el rol actual de transferencia
 */
AdbSocketClient::TransferRole AdbSocketClient::getTransferRole() const
{
    return m_transferRole;
}

/**
 * Solicita archivos multimedia específicos
 */
bool AdbSocketClient::requestMediaFiles(const QList<int> &fileIndices)
{
    if (fileIndices.isEmpty()) {
        return false;
    }

    // Convertir índices a una lista separada por comas
    QStringList indexStrings;
    for (int index : fileIndices) {
        indexStrings.append(QString::number(index));
    }

    return sendCommand(QString("GET_MEDIA_FILES:%1").arg(indexStrings.join(",")));
}

/**
 * Solicita contactos específicos
 */
bool AdbSocketClient::requestContacts(const QStringList &contactIds)
{
    if (contactIds.isEmpty()) {
        return false;
    }

    return sendCommand(QString("GET_CONTACTS:%1").arg(contactIds.join(",")));
}

/**
 * Solicita mensajes específicos
 */
bool AdbSocketClient::requestMessages(const QStringList &messageIds)
{
    if (messageIds.isEmpty()) {
        return false;
    }

    return sendCommand(QString("GET_MESSAGES:%1").arg(messageIds.join(",")));
}

/**
 * Cancela la operación actual
 */
bool AdbSocketClient::cancelOperation()
{
    return sendCommand("CANCEL_OPERATION");
}

/**
 * Lee datos desde el socket
 */
void AdbSocketClient::readFromSocket()
{
    // Leer datos del socket
    QByteArray data = m_socket->readAll();
    m_buffer.append(QString::fromUtf8(data));

    // Procesar líneas completas
    while (m_buffer.contains('\n')) {
        int lineEnd = m_buffer.indexOf('\n');
        QString line = m_buffer.left(lineEnd).trimmed();
        m_buffer = m_buffer.mid(lineEnd + 1);

        if (!line.isEmpty()) {
            processResponse(line);
        }
    }
}

/**
 * Maneja errores del socket
 */
void AdbSocketClient::handleSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    qWarning() << "Socket error:" << m_socket->errorString();

    setConnectionState(Error);
    emit errorOccurred(QString("Socket error: %1").arg(m_socket->errorString()));
}

/**
 * Procesa la finalización del comando adb forward
 */
void AdbSocketClient::onAdbForwardFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        qWarning() << "ADB command failed:" << m_adbProcess.readAllStandardError();
        emit errorOccurred("ADB command failed");
    }
}

/**
 * Procesa la finalización de la instalación de la app
 */
void AdbSocketClient::onInstallAppFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        qWarning() << "Failed to install Bridge Client app:" << m_adbProcess.readAllStandardError();
        emit errorOccurred("Failed to install Bridge Client app");
        return;
    }

    // Continuar con el reenvío de puertos y lanzamiento
    if (forwardTcpPort(m_deviceId, m_adbPath)) {
        launchApp(m_deviceId, m_adbPath, "source");
    }
}

/**
 * Procesa la finalización del lanzamiento de la app
 */
void AdbSocketClient::onLaunchAppFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        qWarning() << "Failed to launch Bridge Client app:" << m_adbProcess.readAllStandardError();
        emit errorOccurred("Failed to launch Bridge Client app");
        return;
    }

    // Esperar un momento antes de intentar conectar
    QTimer::singleShot(1000, this, [this]() {
        connectToDevice(m_deviceId);
    });
}

/**
 * Maneja intentos de reconexión automática
 */
void AdbSocketClient::reconnectTimer()
{
    if (!m_connected && !m_deviceId.isEmpty() && m_reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
        m_reconnectAttempts++;
        qDebug() << "Attempting to reconnect to Bridge Client, attempt" << m_reconnectAttempts;

        // Reintentar el reenvío de puertos y la reconexión
        if (forwardTcpPort(m_deviceId, m_adbPath)) {
            connectToDevice(m_deviceId);
        } else {
            // Si no se puede reenviar, programar otro intento
            m_reconnectTimer->start(RECONNECT_INTERVAL);
        }
    } else {
        m_reconnectTimer->stop();
    }
}

/**
 * Procesa el siguiente comando en la cola
 */
void AdbSocketClient::processNextCommand()
{
    QMutexLocker locker(&m_commandMutex);

    if (m_commandQueue.isEmpty()) {
        m_isProcessingCommands = false;
        return;
    }

    QString command = m_commandQueue.dequeue();
    locker.unlock(); // Desbloquear para la operación de envío

    bool success = sendCommand(command);

    if (!success) {
        qWarning() << "Failed to send queued command:" << command;
    }

    // Programar el siguiente procesamiento
    QTimer::singleShot(100, this, &AdbSocketClient::processNextCommand);
}

/**
 * Verifica periódicamente el estado de la conexión
 */
void AdbSocketClient::checkConnectionState()
{
    if (m_connected) {
        // Enviar ping para verificar que la conexión sigue activa
        ping();
    }
}

/**
 * Configura el reenvío de puerto TCP para la comunicación
 */
bool AdbSocketClient::forwardTcpPort(const QString &deviceId, const QString &adbPath)
{
    if (adbPath.isEmpty()) {
        qWarning() << "ADB path is empty";
        return false;
    }

    QStringList args;
    args << "-s" << deviceId << "forward" << QString("tcp:%1").arg(PORT) << QString("tcp:%1").arg(PORT);

    m_adbProcess.start(adbPath, args);
    if (!m_adbProcess.waitForFinished(5000)) {
        qWarning() << "ADB forward command timed out";
        return false;
    }

    return (m_adbProcess.exitCode() == 0);
}

/**
 * Verifica si Bridge Client está instalado en el dispositivo
 */
bool AdbSocketClient::isAppInstalled(const QString &deviceId, const QString &adbPath)
{
    QStringList args;
    args << "-s" << deviceId << "shell" << "pm" << "list" << "packages" << "com.laniakeapos.bridgeclient";

    m_adbProcess.start(adbPath, args);
    if (!m_adbProcess.waitForFinished(5000)) {
        qWarning() << "ADB package check timed out";
        return false;
    }

    QString output = QString::fromUtf8(m_adbProcess.readAllStandardOutput());
    return output.contains("com.laniakeapos.bridgeclient");
}

/**
 * Instala Bridge Client en el dispositivo
 */
bool AdbSocketClient::installApp(const QString &deviceId, const QString &adbPath)
{
    // Ruta a la APK (debe ser relativa al ejecutable o configurada)
    QString apkPath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/tools/bridgeclient.apk");
    QFileInfo apkInfo(apkPath);

    if (!apkInfo.exists() || !apkInfo.isFile()) {
        qWarning() << "Bridge Client APK not found at:" << apkPath;
        return false;
    }

    disconnect(&m_adbProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
               this, &AdbSocketClient::onAdbForwardFinished);
    connect(&m_adbProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &AdbSocketClient::onInstallAppFinished);

    QStringList args;
    args << "-s" << deviceId << "install" << "-r" << apkPath;

    m_adbProcess.start(adbPath, args);

    // No esperamos a que termine, onInstallAppFinished se llamará cuando finalice
    return true;
}

/**
 * Lanza la aplicación Bridge Client en el dispositivo
 */
bool AdbSocketClient::launchApp(const QString &deviceId, const QString &adbPath, const QString &role)
{
    disconnect(&m_adbProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
               this, &AdbSocketClient::onInstallAppFinished);
    connect(&m_adbProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &AdbSocketClient::onLaunchAppFinished);

    QStringList args;
    args << "-s" << deviceId << "shell" << "am" << "start"
         << "-n" << "com.laniakeapos.bridgeclient/.MainActivity"
         << "-e" << "role" << role;

    m_adbProcess.start(adbPath, args);

    // No esperamos a que termine, onLaunchAppFinished se llamará cuando finalice
    return true;
}

/**
 * Envía un comando al dispositivo
 */
bool AdbSocketClient::sendCommand(const QString &command)
{
    if (!m_connected) {
        qWarning() << "Cannot send command, not connected to Bridge Client:" << command;
        return false;
    }

    QByteArray data = command.toUtf8() + "\n";
    qint64 bytesWritten = m_socket->write(data);

    if (bytesWritten != data.size()) {
        qWarning() << "Failed to write command to socket:" << command;
        return false;
    }

    qDebug() << "Command sent:" << command;
    return m_socket->flush();
}

/**
 * Encola un comando para envío secuencial
 */
bool AdbSocketClient::enqueueCommand(const QString &command)
{
    if (command.isEmpty()) {
        return false;
    }

    QMutexLocker locker(&m_commandMutex);
    m_commandQueue.enqueue(command);

    // Si no estamos procesando comandos, iniciar el procesamiento
    if (!m_isProcessingCommands) {
        m_isProcessingCommands = true;
        QTimer::singleShot(0, this, &AdbSocketClient::processNextCommand);
    }

    return true;
}

/**
 * Procesa una respuesta del dispositivo
 */
void AdbSocketClient::processResponse(const QString &response)
{
    qDebug() << "Received response:" << response;

    // Parsear la respuesta según el protocolo
    if (response.startsWith("CONNECTED:")) {
        qDebug() << "Connected to Bridge Client:" << response.mid(10);
    }
    else if (response.startsWith("DEVICE_INFO:")) {
        QString jsonStr = response.mid(12);
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        if (doc.isObject()) {
            emit deviceInfoReceived(doc.object());
        }
    }
    else if (response.startsWith("ROLE_SET:")) {
        QString role = response.mid(9);
        qDebug() << "Role set to:" << role;
    }
    else if (response == "SCAN_STARTED") {
        emit scanStarted();
    }
    else if (response.startsWith("SCAN_PROGRESS:")) {
        int progress = response.mid(14).toInt();
        emit scanProgress(progress);
    }
    else if (response == "SCAN_COMPLETED") {
        emit scanCompleted();
    }
    else if (response.startsWith("SCAN_ERROR:")) {
        QString error = response.mid(11);
        emit scanError(error);
    }
    else if (response.startsWith("MEDIA_COUNT:")) {
        int count = response.mid(12).toInt();
        qDebug() << "Media files count:" << count;
    }
    else if (response.startsWith("MEDIA_DATA:")) {
        // Formato: MEDIA_DATA:index:count:jsondata
        QStringList parts = response.mid(11).split(':', QString::SkipEmptyParts);
        if (parts.size() >= 3) {
            int index = parts[0].toInt();
            int count = parts[1].toInt();

            // Reconstruir el JSON (puede contener ':' internos)
            QString jsonStr = parts.mid(2).join(':');
            QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
            if (doc.isArray()) {
                emit mediaDataReceived(index, count, doc.array());
            }
        }
    }
    else if (response.startsWith("FILES_COUNT:")) {
        int count = response.mid(12).toInt();
        qDebug() << "Files count:" << count;
    }
    else if (response.startsWith("FILES_DATA:")) {
        // Formato: FILES_DATA:index:count:jsondata
        QStringList parts = response.mid(11).split(':', QString::SkipEmptyParts);
        if (parts.size() >= 3) {
            int index = parts[0].toInt();
            int count = parts[1].toInt();

            // Reconstruir el JSON (puede contener ':' internos)
            QString jsonStr = parts.mid(2).join(':');
            QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
            if (doc.isArray()) {
                emit filesDataReceived(index, count, doc.array());
            }
        }
    }
    else if (response.startsWith("FILE_READY:")) {
        QString filePath = response.mid(11);
        emit fileReady(filePath);
    }
    else if (response.startsWith("FILE_SAVED:")) {
        QString result = response.mid(11);
        emit fileSaved(result);
    }
    else if (response == "PONG") {
        emit pongReceived();
    }
    else if (response.startsWith("ERROR:")) {
        QString error = response.mid(6);
        emit errorOccurred(error);
    }
    else if (response.startsWith("CONTACTS_DATA:")) {
        // Formato: CONTACTS_DATA:jsondata
        QString jsonStr = response.mid(14);
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        if (doc.isArray()) {
            emit contactsDataReceived(doc.array());
        }
    }
    else if (response.startsWith("MESSAGES_DATA:")) {
        // Formato: MESSAGES_DATA:jsondata
        QString jsonStr = response.mid(14);
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        if (doc.isArray()) {
            emit messagesDataReceived(doc.array());
        }
    }
    else if (response.startsWith("FILE_TRANSFER_PROGRESS:")) {
        // Formato: FILE_TRANSFER_PROGRESS:path:bytesReceived:totalBytes
        QStringList parts = response.mid(22).split(':', QString::SkipEmptyParts);
        if (parts.size() >= 3) {
            QString filePath = parts[0];
            qint64 bytesReceived = parts[1].toLongLong();
            qint64 totalBytes = parts[2].toLongLong();

            emit fileTransferProgress(filePath, bytesReceived, totalBytes);
        }
    }
    else {
        emit unknownResponseReceived(response);
    }
}

/**
 * Establece el estado de conexión y emite la señal correspondiente
 */
void AdbSocketClient::setConnectionState(ConnectionState state)
{
    if (m_connectionState != state) {
        m_connectionState = state;
        emit connectionStateChanged(state);
    }
}
