#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QProgressBar>
#include <QListWidget>
#include <QListWidgetItem>
#include <QComboBox>
#include <QMap>
#include <QPixmap>
#include <QIcon>
#include "iconprovider.h"
#include "statemanager.h"
#include <QProgressDialog>

// Forward declarations para evitar dependencias circulares
class DeviceManager;
class DataAnalyzer;
class DataTransferManager;
class TransferStatisticsDialog;

// Luego incluir los archivos de cabecera
#include "devicemanager.h"
#include "dataanalyzer.h"
#include "datatransfermanager.h"
#include "transferstatisticsdialog.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    // Evento de redimensionado para asegurar el centrado correcto
    void resizeEvent(QResizeEvent *event) override;

private slots:
    // Slots para manejar acciones de la UI
    void on_flipButton_clicked();
    void on_startTransferButton_clicked();
    void on_dataTypesList_itemChanged(QListWidgetItem *item);
    void onSourceDeviceChanged(int index);
    void onDestDeviceChanged(int index);

    // Slots para acciones del menú
    void on_actionAcerca_de_triggered();

    // Slots para eventos del DeviceManager
    void onDeviceConnected(const DeviceInfo &device);
    void onDeviceDisconnected(const QString &deviceId);
    void onDeviceAuthorizationChanged(const QString &deviceId, bool authorized);
    void onDeviceManagerError(const QString &message);
    void onDeviceListUpdated();

    // Slots para eventos del DataAnalyzer
    void onAnalysisStarted(const QString &deviceId);
    void onAnalysisProgress(const QString &deviceId, const QString &dataType, int progress);
    void onAnalysisComplete(const QString &deviceId);
    void onAnalysisError(const QString &deviceId, const QString &dataType, const QString &errorMessage);
    void onDataSetUpdated(const QString &deviceId, const QString &dataType);

    // Slots para eventos del DataTransferManager
    void onTransferStarted(qint64 totalSizeEstimate);
    void onTransferProgress(int overallProgress);
    void onTransferTaskStarted(const QString &dataType, int totalItems);
    void onTransferTaskProgress(const QString &dataType, int taskProgressPercent,
                                int processedItems, int totalItems,
                                qint64 processedSize, qint64 totalSize,
                                const QString& currentItemName);
    void onTransferTaskCompleted(const QString &dataType, int successCount);
    void onTransferTaskFailed(const QString &dataType, const QString &errorMessage);
    void onTransferCompleted();
    void onTransferCancelled();
    void onTransferFailed(const QString &errorMessage);

    // Slot para manejar el cierre del diálogo de estadísticas
    void onStatisticsDialogClosed();

private:
    Ui::MainWindow *ui;
    DeviceManager *deviceManager;
    DataAnalyzer *dataAnalyzer;
    DataTransferManager *dataTransferManager;
    TransferStatisticsDialog *m_statisticsDialog;

    // Funciones privadas para la lógica de la aplicación
    void setupInitialUI();
    void setupDataTypesList();
    void updateStartButtonState();
    void updateDeviceUI();
    void updateDeviceComboBoxes();
    void updateDataTypesList();
    QString translateDataTypeForUI(const QString& internalName) const;
    QIcon getIconForDataType(const QString& dataType) const;

    // Función para convertir imágenes a escala de grises
    QPixmap convertToGrayscale(const QPixmap &pixmap);

    // Variables para el estado de la aplicación
    bool isTransferInProgress;
    QString sourceDeviceId;
    QString destDeviceId;

    // Nuevos métodos para StateManager
    void updateUIForState(StateManager::AppState state);
    void updateDeviceDisplays();
    void configureForCurrentState();

    // Método para iniciar análisis de dispositivo
    void onAnalyzeSourceDevice();

    // Integración con IconProvider
    void setupIcons();

    // Indicadores de progreso mejorados
    QProgressDialog* m_analysisProgressDialog;

    // Bandera para indicar si el análisis fue exitoso
    bool m_analysisSuccessful;
};
#endif // MAINWINDOW_H
