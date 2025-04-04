#include "datatransfermanager.h"
#include <QDir>
#include <QTemporaryDir>
#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QMutexLocker>
#include <QJsonDocument>
#include <QJsonObject>

/**
 * Constructor de la clase DataTransferManager
 */
DataTransferManager::DataTransferManager(DeviceManager *deviceManager, DataAnalyzer *dataAnalyzer, QObject *parent)
    : QObject(parent)
    , m_deviceManager(deviceManager)
    , m_dataAnalyzer(dataAnalyzer)
    , m_isTransferring(false)
    , m_totalTransferSize(0)
    , m_totalTransferredSizePreviousTasks(0)
{
    // Conectar señales de procesos
    connect(&m_pullProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &DataTransferManager::onPullProcessFinished);
    connect(&m_pushProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &DataTransferManager::onPushProcessFinished);

    // Manejar stderr para mejores mensajes de error
    connect(&m_pullProcess, &QProcess::readyReadStandardError, this, [this](){
        qWarning() << "Pull Process Error Output:" << m_pullProcess.readAllStandardError();
    });
    connect(&m_pushProcess, &QProcess::readyReadStandardError, this, [this](){
        qWarning() << "Push Process Error Output:" << m_pushProcess.readAllStandardError();
    });
}

/**
 * Destructor
 */
DataTransferManager::~DataTransferManager()
{
    if (m_isTransferring) {
        cancelTransfer();
    }
    cleanupTempDirectory();
}

/**
 * Inicia una transferencia de datos entre dispositivos
 */
bool DataTransferManager::startTransfer(const QString &sourceId, const QString &destId,
                                        const QStringList &dataTypes, bool clearDestination)
{
    QMutexLocker locker(&m_transferMutex);

    if (m_isTransferring) {
        qWarning() << "Transferencia ya en progreso.";
        emit transferFailed("Ya hay una transferencia en progreso");
        return false;
    }

    DeviceInfo sourceDevice = m_deviceManager->getDeviceInfo(sourceId);
    DeviceInfo destDevice = m_deviceManager->getDeviceInfo(destId);

    if (sourceDevice.id.isEmpty() || destDevice.id.isEmpty()) {
        QString errorMsg = "Uno o ambos dispositivos no están disponibles";
        emit transferFailed(errorMsg);
        return false;
    }

    if (!sourceDevice.authorized || !destDevice.authorized) {
        QString errorMsg = "Uno o ambos dispositivos no están autorizados";
        emit transferFailed(errorMsg);
        return false;
    }

    if (!prepareTempDirectory()) {
        QString errorMsg = "No se pudo crear directorio temporal para la transferencia";
        emit transferFailed(errorMsg);
        return false;
    }

    // Resetear estado
    m_dataTypeQueue.clear();
    m_taskStates.clear();
    m_totalTransferSize = 0;
    m_totalTransferredSizePreviousTasks = 0;
    m_currentTask = TransferTask();
    m_transferTimer.start();

    // Verificar capacidades de Bridge Client
    bool sourceBridgeAvailable = sourceDevice.type == "android" &&
                                 m_deviceManager->isBridgeClientConnected(sourceId);
    bool destBridgeAvailable = destDevice.type == "android" &&
                               m_deviceManager->isBridgeClientConnected(destId);

    // Popular cola y calcular tamaño total
    for (const QString &dataType : dataTypes) {
        DataSet dataSet = m_dataAnalyzer->getDataSet(sourceId, dataType);
        if (dataSet.items.isEmpty() || !dataSet.isSupported || !dataSet.errorMessage.isEmpty()) {
            qWarning() << "Saltando tipo de dato:" << dataType << "Items:" << dataSet.items.count()
            << "Soportado:" << dataSet.isSupported << "Error:" << dataSet.errorMessage;
            continue;
        }

        if (!m_dataAnalyzer->isTypeSupported(sourceDevice.type, destDevice.type, dataType)) {
            qWarning() << "Saltando tipo de dato debido a incompatibilidad de plataforma:" << dataType;
            continue;
        }

        m_dataTypeQueue.enqueue(dataType);
        TransferTask taskInfo;
        taskInfo.sourceId = sourceId;
        taskInfo.destId = destId;
        taskInfo.dataType = dataType;
        taskInfo.clearDestination = clearDestination;
        taskInfo.itemsToTransfer = dataSet.items;
        taskInfo.totalItems = dataSet.items.size();
        taskInfo.totalSize = dataSet.totalSize > 0 ? dataSet.totalSize : (dataSet.items.size() * 1024); // Estimación
        taskInfo.processedItems = 0;
        taskInfo.processedSize = 0;
        taskInfo.currentItemIndex = -1;
        taskInfo.status = "waiting";

        // Determinar si se usará Bridge Client para esta tarea
        taskInfo.useBridgeClient = false;
        if (dataType == "photos" || dataType == "videos" || dataType == "music" || dataType == "documents") {
            // Estos tipos pueden beneficiarse de Bridge Client si está disponible
            taskInfo.useBridgeClient = sourceBridgeAvailable && destBridgeAvailable;
        }

        m_taskStates[dataType] = taskInfo;
        m_totalTransferSize += taskInfo.totalSize;
    }

    if (m_dataTypeQueue.isEmpty()) {
        QString errorMsg = "No hay tipos de datos válidos seleccionados o compatibles para transferir";
        emit transferFailed(errorMsg);
        cleanupTempDirectory();
        return false;
    }

    qDebug() << "Iniciando transferencia. Tareas:" << m_dataTypeQueue << "Tamaño Total:" << m_totalTransferSize;
    m_isTransferring = true; // MARCAR COMO ACTIVA *ANTES* DE EMITIR SEÑALES

    locker.unlock(); // Desbloquear antes de emitir señales

    emit transferStarted(m_totalTransferSize);
    emit transferProgress(0);

    startNextTransferTask(); // Iniciar la primera tarea

    return true;
}

/**
 * Cancela la transferencia en curso
 */
void DataTransferManager::cancelTransfer()
{
    QMutexLocker locker(&m_transferMutex);

    if (!m_isTransferring) return;

    qDebug() << "Cancelando transferencia...";
    bool wasActive = m_isTransferring;
    m_isTransferring = false; // Prevenir inicio de nuevos pasos/tareas

    // Terminar procesos en curso
    if (m_pullProcess.state() != QProcess::NotRunning) {
        m_pullProcess.blockSignals(true); // Evitar que finished() se procese después de cancelar
        m_pullProcess.terminate();
        m_pullProcess.waitForFinished(500);
        m_pullProcess.blockSignals(false);
    }

    if (m_pushProcess.state() != QProcess::NotRunning) {
        m_pushProcess.blockSignals(true);
        m_pushProcess.terminate();
        m_pushProcess.waitForFinished(500);
        m_pushProcess.blockSignals(false);
    }

    // Desconectar Bridge Client de ambos dispositivos si estaban en uso
    if (!m_currentTask.sourceId.isEmpty()) {
        disconnectBridgeClientSignals(m_currentTask.sourceId);
    }

    if (!m_currentTask.destId.isEmpty()) {
        disconnectBridgeClientSignals(m_currentTask.destId);
    }

    // Limpiar estado
    m_dataTypeQueue.clear();
    m_taskStates.clear();
    m_currentTask = TransferTask();

    cleanupTempDirectory();

    locker.unlock(); // Desbloquear antes de emitir señales

    // Emitir señales solo si estaba activa para evitar duplicados
    if (wasActive) {
        emit transferCancelled();
        emit transferFinished(false, "Transferencia Cancelada");
    }

    qDebug() << "Transferencia cancelada.";
}

/**
 * Verifica si hay una transferencia en progreso
 */
bool DataTransferManager::isTransferInProgress() const
{
    QMutexLocker locker(&m_transferMutex);
    return m_isTransferring;
}

/**
 * Obtiene el progreso general de la transferencia
 */
int DataTransferManager::getOverallProgress() const
{
    QMutexLocker locker(&m_transferMutex);

    if (m_totalTransferSize <= 0) {
        return (m_isTransferring && !m_dataTypeQueue.isEmpty()) ? 0 : 100;
    }

    qint64 currentTotalProcessed = m_totalTransferredSizePreviousTasks + m_currentTask.processedSize;
    int progress = static_cast<int>((static_cast<double>(currentTotalProcessed) / m_totalTransferSize) * 100.0);

    return qBound(0, progress, 100);
}

/**
 * Obtiene información sobre las tareas activas
 */
QList<TransferTask> DataTransferManager::getActiveTasksInfo() const
{
    QMutexLocker locker(&m_transferMutex);

    QList<TransferTask> infoList;

    if (m_isTransferring && !m_currentTask.dataType.isEmpty()) {
        TransferTask currentInfo = m_currentTask;
        currentInfo.itemsToTransfer.clear(); // No incluir lista completa para ahorrar memoria
        infoList.append(currentInfo);
    }

    for(const QString& type : m_dataTypeQueue) {
        if(m_taskStates.contains(type)) {
            TransferTask waitingInfo = m_taskStates[type];
            waitingInfo.itemsToTransfer.clear();
            infoList.append(waitingInfo);
        }
    }

    return infoList;
}

/**
 * Inicia la siguiente tarea de transferencia
 */
void DataTransferManager::startNextTransferTask()
{
    QMutexLocker locker(&m_transferMutex);

    if (m_dataTypeQueue.isEmpty()) {
        if (m_isTransferring) {
            qDebug() << "Todas las tareas de transferencia finalizadas.";
            bool wasActive = m_isTransferring;
            m_isTransferring = false; // Marcar como inactivo *antes* de emitir

            cleanupTempDirectory();

            locker.unlock(); // Desbloquear antes de emitir señales

            if(wasActive) { // Solo emitir si realmente estaba activa
                emit transferProgress(100);
                emit transferCompleted();
                emit transferFinished(true, "Transferencia Completada");
            }
        }
        return;
    }

    if (!m_isTransferring) return; // Salir si se canceló mientras esperaba

    QString nextDataType = m_dataTypeQueue.dequeue();
    if (!m_taskStates.contains(nextDataType)) {
        qWarning() << "Estado de tarea no encontrado para:" << nextDataType << ". Saltando.";
        locker.unlock();
        QTimer::singleShot(0, this, &DataTransferManager::startNextTransferTask);
        return;
    }

    m_currentTask = m_taskStates[nextDataType];
    m_currentTask.status = "starting";
    m_currentTask.currentItemIndex = -1;
    m_currentTask.processedItems = 0;
    m_currentTask.processedSize = 0;

    qDebug() << "Iniciando tarea:" << m_currentTask.dataType << "Items:" << m_currentTask.totalItems
             << "Tamaño:" << m_currentTask.totalSize << "Usando Bridge Client:" << m_currentTask.useBridgeClient;

    locker.unlock(); // Desbloquear antes de emitir señales

    emit transferTaskStarted(m_currentTask.dataType, m_currentTask.totalItems);
    emitTaskProgress(); // Emitir progreso inicial (0%)

    DeviceInfo sourceDevice = m_deviceManager->getDeviceInfo(m_currentTask.sourceId);
    DeviceInfo destDevice = m_deviceManager->getDeviceInfo(m_currentTask.destId);
    bool taskStarted = false;

    if (sourceDevice.type == "android" && destDevice.type == "android") {
        taskStarted = transferAndroidToAndroid(m_currentTask);
    } else if (sourceDevice.type == "android" && destDevice.type == "ios") {
        taskStarted = transferAndroidToIOS(m_currentTask);
    } else if (sourceDevice.type == "ios" && destDevice.type == "android") {
        taskStarted = transferIOSToAndroid(m_currentTask);
    } else if (sourceDevice.type == "ios" && destDevice.type == "ios") {
        taskStarted = transferIOSToIOS(m_currentTask);
    } else {
        m_currentTask.errorMessage = "Combinación de tipos de dispositivo no soportada.";
        taskStarted = false;
    }

    if (!taskStarted) {
        // Si la preparación de la tarea falló, la finalizamos inmediatamente
        finalizeCurrentTask(false, m_currentTask.errorMessage.isEmpty() ?
                                       "No se pudo iniciar la tarea específica de la plataforma." :
                                       m_currentTask.errorMessage);
    } else {
        // Tarea iniciada, procesar el primer paso/ítem
        processNextTransferStep();
    }
}

/**
 * Procesa el siguiente paso de transferencia
 */
void DataTransferManager::processNextTransferStep()
{
    QMutexLocker locker(&m_transferMutex);

    if (!m_isTransferring) return;

    m_currentTask.currentItemIndex++;

    if (m_currentTask.currentItemIndex >= m_currentTask.totalItems) {
        qDebug() << "Tarea completada (todos los ítems procesados):" << m_currentTask.dataType;
        locker.unlock();
        finalizeCurrentTask(true); // Finalizar la tarea actual como exitosa
        return;
    }

    // Verificar que hay elementos para transferir
    if (m_currentTask.itemsToTransfer.isEmpty() ||
        m_currentTask.currentItemIndex >= m_currentTask.itemsToTransfer.size()) {
        qWarning() << "Índice fuera de límites o lista vacía en processNextTransferStep";
        locker.unlock();
        finalizeCurrentTask(false, "Error interno: Índice fuera de límites o lista vacía.");
        return;
    }

    // Guardar nombre del ítem actual para referencia
    const DataItem& currentItem = m_currentTask.itemsToTransfer[m_currentTask.currentItemIndex];
    m_currentTask.currentItemName = currentItem.displayName;

    locker.unlock(); // Desbloquear antes de continuar

    // Decidir cómo transferir según el tipo de datos
    if (m_currentTask.dataType == "photos" || m_currentTask.dataType == "videos" ||
        m_currentTask.dataType == "music" || m_currentTask.dataType == "documents") {

        if (m_currentTask.useBridgeClient) {
            // Transferir usando Bridge Client
            if (!startPhotoPullViaBridge(currentItem)) {
                // Si falla, usar método tradicional como fallback
                startPhotoPull();
            }
        } else {
            // Método tradicional con ADB pull/push
            startPhotoPull();
        }
    }
    else if (m_currentTask.dataType == "contacts") {
        startContactsTransfer(m_currentTask);
    }
    else if (m_currentTask.dataType == "messages") {
        startMessagesTransfer(m_currentTask);
    }
    else {
        finalizeCurrentTask(false, "Tipo de datos no soportado internamente para transferencia: " + m_currentTask.dataType);
    }
}

/**
 * Implementa transferencia entre dispositivos Android
 */
bool DataTransferManager::transferAndroidToAndroid(TransferTask &task)
{
    // Crear directorio en destino si es necesario (para archivos)
    if (task.dataType == "photos" || task.dataType == "videos" ||
        task.dataType == "music" || task.dataType == "documents") {

        // Determinar directorio de destino
        QString destBaseDir;
        if (task.dataType == "photos" || task.dataType == "videos") {
            destBaseDir = "/sdcard/MobileDataBridge/Media/";
        } else if (task.dataType == "music") {
            destBaseDir = "/sdcard/MobileDataBridge/Music/";
        } else if (task.dataType == "documents") {
            destBaseDir = "/sdcard/MobileDataBridge/Documents/";
        } else {
            destBaseDir = "/sdcard/MobileDataBridge/";
        }

        // Si usamos Bridge Client, podemos hacerlo directamente a través de él
        if (task.useBridgeClient) {
            // Bridge Client se encargará de crear directorios según sea necesario
            return true;
        } else {
            // Método tradicional: usar ADB para crear directorio
            QString adbPath = m_deviceManager->getAdbPath();
            if (adbPath.isEmpty()) {
                task.errorMessage = "Ruta ADB no encontrada.";
                return false;
            }

            // Comando para crear directorio
            QProcess mkdirProcess;
            QStringList args;
            args << "-s" << task.destId << "shell" << "mkdir" << "-p" << destBaseDir;

            qDebug() << "Ejecutando mkdir:" << adbPath << args.join(" ");
            mkdirProcess.start(adbPath, args);

            if (!mkdirProcess.waitForFinished(3000)) {
                qWarning() << "Timeout creando directorio:" << mkdirProcess.errorString();
                task.errorMessage = "Timeout al crear directorio destino.";
                return false;
            }

            if(mkdirProcess.exitCode() != 0) {
                qWarning() << "Error creando directorio:" << mkdirProcess.readAllStandardError();
                task.errorMessage = "Fallo al crear directorio destino.";
                return false;
            }

            qDebug() << "Directorio destino asegurado:" << destBaseDir;
            return true;
        }
    } else if (task.dataType == "contacts" || task.dataType == "messages" || task.dataType == "calls") {
        // Estos tipos de datos no requieren directorio de destino previo
        return true;
    }

    task.errorMessage = "Tipo '" + task.dataType + "' no implementado para Android->Android.";
    return false;
}

/**
 * Implementa transferencia de Android a iOS
 */
bool DataTransferManager::transferAndroidToIOS(TransferTask &task)
{
    task.errorMessage = "Transferencia Android->iOS aún no implementada completamente.";
    return false;
}

/**
 * Implementa transferencia de iOS a Android
 */
bool DataTransferManager::transferIOSToAndroid(TransferTask &task)
{
    task.errorMessage = "Transferencia iOS->Android aún no implementada completamente.";
    return false;
}

/**
 * Implementa transferencia entre dispositivos iOS
 */
bool DataTransferManager::transferIOSToIOS(TransferTask &task)
{
    task.errorMessage = "Transferencia iOS->iOS aún no implementada completamente.";
    return false;
}

/**
 * Inicia la descarga de archivo usando ADB directo
 */
void DataTransferManager::startPhotoPull()
{
    QMutexLocker locker(&m_transferMutex);

    if (!m_isTransferring) return;

    if (m_currentTask.currentItemIndex >= m_currentTask.itemsToTransfer.size()) {
        locker.unlock();
        finalizeCurrentTask(false, "Error interno: Índice fuera de límites (pull).");
        return;
    }

    const DataItem& currentItem = m_currentTask.itemsToTransfer[m_currentTask.currentItemIndex];
    m_currentTask.currentItemName = currentItem.displayName;

    QString sourcePath = currentItem.filePath;
    if (sourcePath.isEmpty()) {
        // Saltar ítem sin ruta
        m_currentTask.processedItems++;
        emitTaskProgress();
        locker.unlock();
        QTimer::singleShot(0, this, &DataTransferManager::processNextTransferStep);
        return;
    }

    m_currentTask.tempFilePath = getTempPathForItem(currentItem.displayName);

    QString adbPath = m_deviceManager->getAdbPath();
    if (adbPath.isEmpty()) {
        locker.unlock();
        finalizeCurrentTask(false, "Error: Ruta ADB no encontrada (pull).");
        return;
    }

    QStringList args;
    args << "-s" << m_currentTask.sourceId << "pull" << sourcePath << m_currentTask.tempFilePath;

    qDebug() << "Copiando archivo:" << sourcePath << "a" << m_currentTask.tempFilePath;
    m_currentTask.status = "pulling";

    locker.unlock(); // Desbloquear antes de emitir señales

    emitTaskProgress(); // Emitir progreso antes de iniciar

    m_pullProcess.start(adbPath, args);
}

/**
 * Maneja la finalización del proceso de pull
 */
void DataTransferManager::onPullProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QMutexLocker locker(&m_transferMutex);

    if (!m_isTransferring) return;

    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        QString errorMsg = QString("Fallo al copiar archivo (pull) '%1': %2 (%3)")
        .arg(m_currentTask.currentItemName)
            .arg(m_pullProcess.errorString())
            .arg(QString(m_pullProcess.readAllStandardError()).trimmed());

        qWarning() << errorMsg;
        m_currentTask.processedItems++;
        emitTaskProgress();

        locker.unlock(); // Desbloquear antes de continuar
        QTimer::singleShot(0, this, &DataTransferManager::processNextTransferStep); // Intentar siguiente ítem
    } else {
        qDebug() << "Pull exitoso para:" << m_currentTask.currentItemName;
        locker.unlock();
        startPhotoPush(); // Iniciar push
    }
}

/**
 * Inicia la subida de archivo usando ADB
 */
void DataTransferManager::startPhotoPush()
{
    QMutexLocker locker(&m_transferMutex);

    if (!m_isTransferring) return;

    if (m_currentTask.currentItemIndex >= m_currentTask.itemsToTransfer.size()) {
        locker.unlock();
        finalizeCurrentTask(false, "Error interno: Índice fuera de límites (push).");
        return;
    }

    const DataItem& currentItem = m_currentTask.itemsToTransfer[m_currentTask.currentItemIndex];

    if (m_currentTask.tempFilePath.isEmpty() || !QFile::exists(m_currentTask.tempFilePath)) {
        qWarning() << "Archivo temporal no encontrado o vacío para push:"
                   << m_currentTask.currentItemName << m_currentTask.tempFilePath;

        m_currentTask.processedItems++;
        emitTaskProgress();

        locker.unlock();
        QTimer::singleShot(0, this, &DataTransferManager::processNextTransferStep); // Saltar e intentar siguiente
        return;
    }

    // Determinar directorio de destino según tipo
    QString destBaseDir;
    if (m_currentTask.dataType == "photos" || m_currentTask.dataType == "videos") {
        destBaseDir = "/sdcard/MobileDataBridge/Media/";
    } else if (m_currentTask.dataType == "music") {
        destBaseDir = "/sdcard/MobileDataBridge/Music/";
    } else if (m_currentTask.dataType == "documents") {
        destBaseDir = "/sdcard/MobileDataBridge/Documents/";
    } else {
        destBaseDir = "/sdcard/MobileDataBridge/";
    }

    QString destPath = destBaseDir + currentItem.displayName;

    QString adbPath = m_deviceManager->getAdbPath();
    if (adbPath.isEmpty()) {
        if (!m_currentTask.tempFilePath.isEmpty()) {
            QFile::remove(m_currentTask.tempFilePath);
        }
        locker.unlock();
        finalizeCurrentTask(false, "Error: Ruta ADB no encontrada (push).");
        return;
    }

    QStringList args;
    args << "-s" << m_currentTask.destId << "push" << m_currentTask.tempFilePath << destPath;

    qDebug() << "Pegando archivo:" << m_currentTask.tempFilePath << "a" << destPath;
    m_currentTask.status = "pushing";

    locker.unlock(); // Desbloquear antes de iniciar proceso

    m_pushProcess.start(adbPath, args);
}

/**
 * Maneja la finalización del proceso de push
 */
void DataTransferManager::onPushProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QMutexLocker locker(&m_transferMutex);

    if (!m_isTransferring) return;

    QString tempFileToDelete = m_currentTask.tempFilePath; // Guardar para eliminar después

    if (!tempFileToDelete.isEmpty()) {
        QFile::remove(tempFileToDelete);
    }

    if (m_currentTask.currentItemIndex >= m_currentTask.itemsToTransfer.size()) {
        qWarning() << "Índice inválido en onPushProcessFinished.";
        locker.unlock();
        QTimer::singleShot(0, this, &DataTransferManager::processNextTransferStep);
        return;
    }

    const DataItem& currentItem = m_currentTask.itemsToTransfer[m_currentTask.currentItemIndex];

    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        QString errorMsg = QString("Fallo al pegar archivo (push) '%1': %2 (%3)")
        .arg(m_currentTask.currentItemName)
            .arg(m_pushProcess.errorString())
            .arg(QString(m_pushProcess.readAllStandardError()).trimmed());

        qWarning() << errorMsg;
    } else {
        qDebug() << "Push exitoso para:" << m_currentTask.currentItemName;
        m_currentTask.processedSize += currentItem.size;
    }

    m_currentTask.processedItems++;

    emitTaskProgress(); // Emitir progreso DESPUÉS del intento
    emitOverallProgress();

    locker.unlock(); // Desbloquear antes de continuar

    QTimer::singleShot(0, this, &DataTransferManager::processNextTransferStep);
}

/**
 * Inicia la transferencia de un archivo usando Bridge Client
 */
bool DataTransferManager::startPhotoPullViaBridge(const DataItem &item)
{
    QMutexLocker locker(&m_transferMutex);

    if (!m_isTransferring) return false;

    // Verificar que Bridge Client está disponible en origen y destino
    AdbSocketClient* sourceBridge = m_deviceManager->getBridgeClient(m_currentTask.sourceId);
    AdbSocketClient* destBridge = m_deviceManager->getBridgeClient(m_currentTask.destId);

    if (!sourceBridge || !sourceBridge->isConnected() || !destBridge || !destBridge->isConnected()) {
        qWarning() << "Bridge Client no disponible en origen o destino para:" << item.displayName;
        return false;
    }

    // Configurar Bridge Clients para la transferencia
    if (!connectToBridgeClient(m_currentTask.sourceId, "source") ||
        !connectToBridgeClient(m_currentTask.destId, "destination")) {
        return false;
    }

    // Preparar la información de archivo a transferir
    QString fileInfo = QString("{\"path\":\"%1\",\"name\":\"%2\",\"size\":%3}")
                           .arg(item.filePath)
                           .arg(item.displayName)
                           .arg(item.size);

    // Solicitar el archivo del origen
    bool success = sourceBridge->requestFile(item.filePath);
    if (!success) {
        qWarning() << "Error al solicitar archivo via Bridge Client:" << item.filePath;
        disconnectBridgeClientSignals(m_currentTask.sourceId);
        disconnectBridgeClientSignals(m_currentTask.destId);
        return false;
    }

    m_currentTask.status = "transferring_via_bridge";

    locker.unlock(); // Desbloquear antes de emitir señales

    emitTaskProgress();

    return true;
}

/**
 * Implementa transferencia de contactos
 */
bool DataTransferManager::startContactsTransfer(TransferTask &task)
{
    // Implementación básica para transferencia de contactos
    // En una versión real, los contactos se exportarían/importarían en formato vCard o similar

    QMutexLocker locker(&m_transferMutex);

    if (task.currentItemIndex >= task.itemsToTransfer.size()) {
        locker.unlock();
        finalizeCurrentTask(false, "Error en transferencia de contactos: índice inválido");
        return false;
    }

    // En este ejemplo simulamos una transferencia de contacto exitosa
    // Para cada contacto simulamos una pequeña espera
    QTimer::singleShot(100, this, [this]() {
        QMutexLocker locker(&m_transferMutex);

        if (!m_isTransferring) return;

        if (m_currentTask.currentItemIndex < m_currentTask.itemsToTransfer.size()) {
            const DataItem& currentItem = m_currentTask.itemsToTransfer[m_currentTask.currentItemIndex];

            // Procesar contacto (simulado)
            m_currentTask.processedItems++;
            m_currentTask.processedSize += currentItem.size;

            locker.unlock();

            emitTaskProgress();
            emitOverallProgress();

            // Procesar el siguiente contacto
            QTimer::singleShot(0, this, &DataTransferManager::processNextTransferStep);
        }
    });

    return true;
}

/**
 * Implementa transferencia de mensajes
 */
bool DataTransferManager::startMessagesTransfer(TransferTask &task)
{
    // Implementación básica para transferencia de mensajes
    // En una versión real, los mensajes se exportarían/importarían en formato XML o similar

    QMutexLocker locker(&m_transferMutex);

    if (task.currentItemIndex >= task.itemsToTransfer.size()) {
        locker.unlock();
        finalizeCurrentTask(false, "Error en transferencia de mensajes: índice inválido");
        return false;
    }

    // En este ejemplo simulamos una transferencia de mensaje exitosa
    QTimer::singleShot(100, this, [this]() {
        QMutexLocker locker(&m_transferMutex);

        if (!m_isTransferring) return;

        if (m_currentTask.currentItemIndex < m_currentTask.itemsToTransfer.size()) {
            const DataItem& currentItem = m_currentTask.itemsToTransfer[m_currentTask.currentItemIndex];

            // Procesar mensaje (simulado)
            m_currentTask.processedItems++;
            m_currentTask.processedSize += currentItem.size;

            locker.unlock();

            emitTaskProgress();
            emitOverallProgress();

            // Procesar el siguiente mensaje
            QTimer::singleShot(0, this, &DataTransferManager::processNextTransferStep);
        }
    });

    return true;
}

/**
 * Finaliza la tarea actual
 */
void DataTransferManager::finalizeCurrentTask(bool success, const QString& errorMsg)
{
    QMutexLocker locker(&m_transferMutex);

    if (!m_isTransferring && !success && m_currentTask.status == "failed") return;

    if (m_currentTask.dataType.isEmpty()) {
        locker.unlock();
        QTimer::singleShot(0, this, &DataTransferManager::startNextTransferTask);
        return;
    }

    qDebug() << "Finalizando tarea:" << m_currentTask.dataType << "Éxito:" << success;

    m_currentTask.status = success ? "completed" : "failed";
    m_currentTask.errorMessage = errorMsg;

    if (m_taskStates.contains(m_currentTask.dataType)) {
        m_taskStates[m_currentTask.dataType] = m_currentTask; // Actualizar estado almacenado
    }

    // Desconectar Bridge Client si se usó
    if (m_currentTask.useBridgeClient) {
        disconnectBridgeClientSignals(m_currentTask.sourceId);
        disconnectBridgeClientSignals(m_currentTask.destId);
    }

    if (success) {
        emit transferTaskProgress(m_currentTask.dataType, 100,
                                  m_currentTask.totalItems, m_currentTask.totalItems,
                                  m_currentTask.totalSize, m_currentTask.totalSize, "");
        emit transferTaskCompleted(m_currentTask.dataType, m_currentTask.processedItems);
        m_totalTransferredSizePreviousTasks += m_currentTask.totalSize; // Acumular tamaño total de la tarea exitosa
    } else {
        emit transferTaskFailed(m_currentTask.dataType, errorMsg);
        m_totalTransferredSizePreviousTasks += m_currentTask.processedSize; // Acumular solo lo procesado de la tarea fallida
    }

    emitOverallProgress();

    locker.unlock(); // Desbloquear antes de continuar

    QTimer::singleShot(0, this, &DataTransferManager::startNextTransferTask); // Iniciar la siguiente tarea
}

/**
 * Emite una señal de progreso general
 */
void DataTransferManager::emitOverallProgress()
{
    emit transferProgress(getOverallProgress());
}

/**
 * Emite una señal de progreso para la tarea actual
 */
void DataTransferManager::emitTaskProgress()
{
    QMutexLocker locker(&m_transferMutex);

    if (!m_isTransferring || m_currentTask.dataType.isEmpty()) return;

    int taskProgressPercent = 0;
    if (m_currentTask.totalSize > 0) {
        taskProgressPercent = static_cast<int>((static_cast<double>(m_currentTask.processedSize) / m_currentTask.totalSize) * 100.0);
    } else if (m_currentTask.totalItems > 0) {
        taskProgressPercent = static_cast<int>((static_cast<double>(m_currentTask.processedItems) / m_currentTask.totalItems) * 100.0);
    } else {
        taskProgressPercent = (m_currentTask.status == "completed" || m_currentTask.status == "failed") ? 100 : 0;
    }
    taskProgressPercent = qBound(0, taskProgressPercent, 100);

    QString currentItemName = m_currentTask.currentItemName;

    locker.unlock(); // Desbloquear antes de emitir señal

    emit transferTaskProgress(
        m_currentTask.dataType, taskProgressPercent,
        m_currentTask.processedItems, m_currentTask.totalItems,
        m_currentTask.processedSize, m_currentTask.totalSize,
        currentItemName
        );
}

/**
 * Prepara un directorio temporal para la transferencia
 */
bool DataTransferManager::prepareTempDirectory()
{
    cleanupTempDirectory();
    QString tempLocation = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (tempLocation.isEmpty()) { return false; }

    m_tempDirOwner = tempLocation + "/MobileDataBridge_Transfer_" +
                     QDateTime::currentDateTime().toString("yyyyMMdd_hhmmsszzz");

    QDir tempDir;
    if (!tempDir.mkpath(m_tempDirOwner)) {
        m_tempDirOwner.clear();
        return false;
    }

    qDebug() << "Directorio temporal creado:" << m_tempDirOwner;
    return true;
}

/**
 * Limpia el directorio temporal
 */
bool DataTransferManager::cleanupTempDirectory()
{
    if (m_tempDirOwner.isEmpty()) return true;

    QDir dir(m_tempDirOwner);
    qDebug() << "Limpiando directorio temporal:" << m_tempDirOwner;
    bool success = dir.removeRecursively();

    if (!success) {
        qWarning() << "Fallo al eliminar directorio temporal:" << m_tempDirOwner;
    }

    m_tempDirOwner.clear();
    return success;
}

/**
 * Obtiene una ruta temporal para un elemento
 */
QString DataTransferManager::getTempPathForItem(const QString& itemName) const
{
    if (m_tempDirOwner.isEmpty()) return QString();

    QString safeName = itemName;
    // Eliminar caracteres inválidos para nombres de archivo
    safeName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");

    return m_tempDirOwner + QDir::separator() + safeName;
}

/**
 * Verifica si Bridge Client está disponible para transferencia
 */
bool DataTransferManager::isBridgeClientAvailable(const QString &deviceId) const
{
    return m_deviceManager->isBridgeClientConnected(deviceId);
}

/**
 * Conecta con Bridge Client para transferencia
 */
bool DataTransferManager::connectToBridgeClient(const QString &deviceId, const QString &role)
{
    AdbSocketClient* bridgeClient = m_deviceManager->getBridgeClient(deviceId);
    if (!bridgeClient || !bridgeClient->isConnected()) {
        return false;
    }

    // Configurar rol
    if (!bridgeClient->setRole(role)) {
        qWarning() << "Error al configurar rol" << role << "para Bridge Client en dispositivo" << deviceId;
        return false;
    }

    // Conectar señales para transferencia
    if (role == "source") {
        // Para dispositivo origen
        connect(bridgeClient, &AdbSocketClient::fileReady, this, &DataTransferManager::onBridgeClientFileReady);
        connect(bridgeClient, &AdbSocketClient::fileTransferProgress, this, &DataTransferManager::onBridgeClientFileProgress);
        connect(bridgeClient, &AdbSocketClient::errorOccurred, this, &DataTransferManager::onBridgeClientError);
    } else if (role == "destination") {
        // Para dispositivo destino
        connect(bridgeClient, &AdbSocketClient::fileSaved, this, &DataTransferManager::onBridgeClientFileSaved);
        connect(bridgeClient, &AdbSocketClient::errorOccurred, this, &DataTransferManager::onBridgeClientError);
    }

    return true;
}

/**
 * Desconecta señales de Bridge Client
 */
void DataTransferManager::disconnectBridgeClientSignals(const QString &deviceId)
{
    AdbSocketClient* bridgeClient = m_deviceManager->getBridgeClient(deviceId);
    if (bridgeClient) {
        disconnect(bridgeClient, &AdbSocketClient::fileReady, this, &DataTransferManager::onBridgeClientFileReady);
        disconnect(bridgeClient, &AdbSocketClient::fileSaved, this, &DataTransferManager::onBridgeClientFileSaved);
        disconnect(bridgeClient, &AdbSocketClient::fileTransferProgress, this, &DataTransferManager::onBridgeClientFileProgress);
        disconnect(bridgeClient, &AdbSocketClient::errorOccurred, this, &DataTransferManager::onBridgeClientError);
    }
}

/**
 * Maneja eventos cuando un archivo está listo para transferir desde Bridge Client
 */
void DataTransferManager::onBridgeClientFileReady(const QString &filePath)
{
    QMutexLocker locker(&m_transferMutex);

    if (!m_isTransferring) return;

    qDebug() << "Archivo listo desde Bridge Client:" << filePath;

    // Verificar que Bridge Client destino está disponible
    AdbSocketClient* destBridge = m_deviceManager->getBridgeClient(m_currentTask.destId);
    if (!destBridge || !destBridge->isConnected()) {
        qWarning() << "Bridge Client destino no disponible para guardar:" << filePath;

        // Continuar con el siguiente ítem
        m_currentTask.processedItems++;
        locker.unlock();
        emitTaskProgress();
        QTimer::singleShot(0, this, &DataTransferManager::processNextTransferStep);
        return;
    }

    // Calcular tamaño estimado del archivo para progreso
    qint64 itemSize = 0;
    if (m_currentTask.currentItemIndex < m_currentTask.itemsToTransfer.size()) {
        itemSize = m_currentTask.itemsToTransfer[m_currentTask.currentItemIndex].size;
    }

    // Construir información del archivo a guardar
    QString fileInfo = QString("{\"path\":\"%1\",\"name\":\"%2\",\"size\":%3}")
                           .arg(filePath)
                           .arg(m_currentTask.currentItemName)
                           .arg(itemSize);

    // Enviar comando para guardar en destino
    destBridge->saveFile(fileInfo);
}

/**
 * Maneja eventos cuando un archivo ha sido guardado por Bridge Client
 */
void DataTransferManager::onBridgeClientFileSaved(const QString &result)
{
    QMutexLocker locker(&m_transferMutex);

    if (!m_isTransferring) return;

    qDebug() << "Archivo guardado por Bridge Client, resultado:" << result;

    // Actualizar progreso
    if (m_currentTask.currentItemIndex < m_currentTask.itemsToTransfer.size()) {
        if (result.startsWith("OK")) {
            m_currentTask.processedSize += m_currentTask.itemsToTransfer[m_currentTask.currentItemIndex].size;
        }
        m_currentTask.processedItems++;
    }

    locker.unlock();

    emitTaskProgress();
    emitOverallProgress();

    // Continuar con el siguiente ítem
    QTimer::singleShot(0, this, &DataTransferManager::processNextTransferStep);
}

/**
 * Maneja eventos de progreso de transferencia desde Bridge Client
 */
void DataTransferManager::onBridgeClientFileProgress(const QString &filePath, qint64 bytesReceived, qint64 totalBytes)
{
    QMutexLocker locker(&m_transferMutex);

    if (!m_isTransferring) return;

    qDebug() << "Progreso de transferencia Bridge Client:" << filePath
             << bytesReceived << "/" << totalBytes;

    // Actualizar progreso parcial para el ítem actual
    if (m_currentTask.currentItemIndex < m_currentTask.itemsToTransfer.size()) {
        // Calcular progreso proporcional para este ítem
        double progress = totalBytes > 0 ? static_cast<double>(bytesReceived) / totalBytes : 0;
        qint64 fullItemSize = m_currentTask.itemsToTransfer[m_currentTask.currentItemIndex].size;
        qint64 currentProcessedSize = progress * fullItemSize;

        // Solo actualizar tarea, no el contador de ítems todavía
        m_currentTask.processedSize = currentProcessedSize;
    }

    locker.unlock();

    // Reportar progreso
    emitTaskProgress();
    emitOverallProgress();
}

/**
 * Maneja errores de Bridge Client
 */
void DataTransferManager::onBridgeClientError(const QString &errorMessage)
{
    QMutexLocker locker(&m_transferMutex);

    if (!m_isTransferring) return;

    qWarning() << "Error en Bridge Client durante transferencia:" << errorMessage;

    // Actualizar contador e intentar siguiente ítem
    if (m_currentTask.currentItemIndex < m_currentTask.itemsToTransfer.size()) {
        m_currentTask.processedItems++;
    }

    locker.unlock();

    emitTaskProgress();

    // Continuar con el siguiente ítem
    QTimer::singleShot(0, this, &DataTransferManager::processNextTransferStep);
}
