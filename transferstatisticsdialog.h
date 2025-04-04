#ifndef TRANSFERSTATISTICSDIALOG_H
#define TRANSFERSTATISTICSDIALOG_H

#include <QDialog>
#include <QDateTime>
#include <QTimer>
#include <QMap>
#include <QListWidgetItem>
#include <QIcon>

// Declaración anticipada
namespace Ui {
class TransferStatisticsDialog;
}

class TransferStatisticsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TransferStatisticsDialog(QWidget *parent = nullptr);
    ~TransferStatisticsDialog();

    void setTotalTransferSize(qint64 totalSize);
    void setSourceDestinationInfo(const QString& sourceName, const QString& sourceType,
                                  const QString& destName, const QString& destType);

    // Funciones de ayuda estáticas
    static QString formatSize(qint64 bytes);
    QString formatTime(int seconds) const;

signals:
    void transferCancelledRequested();

public slots:
    // Slots para recibir señales de DataTransferManager
    void onTransferStarted();
    void onOverallProgressUpdated(int progress);
    void onTaskStarted(const QString &dataType, int totalItems);
    void onTaskProgressUpdated(const QString &dataType, int taskProgressPercent,
                               int processedItems, int totalItems,
                               qint64 processedSize, qint64 totalSize,
                               const QString& currentItemName);
    void onTaskCompleted(const QString &dataType, int successCount);
    void onTaskFailed(const QString &dataType, const QString &errorMessage);
    void onTransferFinished(bool success, const QString& finalMessage);

private slots:
    void updateTimers();
    void on_btnClose_clicked();
    void on_btnCancel_clicked();
    void handleTransferFinished();

private:
    Ui::TransferStatisticsDialog *ui;
    QTimer *m_timer;
    QDateTime m_startTime;
    QDateTime m_lastProgressUpdateTime;
    qint64 m_totalSize;
    qint64 m_lastProcessedSize;
    int m_completedTasks;
    int m_failedTasks;
    bool m_transferActive;
    QString m_finalStatusMessage;
    QString m_currentTaskDataType;

    // Variables para información de origen y destino
    QString m_sourceName;
    QString m_sourceType;
    QString m_destName;
    QString m_destType;

    QMap<QString, QListWidgetItem*> m_taskItems;

    // Funciones de ayuda privadas
    void updateTaskItemText(const QString &dataType, const QString &status, int processed = -1, int total = -1, qint64 sizeProcessed = -1, qint64 sizeTotal = -1, const QString& currentItem = "");
    QString translateDataType(const QString& dataType) const;
    QIcon getIconForDataType(const QString& dataType) const;

    // Sobrecarga del evento de cierre
    void closeEvent(QCloseEvent *event) override;
};

#endif // TRANSFERSTATISTICSDIALOG_H
