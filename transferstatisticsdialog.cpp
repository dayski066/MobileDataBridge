#include "transferstatisticsdialog.h"
#include "ui_transferstatisticsdialog.h" // Incluye el .h generado por el .ui
#include <QDebug>
#include <QCloseEvent> // Incluir para QCloseEvent
#include <QRegularExpression> // Incluir para sanitizar nombres de archivo
#include <QTimer> // Incluir QTimer si no estaba ya

TransferStatisticsDialog::TransferStatisticsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TransferStatisticsDialog),
    m_timer(new QTimer(this)),
    m_totalSize(0),
    m_lastProcessedSize(0),
    m_completedTasks(0),
    m_failedTasks(0),
    m_transferActive(false),
    m_currentTaskDataType("") // Inicializar
{
    ui->setupUi(this);
    ui->lblSummary->setVisible(false); // Ocultar resumen inicialmente

    connect(m_timer, &QTimer::timeout, this, &TransferStatisticsDialog::updateTimers);
    // La conexión de btnClose se hace automáticamente por nombre (on_btnClose_clicked)
    // La conexión de btnCancel se hace automáticamente por nombre (on_btnCancel_clicked)

    // Prevenir el cierre con 'X' mientras la transferencia está activa inicialmente
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);
}

TransferStatisticsDialog::~TransferStatisticsDialog()
{
    delete ui;
    // m_timer se elimina automáticamente debido a la propiedad del padre
}

void TransferStatisticsDialog::setTotalTransferSize(qint64 totalSize)
{
    m_totalSize = totalSize;
    qDebug() << "Statistics Dialog: Total size set to" << m_totalSize;
}

void TransferStatisticsDialog::setSourceDestinationInfo(const QString& sourceName, const QString& sourceType,
                                                        const QString& destName, const QString& destType)
{
    m_sourceName = sourceName;
    m_sourceType = sourceType;
    m_destName = destName;
    m_destType = destType;

    // Actualizar el título del diálogo con la información
    setWindowTitle(QString("Transferencia: %1 → %2").arg(m_sourceName).arg(m_destName));
}

QString TransferStatisticsDialog::translateDataType(const QString& dataType) const
{
    // Centralizar traducción (se puede expandir)
    if (dataType == "contacts") return "Contactos";
    if (dataType == "messages") return "Mensajes";
    if (dataType == "photos") return "Fotos";
    if (dataType == "videos") return "Videos";
    if (dataType == "calls") return "Llamadas";
    if (dataType == "calendar") return "Calendario";
    if (dataType == "music") return "Música";
    return dataType; // Retornar original si no hay traducción
}


void TransferStatisticsDialog::onTransferStarted()
{
    m_startTime = QDateTime::currentDateTime();
    m_lastProgressUpdateTime = m_startTime;
    m_lastProcessedSize = 0;
    m_completedTasks = 0;
    m_failedTasks = 0;
    m_transferActive = true;
    m_finalStatusMessage.clear();
    m_currentTaskDataType = ""; // Limpiar al inicio


    ui->lblStatus->setText("Estado: Transfiriendo...");
    ui->progressBarTotal->setValue(0);
    ui->progressBarCurrentTask->setValue(0); // Asegurar que la barra de tarea también empiece en 0
    ui->progressBarCurrentTask->setFormat("%p%"); // Formato inicial
    ui->listTasks->clear();
    m_taskItems.clear();
    ui->lblElapsedTime->setText("00:00:00");
    ui->lblEstimatedTime->setText("--:--:--");
    ui->lblTimeRemaining->setText("--:--:--");
    ui->lblSummary->setVisible(false);
    ui->btnCancel->setEnabled(true);
    ui->btnClose->setEnabled(false);

    // Deshabilitar botón de cierre 'X' mientras está activo
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);
    show(); // Asegura que el cambio de flags tenga efecto

    m_timer->start(1000); // Actualizar temporizadores cada segundo
    qDebug() << "Statistics Dialog: Transfer started.";
}

void TransferStatisticsDialog::onOverallProgressUpdated(int progress)
{
    if (!m_transferActive) return;
    ui->progressBarTotal->setValue(progress);
    // La estimación de tiempo se actualiza en updateTimers basada en cambios de tamaño
}

void TransferStatisticsDialog::onTaskStarted(const QString &dataType, int totalItems)
{
    if (!m_transferActive) return;
    QString taskName = translateDataType(dataType);
    qDebug() << "Statistics Dialog: Task started:" << dataType << "Items:" << totalItems;

    m_currentTaskDataType = dataType; // Almacena el tipo de dato actual
    ui->progressBarCurrentTask->setValue(0); // Resetea la barra de progreso de tarea
    ui->progressBarCurrentTask->setFormat(QString("%1: %p%").arg(taskName)); // Actualiza formato de la barra

    if (!m_taskItems.contains(dataType)) {
        QListWidgetItem* item = new QListWidgetItem(ui->listTasks);
        item->setData(Qt::UserRole, dataType); // Almacena tipo original si es necesario
        item->setData(Qt::UserRole + 1, false); // Flag: completado (inicialmente falso)
        item->setData(Qt::UserRole + 2, 0LL);   // Almacena tamaño total de la tarea
        item->setData(Qt::UserRole + 3, 0LL);   // Almacena tamaño procesado de la tarea
        item->setData(Qt::UserRole + 4, 0);    // Almacena ítems totales de la tarea
        m_taskItems[dataType] = item;
        ui->listTasks->addItem(item);
    }
    // Almacena el total de ítems para referencia posterior
    m_taskItems[dataType]->setData(Qt::UserRole + 4, totalItems);
    updateTaskItemText(dataType, "Iniciando...", 0, totalItems);
}

void TransferStatisticsDialog::onTaskProgressUpdated(const QString &dataType, int taskProgressPercent, // << NUEVO PARÁMETRO
                                                     int processedItems, int totalItems,
                                                     qint64 processedSize, qint64 totalSize,
                                                     const QString& currentItemName)
{
    if (!m_transferActive) return;

    // Asegura que estamos actualizando la barra para la tarea correcta
    if (dataType == m_currentTaskDataType) {
        ui->progressBarCurrentTask->setValue(taskProgressPercent); // << ACTUALIZA BARRA TAREA ACTUAL
    } else {
        // Esto puede ocurrir si las señales llegan desordenadas justo en el cambio de tarea
        qDebug() << "Progress update received for non-current task:" << dataType << ", current is:" << m_currentTaskDataType;
    }

    // Actualiza el elemento en la lista
    updateTaskItemText(dataType, "Transfiriendo", processedItems, totalItems, processedSize, totalSize, currentItemName);

    // Almacena el progreso actual para recálculos y para el resumen final
    if (m_taskItems.contains(dataType)) {
        m_taskItems[dataType]->setData(Qt::UserRole + 2, totalSize); // Almacena tamaño total de la tarea
        m_taskItems[dataType]->setData(Qt::UserRole + 3, processedSize); // Almacena tamaño procesado de la tarea
    }

    // --- Lógica de estimación de tiempo y tamaño ---
    // Recalcular tamaño total procesado de forma segura
    qint64 recalcTotalProcessed = 0;
    for (const auto& taskDataTypeIter : m_taskItems.keys()) {
        QListWidgetItem* item = m_taskItems[taskDataTypeIter];
        bool taskComplete = item->data(Qt::UserRole + 1).toBool();
        qint64 taskTotalForItem = item->data(Qt::UserRole + 2).toLongLong(); // Tamaño total almacenado para esta tarea
        qint64 taskProcessedForItem = item->data(Qt::UserRole + 3).toLongLong(); // Tamaño procesado almacenado

        if (taskDataTypeIter == dataType) { // Tarea actual recibiendo progreso
            recalcTotalProcessed += processedSize; // Usa el valor de la señal
        } else if (taskComplete) {
            recalcTotalProcessed += taskTotalForItem; // Suma tamaño total para tareas completadas
        } else {
            // Para tareas no completadas y no actuales, suma su progreso almacenado previamente
            recalcTotalProcessed += taskProcessedForItem;
        }
    }

    // Actualiza el último tamaño procesado para cálculo de velocidad
    m_lastProcessedSize = recalcTotalProcessed;
    m_lastProgressUpdateTime = QDateTime::currentDateTime();

    // La actualización de la barra de progreso total es manejada por onOverallProgressUpdated
}

void TransferStatisticsDialog::onTaskCompleted(const QString &dataType, int successCount)
{
    if (!m_taskItems.contains(dataType)) return;
    qDebug() << "Statistics Dialog: Task completed:" << dataType << "Count:" << successCount;
    m_completedTasks++;
    QListWidgetItem* item = m_taskItems[dataType];
    qint64 taskTotalSize = item->data(Qt::UserRole + 2).toLongLong();
    int taskTotalItems = item->data(Qt::UserRole + 4).toInt();

    updateTaskItemText(dataType, QString("Completado (%1/%2)").arg(successCount).arg(taskTotalItems),
                       successCount, taskTotalItems, // Usa successCount como ítems procesados finales
                       taskTotalSize, taskTotalSize ); // Usa tamaño total como procesado
    item->setForeground(Qt::darkGreen);
    item->setData(Qt::UserRole+1, true); // Marcar como completado

    // Si esta era la tarea actual, resetea la barra de tarea actual para la siguiente
    if (dataType == m_currentTaskDataType) {
        ui->progressBarCurrentTask->setValue(100); // Asegura 100% al final de la tarea
        // No resetear m_currentTaskDataType aquí, se hace en onTaskStarted de la siguiente
    }
}

void TransferStatisticsDialog::onTaskFailed(const QString &dataType, const QString &errorMessage)
{
    if (!m_taskItems.contains(dataType)) return;
    qDebug() << "Statistics Dialog: Task failed:" << dataType << "Error:" << errorMessage;
    m_failedTasks++;
    updateTaskItemText(dataType, "Falló: " + errorMessage);
    m_taskItems[dataType]->setForeground(Qt::red);
    m_taskItems[dataType]->setData(Qt::UserRole+1, false); // Marcar como no completado exitosamente

    // Si esta era la tarea actual, resetea la barra de tarea actual
    if (dataType == m_currentTaskDataType) {
        // Podríamos dejar la barra en el valor que tenía o resetearla a 0
        // Dejarla como estaba puede indicar dónde falló.
        // ui->progressBarCurrentTask->setValue(0);
    }
}

void TransferStatisticsDialog::onTransferFinished(bool success, const QString& finalMessage)
{
    // Evita procesar la señal de finalización dos veces si ya se manejó
    if (!m_transferActive && m_finalStatusMessage.isEmpty()) {
        return;
    }
    qDebug() << "Statistics Dialog: Transfer finished signal received. Success:" << success << "Message:" << finalMessage;
    m_transferActive = false;
    m_timer->stop();

    m_finalStatusMessage = finalMessage; // Almacena mensaje para mostrar

    // Usa un temporizador de un solo disparo para retrasar ligeramente las actualizaciones finales de la UI,
    // asegurando que todas las señales de progreso/tarea probablemente se hayan procesado.
    QTimer::singleShot(100, this, &TransferStatisticsDialog::handleTransferFinished);
}

void TransferStatisticsDialog::handleTransferFinished()
{
    qDebug() << "Statistics Dialog: Handling transfer finished state.";
    m_currentTaskDataType = ""; // Resetea el tipo de tarea actual
    // Opcional: resetear la barra de tarea actual o dejarla en el último estado
    // ui->progressBarCurrentTask->setValue(0);
    ui->progressBarCurrentTask->setFormat("%p%"); // Resetea formato de barra

    // Actualiza valores finales del temporizador
    updateTimers();

    // Actualiza etiqueta de estado
    QString finalStatus;
    if (!m_finalStatusMessage.isEmpty()){
        finalStatus = QString("Estado: %1").arg(m_finalStatusMessage);
    } else if (m_failedTasks > 0) {
        finalStatus = QString("Estado: Completado con %1 errores").arg(m_failedTasks);
    } else if (m_completedTasks > 0) {
        finalStatus = "Estado: Transferencia Completada";
    } else {
        // Caso: cancelado sin mensaje o sin tareas válidas
        finalStatus = "Estado: Transferencia Finalizada";
    }
    ui->lblStatus->setText(finalStatus);


    // Asegura que la barra de progreso total esté en 100 si fue exitosa, o refleje el progreso real
    if (m_completedTasks > 0 && m_failedTasks == 0 && m_finalStatusMessage.contains("Completada", Qt::CaseInsensitive)) {
        ui->progressBarTotal->setValue(100);
    } else {
        // Actualiza con el progreso final calculado
        if (m_totalSize > 0) {
            int finalProgress = static_cast<int>((static_cast<double>(m_lastProcessedSize) / m_totalSize) * 100.0);
            ui->progressBarTotal->setValue(qBound(0, finalProgress, 100));
        } else if (m_completedTasks > 0 || m_failedTasks > 0) {
            // Si no hay tamaño, pero hubo tareas, estima progreso basado en tareas
            int totalTasks = m_completedTasks + m_failedTasks;
            if (totalTasks > 0) {
                int finalProgress = static_cast<int>((static_cast<double>(m_completedTasks) / totalTasks) * 100.0);
                ui->progressBarTotal->setValue(qBound(0, finalProgress, 100));
            }
        }
    }


    // Genera y muestra el resumen
    qint64 elapsedSeconds = m_startTime.secsTo(QDateTime::currentDateTime());
    QString summary = QString("Resumen: %1 tareas completadas, %2 tareas fallidas.\n")
                          .arg(m_completedTasks)
                          .arg(m_failedTasks);
    summary += QString("Datos transferidos (aprox): %1.\n").arg(formatSize(m_lastProcessedSize)); // Usa el último tamaño calculado
    summary += QString("Tiempo total: %1.").arg(formatTime(elapsedSeconds));
    ui->lblSummary->setText(summary);
    ui->lblSummary->setVisible(true);

    // Actualiza botones
    ui->btnCancel->setEnabled(false);
    ui->btnClose->setEnabled(true);

    // Permite cerrar con 'X' ahora
    setWindowFlags(windowFlags() | Qt::WindowCloseButtonHint);
    show(); // Asegura que los flags tengan efecto
}


void TransferStatisticsDialog::updateTimers()
{
    if (!m_startTime.isValid()) return; // No hacer nada si no se ha iniciado

    qint64 elapsedSeconds = m_startTime.secsTo(QDateTime::currentDateTime());
    ui->lblElapsedTime->setText(formatTime(elapsedSeconds));

    // Solo calcular estimaciones si la transferencia está activa
    if (!m_transferActive) {
        ui->lblTimeRemaining->setText("00:00:00"); // Poner 0 cuando termina
        // Dejar tiempo estimado total como estaba o poner el tiempo transcurrido?
        ui->lblEstimatedTime->setText(formatTime(elapsedSeconds));
        return;
    }

    // Estimar tiempo restante
    if (m_lastProcessedSize > 0 && elapsedSeconds > 2 && m_totalSize > 0) { // Necesita datos y tiempo para estimar
        qint64 nowMillis = QDateTime::currentDateTime().toMSecsSinceEpoch();
        qint64 lastUpdateMillis = m_lastProgressUpdateTime.toMSecsSinceEpoch();
        qint64 timeDiffMillis = nowMillis - lastUpdateMillis;

        // Usar velocidad promedio general para más estabilidad
        double bytesPerSecond = static_cast<double>(m_lastProcessedSize) / elapsedSeconds;

        if (bytesPerSecond > 1) { // Evitar división por cero o velocidades muy bajas
            qint64 remainingBytes = m_totalSize - m_lastProcessedSize;
            if (remainingBytes < 0) remainingBytes = 0;
            qint64 remainingSeconds = static_cast<qint64>(remainingBytes / bytesPerSecond);
            qint64 estimatedTotalSeconds = elapsedSeconds + remainingSeconds;

            ui->lblTimeRemaining->setText(formatTime(remainingSeconds));
            ui->lblEstimatedTime->setText(formatTime(estimatedTotalSeconds));
        } else {
            // Velocidad demasiado baja para estimar fiablemente
            ui->lblTimeRemaining->setText("--:--:--");
            ui->lblEstimatedTime->setText("--:--:--");
        }

    } else {
        // Aún no hay suficientes datos/tiempo
        ui->lblTimeRemaining->setText("--:--:--");
        ui->lblEstimatedTime->setText("--:--:--");
    }
}

QString TransferStatisticsDialog::formatTime(int totalSeconds) const
{
    if (totalSeconds < 0) return "--:--:--";
    int seconds = totalSeconds % 60;
    int totalMinutes = totalSeconds / 60;
    int minutes = totalMinutes % 60;
    int hours = totalMinutes / 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

// Definición de formatSize (ESTÁTICA)
QString TransferStatisticsDialog::formatSize(qint64 bytes) // SIN CONST
{
    const qint64 kb = 1024;
    const qint64 mb = 1024 * kb;
    const qint64 gb = 1024 * mb;

    if (bytes < kb) {
        return QString::number(bytes) + " B";
    } else if (bytes < mb) {
        return QString::number(bytes / static_cast<double>(kb), 'f', 1) + " KB";
    } else if (bytes < gb) {
        return QString::number(bytes / static_cast<double>(mb), 'f', 2) + " MB";
    } else {
        return QString::number(bytes / static_cast<double>(gb), 'f', 2) + " GB";
    }
}

void TransferStatisticsDialog::updateTaskItemText(const QString &dataType, const QString &status, int processed, int total, qint64 sizeProcessed, qint64 sizeTotal, const QString& currentItem)
{
    if (!m_taskItems.contains(dataType)) return;

    QListWidgetItem* item = m_taskItems[dataType];
    QString taskName = translateDataType(dataType);
    QString text = QString("%1: %2").arg(taskName).arg(status);

    if (total > 0 && processed >= 0) {
        text += QString(" (%1/%2").arg(processed).arg(total);
        // No es necesario item->setData aquí porque se hace en los slots que llaman a esta función
        if (sizeTotal > 0 && sizeProcessed >= 0) {
            text += QString(" - %1/%2").arg(formatSize(sizeProcessed)).arg(formatSize(sizeTotal));
        }
        text += ")";
    }
    if (!currentItem.isEmpty()) {
        // Acortar nombres de archivo largos si es necesario para la UI
        const int maxLen = 30; // Máxima longitud deseada para el nombre del ítem
        QString shortItemName = currentItem;
        if (shortItemName.length() > maxLen) {
            // Estrategia simple: tomar principio y fin
            shortItemName = shortItemName.left(maxLen / 2 - 2) + "..." + shortItemName.right(maxLen / 2 -1);
        }
        text += QString(" [%1]").arg(shortItemName);
    }

    item->setText(text);
}


void TransferStatisticsDialog::on_btnClose_clicked()
{
    accept(); // Cierra el diálogo con resultado Aceptado
}

void TransferStatisticsDialog::on_btnCancel_clicked()
{
    qDebug() << "Statistics Dialog: Cancel clicked.";
    emit transferCancelledRequested();
    // La UI se actualizará a "Cancelado" cuando se reciba la señal transferCancelled
    // y se procese por onTransferFinished -> handleTransferFinished
    ui->lblStatus->setText("Estado: Cancelando...");
    ui->btnCancel->setEnabled(false); // Deshabilitar botón después de hacer clic
}

// Sobrecarga del evento de cierre para prevenir cierre mientras está activo
void TransferStatisticsDialog::closeEvent(QCloseEvent *event)
{
    if (m_transferActive) {
        // Opcionalmente mostrar un diálogo confirmando la cancelación
        // Por ahora, simplemente ignora la petición de cierre o dispara la cancelación
        qDebug() << "Close event ignored while transfer is active. Triggering cancel.";
        on_btnCancel_clicked(); // Tratar clic en 'X' como petición de cancelar
        event->ignore(); // Mantener el diálogo abierto hasta que la cancelación se confirme
    } else {
        QDialog::closeEvent(event); // Permitir cierre si no está activo
    }
}
