#ifndef ADBSOCKETCLIENT_H
#define ADBSOCKETCLIENT_H

#include <QObject>
#include <QProcess>
#include <QTcpSocket>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QTimer>
#include <QCoreApplication>
#include <QMutex>
#include <QQueue>

/**
 * @brief Clase cliente para comunicación con Bridge Client Android vía socket TCP
 *
 * Esta clase maneja la comunicación bidireccional con la aplicación Bridge Client
 * en dispositivos Android, utilizando un socket TCP redirigido a través de ADB.
 * Permite enviar comandos, recibir respuestas y gestionar el estado de la conexión.
 */
class AdbSocketClient : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Enumeración de estados del cliente
     */
    enum ConnectionState {
        Disconnected,     ///< Sin conexión activa
        Connecting,       ///< Intentando establecer conexión
        Connected,        ///< Conexión establecida
        Error             ///< Estado de error
    };

    /**
     * @brief Enumeración de roles de transferencia
     */
    enum TransferRole {
        Source,         ///< Dispositivo de origen (envía datos)
        Destination,    ///< Dispositivo de destino (recibe datos)
        Unknown         ///< Rol no definido
    };

    /**
     * @brief Constructor
     * @param parent Objeto padre (por defecto nullptr)
     */
    explicit AdbSocketClient(QObject *parent = nullptr);

    /**
     * @brief Destructor
     */
    ~AdbSocketClient();

    /**
     * @brief Establecer conexión con el dispositivo Android
     * @param deviceId Identificador del dispositivo
     * @return true si la conexión se inició correctamente, false en caso contrario
     */
    bool connectToDevice(const QString &deviceId);

    /**
     * @brief Desconectar del dispositivo
     */
    void disconnectFromDevice();

    /**
     * @brief Verificar si hay una conexión activa
     * @return true si está conectado, false en caso contrario
     */
    bool isConnected() const;

    /**
     * @brief Establecer el rol del dispositivo
     * @param role Rol ("source" o "destination")
     * @return true si el comando se envió correctamente, false en caso contrario
     */
    bool setRole(const QString &role);

    /**
     * @brief Iniciar el escaneo del dispositivo
     * @return true si el comando se envió correctamente, false en caso contrario
     */
    bool startScan();

    /**
     * @brief Obtener información del dispositivo
     * @return true si el comando se envió correctamente, false en caso contrario
     */
    bool getDeviceInfo();

    /**
     * @brief Solicitar un archivo específico
     * @param filePath Ruta del archivo en el dispositivo
     * @return true si el comando se envió correctamente, false en caso contrario
     */
    bool requestFile(const QString &filePath);

    /**
     * @brief Guardar un archivo en el dispositivo
     * @param fileInfo Información sobre el archivo a guardar
     * @return true si el comando se envió correctamente, false en caso contrario
     */
    bool saveFile(const QString &fileInfo);

    /**
     * @brief Enviar comando ping para verificar conexión
     * @return true si el comando se envió correctamente, false en caso contrario
     */
    bool ping();

    /**
     * @brief Configurar y conectar Bridge Client automáticamente
     * @param deviceId Identificador del dispositivo
     * @param adbPath Ruta al ejecutable ADB
     * @return true si la configuración se inició correctamente, false en caso contrario
     */
    bool setupBridgeClient(const QString &deviceId, const QString &adbPath);

    /**
     * @brief Obtener el estado actual de conexión
     * @return Estado de conexión
     */
    ConnectionState getConnectionState() const;

    /**
     * @brief Obtener el rol actual de transferencia
     * @return Rol de transferencia
     */
    TransferRole getTransferRole() const;

    /**
     * @brief Solicitar archivos multimedia específicos
     * @param fileIndices Índices de los archivos requeridos
     * @return true si el comando se envió correctamente, false en caso contrario
     */
    bool requestMediaFiles(const QList<int> &fileIndices);

    /**
     * @brief Solicitar contactos específicos
     * @param contactIds Lista de IDs de contactos
     * @return true si el comando se envió correctamente, false en caso contrario
     */
    bool requestContacts(const QStringList &contactIds);

    /**
     * @brief Solicitar mensajes específicos
     * @param messageIds Lista de IDs de mensajes
     * @return true si el comando se envió correctamente, false en caso contrario
     */
    bool requestMessages(const QStringList &messageIds);

    /**
     * @brief Cancelar operación en curso
     * @return true si el comando se envió correctamente, false en caso contrario
     */
    bool cancelOperation();

signals:
    // Señales para eventos de conexión
    /**
     * @brief Señal emitida cuando se establece la conexión
     */
    void connected();

    /**
     * @brief Señal emitida cuando se pierde la conexión
     */
    void disconnected();

    /**
     * @brief Señal emitida cuando ocurre un error
     * @param errorMessage Mensaje de error
     */
    void errorOccurred(const QString &errorMessage);

    // Señales para respuestas
    /**
     * @brief Señal emitida cuando se recibe información del dispositivo
     * @param deviceInfo Objeto JSON con la información
     */
    void deviceInfoReceived(const QJsonObject &deviceInfo);

    /**
     * @brief Señal emitida cuando inicia el escaneo
     */
    void scanStarted();

    /**
     * @brief Señal emitida para reportar progreso del escaneo
     * @param progress Porcentaje de progreso (0-100)
     */
    void scanProgress(int progress);

    /**
     * @brief Señal emitida cuando el escaneo finaliza
     */
    void scanCompleted();

    /**
     * @brief Señal emitida si ocurre un error durante el escaneo
     * @param errorMessage Mensaje de error
     */
    void scanError(const QString &errorMessage);

    /**
     * @brief Señal emitida con lotes de datos multimedia
     * @param index Índice del lote actual
     * @param count Total de lotes
     * @param mediaData Array JSON con datos multimedia
     */
    void mediaDataReceived(int index, int count, const QJsonArray &mediaData);

    /**
     * @brief Señal emitida con lotes de datos de archivos
     * @param index Índice del lote actual
     * @param count Total de lotes
     * @param filesData Array JSON con datos de archivos
     */
    void filesDataReceived(int index, int count, const QJsonArray &filesData);

    /**
     * @brief Señal emitida cuando un archivo solicitado está listo
     * @param filePath Ruta del archivo
     */
    void fileReady(const QString &filePath);

    /**
     * @brief Señal emitida cuando un archivo se ha guardado
     * @param result Resultado de la operación
     */
    void fileSaved(const QString &result);

    /**
     * @brief Señal emitida en respuesta a un ping
     */
    void pongReceived();

    /**
     * @brief Señal emitida cuando se recibe una respuesta no reconocida
     * @param response Texto de la respuesta
     */
    void unknownResponseReceived(const QString &response);

    /**
     * @brief Señal emitida cuando se reciben datos de contactos
     * @param contactsData Array JSON con datos de contactos
     */
    void contactsDataReceived(const QJsonArray &contactsData);

    /**
     * @brief Señal emitida cuando se reciben datos de mensajes
     * @param messagesData Array JSON con datos de mensajes
     */
    void messagesDataReceived(const QJsonArray &messagesData);

    /**
     * @brief Señal emitida cuando cambia el estado de la conexión
     * @param state Nuevo estado
     */
    void connectionStateChanged(ConnectionState state);

    /**
     * @brief Señal emitida cuando un archivo está en progreso de transferencia
     * @param filePath Ruta del archivo
     * @param bytesReceived Bytes recibidos
     * @param totalBytes Bytes totales
     */
    void fileTransferProgress(const QString &filePath, qint64 bytesReceived, qint64 totalBytes);

private slots:
    /**
     * @brief Slot para leer datos del socket
     */
    void readFromSocket();

    /**
     * @brief Slot para manejar errores del socket
     * @param error Código de error
     */
    void handleSocketError(QAbstractSocket::SocketError error);

    /**
     * @brief Slot para procesar finalización del comando adb forward
     * @param exitCode Código de salida
     * @param exitStatus Estado de salida
     */
    void onAdbForwardFinished(int exitCode, QProcess::ExitStatus exitStatus);

    /**
     * @brief Slot para procesar finalización de instalación de app
     * @param exitCode Código de salida
     * @param exitStatus Estado de salida
     */
    void onInstallAppFinished(int exitCode, QProcess::ExitStatus exitStatus);

    /**
     * @brief Slot para procesar finalización de lanzamiento de app
     * @param exitCode Código de salida
     * @param exitStatus Estado de salida
     */
    void onLaunchAppFinished(int exitCode, QProcess::ExitStatus exitStatus);

    /**
     * @brief Slot para manejar reconexiones automáticas
     */
    void reconnectTimer();

    /**
     * @brief Slot para enviar siguientes comandos en cola
     */
    void processNextCommand();

    /**
     * @brief Slot para verificar estado de conexión periódicamente
     */
    void checkConnectionState();

private:
    // Métodos privados
    /**
     * @brief Configurar reenvío de puertos TCP
     * @param deviceId Identificador del dispositivo
     * @param adbPath Ruta al ejecutable ADB
     * @return true si la operación fue exitosa, false en caso contrario
     */
    bool forwardTcpPort(const QString &deviceId, const QString &adbPath);

    /**
     * @brief Verificar si Bridge Client está instalado
     * @param deviceId Identificador del dispositivo
     * @param adbPath Ruta al ejecutable ADB
     * @return true si está instalado, false en caso contrario
     */
    bool isAppInstalled(const QString &deviceId, const QString &adbPath);

    /**
     * @brief Instalar Bridge Client en el dispositivo
     * @param deviceId Identificador del dispositivo
     * @param adbPath Ruta al ejecutable ADB
     * @return true si la operación se inició correctamente, false en caso contrario
     */
    bool installApp(const QString &deviceId, const QString &adbPath);

    /**
     * @brief Lanzar la aplicación Bridge Client
     * @param deviceId Identificador del dispositivo
     * @param adbPath Ruta al ejecutable ADB
     * @param role Rol inicial ("source" o "destination")
     * @return true si la operación se inició correctamente, false en caso contrario
     */
    bool launchApp(const QString &deviceId, const QString &adbPath, const QString &role);

    /**
     * @brief Enviar un comando al dispositivo
     * @param command Comando a enviar
     * @return true si el comando se envió correctamente, false en caso contrario
     */
    bool sendCommand(const QString &command);

    /**
     * @brief Poner un comando en cola para envío secuencial
     * @param command Comando a encolar
     * @return true si se encoló correctamente, false en caso contrario
     */
    bool enqueueCommand(const QString &command);

    /**
     * @brief Procesar una respuesta recibida
     * @param response Texto de la respuesta
     */
    void processResponse(const QString &response);

    /**
     * @brief Establecer el estado de conexión
     * @param state Nuevo estado
     */
    void setConnectionState(ConnectionState state);

    // Variables miembro
    QTcpSocket *m_socket;             ///< Socket TCP para comunicación
    QProcess m_adbProcess;           ///< Proceso para comandos ADB
    QString m_deviceId;              ///< ID del dispositivo conectado
    QString m_adbPath;               ///< Ruta al ejecutable ADB
    bool m_connected;                ///< Flag de conexión activa
    QString m_buffer;                ///< Buffer para datos recibidos
    QTimer *m_reconnectTimer;        ///< Timer para reconexión automática
    int m_reconnectAttempts;         ///< Contador de intentos de reconexión
    ConnectionState m_connectionState; ///< Estado actual de la conexión
    TransferRole m_transferRole;     ///< Rol actual de transferencia
    QMutex m_commandMutex;           ///< Mutex para sincronizar envío de comandos
    QQueue<QString> m_commandQueue;  ///< Cola de comandos pendientes
    QTimer *m_connectionCheckTimer;  ///< Timer para verificar estado periódicamente
    bool m_isProcessingCommands;     ///< Flag para control de procesamiento de cola

    // Constantes
    static const int PORT = 38300;                ///< Puerto para comunicación ADB
    static const int RECONNECT_INTERVAL = 5000;   ///< 5 segundos entre intentos
    static const int MAX_RECONNECT_ATTEMPTS = 3;  ///< Máximo de intentos de reconexión
    static const int CONNECTION_CHECK_INTERVAL = 10000; ///< 10 segundos para verificación
    static const int COMMAND_TIMEOUT = 30000;     ///< 30 segundos de timeout por comando
};

#endif // ADBSOCKETCLIENT_H
