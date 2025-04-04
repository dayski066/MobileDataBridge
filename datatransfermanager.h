#ifndef DATATRANSFERMANAGER_H
#define DATATRANSFERMANAGER_H

#include <QObject>
#include <QProcess>
#include <QMap>
#include <QQueue>
#include <QMutex>
#include <QElapsedTimer>
#include "devicemanager.h"
#include "dataanalyzer.h"

// Estructura para seguimiento de tareas de transferencia
struct TransferTask {
    QString sourceId;
    QString destId;
    QString dataType;
    bool clearDestination;
    QList<DataItem> itemsToTransfer;
    int totalItems;
    int processedItems;
    qint64 totalSize;
    qint64 processedSize;
    int currentItemIndex;
    QString currentItemName;
    QString status;
    QString tempFilePath;
    QString errorMessage;
    bool useBridgeClient;  // Indica si debe usarse Bridge Client para esta tarea
};

/**
 * @class DataTransferManager
 * @brief Clase responsable de transferir datos entre dispositivos
 *
 * Esta clase maneja el proceso de transferencia de datos entre dispositivos,
 * gestionando diferentes tipos de datos (fotos, contactos, etc.) y adaptándose
 * a las capacidades disponibles (ADB directo o Bridge Client).
 */
class DataTransferManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructor
     * @param deviceManager Puntero al gestor de dispositivos
     * @param dataAnalyzer Puntero al analizador de datos
     * @param parent Objeto padre (opcional)
     */
    explicit DataTransferManager(DeviceManager *deviceManager, DataAnalyzer *dataAnalyzer, QObject *parent = nullptr);

    /**
     * @brief Destructor
     */
    ~DataTransferManager();

    /**
     * @brief Inicia una transferencia de datos entre dispositivos
     * @param sourceId ID del dispositivo origen
     * @param destId ID del dispositivo destino
     * @param dataTypes Lista de tipos de datos a transferir
     * @param clearDestination Si es true, se borrarán datos existentes en destino
     * @return true si la transferencia se inició correctamente
     */
    bool startTransfer(const QString &sourceId, const QString &destId, const QStringList &dataTypes, bool clearDestination = false);

    /**
     * @brief Cancela la transferencia en curso
     */
    void cancelTransfer();

    /**
     * @brief Verifica si hay una transferencia en progreso
     * @return true si hay una transferencia activa
     */
    bool isTransferInProgress() const;

    /**
     * @brief Obtiene el progreso general de la transferencia
     * @return Porcentaje de progreso (0-100)
     */
    int getOverallProgress() const;

    /**
     * @brief Obtiene información sobre las tareas activas
     * @return Lista de tareas activas
     */
    QList<TransferTask> getActiveTasksInfo() const;

signals:
    /**
     * @brief Señal emitida cuando se inicia una transferencia
     * @param totalSize Tamaño total estimado en bytes
     */
    void transferStarted(qint64 totalSize);

    /**
     * @brief Señal emitida para reportar progreso general
     * @param overallProgress Porcentaje de progreso (0-100)
     */
    void transferProgress(int overallProgress);

    /**
     * @brief Señal emitida cuando se inicia una tarea específica
     * @param dataType Tipo de datos
     * @param totalItems Cantidad total de elementos
     */
    void transferTaskStarted(const QString &dataType, int totalItems);

    /**
     * @brief Señal emitida para reportar progreso de una tarea
     * @param dataType Tipo de datos
     * @param taskProgressPercent Porcentaje de progreso de la tarea
     * @param processedItems Elementos procesados
     * @param totalItems Total de elementos
     * @param processedSize Bytes procesados
     * @param totalSize Bytes totales
     * @param currentItemName Nombre del elemento actual
     */
    void transferTaskProgress(const QString &dataType, int taskProgressPercent,
                              int processedItems, int totalItems,
                              qint64 processedSize, qint64 totalSize,
                              const QString& currentItemName);

    /**
     * @brief Señal emitida cuando se completa una tarea
     * @param dataType Tipo de datos
     * @param successCount Cantidad de elementos transferidos exitosamente
     */
    void transferTaskCompleted(const QString &dataType, int successCount);

    /**
     * @brief Señal emitida cuando falla una tarea
     * @param dataType Tipo de datos
     * @param errorMessage Mensaje de error
     */
    void transferTaskFailed(const QString &dataType, const QString &errorMessage);

    /**
     * @brief Señal emitida cuando se completa toda la transferencia
     */
    void transferCompleted();

    /**
     * @brief Señal emitida si se cancela la transferencia
     */
    void transferCancelled();

    /**
     * @brief Señal emitida si falla toda la transferencia
     * @param errorMessage Mensaje de error
     */
    void transferFailed(const QString &errorMessage);

    /**
     * @brief Señal emitida cuando finaliza la transferencia
     * @param success true si fue exitosa, false en caso contrario
     * @param message Mensaje descriptivo
     */
    void transferFinished(bool success, const QString& message);

private slots:
    /**
     * @brief Maneja la finalización del proceso de pull (copia desde origen)
     * @param exitCode Código de salida
     * @param exitStatus Estado de salida
     */
    void onPullProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

    /**
     * @brief Maneja la finalización del proceso de push (copia a destino)
     * @param exitCode Código de salida
     * @param exitStatus Estado de salida
     */
    void onPushProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

    /**
     * @brief Maneja eventos cuando un archivo está listo para transferir desde Bridge Client
     * @param filePath Ruta del archivo
     */
    void onBridgeClientFileReady(const QString &filePath);

    /**
     * @brief Maneja eventos cuando un archivo ha sido guardado por Bridge Client
     * @param result Resultado de la operación
     */
    void onBridgeClientFileSaved(const QString &result);

    /**
     * @brief Maneja eventos de progreso de transferencia desde Bridge Client
     * @param filePath Ruta del archivo
     * @param bytesReceived Bytes recibidos
     * @param totalBytes Bytes totales
     */
    void onBridgeClientFileProgress(const QString &filePath, qint64 bytesReceived, qint64 totalBytes);

    /**
     * @brief Maneja errores de Bridge Client
     * @param errorMessage Mensaje de error
     */
    void onBridgeClientError(const QString &errorMessage);

private:
    /**
     * @brief Inicia la siguiente tarea de transferencia
     */
    void startNextTransferTask();

    /**
     * @brief Procesa el siguiente paso de transferencia
     */
    void processNextTransferStep();

    /**
     * @brief Finaliza la tarea actual
     * @param success true si fue exitosa, false en caso contrario
     * @param errorMessage Mensaje de error (opcional)
     */
    void finalizeCurrentTask(bool success, const QString& errorMessage = QString());

    /**
     * @brief Emite una señal de progreso general
     */
    void emitOverallProgress();

    /**
     * @brief Emite una señal de progreso para la tarea actual
     */
    void emitTaskProgress();

    /**
     * @brief Implementa transferencia entre dispositivos Android
     * @param task Tarea de transferencia
     * @return true si se inició correctamente
     */
    bool transferAndroidToAndroid(TransferTask &task);

    /**
     * @brief Implementa transferencia de Android a iOS
     * @param task Tarea de transferencia
     * @return true si se inició correctamente
     */
    bool transferAndroidToIOS(TransferTask &task);

    /**
     * @brief Implementa transferencia de iOS a Android
     * @param task Tarea de transferencia
     * @return true si se inició correctamente
     */
    bool transferIOSToAndroid(TransferTask &task);

    /**
     * @brief Implementa transferencia entre dispositivos iOS
     * @param task Tarea de transferencia
     * @return true si se inició correctamente
     */
    bool transferIOSToIOS(TransferTask &task);

    /**
     * @brief Inicia la descarga de foto usando ADB directo
     */
    void startPhotoPull();

    /**
     * @brief Inicia la subida de foto usando ADB directo
     */
    void startPhotoPush();

    /**
     * @brief Inicia la transferencia de una foto usando Bridge Client
     * @param item Elemento a transferir
     * @return true si se inició correctamente
     */
    bool startPhotoPullViaBridge(const DataItem &item);

    /**
     * @brief Implementa transferencia de contactos
     * @param task Tarea de transferencia
     * @return true si se inició correctamente
     */
    bool startContactsTransfer(TransferTask &task);

    /**
     * @brief Implementa transferencia de mensajes
     * @param task Tarea de transferencia
     * @return true si se inició correctamente
     */
    bool startMessagesTransfer(TransferTask &task);

    /**
     * @brief Prepara un directorio temporal para la transferencia
     * @return true si se creó correctamente
     */
    bool prepareTempDirectory();

    /**
     * @brief Limpia el directorio temporal
     * @return true si se limpió correctamente
     */
    bool cleanupTempDirectory();

    /**
     * @brief Obtiene una ruta temporal para un elemento
     * @param itemName Nombre del elemento
     * @return Ruta completa al archivo temporal
     */
    QString getTempPathForItem(const QString& itemName) const;

    /**
     * @brief Verifica si Bridge Client está disponible para transferencia
     * @param deviceId ID del dispositivo
     * @return true si está disponible
     */
    bool isBridgeClientAvailable(const QString &deviceId) const;

    /**
     * @brief Conecta con Bridge Client para transferencia
     * @param deviceId ID del dispositivo
     * @param role Rol ("source" o "destination")
     * @return true si se conectó correctamente
     */
    bool connectToBridgeClient(const QString &deviceId, const QString &role);

    /**
     * @brief Desconecta señales de Bridge Client
     * @param deviceId ID del dispositivo
     */
    void disconnectBridgeClientSignals(const QString &deviceId);

    // Variables miembro
    DeviceManager *m_deviceManager;
    DataAnalyzer *m_dataAnalyzer;
    bool m_isTransferring;
    QQueue<QString> m_dataTypeQueue;
    QMap<QString, TransferTask> m_taskStates;
    TransferTask m_currentTask;
    QProcess m_pullProcess;
    QProcess m_pushProcess;
    QString m_tempDirOwner;
    qint64 m_totalTransferSize;
    qint64 m_totalTransferredSizePreviousTasks;
    QMutex m_transferMutex;
    QElapsedTimer m_transferTimer;
};

#endif // DATATRANSFERMANAGER_H
