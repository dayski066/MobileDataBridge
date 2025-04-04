#ifndef DATAANALYZER_H
#define DATAANALYZER_H

#include <QObject>
#include <QProcess>
#include <QMap>
#include <QList>
#include <QVariantMap>
#include <QDateTime>
#include <QQueue>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include "devicemanager.h"

// Estructura para representar un elemento de datos individual
struct DataItem {
    QString id;           // Identificador único del elemento (ej. ruta de archivo para fotos)
    QString displayName;  // Nombre para mostrar en la UI
    QVariantMap data;     // Datos específicos del elemento (campos de contacto, contenido de mensaje, etc.)
    QString filePath;     // Ruta del archivo si es un elemento multimedia
    qint64 size;          // Tamaño en bytes (para multimedia)
    QDateTime dateTime;   // Fecha/hora asociada al elemento
};

// Estructura para representar un conjunto de datos
struct DataSet {
    QString type;         // Tipo de datos ("contacts", "messages", "photos", etc.)
    QList<DataItem> items; // Elementos en este conjunto
    qint64 totalSize;     // Tamaño total en bytes
    bool isSupported;     // Si este tipo de datos es compatible con la transferencia actual
    QString errorMessage; // Mensaje de error si ocurrió alguno durante el análisis
};

// Estructura para una tarea de análisis
struct AnalysisTask {
    QString deviceId;
    QString dataType;
    bool quickScan;
    QVariantMap data;     // Para pasar datos extra (ej. basePath para fotos)
    bool useBridgeClient; // Indica si se debe usar Bridge Client para esta tarea
};

/**
 * @class DataAnalyzer
 * @brief Clase encargada de analizar y catalogar datos en dispositivos
 *
 * Esta clase se encarga de escanear dispositivos para encontrar datos transferibles,
 * usando métodos directos (ADB, libimobiledevice) o a través de Bridge Client cuando
 * está disponible. Maneja el análisis de fotos, videos, contactos, mensajes, etc.
 */
class DataAnalyzer : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Constructor
     * @param deviceManager Puntero al gestor de dispositivos
     * @param parent Objeto padre (opcional)
     */
    explicit DataAnalyzer(DeviceManager *deviceManager, QObject *parent = nullptr);

    /**
     * @brief Destructor
     */
    ~DataAnalyzer();

    /**
     * @brief Analiza un dispositivo para encontrar datos transferibles
     * @param deviceId Identificador del dispositivo
     * @param quickScan Si es verdadero, realiza un análisis más rápido pero menos completo
     * @return true si el análisis se inició correctamente
     */
    bool analyzeDevice(const QString &deviceId, bool quickScan = false);

    /**
     * @brief Obtiene un conjunto de datos previamente analizado
     * @param deviceId Identificador del dispositivo
     * @param dataType Tipo de datos ("photos", "contacts", etc.)
     * @return Conjunto de datos solicitado, o conjunto vacío si no existe
     */
    DataSet getDataSet(const QString &deviceId, const QString &dataType);

    /**
     * @brief Obtiene los tipos de datos soportados para transferencia entre dos dispositivos
     * @param sourceId ID del dispositivo origen
     * @param destId ID del dispositivo destino
     * @return Lista de tipos de datos soportados
     */
    QStringList getSupportedDataTypes(const QString &sourceId, const QString &destId);

    /**
     * @brief Calcula el tamaño total de los tipos de datos seleccionados
     * @param deviceId Identificador del dispositivo
     * @param dataTypes Lista de tipos de datos
     * @return Tamaño total en bytes
     */
    qint64 getTotalSize(const QString &deviceId, const QStringList &dataTypes);

    /**
     * @brief Verifica si un tipo de datos es soportado entre dispositivos
     * @param sourceType Tipo de dispositivo origen ("android", "ios")
     * @param destType Tipo de dispositivo destino
     * @param dataType Tipo de datos a verificar
     * @return true si es soportado
     */
    bool isTypeSupported(const QString &sourceType, const QString &destType, const QString &dataType);

    /**
     * @brief Obtiene la razón por la que un tipo de datos no es soportado
     * @param sourceType Tipo de dispositivo origen
     * @param destType Tipo de dispositivo destino
     * @param dataType Tipo de datos
     * @return Mensaje explicativo o cadena vacía si es soportado
     */
    QString getIncompatibilityReason(const QString &sourceType, const QString &destType, const QString &dataType);

signals:
    /**
     * @brief Señal emitida cuando se inicia el análisis de un dispositivo
     * @param deviceId Identificador del dispositivo
     */
    void analysisStarted(const QString &deviceId);

    /**
     * @brief Señal emitida para reportar el progreso del análisis
     * @param deviceId Identificador del dispositivo
     * @param dataType Tipo de datos que se está analizando
     * @param progress Progreso de 0 a 100
     */
    void analysisProgress(const QString &deviceId, const QString &dataType, int progress);

    /**
     * @brief Señal emitida cuando se completa el análisis de un dispositivo
     * @param deviceId Identificador del dispositivo
     */
    void analysisComplete(const QString &deviceId);

    /**
     * @brief Señal emitida cuando ocurre un error durante el análisis
     * @param deviceId Identificador del dispositivo
     * @param dataType Tipo de datos donde ocurrió el error, o "all" para error general
     * @param errorMessage Mensaje descriptivo del error
     */
    void analysisError(const QString &deviceId, const QString &dataType, const QString &errorMessage);

    /**
     * @brief Señal emitida cuando se actualiza un conjunto de datos
     * @param deviceId Identificador del dispositivo
     * @param dataType Tipo de datos actualizado
     */
    void dataSetUpdated(const QString &deviceId, const QString &dataType);

    /**
     * @brief Señal interna para procesar la cola de tareas
     * @param deviceId Identificador del dispositivo que finalizó una tarea
     */
    void analysisFinishedForDevice(const QString &deviceId);

private slots:
    /**
     * @brief Procesa la siguiente tarea de análisis en la cola
     */
    void processNextAnalysisTask();

    /**
     * @brief Maneja la finalización del proceso de análisis externo
     * @param exitCode Código de salida del proceso
     * @param exitStatus Estado de salida
     */
    void onAnalysisProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

    /**
     * @brief Maneja eventos cuando Bridge Client recibe información del dispositivo
     * @param deviceInfo Objeto JSON con información del dispositivo
     */
    void onBridgeClientDeviceInfo(const QJsonObject &deviceInfo);

    /**
     * @brief Maneja datos multimedia recibidos de Bridge Client
     * @param index Índice del lote actual
     * @param count Total de lotes
     * @param mediaData Array JSON con datos multimedia
     */
    void onBridgeClientMediaData(int index, int count, const QJsonArray &mediaData);

    /**
     * @brief Maneja datos de archivos recibidos de Bridge Client
     * @param index Índice del lote actual
     * @param count Total de lotes
     * @param filesData Array JSON con datos de archivos
     */
    void onBridgeClientFilesData(int index, int count, const QJsonArray &filesData);

    /**
     * @brief Maneja evento de escaneo completado en Bridge Client
     */
    void onBridgeClientScanCompleted();

    /**
     * @brief Maneja errores de escaneo en Bridge Client
     * @param errorMessage Mensaje de error
     */
    void onBridgeClientScanError(const QString &errorMessage);

    /**
     * @brief Maneja progreso de escaneo en Bridge Client
     * @param progress Porcentaje de progreso (0-100)
     */
    void onBridgeClientScanProgress(int progress);

    /**
     * @brief Maneja datos de contactos recibidos de Bridge Client
     * @param contactsData Array JSON con datos de contactos
     */
    void onBridgeClientContactsData(const QJsonArray &contactsData);

    /**
     * @brief Maneja datos de mensajes recibidos de Bridge Client
     * @param messagesData Array JSON con datos de mensajes
     */
    void onBridgeClientMessagesData(const QJsonArray &messagesData);

private:
    /**
     * @brief Inicia una tarea de análisis específica
     * @param task Tarea a ejecutar
     */
    void startAnalysisTask(const AnalysisTask& task);

    /**
     * @brief Inicia análisis para un dispositivo Android
     * @param task Tarea de análisis
     */
    void startAndroidAnalysis(const AnalysisTask& task);

    /**
     * @brief Inicia análisis para un dispositivo iOS
     * @param task Tarea de análisis
     */
    void startIOSAnalysis(const AnalysisTask& task);

    /**
     * @brief Inicia análisis con Bridge Client
     * @param task Tarea de análisis
     */
    void startAndroidAnalysisViaBridge(const AnalysisTask& task);

    // Métodos de análisis específicos para Android usando métodos directos
    void analyzeAndroidPhotosReal(const QString &deviceId);
    void analyzeAndroidContacts(const QString &deviceId);
    void analyzeAndroidMessages(const QString &deviceId);
    void analyzeAndroidCalls(const QString &deviceId);

    // Métodos de análisis específicos para iOS (simulados o reales)
    void analyzeIOSContacts(const QString &deviceId);
    void analyzeIOSMessages(const QString &deviceId);
    void analyzeIOSPhotos(const QString &deviceId);
    void analyzeIOSCalls(const QString &deviceId);

    // Métodos parser para diferentes formatos de datos
    QList<DataItem> parseAndroidContacts(const QString &output);
    QList<DataItem> parseAndroidMessages(const QString &output);
    QList<DataItem> parseAndroidCalls(const QString &output);
    QList<DataItem> parseAndroidPhotoList(const QString &output, const QString& basePath);
    QList<DataItem> parseJsonMediaData(const QJsonArray &jsonArray);
    QList<DataItem> parseJsonFilesData(const QJsonArray &jsonArray);
    QList<DataItem> parseJsonContactsData(const QJsonArray &jsonArray);
    QList<DataItem> parseJsonMessagesData(const QJsonArray &jsonArray);

    // Métodos auxiliares
    QString getAdbCommand(const QString &deviceId, const QString &command);
    QString getIdeviceCommand(const QString &deviceId, const QString &tool, const QStringList &args);
    void finalizeAnalysis(const QString& deviceId, const QString& dataType, bool success, const QString& errorMsg = "");
    void createBasicDataSet(const QString &deviceId, const QString &dataType);
    void disconnectBridgeClientSignals(const QString &deviceId);
    void updateBridgeScanProgress(const QString &deviceId, const QString &dataType, int progress);

    // Variables miembro
    DeviceManager *m_deviceManager;
    QProcess *m_analysisProcess;
    QMap<QString, QMap<QString, DataSet>> m_dataSets; // Mapa de [deviceId][dataType] -> DataSet

    QQueue<AnalysisTask> m_analysisQueue; // Cola para tareas de análisis pendientes
    AnalysisTask m_currentAnalysisTask;   // Tarea que se está procesando actualmente
    bool m_isAnalyzing; // Flag que indica si el procesador de la cola está activo
    QMap<QString, int> m_pendingTasksPerDevice; // Rastrea tareas pendientes para señal analysisComplete

    // Mapas para el seguimiento de tareas de Bridge Client
    QMap<QString, QMap<QString, bool>> m_bridgeScanComplete; // [deviceId][dataType] -> completado
    QMutex m_dataSetMutex; // Mutex para proteger acceso a m_dataSets
};

#endif // DATAANALYZER_H
