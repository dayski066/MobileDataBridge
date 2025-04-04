#include "dataanalyzer.h"
#include <QDebug>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QTimer>
#include <QMutexLocker>

/**
 * Constructor de la clase DataAnalyzer
 */
DataAnalyzer::DataAnalyzer(DeviceManager *deviceManager, QObject *parent)
    : QObject(parent)
    , m_deviceManager(deviceManager)
    , m_analysisProcess(new QProcess(this))
    , m_isAnalyzing(false)
{
    // Conectar la señal finished del proceso
    connect(m_analysisProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &DataAnalyzer::onAnalysisProcessFinished);

    // Conectar señal interna para procesar la cola
    connect(this, &DataAnalyzer::analysisFinishedForDevice, this, &DataAnalyzer::processNextAnalysisTask, Qt::QueuedConnection);
}

/**
 * Destructor
 */
DataAnalyzer::~DataAnalyzer()
{
    // QProcess se limpia automáticamente debido a la propiedad del padre
}

/**
 * Analiza un dispositivo para encontrar datos transferibles
 */
bool DataAnalyzer::analyzeDevice(const QString &deviceId, bool quickScan)
{
    DeviceInfo device = m_deviceManager->getDeviceInfo(deviceId);
    if (device.id.isEmpty()) {
        qWarning() << "AnalyzeDevice: Dispositivo no encontrado:" << deviceId;
        return false;
    }

    if (!device.authorized) {
        qWarning() << "AnalyzeDevice: Dispositivo no autorizado:" << deviceId;
        emit analysisError(deviceId, "all", "El dispositivo no está autorizado para acceder a los datos.");
        return false;
    }

    // Comprobar si Bridge Client está disponible para este dispositivo
    bool useBridgeClient = false;
    if (device.type == "android") {
        // Inicializar Bridge Client si no está ya conectado
        if (!m_deviceManager->isBridgeClientConnected(deviceId)) {
            useBridgeClient = m_deviceManager->setupBridgeClient(deviceId);
            if (useBridgeClient) {
                qDebug() << "Bridge Client inicializado para el dispositivo:" << deviceId;
            } else {
                qDebug() << "No se pudo inicializar Bridge Client, usando ADB directo";
            }
        } else {
            useBridgeClient = true;
            qDebug() << "Bridge Client ya conectado para el dispositivo:" << deviceId;
        }
    }

    // Limpiar resultados previos para este dispositivo antes de empezar
    QMutexLocker locker(&m_dataSetMutex);
    if (m_dataSets.contains(deviceId)) {
        m_dataSets[deviceId].clear();
    } else {
        m_dataSets[deviceId] = QMap<QString, DataSet>();
    }
    locker.unlock();

    // Poner en cola las tareas de análisis
    QStringList dataTypesToAnalyze;
    if (device.type == "android") {
        // Tipos de datos a analizar para Android
        dataTypesToAnalyze << "photos" << "videos" << "contacts" << "messages" << "calls";

        // Si tenemos Bridge Client, podemos ampliar tipos
        if (useBridgeClient) {
            dataTypesToAnalyze << "music" << "documents" << "applications";
        }
    } else if (device.type == "ios") {
        // Tipos de datos a analizar para iOS
        dataTypesToAnalyze << "photos" << "contacts" << "messages" << "calls";
    } else {
        emit analysisError(deviceId, "all", "Tipo de dispositivo no soportado: " + device.type);
        return false;
    }

    // Resetear contador de tareas pendientes y emitir analysisStarted
    m_pendingTasksPerDevice[deviceId] = dataTypesToAnalyze.size();
    emit analysisStarted(deviceId);

    // Si estamos usando Bridge Client y es una exploración completa, limpiar trackeo
    if (useBridgeClient && !quickScan) {
        m_bridgeScanComplete[deviceId].clear();
    }

    for (const QString& type : dataTypesToAnalyze) {
        AnalysisTask task;
        task.deviceId = deviceId;
        task.dataType = type;
        task.quickScan = quickScan;
        task.useBridgeClient = useBridgeClient;
        m_analysisQueue.enqueue(task);

        // Inicializar estado de Bridge Client para esta tarea si aplica
        if (useBridgeClient && !quickScan) {
            m_bridgeScanComplete[deviceId][type] = false;
        }
    }

    // Empezar a procesar la cola si no está ya en ejecución
    if (!m_isAnalyzing) {
        m_isAnalyzing = true;
        QMetaObject::invokeMethod(this, "processNextAnalysisTask", Qt::QueuedConnection);
    }

    return true;
}

/**
 * Procesa la siguiente tarea de análisis en la cola
 */
void DataAnalyzer::processNextAnalysisTask()
{
    if (m_analysisQueue.isEmpty()) {
        m_isAnalyzing = false;
        // No quedan tareas para ningún dispositivo
        return;
    }

    // Comprobar si el proceso de análisis está libre
    if (m_analysisProcess->state() != QProcess::NotRunning) {
        // El proceso está ocupado, esperar a que termine
        qDebug() << "Proceso de análisis ocupado, esperando...";
        return; // La señal finished disparará la siguiente llamada
    }

    m_currentAnalysisTask = m_analysisQueue.dequeue();
    qDebug() << "Procesando tarea de análisis para dispositivo:"
             << m_currentAnalysisTask.deviceId
             << "Tipo:" << m_currentAnalysisTask.dataType
             << "Usando Bridge Client:" << m_currentAnalysisTask.useBridgeClient;

    startAnalysisTask(m_currentAnalysisTask);

    // Si la cola está vacía después de dequeue, marcar como inactivo
    if (m_analysisQueue.isEmpty()) {
        m_isAnalyzing = false;
    }
}

/**
 * Inicia una tarea de análisis específica
 */
void DataAnalyzer::startAnalysisTask(const AnalysisTask& task)
{
    DeviceInfo device = m_deviceManager->getDeviceInfo(task.deviceId);
    if (device.id.isEmpty() || !device.authorized) {
        finalizeAnalysis(task.deviceId, task.dataType, false, "Dispositivo no disponible o no autorizado al iniciar tarea.");
        return;
    }

    emit analysisProgress(task.deviceId, task.dataType, 0); // Indicar inicio de tarea

    if (device.type == "android") {
        startAndroidAnalysis(task);
    } else if (device.type == "ios") {
        startIOSAnalysis(task);
    } else {
        finalizeAnalysis(task.deviceId, task.dataType, false, "Tipo de dispositivo no soportado.");
    }
}

/**
 * Inicia análisis para un dispositivo Android
 */
void DataAnalyzer::startAndroidAnalysis(const AnalysisTask& task)
{
    // Comprobar si debemos usar Bridge Client para esta tarea
    if (task.useBridgeClient) {
        // Verificar que el cliente de Bridge está disponible
        AdbSocketClient* bridgeClient = m_deviceManager->getBridgeClient(task.deviceId);
        if (bridgeClient && bridgeClient->isConnected()) {
            startAndroidAnalysisViaBridge(task);
            return;
        } else {
            qWarning() << "Bridge Client no disponible o no conectado. Usando ADB directo.";
            // Continuar con análisis directo como fallback
        }
    }

    // Usar métodos directos de ADB si no se usa Bridge Client o no está disponible
    if (task.dataType == "photos") {
        analyzeAndroidPhotosReal(task.deviceId);
    }
    else if (task.dataType == "contacts") {
        analyzeAndroidContacts(task.deviceId);
    }
    else if (task.dataType == "messages") {
        analyzeAndroidMessages(task.deviceId);
    }
    else if (task.dataType == "calls") {
        analyzeAndroidCalls(task.deviceId);
    }
    else {
        finalizeAnalysis(task.deviceId, task.dataType, false,
                         "Tipo de dato no soportado para análisis Android directo: " + task.dataType);
    }
}

/**
 * Inicia análisis para un dispositivo iOS
 */
void DataAnalyzer::startIOSAnalysis(const AnalysisTask& task)
{
    // Análisis iOS (simulado o real)
    if (task.dataType == "photos") {
        analyzeIOSPhotos(task.deviceId);
    } else if (task.dataType == "contacts") {
        analyzeIOSContacts(task.deviceId);
    } else if (task.dataType == "messages") {
        analyzeIOSMessages(task.deviceId);
    } else if (task.dataType == "calls") {
        analyzeIOSCalls(task.deviceId);
    } else {
        finalizeAnalysis(task.deviceId, task.dataType, false, "Tipo de dato no soportado para análisis iOS.");
        return;
    }

    // Para tareas que se manejan inmediatamente (simuladas)
    if (task.dataType == "photos" || task.dataType == "contacts" ||
        task.dataType == "messages" || task.dataType == "calls") {
        finalizeAnalysis(task.deviceId, task.dataType, true);
    }
}

/**
 * Obtiene un conjunto de datos previamente analizado
 */
DataSet DataAnalyzer::getDataSet(const QString &deviceId, const QString &dataType)
{
    // Devolver dataset existente o uno vacío/no soportado
    QMutexLocker locker(&m_dataSetMutex);
    if (m_dataSets.contains(deviceId) && m_dataSets[deviceId].contains(dataType)) {
        return m_dataSets[deviceId][dataType];
    }

    DataSet emptySet;
    emptySet.type = dataType;
    emptySet.totalSize = 0;
    emptySet.isSupported = false; // Por defecto no soportado si no se encuentra
    return emptySet;
}

/**
 * Obtiene los tipos de datos soportados para transferencia entre dos dispositivos
 */
QStringList DataAnalyzer::getSupportedDataTypes(const QString &sourceId, const QString &destId)
{
    // Determinar compatibilidad entre dispositivos
    DeviceInfo sourceDevice = m_deviceManager->getDeviceInfo(sourceId);
    DeviceInfo destDevice = m_deviceManager->getDeviceInfo(destId);

    if(sourceDevice.id.isEmpty() || destDevice.id.isEmpty()) {
        return QStringList(); // No se puede determinar si los dispositivos son desconocidos
    }

    // Lista base de tipos potencialmente transferibles
    QStringList allTypes = {"contacts", "messages", "photos", "videos", "calls", "calendar", "music", "documents", "applications"};
    QStringList supportedTypes;

    for (const QString &type : allTypes) {
        if (isTypeSupported(sourceDevice.type, destDevice.type, type)) {
            // Verificar si tenemos datos para este tipo
            QMutexLocker locker(&m_dataSetMutex);
            if (m_dataSets.contains(sourceId) && m_dataSets[sourceId].contains(type) &&
                !m_dataSets[sourceId][type].items.isEmpty()) {
                supportedTypes << type;
            }
        }
    }

    return supportedTypes;
}

/**
 * Calcula el tamaño total de los tipos de datos seleccionados
 */
qint64 DataAnalyzer::getTotalSize(const QString &deviceId, const QStringList &dataTypes)
{
    // Calcular tamaño total basado en datos analizados
    qint64 total = 0;

    QMutexLocker locker(&m_dataSetMutex);
    if (!m_dataSets.contains(deviceId)) {
        return 0;
    }

    const auto& deviceData = m_dataSets[deviceId];
    for (const QString &type : dataTypes) {
        if (deviceData.contains(type)) {
            total += deviceData[type].totalSize;
        }
    }

    return total;
}

/**
 * Verifica si un tipo de datos es soportado entre dispositivos
 */
bool DataAnalyzer::isTypeSupported(const QString &sourceType, const QString &destType, const QString &dataType)
{
    // Verificar compatibilidad entre plataformas
    if (dataType == "contacts") return true;
    if (dataType == "photos" || dataType == "videos") return true;
    if (dataType == "documents") return true;
    if (dataType == "music") return true;

    // Manejar casos especiales de incompatibilidad
    if (dataType == "messages" && sourceType == "android" && destType == "ios") return false;
    if (dataType == "calls" && destType == "ios") return false;
    if (dataType == "calendar" && sourceType != destType) return false;
    if (dataType == "applications" && sourceType != destType) return false;

    // Por defecto, asumir compatible si no hay regla específica
    return true;
}

/**
 * Obtiene la razón por la que un tipo de datos no es soportado
 */
QString DataAnalyzer::getIncompatibilityReason(const QString &sourceType, const QString &destType, const QString &dataType)
{
    if (isTypeSupported(sourceType, destType, dataType)) return QString();

    if (dataType == "messages" && sourceType == "android" && destType == "ios")
        return "iOS no permite importar mensajes SMS/MMS desde Android.";

    if (dataType == "calls" && destType == "ios")
        return "iOS no permite importar registros de llamadas.";

    if (dataType == "calendar" && sourceType != destType)
        return "La transferencia de calendario solo es posible entre dispositivos del mismo tipo.";

    if (dataType == "applications" && sourceType != destType)
        return "Las aplicaciones no pueden transferirse entre diferentes sistemas operativos.";

    return "Transferencia de " + dataType + " no soportada entre " + sourceType + " y " + destType + ".";
}

/**
 * Inicia análisis usando Bridge Client
 */
void DataAnalyzer::startAndroidAnalysisViaBridge(const AnalysisTask& task)
{
    qDebug() << "Starting Android analysis via Bridge Client for" << task.deviceId << "type:" << task.dataType;

    AdbSocketClient* bridgeClient = m_deviceManager->getBridgeClient(task.deviceId);
    if (!bridgeClient || !bridgeClient->isConnected()) {
        finalizeAnalysis(task.deviceId, task.dataType, false, "Bridge Client no disponible o no conectado");
        return;
    }

    // Desconectar conexiones anteriores para evitar duplicados
    disconnectBridgeClientSignals(task.deviceId);

    // Configurar cliente como fuente
    bridgeClient->setRole("source");

    // Conectar señales temporalmente para esta tarea
    connect(bridgeClient, &AdbSocketClient::deviceInfoReceived, this,
            &DataAnalyzer::onBridgeClientDeviceInfo);

    connect(bridgeClient, &AdbSocketClient::mediaDataReceived, this,
            &DataAnalyzer::onBridgeClientMediaData);

    connect(bridgeClient, &AdbSocketClient::filesDataReceived, this,
            &DataAnalyzer::onBridgeClientFilesData);

    connect(bridgeClient, &AdbSocketClient::scanCompleted, this,
            &DataAnalyzer::onBridgeClientScanCompleted);

    connect(bridgeClient, &AdbSocketClient::scanError, this,
            &DataAnalyzer::onBridgeClientScanError);

    connect(bridgeClient, &AdbSocketClient::scanProgress, this,
            &DataAnalyzer::onBridgeClientScanProgress);

    connect(bridgeClient, &AdbSocketClient::contactsDataReceived, this,
            &DataAnalyzer::onBridgeClientContactsData);

    connect(bridgeClient, &AdbSocketClient::messagesDataReceived, this,
            &DataAnalyzer::onBridgeClientMessagesData);

    // Iniciar el escaneo
    m_bridgeScanComplete[task.deviceId][task.dataType] = false;
    bridgeClient->startScan();

    // Emitir evento de progreso inicial
    emit analysisProgress(task.deviceId, task.dataType, 0);
}

/**
 * Desconecta las señales de Bridge Client para evitar conexiones duplicadas
 */
void DataAnalyzer::disconnectBridgeClientSignals(const QString &deviceId)
{
    AdbSocketClient* bridgeClient = m_deviceManager->getBridgeClient(deviceId);
    if (bridgeClient) {
        disconnect(bridgeClient, &AdbSocketClient::deviceInfoReceived, this, &DataAnalyzer::onBridgeClientDeviceInfo);
        disconnect(bridgeClient, &AdbSocketClient::mediaDataReceived, this, &DataAnalyzer::onBridgeClientMediaData);
        disconnect(bridgeClient, &AdbSocketClient::filesDataReceived, this, &DataAnalyzer::onBridgeClientFilesData);
        disconnect(bridgeClient, &AdbSocketClient::scanCompleted, this, &DataAnalyzer::onBridgeClientScanCompleted);
        disconnect(bridgeClient, &AdbSocketClient::scanError, this, &DataAnalyzer::onBridgeClientScanError);
        disconnect(bridgeClient, &AdbSocketClient::scanProgress, this, &DataAnalyzer::onBridgeClientScanProgress);
        disconnect(bridgeClient, &AdbSocketClient::contactsDataReceived, this, &DataAnalyzer::onBridgeClientContactsData);
        disconnect(bridgeClient, &AdbSocketClient::messagesDataReceived, this, &DataAnalyzer::onBridgeClientMessagesData);
    }
}

/**
 * Actualiza el progreso del escaneo con Bridge Client
 */
void DataAnalyzer::updateBridgeScanProgress(const QString &deviceId, const QString &dataType, int progress)
{
    emit analysisProgress(deviceId, dataType, progress);
}

/**
 * Maneja la finalización del proceso de análisis externo
 */
void DataAnalyzer::onAnalysisProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_currentAnalysisTask.deviceId.isEmpty()) {
        qWarning() << "Proceso finalizado sin tarea de análisis activa";
        return;
    }

    QString stdOut = m_analysisProcess->readAllStandardOutput();
    QString stdErr = m_analysisProcess->readAllStandardError();
    QString deviceId = m_currentAnalysisTask.deviceId;
    QString dataType = m_currentAnalysisTask.data["type"].toString();

    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        QString errorMsg = QString("Error en análisis de %1 (Código: %2): %3")
                               .arg(dataType)
                               .arg(exitCode)
                               .arg(stdErr.isEmpty() ? "Error desconocido" : stdErr);

        qWarning() << errorMsg;
        emit analysisError(deviceId, dataType, errorMsg);
        finalizeAnalysis(deviceId, dataType, false, errorMsg);
        return;
    }

    DataSet dataSet;
    dataSet.type = dataType;
    dataSet.isSupported = true;

    if (dataType == "photos") {
        QString basePath = m_currentAnalysisTask.data["basePath"].toString();
        dataSet.items = parseAndroidPhotoList(stdOut, basePath);
    }
    else if (dataType == "contacts") {
        dataSet.items = parseAndroidContacts(stdOut);
    }
    else if (dataType == "messages") {
        dataSet.items = parseAndroidMessages(stdOut);
    }
    else if (dataType == "calls") {
        dataSet.items = parseAndroidCalls(stdOut);
    }
    else {
        // Manejar otros tipos de datos...
    }

    // Calcular tamaño total
    dataSet.totalSize = 0;
    for (const DataItem &item : dataSet.items) {
        dataSet.totalSize += item.size;
    }

    // Almacenar resultados
    QMutexLocker locker(&m_dataSetMutex);
    m_dataSets[deviceId][dataType] = dataSet;
    locker.unlock();

    emit dataSetUpdated(deviceId, dataType);
    emit analysisProgress(deviceId, dataType, 100);

    // Finalizar tarea
    finalizeAnalysis(deviceId, dataType, true);
}

/**
 * Finaliza una tarea de análisis
 */
void DataAnalyzer::finalizeAnalysis(const QString& deviceId, const QString& dataType, bool success, const QString& errorMsg)
{
    // Asegurarse de que deviceId y dataType son válidos
    if (deviceId.isEmpty() || dataType.isEmpty()) {
        qWarning() << "Intento de finalizar análisis con deviceId o dataType vacío.";
        // Intentar disparar la siguiente tarea de todas formas para evitar bloqueo
        emit analysisFinishedForDevice(deviceId);
        return;
    }

    qDebug() << "Finalizando análisis para dispositivo:" << deviceId << "Tipo:" << dataType << "Éxito:" << success;

    // Asegurar que exista un dataset incluso si el análisis falló, marcarlo como no soportado/error
    QMutexLocker locker(&m_dataSetMutex);
    if (!m_dataSets.contains(deviceId) || !m_dataSets[deviceId].contains(dataType)) {
        DataSet failedSet;
        failedSet.type = dataType;
        failedSet.isSupported = false;
        failedSet.errorMessage = errorMsg.isEmpty() ? "Análisis falló o no produjo datos." : errorMsg;
        failedSet.totalSize = 0;
        m_dataSets[deviceId][dataType] = failedSet;
        locker.unlock();
        emit dataSetUpdated(deviceId, dataType); // Notificar a la UI sobre el estado fallido
    } else if (!success) {
        // Si success es falso pero el dataset existe, actualizar mensaje de error
        m_dataSets[deviceId][dataType].errorMessage = errorMsg;
        m_dataSets[deviceId][dataType].isSupported = false; // Marcar como no soportado en error
        locker.unlock();
        emit dataSetUpdated(deviceId, dataType); // Notificar a la UI sobre el estado de error
    } else {
        locker.unlock();
    }

    if (success) {
        emit analysisProgress(deviceId, dataType, 100); // Tarea completada exitosamente
    } else {
        emit analysisError(deviceId, dataType, errorMsg);
    }

    // Decrementar contador de tareas pendientes para el dispositivo
    if (m_pendingTasksPerDevice.contains(deviceId)) {
        m_pendingTasksPerDevice[deviceId]--;
        if (m_pendingTasksPerDevice[deviceId] <= 0) {
            qDebug() << "Todas las tareas de análisis completadas para dispositivo:" << deviceId;
            emit analysisComplete(deviceId); // Todas las tareas para este dispositivo terminaron
            m_pendingTasksPerDevice.remove(deviceId); // Limpiar
        }
    } else {
        qWarning() << "Contador de tareas pendientes no encontrado para dispositivo:" << deviceId << "al finalizar" << dataType;
    }

    // Disparar procesamiento de la siguiente tarea de la cola
    emit analysisFinishedForDevice(deviceId);
}

/**
 * Maneja la recepción de información de dispositivo desde Bridge Client
 */
void DataAnalyzer::onBridgeClientDeviceInfo(const QJsonObject &deviceInfo)
{
    qDebug() << "Received device info from Bridge Client";
    // Procesar información del dispositivo si es necesario
}

/**
 * Maneja la recepción de datos multimedia desde Bridge Client
 */
void DataAnalyzer::onBridgeClientMediaData(int index, int count, const QJsonArray &mediaData)
{
    qDebug() << "Received media data from Bridge Client chunk" << (index + 1) << "of" << count;

    QString deviceId = m_currentAnalysisTask.deviceId;
    if (deviceId.isEmpty()) {
        qWarning() << "Received media data but no active task";
        return;
    }

    // Procesar datos multimedia (fotos, videos, música)
    QList<DataItem> items = parseJsonMediaData(mediaData);

    // Agrupar elementos por tipo
    QMap<QString, QList<DataItem>> itemsByType;
    for (const DataItem &item : items) {
        QString type;
        if (item.data.contains("mediaType")) {
            type = item.data["mediaType"].toString();
        } else {
            // Inferir tipo por extensión
            QString filePath = item.filePath.toLower();
            if (filePath.endsWith(".jpg") || filePath.endsWith(".jpeg") ||
                filePath.endsWith(".png") || filePath.endsWith(".gif")) {
                type = "photos";
            } else if (filePath.endsWith(".mp4") || filePath.endsWith(".3gp") ||
                       filePath.endsWith(".mkv") || filePath.endsWith(".avi")) {
                type = "videos";
            } else if (filePath.endsWith(".mp3") || filePath.endsWith(".m4a") ||
                       filePath.endsWith(".ogg") || filePath.endsWith(".flac")) {
                type = "music";
            } else {
                type = "other";
            }
        }

        itemsByType[type].append(item);
    }

    // Actualizar conjuntos de datos por tipo
    QMutexLocker locker(&m_dataSetMutex);
    for (auto it = itemsByType.begin(); it != itemsByType.end(); ++it) {
        QString type = it.key();
        QList<DataItem> typeItems = it.value();

        if (!m_dataSets.contains(deviceId)) {
            m_dataSets[deviceId] = QMap<QString, DataSet>();
        }

        if (!m_dataSets[deviceId].contains(type)) {
            // Crear nuevo dataset
            DataSet newDataSet;
            newDataSet.type = type;
            newDataSet.isSupported = true;
            newDataSet.items = typeItems;
            newDataSet.totalSize = 0;

            for (const DataItem &item : typeItems) {
                newDataSet.totalSize += item.size;
            }

            m_dataSets[deviceId][type] = newDataSet;
        } else {
            // Actualizar dataset existente
            QList<DataItem> &existingItems = m_dataSets[deviceId][type].items;
            qint64 &totalSize = m_dataSets[deviceId][type].totalSize;

            // Verificar duplicados basados en id/filePath
            QSet<QString> existingPaths;
            for (const DataItem &item : existingItems) {
                existingPaths.insert(item.filePath);
            }

            for (const DataItem &item : typeItems) {
                if (!existingPaths.contains(item.filePath)) {
                    existingItems.append(item);
                    totalSize += item.size;
                }
            }
        }
    }
    locker.unlock();

    // Emitir actualizaciones para cada tipo
    for (auto it = itemsByType.begin(); it != itemsByType.end(); ++it) {
        QString type = it.key();
        emit dataSetUpdated(deviceId, type);
    }

    // Actualizar progreso total aproximado
    int progress = (((index + 1) * 100) / count);
    updateBridgeScanProgress(deviceId, m_currentAnalysisTask.dataType, progress);
}

/**
 * Maneja la recepción de datos de archivos desde Bridge Client
 */
void DataAnalyzer::onBridgeClientFilesData(int index, int count, const QJsonArray &filesData)
{
    qDebug() << "Received files data from Bridge Client chunk" << (index + 1) << "of" << count;

    QString deviceId = m_currentAnalysisTask.deviceId;
    if (deviceId.isEmpty()) {
        qWarning() << "Received files data but no active task";
        return;
    }

    // Procesar datos de archivos (documentos, APKs, etc.)
    QList<DataItem> items = parseJsonFilesData(filesData);

    // Agrupar por tipo
    QMap<QString, QList<DataItem>> itemsByType;
    for (const DataItem &item : items) {
        QString type;
        if (item.data.contains("fileType")) {
            type = item.data["fileType"].toString();
        } else {
            // Inferir tipo por extensión
            QString filePath = item.filePath.toLower();
            if (filePath.endsWith(".pdf") || filePath.endsWith(".doc") ||
                filePath.endsWith(".docx") || filePath.endsWith(".txt")) {
                type = "documents";
            } else if (filePath.endsWith(".apk")) {
                type = "applications";
            } else {
                type = "other";
            }
        }

        itemsByType[type].append(item);
    }

    // Actualizar conjuntos de datos por tipo (similar a onBridgeClientMediaData)
    QMutexLocker locker(&m_dataSetMutex);
    for (auto it = itemsByType.begin(); it != itemsByType.end(); ++it) {
        QString type = it.key();
        QList<DataItem> typeItems = it.value();

        if (!m_dataSets.contains(deviceId)) {
            m_dataSets[deviceId] = QMap<QString, DataSet>();
        }

        if (!m_dataSets[deviceId].contains(type)) {
            // Crear nuevo dataset
            DataSet newDataSet;
            newDataSet.type = type;
            newDataSet.isSupported = true;
            newDataSet.items = typeItems;
            newDataSet.totalSize = 0;

            for (const DataItem &item : typeItems) {
                newDataSet.totalSize += item.size;
            }

            m_dataSets[deviceId][type] = newDataSet;
        } else {
            // Actualizar dataset existente
            QList<DataItem> &existingItems = m_dataSets[deviceId][type].items;
            qint64 &totalSize = m_dataSets[deviceId][type].totalSize;

            // Verificar duplicados
            QSet<QString> existingPaths;
            for (const DataItem &item : existingItems) {
                existingPaths.insert(item.filePath);
            }

            for (const DataItem &item : typeItems) {
                if (!existingPaths.contains(item.filePath)) {
                    existingItems.append(item);
                    totalSize += item.size;
                }
            }
        }
    }
    locker.unlock();

    // Emitir actualizaciones para cada tipo
    for (auto it = itemsByType.begin(); it != itemsByType.end(); ++it) {
        QString type = it.key();
        emit dataSetUpdated(deviceId, type);
    }

    // Actualizar progreso
    int progress = (((index + 1) * 100) / count);
    updateBridgeScanProgress(deviceId, m_currentAnalysisTask.dataType, progress);
}

/**
 * Maneja la recepción de datos de contactos desde Bridge Client
 */
void DataAnalyzer::onBridgeClientContactsData(const QJsonArray &contactsData)
{
    qDebug() << "Received contacts data from Bridge Client";

    QString deviceId = m_currentAnalysisTask.deviceId;
    if (deviceId.isEmpty()) {
        qWarning() << "Received contacts data but no active task";
        return;
    }

    // Procesar datos de contactos
    QList<DataItem> contacts = parseJsonContactsData(contactsData);

    // Actualizar dataset de contactos
    QMutexLocker locker(&m_dataSetMutex);
    if (!m_dataSets.contains(deviceId)) {
        m_dataSets[deviceId] = QMap<QString, DataSet>();
    }

    DataSet contactsSet;
    contactsSet.type = "contacts";
    contactsSet.isSupported = true;
    contactsSet.items = contacts;
    contactsSet.totalSize = 0;

    // Calcular tamaño total
    for (const DataItem &contact : contacts) {
        contactsSet.totalSize += contact.size;
    }

    m_dataSets[deviceId]["contacts"] = contactsSet;
    locker.unlock();

    emit dataSetUpdated(deviceId, "contacts");
    updateBridgeScanProgress(deviceId, "contacts", 100);
}

/**
 * Maneja la recepción de datos de mensajes desde Bridge Client
 */
void DataAnalyzer::onBridgeClientMessagesData(const QJsonArray &messagesData)
{
    qDebug() << "Received messages data from Bridge Client";

    QString deviceId = m_currentAnalysisTask.deviceId;
    if (deviceId.isEmpty()) {
        qWarning() << "Received messages data but no active task";
        return;
    }

    // Procesar datos de mensajes
    QList<DataItem> messages = parseJsonMessagesData(messagesData);

    // Actualizar dataset de mensajes
    QMutexLocker locker(&m_dataSetMutex);
    if (!m_dataSets.contains(deviceId)) {
        m_dataSets[deviceId] = QMap<QString, DataSet>();
    }

    DataSet messagesSet;
    messagesSet.type = "messages";
    messagesSet.isSupported = true;
    messagesSet.items = messages;
    messagesSet.totalSize = 0;

    // Calcular tamaño total
    for (const DataItem &message : messages) {
        messagesSet.totalSize += message.size;
    }

    m_dataSets[deviceId]["messages"] = messagesSet;
    locker.unlock();

    emit dataSetUpdated(deviceId, "messages");
    updateBridgeScanProgress(deviceId, "messages", 100);
}

/**
 * Maneja la actualización de progreso desde Bridge Client
 */
void DataAnalyzer::onBridgeClientScanProgress(int progress)
{
    if (m_currentAnalysisTask.deviceId.isEmpty() || m_currentAnalysisTask.dataType.isEmpty()) {
        return;
    }

    // Reportar progreso del escaneo
    updateBridgeScanProgress(m_currentAnalysisTask.deviceId, m_currentAnalysisTask.dataType, progress);
}

/**
 * Maneja la finalización del escaneo desde Bridge Client
 */
void DataAnalyzer::onBridgeClientScanCompleted()
{
    QString deviceId = m_currentAnalysisTask.deviceId;
    QString dataType = m_currentAnalysisTask.dataType;

    qDebug() << "Bridge Client scan completed for device:" << deviceId << "type:" << dataType;

    // Marcar como completado
    if (!deviceId.isEmpty() && !dataType.isEmpty()) {
        m_bridgeScanComplete[deviceId][dataType] = true;
        finalizeAnalysis(deviceId, dataType, true);
    }

    // Desconectar señales de Bridge Client
    disconnectBridgeClientSignals(deviceId);
}

/**
 * Maneja errores de escaneo desde Bridge Client
 */
void DataAnalyzer::onBridgeClientScanError(const QString &errorMessage)
{
    QString deviceId = m_currentAnalysisTask.deviceId;
    QString dataType = m_currentAnalysisTask.dataType;

    qDebug() << "Bridge Client scan error for device:" << deviceId << "type:" << dataType << "error:" << errorMessage;

    if (!deviceId.isEmpty() && !dataType.isEmpty()) {
        finalizeAnalysis(deviceId, dataType, false, errorMessage);
    }

    // Desconectar señales
    disconnectBridgeClientSignals(deviceId);
}

/**
 * Analiza fotos en dispositivos Android usando método directo
 */
void DataAnalyzer::analyzeAndroidPhotosReal(const QString &deviceId)
{
    // Usar adb para listar archivos en directorios comunes
    QString photoPath = "/sdcard/DCIM/Camera/"; // Ruta inicial a comprobar
    QString command = QString("shell ls -l %1").arg(photoPath);
    QString fullAdbCommand = getAdbCommand(deviceId, command);

    if (fullAdbCommand.isEmpty()) {
        finalizeAnalysis(deviceId, "photos", false, "No se pudo construir el comando ADB (path no encontrado).");
        return;
    }

    qDebug() << "Ejecutando comando para fotos:" << fullAdbCommand;

    // Añadir la ruta base a los datos de la tarea para que el slot finished la use
    m_currentAnalysisTask.data["basePath"] = photoPath;
    m_currentAnalysisTask.data["type"] = "photos";

    // Preparar argumentos para QProcess::start
    QStringList arguments = fullAdbCommand.split(' ');
    QString program = arguments.takeFirst(); // El path a adb

    // Iniciar el proceso
    m_analysisProcess->start(program, arguments);
}

/**
 * Analiza contactos en dispositivos Android usando método directo
 */
void DataAnalyzer::analyzeAndroidContacts(const QString &deviceId)
{
    // Comando ADB para obtener todos los contactos
    QString command = "shell content query --uri content://com.android.contacts/data --projection _id,display_name,times_contacted,last_time_contacted";
    QString fullAdbCommand = getAdbCommand(deviceId, command);

    if (fullAdbCommand.isEmpty()) {
        finalizeAnalysis(deviceId, "contacts", false, "No se pudo construir el comando ADB para contactos");
        return;
    }

    // Preparar los datos para el análisis
    m_currentAnalysisTask.data["type"] = "contacts";

    // Dividir el comando para ejecutarlo con QProcess
    QStringList arguments = fullAdbCommand.split(' ');
    QString program = arguments.takeFirst();

    // Iniciar el proceso
    m_analysisProcess->start(program, arguments);
}

/**
 * Analiza mensajes en dispositivos Android usando método directo
 */
void DataAnalyzer::analyzeAndroidMessages(const QString &deviceId)
{
    // Comandos para obtener mensajes SMS
    QString command = "shell content query --uri content://sms --projection _id,address,body,date";
    QString fullAdbCommand = getAdbCommand(deviceId, command);

    if (fullAdbCommand.isEmpty()) {
        finalizeAnalysis(deviceId, "messages", false, "No se pudo construir el comando ADB para mensajes");
        return;
    }

    m_currentAnalysisTask.data["type"] = "messages";

    QStringList arguments = fullAdbCommand.split(' ');
    QString program = arguments.takeFirst();

    m_analysisProcess->start(program, arguments);
}

/**
 * Analiza registros de llamadas en dispositivos Android usando método directo
 */
void DataAnalyzer::analyzeAndroidCalls(const QString &deviceId)
{
    // Comando para obtener el registro de llamadas
    QString command = "shell content query --uri content://call_log/calls --projection _id,number,date,duration,type";
    QString fullAdbCommand = getAdbCommand(deviceId, command);

    if (fullAdbCommand.isEmpty()) {
        finalizeAnalysis(deviceId, "calls", false, "No se pudo construir el comando ADB para llamadas");
        return;
    }

    m_currentAnalysisTask.data["type"] = "calls";

    QStringList arguments = fullAdbCommand.split(' ');
    QString program = arguments.takeFirst();

    m_analysisProcess->start(program, arguments);
}

/**
 * Método simulado para contactos iOS
 */
void DataAnalyzer::analyzeIOSContacts(const QString &deviceId)
{
    // Simulación de contactos iOS
    DataSet contactSet;
    contactSet.type = "contacts";
    contactSet.isSupported = true;
    contactSet.items.clear();
    contactSet.totalSize = 0;

    // Generar datos simulados
    for (int i = 1; i <= 20; i++) {
        DataItem contact;
        contact.id = QString::number(i);
        contact.displayName = QString("Contacto iOS %1").arg(i);
        contact.size = 512;
        contact.dateTime = QDateTime::currentDateTime().addDays(-i);
        contactSet.items.append(contact);
        contactSet.totalSize += contact.size;
    }

    // Guardar dataset
    QMutexLocker locker(&m_dataSetMutex);
    m_dataSets[deviceId]["contacts"] = contactSet;
    locker.unlock();

    emit dataSetUpdated(deviceId, "contacts");
}

/**
 * Método simulado para mensajes iOS
 */
void DataAnalyzer::analyzeIOSMessages(const QString &deviceId)
{
    // Simulación de mensajes iOS
    DataSet messageSet;
    messageSet.type = "messages";
    messageSet.isSupported = true;
    messageSet.items.clear();
    messageSet.totalSize = 0;

    // Generar datos simulados
    for (int i = 1; i <= 15; i++) {
        DataItem message;
        message.id = QString::number(i);
        message.displayName = QString("Mensaje iOS %1").arg(i);
        message.size = 256;
        message.dateTime = QDateTime::currentDateTime().addDays(-i);
        message.data["body"] = QString("Contenido del mensaje %1").arg(i);
        messageSet.items.append(message);
        messageSet.totalSize += message.size;
    }

    // Guardar dataset
    QMutexLocker locker(&m_dataSetMutex);
    m_dataSets[deviceId]["messages"] = messageSet;
    locker.unlock();

    emit dataSetUpdated(deviceId, "messages");
}

/**
 * Método simulado para fotos iOS
 */
void DataAnalyzer::analyzeIOSPhotos(const QString &deviceId)
{
    // Simulación de fotos iOS
    DataSet photoSet;
    photoSet.type = "photos";
    photoSet.isSupported = true;
    photoSet.items.clear();
    photoSet.totalSize = 0;

    // Generar datos simulados
    for (int i = 1; i <= 10; i++) {
        DataItem photo;
        photo.id = QString("IMG_%1.JPG").arg(i);
        photo.displayName = QString("Foto iOS %1").arg(i);
        photo.filePath = QString("/private/var/mobile/Media/DCIM/100APPLE/IMG_%1.JPG").arg(i);
        photo.size = (1 + (i%3)) * 1024 * 1024; // 1-3 MB
        photo.dateTime = QDateTime::currentDateTime().addDays(-i);
        photoSet.items.append(photo);
        photoSet.totalSize += photo.size;
    }

    // Guardar dataset
    QMutexLocker locker(&m_dataSetMutex);
    m_dataSets[deviceId]["photos"] = photoSet;
    locker.unlock();

    emit dataSetUpdated(deviceId, "photos");
}

/**
 * Método simulado para llamadas iOS
 */
void DataAnalyzer::analyzeIOSCalls(const QString &deviceId)
{
    // Simulación de llamadas iOS
    DataSet callSet;
    callSet.type = "calls";
    callSet.isSupported = true;
    callSet.items.clear();
    callSet.totalSize = 0;

    // Generar datos simulados
    for (int i = 1; i <= 8; i++) {
        DataItem call;
        call.id = QString::number(i);
        call.displayName = QString("Llamada iOS %1").arg(i);
        call.size = 128;
        call.dateTime = QDateTime::currentDateTime().addDays(-i);
        call.data["duration"] = 60 + (i * 30); // Duración en segundos
        call.data["type"] = (i % 2 == 0) ? "Entrante" : "Saliente";
        callSet.items.append(call);
        callSet.totalSize += call.size;
    }

    // Guardar dataset
    QMutexLocker locker(&m_dataSetMutex);
    m_dataSets[deviceId]["calls"] = callSet;
    locker.unlock();

    emit dataSetUpdated(deviceId, "calls");
}

/**
 * Construye comando ADB para un dispositivo y operación
 */
QString DataAnalyzer::getAdbCommand(const QString &deviceId, const QString &command)
{
    // Construir la cadena completa del comando ADB incluyendo el ID del dispositivo
    QString adbPath = m_deviceManager->getAdbPath();
    if (adbPath.isEmpty()) {
        qWarning() << "¡La ruta de ADB no está configurada!";
        return QString(); // No se puede construir comando sin ruta ADB
    }

    return QString("%1 -s %2 %3").arg(adbPath).arg(deviceId).arg(command);
}

/**
 * Construye comando libimobiledevice para un dispositivo iOS
 */
QString DataAnalyzer::getIdeviceCommand(const QString &deviceId, const QString &tool, const QStringList &args)
{
    QPair<QString, bool> ideviceInfo = m_deviceManager->getLibimobiledeviceInfo();
    QString basePath = ideviceInfo.first;
    bool available = ideviceInfo.second;

    if (!available || basePath.isEmpty()) {
        qWarning() << "Ruta de libimobiledevice no encontrada o herramienta no disponible.";
        return QString();
    }

    QString toolExe = tool;
#ifdef Q_OS_WIN
    toolExe += ".exe";
#endif

    QString toolPath = QDir::cleanPath(basePath + QDir::separator() + toolExe);

    if (!QFileInfo::exists(toolPath)) {
        toolPath = toolExe; // Usar solo el nombre de la herramienta si no se encuentra en la ruta
        qWarning() << "Herramienta" << tool << "no encontrada en" << basePath << ", intentando desde PATH.";
    }

    QStringList fullArgs;
    if (!deviceId.isEmpty()) {
        bool hasUuidArg = false;
        for(const QString& arg : args) {
            if (arg == "-u" || arg == "--udid") {
                hasUuidArg = true;
                break;
            }
        }
        if (!hasUuidArg) {
            fullArgs << "-u" << deviceId;
        }
    }
    fullArgs.append(args);

    return toolPath + " " + fullArgs.join(" "); // Para logs
}

/**
 * Procesa datos JSON de archivos multimedia
 */
QList<DataItem> DataAnalyzer::parseJsonMediaData(const QJsonArray &jsonArray)
{
    QList<DataItem> items;

    for (int i = 0; i < jsonArray.size(); ++i) {
        QJsonObject obj = jsonArray[i].toObject();

        // Extraer datos básicos
        QString path = obj["path"].toString();
        QString name = obj["name"].toString();
        qint64 size = obj["size"].toVariant().toLongLong();
        QString type = obj["type"].toString();
        qint64 dateModified = obj["dateModified"].toVariant().toLongLong();
        QString mimeType = obj["mimeType"].toString();

        if (path.isEmpty() || name.isEmpty()) {
            continue; // Saltar elementos sin información esencial
        }

        // Crear objeto DataItem
        DataItem item;
        item.id = path; // Usar ruta como ID único
        item.displayName = name;
        item.filePath = path;
        item.size = size;
        item.dateTime = QDateTime::fromMSecsSinceEpoch(dateModified);

        // Mapear tipos de archivo a categorías
        if (type == "IMAGE") {
            item.data["mediaType"] = "photos";
        } else if (type == "VIDEO") {
            item.data["mediaType"] = "videos";
        } else if (type == "AUDIO") {
            item.data["mediaType"] = "music";
        } else {
            item.data["mediaType"] = "other";
        }

        item.data["mimeType"] = mimeType;

        // Procesar metadatos adicionales si existen
        if (obj.contains("metadata")) {
            QJsonObject metaObj = obj["metadata"].toObject();
            for (auto it = metaObj.begin(); it != metaObj.end(); ++it) {
                item.data[it.key()] = it.value().toVariant();
            }
        }

        items.append(item);
    }

    return items;
}

/**
 * Procesa datos JSON de archivos generales
 */
QList<DataItem> DataAnalyzer::parseJsonFilesData(const QJsonArray &jsonArray)
{
    QList<DataItem> items;

    for (int i = 0; i < jsonArray.size(); ++i) {
        QJsonObject obj = jsonArray[i].toObject();

        // Extraer datos básicos
        QString path = obj["path"].toString();
        QString name = obj["name"].toString();
        qint64 size = obj["size"].toVariant().toLongLong();
        QString type = obj["type"].toString();
        qint64 dateModified = obj["dateModified"].toVariant().toLongLong();
        QString mimeType = obj["mimeType"].toString();

        if (path.isEmpty() || name.isEmpty()) {
            continue; // Saltar elementos sin información esencial
        }

        // Crear objeto DataItem
        DataItem item;
        item.id = path; // Usar ruta como ID único
        item.displayName = name;
        item.filePath = path;
        item.size = size;
        item.dateTime = QDateTime::fromMSecsSinceEpoch(dateModified);

        // Mapear tipos de archivo a categorías
        if (type == "DOCUMENT") {
            item.data["fileType"] = "documents";
        } else if (type == "APK") {
            item.data["fileType"] = "applications";
        } else if (type == "ARCHIVE") {
            item.data["fileType"] = "archives";
        } else {
            item.data["fileType"] = "other";
        }

        item.data["mimeType"] = mimeType;

        items.append(item);
    }

    return items;
}

/**
 * Procesa datos JSON de contactos
 */
QList<DataItem> DataAnalyzer::parseJsonContactsData(const QJsonArray &jsonArray)
{
    QList<DataItem> contacts;

    for (int i = 0; i < jsonArray.size(); ++i) {
        QJsonObject obj = jsonArray[i].toObject();

        // Extraer datos básicos
        QString id = obj["id"].toString();
        QString displayName = obj["displayName"].toString();

        if (id.isEmpty() || displayName.isEmpty()) {
            continue; // Saltar contactos sin información esencial
        }

        // Crear objeto DataItem
        DataItem contact;
        contact.id = id;
        contact.displayName = displayName;
        contact.size = 1024; // Tamaño estimado para contactos

        // Almacenar números de teléfono
        if (obj.contains("phoneNumbers")) {
            QJsonArray phones = obj["phoneNumbers"].toArray();
            QStringList phoneList;
            for (int j = 0; j < phones.size(); ++j) {
                phoneList.append(phones[j].toString());
            }
            contact.data["phones"] = phoneList;
        }

        // Almacenar correos electrónicos
        if (obj.contains("emails")) {
            QJsonArray emails = obj["emails"].toArray();
            QStringList emailList;
            for (int j = 0; j < emails.size(); ++j) {
                emailList.append(emails[j].toString());
            }
            contact.data["emails"] = emailList;
        }

        // Almacenar URI de foto si existe
        if (obj.contains("photoUri")) {
            contact.data["photoUri"] = obj["photoUri"].toString();
        }

        contacts.append(contact);
    }

    return contacts;
}

/**
 * Procesa datos JSON de mensajes
 */
QList<DataItem> DataAnalyzer::parseJsonMessagesData(const QJsonArray &jsonArray)
{
    QList<DataItem> messages;

    for (int i = 0; i < jsonArray.size(); ++i) {
        QJsonObject obj = jsonArray[i].toObject();

        // Extraer datos básicos
        QString id = obj["id"].toString();
        QString threadId = obj["threadId"].toString();
        QString address = obj["address"].toString();
        qint64 date = obj["date"].toVariant().toLongLong();
        QString body = obj["body"].toString();
        bool isRead = obj["isRead"].toBool();
        int type = obj["type"].toInt();

        if (id.isEmpty() || body.isEmpty()) {
            continue; // Saltar mensajes sin información esencial
        }

        // Crear objeto DataItem
        DataItem message;
        message.id = id;
        message.displayName = QString("Mensaje de %1").arg(address);
        message.size = body.length() + address.length(); // Tamaño aproximado
        message.dateTime = QDateTime::fromMSecsSinceEpoch(date);

        // Almacenar datos adicionales
        message.data["threadId"] = threadId;
        message.data["address"] = address;
        message.data["body"] = body;
        message.data["isRead"] = isRead;
        message.data["type"] = type;

        messages.append(message);
    }

    return messages;
}

/**
 * Procesa listas de fotos de Android
 */
QList<DataItem> DataAnalyzer::parseAndroidPhotoList(const QString &output, const QString& basePath)
{
    QList<DataItem> photos;
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);

    // Formato típico de salida de `ls -l`:
    // -rw-rw---- 1 u0_a171 media_rw 3133747 2024-03-29 10:00 IMG_20240329_100000.jpg
    QRegularExpression re(
        "^([-d])"              // 1: Tipo (d para directorio, - para archivo)
        "([rwx-]{9})"          // 2: Permisos (ignorados)
        "\\s+\\d+\\s+"         //    Contador de enlaces (ignorado)
        "([^\\s]+)\\s+"        // 3: Propietario (ignorado)
        "([^\\s]+)\\s+"        // 4: Grupo (ignorado)
        "(\\d+)"               // 5: Tamaño
        "\\s+(\\d{4}-\\d{2}-\\d{2})" // 6: Fecha (YYYY-MM-DD)
        "\\s+(\\d{2}:\\d{2})"  // 7: Hora (HH:MM)
        "\\s+(.+)$"            // 8: Nombre de archivo
        );

    // Año actual para archivos sin año
    int currentYear = QDate::currentDate().year();

    for (const QString &line : lines) {
        QString trimmedLine = line.trimmed();
        if (trimmedLine.isEmpty()) continue;

        QRegularExpressionMatch match = re.match(trimmedLine);
        if (match.hasMatch()) {
            QString type = match.captured(1);
            // Saltar directorios
            if (type == "d") {
                continue;
            }

            QString filename = match.captured(8);
            // Saltar archivos ocultos
            if (filename.startsWith('.')) {
                continue;
            }

            // Comprobar extensiones de imagen
            if (!filename.endsWith(".jpg", Qt::CaseInsensitive) &&
                !filename.endsWith(".jpeg", Qt::CaseInsensitive) &&
                !filename.endsWith(".png", Qt::CaseInsensitive) &&
                !filename.endsWith(".gif", Qt::CaseInsensitive) &&
                !filename.endsWith(".bmp", Qt::CaseInsensitive) &&
                !filename.endsWith(".webp", Qt::CaseInsensitive) &&
                !filename.endsWith(".heic", Qt::CaseInsensitive) &&
                !filename.endsWith(".heif", Qt::CaseInsensitive)) {
                continue;
            }

            DataItem photo;
            photo.size = match.captured(5).toLongLong();
            photo.displayName = filename;

            // Construir ruta completa
            QString currentBasePath = basePath;
            if (!currentBasePath.endsWith('/')) {
                currentBasePath += '/';
            }
            photo.filePath = currentBasePath + filename;
            photo.id = photo.filePath;

            // Parsear fecha/hora
            QString dateStr = match.captured(6);
            QString timeStr = match.captured(7);
            QDateTime dt = QDateTime::fromString(dateStr + " " + timeStr, "yyyy-MM-dd HH:mm");
            if(dt.isValid()) {
                // Manejar fechas antiguas
                if(dt.date().year() < 1980) {
                    QDate potentialDate(currentYear, dt.date().month(), dt.date().day());
                    if (potentialDate.isValid() && potentialDate <= QDate::currentDate()) {
                        dt.setDate(potentialDate);
                    } else {
                        potentialDate.setDate(currentYear - 1, dt.date().month(), dt.date().day());
                        if (potentialDate.isValid()) {
                            dt.setDate(potentialDate);
                        }
                    }
                }
                photo.dateTime = dt;
            } else {
                photo.dateTime = QDateTime();
            }

            photos.append(photo);
        } else {
            qWarning() << "No se pudo parsear línea:" << trimmedLine;
        }
    }

    qDebug() << "Parseados" << photos.count() << "ítems de foto desde" << basePath;
    return photos;
}

/**
 * Procesa datos de contactos Android
 */
QList<DataItem> DataAnalyzer::parseAndroidContacts(const QString &output)
{
    QList<DataItem> contacts;
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);

    // Expresión regular para parsear la salida de ADB
    QRegularExpression contactRegex(
        "Row:\\s\\d+\\s_id=(\\d+),.*"
        "display_name=([^,]+),.*"
        "times_contacted=(\\d+),.*"
        "last_time_contacted=(\\d+)"
        );

    for (const QString &line : lines) {
        QRegularExpressionMatch match = contactRegex.match(line);
        if (match.hasMatch()) {
            DataItem contact;
            contact.id = match.captured(1);
            contact.displayName = match.captured(2).trimmed();

            // Convertir timestamp a QDateTime
            qint64 lastContact = match.captured(4).toLongLong();
            contact.dateTime = QDateTime::fromMSecsSinceEpoch(lastContact * 1000);

            // Tamaño estimado basado en campos
            contact.size = 1024 + (contact.displayName.size() * 2);

            contacts.append(contact);
        }
    }

    qDebug() << "Contactos encontrados:" << contacts.size();
    return contacts;
}

/**
 * Procesa datos de mensajes Android
 */
QList<DataItem> DataAnalyzer::parseAndroidMessages(const QString &output)
{
    QList<DataItem> messages;
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);

    QRegularExpression messageRegex(
        "Row:\\s\\d+\\s_id=(\\d+),\\s*"
        "address=([^,]+),\\s*"
        "body=([^,]+),\\s*"
        "date=(\\d+)"
        );

    for (const QString &line : lines) {
        QRegularExpressionMatch match = messageRegex.match(line);
        if (match.hasMatch()) {
            DataItem message;
            message.id = match.captured(1);
            QString address = match.captured(2).trimmed();
            QString body = match.captured(3).trimmed();

            message.displayName = QString("Mensaje de %1").arg(address);
            message.data["body"] = body;

            qint64 timestamp = match.captured(4).toLongLong();
            message.dateTime = QDateTime::fromMSecsSinceEpoch(timestamp);

            // Tamaño basado en longitud del mensaje
            message.size = body.size() + address.size();

            messages.append(message);
        }
    }

    qDebug() << "Mensajes encontrados:" << messages.size();
    return messages;
}

/**
 * Procesa datos de llamadas Android
 */
QList<DataItem> DataAnalyzer::parseAndroidCalls(const QString &output)
{
    QList<DataItem> calls;
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);

    QRegularExpression callRegex(
        "Row:\\s\\d+\\s_id=(\\d+),\\s*"
        "number=([^,]+),\\s*"
        "date=(\\d+),\\s*"
        "duration=(\\d+),\\s*"
        "type=(\\d+)"
        );

    for (const QString &line : lines) {
        QRegularExpressionMatch match = callRegex.match(line);
        if (match.hasMatch()) {
            DataItem call;
            call.id = match.captured(1);
            QString number = match.captured(2).trimmed();

            // Determinar tipo de llamada
            int callType = match.captured(5).toInt();
            QString typeStr;
            if (callType == 1) typeStr = "Entrante";
            else if (callType == 2) typeStr = "Saliente";
            else typeStr = "Perdida";

            call.displayName = QString("Llamada %1 - %2").arg(typeStr, number);

            qint64 timestamp = match.captured(3).toLongLong();
            call.dateTime = QDateTime::fromMSecsSinceEpoch(timestamp);

            // Tamaño basado en duración
            call.size = match.captured(4).toInt() * 10; // Factor aproximado

            calls.append(call);
        }
    }

    qDebug() << "Llamadas encontradas:" << calls.size();
    return calls;
}
