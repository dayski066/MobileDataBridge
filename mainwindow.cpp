#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QTimer>
#include <QStyle>
#include <QPixmap>
#include <QApplication>
#include <QPainter>
#include <QResizeEvent>
#include "transferstatisticsdialog.h"
#include <QProgressDialog>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , sourceDeviceId("")
    , destDeviceId("")
    , m_statisticsDialog(nullptr)
    , isTransferInProgress(false)
    , m_analysisProgressDialog(nullptr)
    , m_analysisSuccessful(false)
{
    ui->setupUi(this);
    setupInitialUI();
    setupDataTypesList();
    setupIcons();
    ui->progressFrame->setVisible(false);

    // Configurar acciones del menú
    connect(ui->actionSalir, &QAction::triggered, this, &MainWindow::close);
    connect(ui->actionAcerca_de, &QAction::triggered, this, &MainWindow::on_actionAcerca_de_triggered);

    // Configurar botón flip
    ui->flipButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_BrowserReload));
    ui->flipButton->setToolTip("Swap Source and Destination");

    // Conectar con StateManager
    connect(&StateManager::instance(), &StateManager::stateChanged,
            this, &MainWindow::updateUIForState);
    connect(&StateManager::instance(), &StateManager::sourceDeviceChanged,
            this, &MainWindow::updateDeviceDisplays);
    connect(&StateManager::instance(), &StateManager::destDeviceChanged,
            this, &MainWindow::updateDeviceDisplays);

    // --- DeviceManager Connections ---
    deviceManager = new DeviceManager(this);
    connect(deviceManager, &DeviceManager::deviceConnected, this, &MainWindow::onDeviceConnected);
    connect(deviceManager, &DeviceManager::deviceDisconnected, this, &MainWindow::onDeviceDisconnected);
    connect(deviceManager, &DeviceManager::deviceAuthorizationChanged, this, &MainWindow::onDeviceAuthorizationChanged);
    connect(deviceManager, &DeviceManager::error, this, &MainWindow::onDeviceManagerError);
    connect(deviceManager, &DeviceManager::deviceListUpdated, this, &MainWindow::onDeviceListUpdated);

    // --- DataAnalyzer Connections ---
    dataAnalyzer = new DataAnalyzer(deviceManager, this);
    connect(dataAnalyzer, &DataAnalyzer::analysisStarted, this, &MainWindow::onAnalysisStarted);
    connect(dataAnalyzer, &DataAnalyzer::analysisProgress, this, &MainWindow::onAnalysisProgress);
    connect(dataAnalyzer, &DataAnalyzer::analysisComplete, this, &MainWindow::onAnalysisComplete);
    connect(dataAnalyzer, &DataAnalyzer::analysisError, this, &MainWindow::onAnalysisError);
    connect(dataAnalyzer, &DataAnalyzer::dataSetUpdated, this, &MainWindow::onDataSetUpdated);

    // --- DataTransferManager Connections ---
    dataTransferManager = new DataTransferManager(deviceManager, dataAnalyzer, this);
    connect(dataTransferManager, &DataTransferManager::transferStarted, this, &MainWindow::onTransferStarted);
    connect(dataTransferManager, &DataTransferManager::transferProgress, this, &MainWindow::onTransferProgress);
    connect(dataTransferManager, &DataTransferManager::transferTaskStarted, this, &MainWindow::onTransferTaskStarted);
    connect(dataTransferManager, &DataTransferManager::transferTaskProgress, this, &MainWindow::onTransferTaskProgress);
    connect(dataTransferManager, &DataTransferManager::transferTaskCompleted, this, &MainWindow::onTransferTaskCompleted);
    connect(dataTransferManager, &DataTransferManager::transferTaskFailed, this, &MainWindow::onTransferTaskFailed);
    connect(dataTransferManager, &DataTransferManager::transferCompleted, this, &MainWindow::onTransferCompleted);
    connect(dataTransferManager, &DataTransferManager::transferCancelled, this, &MainWindow::onTransferCancelled);
    connect(dataTransferManager, &DataTransferManager::transferFailed, this, &MainWindow::onTransferFailed);

    // Conexión a la señal final (importante)
    connect(dataTransferManager, &DataTransferManager::transferFinished, this, [this](bool success, const QString& msg){
        Q_UNUSED(success); Q_UNUSED(msg);
        qDebug() << "MainWindow: Recibida señal transferFinished.";
        if (m_statisticsDialog) {
            disconnect(m_statisticsDialog, &QDialog::finished, this, &MainWindow::onStatisticsDialogClosed);
            connect(m_statisticsDialog, &QDialog::finished, this, &MainWindow::onStatisticsDialogClosed);
        }
        isTransferInProgress = false;
        updateStartButtonState();
        ui->flipButton->setEnabled(true);

        // Volver al estado ReadyForTransfer si el origen y destino siguen conectados
        if (!sourceDeviceId.isEmpty() && !destDeviceId.isEmpty()) {
            StateManager::instance().setAppState(StateManager::ReadyForTransfer);
        }

        ui->progressFrame->setVisible(false);
        ui->statusbar->showMessage("Transfer finished.", 5000);
    });

    // Configurar ComboBox
    connect(ui->sourceDeviceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSourceDeviceChanged);
    connect(ui->destDeviceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onDestDeviceChanged);

    // Iniciar detección
    if (!deviceManager->startDeviceDetection()) {
        QMessageBox::warning(this, "Initialization Error", "Could not initialize detection tools. Please verify ADB/libimobiledevice installation.");
    } else {
        ui->statusbar->showMessage("Waiting for devices...", 5000);
    }

    setWindowTitle("Mobile Data Bridge");
    updateDeviceUI();
    updateStartButtonState();

    // Establecer estado inicial
    StateManager::instance().setAppState(StateManager::NoDevices);
}

MainWindow::~MainWindow()
{
    if (deviceManager) {
        deviceManager->stopDeviceDetection();
    }
    delete ui;
}

// --- SETUP AND UI UPDATE FUNCTIONS ---
void MainWindow::setupInitialUI()
{
    // Configurar tamaños y alineaciones iniciales
    ui->sourceImageLabel->setScaledContents(false);
    ui->sourceImageLabel->setAlignment(Qt::AlignCenter);
    ui->destImageLabel->setScaledContents(false);
    ui->destImageLabel->setAlignment(Qt::AlignCenter);

    // Estilos modernos con gradientes y sombras
    QString defaultStyle = "QLabel { "
                           "background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                           "stop:0 #f8f9fa, stop:1 #e9ecef); "
                           "border-radius: 12px; "
                           "border: 1px solid #dee2e6; "
                           "padding: 10px; }";
    ui->sourceImageLabel->setStyleSheet(defaultStyle);
    ui->destImageLabel->setStyleSheet(defaultStyle);

    // Estilos para la barra de progreso
    QString progressStyle = "QProgressBar { "
                            "border: 1px solid #dee2e6; "
                            "border-radius: 5px; "
                            "background-color: #f8f9fa; "
                            "text-align: center; }"
                            "QProgressBar::chunk { "
                            "background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                            "stop:0 #4dabf7, stop:1 #3a8eda); "
                            "border-radius: 5px; }";
    ui->progressBar->setStyleSheet(progressStyle);

    // Estilos para botones
    QString buttonStyle = "QPushButton { "
                          "background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                          "stop:0 #4dabf7, stop:1 #3a8eda); "
                          "color: white; "
                          "border-radius: 6px; "
                          "padding: 8px 16px; "
                          "font-weight: bold; }"
                          "QPushButton:hover { "
                          "background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                          "stop:0 #3a8eda, stop:1 #1971c2); }"
                          "QPushButton:disabled { "
                          "background-color: #adb5bd; }";
    ui->startTransferButton->setStyleSheet(buttonStyle);

    // Estilo para el botón flip
    QString flipButtonStyle = "QPushButton { "
                              "background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                              "stop:0 #6c757d, stop:1 #495057); "
                              "color: white; "
                              "border-radius: 6px; "
                              "padding: 6px; }"
                              "QPushButton:hover { "
                              "background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                              "stop:0 #5a6268, stop:1 #343a40); }"
                              "QPushButton:disabled { "
                              "background-color: #adb5bd; }";
    ui->flipButton->setStyleSheet(flipButtonStyle);

    // Estilo para la lista de datos
    QString listStyle = "QListWidget { "
                        "border-radius: 6px; "
                        "border: 1px solid #dee2e6; "
                        "background-color: white; }"
                        "QListWidget::item { "
                        "border-bottom: 1px solid #f1f3f5; "
                        "padding: 5px; }"
                        "QListWidget::item:selected { "
                        "background-color: #e7f5ff; "
                        "color: #1864ab; }";
    ui->dataTypesList->setStyleSheet(listStyle);

    // Estilo para el checkbox
    QString checkboxStyle = "QCheckBox { "
                            "spacing: 8px; }"
                            "QCheckBox::indicator { "
                            "width: 18px; "
                            "height: 18px; }"
                            "QCheckBox::indicator:unchecked { "
                            "border: 1px solid #ced4da; "
                            "border-radius: 3px; "
                            "background-color: white; }"
                            "QCheckBox::indicator:checked { "
                            "border: 1px solid #1971c2; "
                            "border-radius: 3px; "
                            "background-color: #3a8eda; "
                            "image: url(:/checkbox_checked.png); }";
    ui->clearBeforeCopyCheckBox->setStyleSheet(checkboxStyle);

    // Estilos para los combobox
    QString comboBoxStyle = "QComboBox { "
                            "border: 1px solid #ced4da; "
                            "border-radius: 5px; "
                            "padding: 5px; "
                            "background-color: white; }"
                            "QComboBox::drop-down { "
                            "border: none; "
                            "width: 24px; }"
                            "QComboBox::down-arrow { "
                            "image: url(:/dropdown_arrow.png); }";
    ui->sourceDeviceComboBox->setStyleSheet(comboBoxStyle);
    ui->destDeviceComboBox->setStyleSheet(comboBoxStyle);

    // Marco de progreso
    QString progressFrameStyle = "QFrame { "
                                 "border: 1px solid #dee2e6; "
                                 "border-radius: 8px; "
                                 "background-color: #f8f9fa; "
                                 "padding: 10px; }";
    ui->progressFrame->setStyleSheet(progressFrameStyle);

    // Cargar imágenes de placeholder
    QPixmap androidPlaceholder(":/android-icon.svg");
    QPixmap iosPlaceholder(":/ios-icon.svg");
    if (!androidPlaceholder.isNull()) {
        ui->sourceImageLabel->setPixmap(androidPlaceholder.scaled(
            ui->sourceImageLabel->width() - 40,
            ui->sourceImageLabel->height() - 40,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
    }
    if (!iosPlaceholder.isNull()) {
        ui->destImageLabel->setPixmap(iosPlaceholder.scaled(
            ui->destImageLabel->width() - 40,
            ui->destImageLabel->height() - 40,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
    }

    // Configurar textos iniciales con HTML mejorado
    ui->sourceDeviceLabel->setText("<div style='text-align:center;'>"
                                   "<span style='font-size:12pt; font-weight:bold; color:#343a40;'>"
                                   "Conecte un dispositivo</span><br>"
                                   "<span style='color:#6c757d;'>(Origen)</span></div>");
    ui->destDeviceLabel->setText("<div style='text-align:center;'>"
                                 "<span style='font-size:12pt; font-weight:bold; color:#343a40;'>"
                                 "Conecte un dispositivo</span><br>"
                                 "<span style='color:#6c757d;'>(Destino)</span></div>");
    ui->sourceDeviceLabel->setTextFormat(Qt::RichText);
    ui->destDeviceLabel->setTextFormat(Qt::RichText);

    // Ajustes adicionales
    setWindowTitle("Mobile Data Bridge");
    if (ui->dataTypesHeaderLabel) {
        ui->dataTypesHeaderLabel->setStyleSheet("QLabel { color: #1971c2; font-size: 12pt; font-weight: bold; }");
    }
}

void MainWindow::setupDataTypesList()
{
    QListWidget* dataList = ui->dataTypesList;
    dataList->clear();
    dataList->setEnabled(false);
    ui->clearBeforeCopyCheckBox->setEnabled(false);
    connect(dataList, &QListWidget::itemChanged, this, &MainWindow::on_dataTypesList_itemChanged);
    ui->startTransferButton->setEnabled(false);
}

void MainWindow::setupBridgeClientButton()
{
    // Crear botón para inicializar Bridge Client
    bridgeClientButton = new QPushButton(tr("Inicializar Bridge Client"), this);
    bridgeClientButton->setEnabled(false);
    bridgeClientButton->setToolTip(tr("Instalar y configurar la aplicación Bridge Client en el dispositivo Android"));

    // Añadir icono si está disponible
    bridgeClientButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));

    // Estilo para el botón similar a otros botones
    QString buttonStyle = "QPushButton { "
                          "background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                          "stop:0 #4dabf7, stop:1 #3a8eda); "
                          "color: white; "
                          "border-radius: 6px; "
                          "padding: 8px 16px; "
                          "font-weight: bold; }"
                          "QPushButton:hover { "
                          "background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                          "stop:0 #3a8eda, stop:1 #1971c2); }"
                          "QPushButton:disabled { "
                          "background-color: #adb5bd; }";
    bridgeClientButton->setStyleSheet(buttonStyle);

    // Añadir botón al layout principal
    // Debe añadirse a un layout existente o crearse uno nuevo
    // Por ejemplo, si usas controlsLayout:
    ui->controlsLayout->addWidget(bridgeClientButton);

    // Conectar el botón a la acción
    connect(bridgeClientButton, &QPushButton::clicked, this, &MainWindow::onBridgeClientButtonClicked);
}

void MainWindow::updateDeviceComboBoxes()
{
    // Desconectar señales temporalmente para evitar activar los slots durante la actualización
    ui->sourceDeviceComboBox->blockSignals(true);
    ui->destDeviceComboBox->blockSignals(true);

    // Guardar selecciones actuales
    QString currentSourceId = sourceDeviceId;
    QString currentDestId = destDeviceId;

    // Limpiar y actualizar ComboBoxes
    ui->sourceDeviceComboBox->clear();
    ui->destDeviceComboBox->clear();
    ui->sourceDeviceComboBox->addItem("Select Source Device", "");
    ui->destDeviceComboBox->addItem("Select Destination Device", "");

    // Añadir dispositivos a los ComboBoxes
    QList<DeviceInfo> devices = deviceManager->getConnectedDevices();
    for (const DeviceInfo& device : devices) {
        QString displayText = QString("%1 (%2)%3").arg(
            device.name,
            device.model,
            device.authorized ? "" : " - Not Authorized"
            );
        ui->sourceDeviceComboBox->addItem(displayText, device.id);
        ui->destDeviceComboBox->addItem(displayText, device.id);
    }

    // Restaurar selecciones anteriores si es posible
    int sourceIndex = ui->sourceDeviceComboBox->findData(currentSourceId);
    int destIndex = ui->destDeviceComboBox->findData(currentDestId);
    if (sourceIndex >= 0) {
        ui->sourceDeviceComboBox->setCurrentIndex(sourceIndex);
    }
    if (destIndex >= 0) {
        ui->destDeviceComboBox->setCurrentIndex(destIndex);
    }

    // Restaurar señales
    ui->sourceDeviceComboBox->blockSignals(false);
    ui->destDeviceComboBox->blockSignals(false);
}

void MainWindow::updateDeviceUI()
{
    bool sourceConnected = !sourceDeviceId.isEmpty();
    bool destConnected = !destDeviceId.isEmpty();
    DeviceInfo sourceDevice;
    DeviceInfo destDevice;

    // Cargar Placeholders con los nombres correctos
    QPixmap placeholderAndroidPixmap(":/android-icon.svg");
    QPixmap placeholderIOSPixmap(":/ios-icon.svg");
    QPixmap fallbackPixmap = QApplication::style()->standardIcon(QStyle::SP_DriveNetIcon).pixmap(240, 240);

    // Actualizar ComboBoxes
    updateDeviceComboBoxes();

    // Actualizar Fuente
    if (sourceConnected) {
        sourceDevice = deviceManager->getDeviceInfo(sourceDeviceId);
        QString authorizedStatus = sourceDevice.authorized ?
                                       "<span style='color:#28a745;'>✓ Autorizado</span>" :
                                       "<span style='color:#dc3545;'>⚠️ Autorización requerida</span>";

        QString sourceText = QString("<div style='text-align:center;'>"
                                     "<span style='font-size:13pt; font-weight:bold; color:#212529;'>%1</span><br>"
                                     "<span style='color:#495057;'>(%2)</span><br>"
                                     "%3</div>")
                                 .arg(sourceDevice.name)
                                 .arg(sourceDevice.model)
                                 .arg(authorizedStatus);

        QPixmap sourcePixmap = fallbackPixmap;
        if (sourceDevice.type == "android" && !placeholderAndroidPixmap.isNull()) {
            sourcePixmap = placeholderAndroidPixmap;
        } else if (sourceDevice.type == "ios" && !placeholderIOSPixmap.isNull()) {
            sourcePixmap = placeholderIOSPixmap;
        }
        sourcePixmap = sourcePixmap.scaled(
            ui->sourceImageLabel->width() - 40,
            ui->sourceImageLabel->height() - 40,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);

        ui->sourceDeviceLabel->setText(sourceText);
        ui->sourceDeviceLabel->setTextFormat(Qt::RichText);
        ui->sourceImageLabel->setPixmap(sourcePixmap);
        ui->sourceImageLabel->setAlignment(Qt::AlignCenter);

        // Estilo moderno para el fondo dependiendo del estado
        if (sourceDevice.authorized) {
            ui->sourceImageLabel->setStyleSheet("QLabel { "
                                                "background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                                                "stop:0 #e7f5ff, stop:1 #d0ebff); "
                                                "border-radius: 12px; "
                                                "border: 2px solid #339af0; "
                                                "padding: 10px; }");
        } else {
            ui->sourceImageLabel->setStyleSheet("QLabel { "
                                                "background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                                                "stop:0 #fff5f5, stop:1 #ffe3e3); "
                                                "border-radius: 12px; "
                                                "border: 2px solid #fa5252; "
                                                "padding: 10px; }");
        }
    } else {
        ui->sourceDeviceLabel->setText("<div style='text-align:center;'>"
                                       "<span style='font-size:12pt; font-weight:bold; color:#343a40;'>"
                                       "Conecte un dispositivo</span><br>"
                                       "<span style='color:#6c757d;'>(Origen)</span></div>");
        ui->sourceDeviceLabel->setTextFormat(Qt::RichText);

        QPixmap greyPlaceholder = convertToGrayscale(placeholderAndroidPixmap);
        greyPlaceholder = greyPlaceholder.scaled(
            ui->sourceImageLabel->width() - 40,
            ui->sourceImageLabel->height() - 40,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
        ui->sourceImageLabel->setPixmap(greyPlaceholder);
        ui->sourceImageLabel->setAlignment(Qt::AlignCenter);
        ui->sourceImageLabel->setStyleSheet("QLabel { "
                                            "background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                                            "stop:0 #f8f9fa, stop:1 #e9ecef); "
                                            "border-radius: 12px; "
                                            "border: 2px dashed #ced4da; "
                                            "padding: 10px; }");
    }

    // Actualizar Destino
    if (destConnected) {
        destDevice = deviceManager->getDeviceInfo(destDeviceId);
        QString authorizedStatus = destDevice.authorized ?
                                       "<span style='color:#28a745;'>✓ Autorizado</span>" :
                                       "<span style='color:#dc3545;'>⚠️ Autorización requerida</span>";

        QString destText = QString("<div style='text-align:center;'>"
                                   "<span style='font-size:13pt; font-weight:bold; color:#212529;'>%1</span><br>"
                                   "<span style='color:#495057;'>(%2)</span><br>"
                                   "%3</div>")
                               .arg(destDevice.name)
                               .arg(destDevice.model)
                               .arg(authorizedStatus);

        QPixmap destPixmap = fallbackPixmap;
        if (destDevice.type == "ios" && !placeholderIOSPixmap.isNull()) {
            destPixmap = placeholderIOSPixmap;
        } else if (destDevice.type == "android" && !placeholderAndroidPixmap.isNull()) {
            destPixmap = placeholderAndroidPixmap;
        }
        destPixmap = destPixmap.scaled(
            ui->destImageLabel->width() - 40,
            ui->destImageLabel->height() - 40,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);

        ui->destDeviceLabel->setText(destText);
        ui->destDeviceLabel->setTextFormat(Qt::RichText);
        ui->destImageLabel->setPixmap(destPixmap);
        ui->destImageLabel->setAlignment(Qt::AlignCenter);

        // Estilo moderno para el fondo dependiendo del estado
        if (destDevice.authorized) {
            ui->destImageLabel->setStyleSheet("QLabel { "
                                              "background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                                              "stop:0 #e7f5ff, stop:1 #d0ebff); "
                                              "border-radius: 12px; "
                                              "border: 2px solid #339af0; "
                                              "padding: 10px; }");
        } else {
            ui->destImageLabel->setStyleSheet("QLabel { "
                                              "background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                                              "stop:0 #fff5f5, stop:1 #ffe3e3); "
                                              "border-radius: 12px; "
                                              "border: 2px solid #fa5252; "
                                              "padding: 10px; }");
        }
    } else {
        ui->destDeviceLabel->setText("<div style='text-align:center;'>"
                                     "<span style='font-size:12pt; font-weight:bold; color:#343a40;'>"
                                     "Conecte un dispositivo</span><br>"
                                     "<span style='color:#6c757d;'>(Destino)</span></div>");
        ui->destDeviceLabel->setTextFormat(Qt::RichText);

        QPixmap greyPlaceholder = convertToGrayscale(placeholderIOSPixmap);
        greyPlaceholder = greyPlaceholder.scaled(
            ui->destImageLabel->width() - 40,
            ui->destImageLabel->height() - 40,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
        ui->destImageLabel->setPixmap(greyPlaceholder);
        ui->destImageLabel->setAlignment(Qt::AlignCenter);
        ui->destImageLabel->setStyleSheet("QLabel { "
                                          "background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                                          "stop:0 #f8f9fa, stop:1 #e9ecef); "
                                          "border-radius: 12px; "
                                          "border: 2px dashed #ced4da; "
                                          "padding: 10px; }");
    }

    // Actualizar estilo y texto del botón "Flip"
    ui->flipButton->setEnabled(sourceConnected || destConnected);
    updateDataTypesList();
}

void MainWindow::updateDataTypesList()
{
    QListWidget* dataList = ui->dataTypesList;
    QMap<QString, bool> currentSelections;

    // Guardar selecciones actuales
    for (int i = 0; i < dataList->count(); ++i) {
        QString internalName = dataList->item(i)->data(Qt::UserRole).toString();
        if (!internalName.isEmpty()) {
            currentSelections[internalName] = (dataList->item(i)->checkState() == Qt::Checked);
        }
    }

    // Bloquear señales para evitar llamadas innecesarias a itemChanged
    dataList->blockSignals(true);
    dataList->clear();

    if (sourceDeviceId.isEmpty()) {
        dataList->setEnabled(false);
        ui->clearBeforeCopyCheckBox->setEnabled(false);
        dataList->blockSignals(false);  // Desbloquear señales antes de salir
        updateStartButtonState();
        return;
    }

    DeviceInfo sourceDevice = deviceManager->getDeviceInfo(sourceDeviceId);
    DeviceInfo destDevice = deviceManager->getDeviceInfo(destDeviceId);

    QStringList displayOrder = {"photos", "videos", "contacts", "messages", "calls", "calendar", "music", "notes", "voice_memos", "voicemail"};
    QStringList supportedTypes;
    if (!destDevice.id.isEmpty()) {
        supportedTypes = dataAnalyzer->getSupportedDataTypes(sourceDeviceId, destDeviceId);
    }

    bool anyDataAvailable = false;

    // Imprimir información de depuración
    qDebug() << "Updating data types list for source:" << sourceDeviceId;

    for (const QString &type : displayOrder) {
        DataSet dataSet = dataAnalyzer->getDataSet(sourceDeviceId, type);
        int count = dataSet.items.size();
        qint64 size = dataSet.totalSize;

        qDebug() << "Processing data type:" << type << "Count:" << count << "Size:" << size
                 << "Supported:" << dataSet.isSupported << "Error:" << dataSet.errorMessage;

        bool isSupportedBySource = dataSet.errorMessage.isEmpty();
        bool isTransferSupported = !destDevice.id.isEmpty() && supportedTypes.contains(type);

        QString displayName = translateDataTypeForUI(type);
        QString countString = (count > 0) ? QString::number(count) : "0";
        QString sizeString = (size > 0) ? TransferStatisticsDialog::formatSize(size) : "";

        QString displayText = displayName;
        if (count > 0 || size > 0) {
            displayText += QString(" (%1").arg(countString);
            if (size > 0) {
                displayText += QString(" - %1").arg(sizeString);
            }
            displayText += ")";
        }

        QListWidgetItem* item = new QListWidgetItem(displayText, dataList);
        item->setData(Qt::UserRole, type);
        item->setIcon(getIconForDataType(type));

        Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
        bool canBeChecked = false;
        QString toolTipText;
        if (!sourceDevice.authorized) {
            toolTipText = "Source device not authorized.";
        } else if (!isSupportedBySource) {
            toolTipText = "Error analyzing: " + dataSet.errorMessage;
        } else if (count == 0) {
            toolTipText = "No items found.";
        } else if (destDevice.id.isEmpty()) {
            toolTipText = "Please connect destination device.";
        } else if (!destDevice.authorized) {
            toolTipText = "Destination device not authorized.";
        } else if (!isTransferSupported) {
            toolTipText = dataAnalyzer->getIncompatibilityReason(sourceDevice.type, destDevice.type, type);
            if (toolTipText.isEmpty()) toolTipText = "Transfer not supported.";
        } else {
            canBeChecked = true;
            anyDataAvailable = true;
            toolTipText = QString("Transfer %1 items (%2) from '%3' to '%4'")
                              .arg(countString)
                              .arg(sizeString)
                              .arg(sourceDevice.name)
                              .arg(destDevice.name);
        }

        if (canBeChecked) {
            flags |= Qt::ItemIsEnabled;
            item->setCheckState(currentSelections.value(type, false) ? Qt::Checked : Qt::Unchecked);
            item->setBackground(QColor(240, 255, 240));
        } else {
            item->setCheckState(Qt::Unchecked);
            item->setForeground(QApplication::palette().color(QPalette::Disabled, QPalette::Text));
            if (!sourceDevice.authorized || !destDevice.authorized || !isTransferSupported) {
                item->setBackground(QColor(255, 240, 240));
            }
        }

        item->setFlags(flags);
        item->setToolTip(toolTipText);
    }

    if (dataList->count() == 0) {
        QListWidgetItem* emptyItem = new QListWidgetItem("No data found to transfer", dataList);
        emptyItem->setFlags(Qt::NoItemFlags);
        emptyItem->setTextAlignment(Qt::AlignCenter);
    }

    // Determinar si debemos habilitar los controles
    bool enableControls = anyDataAvailable && sourceDevice.authorized;

    qDebug() << "Enable data controls:" << enableControls
             << "Any data:" << anyDataAvailable
             << "Dest connected:" << !destDevice.id.isEmpty()
             << "Source auth:" << sourceDevice.authorized
             << "Dest auth:" << (destDevice.id.isEmpty() ? false : destDevice.authorized);

    dataList->setEnabled(enableControls);
    ui->clearBeforeCopyCheckBox->setEnabled(enableControls);

    // Desbloquear señales después de completar todas las modificaciones
    dataList->blockSignals(false);

    updateStartButtonState();

    if (ui->dataTypesHeaderLabel) {
        ui->dataTypesHeaderLabel->setText(enableControls ?
                                              "Select content to copy:" :
                                              "No items available to transfer");
    }
}

void MainWindow::updateStartButtonState()
{
    bool devicesReady = false;
    if (!sourceDeviceId.isEmpty() && !destDeviceId.isEmpty()) {
        DeviceInfo sourceDevice = deviceManager->getDeviceInfo(sourceDeviceId);
        DeviceInfo destDevice = deviceManager->getDeviceInfo(destDeviceId);
        devicesReady = sourceDevice.authorized && destDevice.authorized;
    }

    bool dataSelected = false;
    for (int i = 0; i < ui->dataTypesList->count(); i++) {
        QListWidgetItem* item = ui->dataTypesList->item(i);
        if (item->checkState() == Qt::Checked && (item->flags() & Qt::ItemIsEnabled)) {
            dataSelected = true;
            break;
        }
    }

    ui->startTransferButton->setEnabled(devicesReady && dataSelected && !isTransferInProgress);
}

QString MainWindow::translateDataTypeForUI(const QString& internalName) const
{
    if (internalName == "contacts") return "Contacts";
    if (internalName == "messages") return "Messages";
    if (internalName == "photos") return "Photos";
    if (internalName == "videos") return "Videos";
    if (internalName == "calls") return "Call Logs";
    if (internalName == "calendar") return "Calendar";
    if (internalName == "music") return "Music";
    if (internalName == "notes") return "Notes";
    if (internalName == "voice_memos") return "Voice Memos";
    if (internalName == "voicemail") return "Voicemail";
    return internalName;
}

QIcon MainWindow::getIconForDataType(const QString& dataType) const
{
    // Usar IconProvider para obtener los iconos
    return IconProvider::instance().getDataTypeIcon(dataType);
}

QPixmap MainWindow::convertToGrayscale(const QPixmap &pixmap)
{
    QImage image = pixmap.toImage();
    QImage grayImage = image.convertToFormat(QImage::Format_Grayscale8);
    return QPixmap::fromImage(grayImage);
}

// --- SLOTS PARA EVENTOS DE UI ---
void MainWindow::on_flipButton_clicked()
{
    QString tempId = sourceDeviceId;
    sourceDeviceId = destDeviceId;
    destDeviceId = tempId;
    qDebug() << "Devices swapped. Source:" << sourceDeviceId << "Destination:" << destDeviceId;

    int sourceIndex = ui->sourceDeviceComboBox->findData(sourceDeviceId);
    int destIndex = ui->destDeviceComboBox->findData(destDeviceId);

    ui->sourceDeviceComboBox->blockSignals(true);
    ui->destDeviceComboBox->blockSignals(true);

    if (sourceIndex >= 0) {
        ui->sourceDeviceComboBox->setCurrentIndex(sourceIndex);
    } else {
        ui->sourceDeviceComboBox->setCurrentIndex(0);
    }

    if (destIndex >= 0) {
        ui->destDeviceComboBox->setCurrentIndex(destIndex);
    } else {
        ui->destDeviceComboBox->setCurrentIndex(0);
    }

    ui->sourceDeviceComboBox->blockSignals(false);
    ui->destDeviceComboBox->blockSignals(false);

    updateDeviceUI();
}

void MainWindow::on_startTransferButton_clicked()
{
    if (sourceDeviceId.isEmpty() || destDeviceId.isEmpty()) return;
    DeviceInfo sourceDev = deviceManager->getDeviceInfo(sourceDeviceId);
    DeviceInfo destDev = deviceManager->getDeviceInfo(destDeviceId);
    if (!sourceDev.authorized || !destDev.authorized) return;

    QStringList selectedTypes;
    qint64 estimatedTotalSize = 0;
    for (int i = 0; i < ui->dataTypesList->count(); ++i) {
        QListWidgetItem* item = ui->dataTypesList->item(i);
        if (item->checkState() == Qt::Checked && (item->flags() & Qt::ItemIsEnabled)) {
            QString internalName = item->data(Qt::UserRole).toString();
            if (!internalName.isEmpty()) {
                selectedTypes.append(internalName);
                DataSet ds = dataAnalyzer->getDataSet(sourceDeviceId, internalName);
                estimatedTotalSize += ds.totalSize > 0 ? ds.totalSize : ds.items.size();
            }
        }
    }

    if (selectedTypes.isEmpty()) {
        QMessageBox::warning(this, "Empty Selection", "Please select data to transfer.");
        return;
    }

    bool clearBeforeCopy = ui->clearBeforeCopyCheckBox->isChecked();
    QString confirmationMessage = QString("Transfer from %1 to %2:\n").arg(sourceDev.name).arg(destDev.name);
    for (const QString& type : selectedTypes) {
        confirmationMessage += QString("- %1\n").arg(translateDataTypeForUI(type));
    }
    confirmationMessage += QString("\nEstimated total size: %1\n").arg(TransferStatisticsDialog::formatSize(estimatedTotalSize));
    if (clearBeforeCopy) {
        confirmationMessage += "\nWARNING! Existing data on the destination will be deleted.\n";
    }
    confirmationMessage += "\nDo you want to continue?";

    QMessageBox::StandardButton reply = QMessageBox::question(this, "Confirm Transfer",
                                                              confirmationMessage,
                                                              QMessageBox::Yes | QMessageBox::No,
                                                              QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        if (m_statisticsDialog) {
            delete m_statisticsDialog;
            m_statisticsDialog = nullptr;
        }
        m_statisticsDialog = new TransferStatisticsDialog(this);
        m_statisticsDialog->setSourceDestinationInfo(
            sourceDev.name, sourceDev.type,
            destDev.name, destDev.type);

        connect(dataTransferManager, &DataTransferManager::transferStarted, m_statisticsDialog, [this](qint64 totalSize){
            if (m_statisticsDialog) {
                m_statisticsDialog->setTotalTransferSize(totalSize);
                m_statisticsDialog->onTransferStarted();
            }
        });
        connect(dataTransferManager, &DataTransferManager::transferProgress, m_statisticsDialog, &TransferStatisticsDialog::onOverallProgressUpdated);
        connect(dataTransferManager, &DataTransferManager::transferTaskStarted, m_statisticsDialog, &TransferStatisticsDialog::onTaskStarted);
        connect(dataTransferManager, &DataTransferManager::transferTaskProgress, m_statisticsDialog, &TransferStatisticsDialog::onTaskProgressUpdated);
        connect(dataTransferManager, &DataTransferManager::transferTaskCompleted, m_statisticsDialog, &TransferStatisticsDialog::onTaskCompleted);
        connect(dataTransferManager, &DataTransferManager::transferTaskFailed, m_statisticsDialog, &TransferStatisticsDialog::onTaskFailed);
        connect(dataTransferManager, &DataTransferManager::transferFinished, m_statisticsDialog, &TransferStatisticsDialog::onTransferFinished);
        connect(m_statisticsDialog, &TransferStatisticsDialog::transferCancelledRequested, dataTransferManager, &DataTransferManager::cancelTransfer);

        if (dataTransferManager->startTransfer(sourceDeviceId, destDeviceId, selectedTypes, clearBeforeCopy)) {
            isTransferInProgress = true;
            updateStartButtonState();
            ui->flipButton->setEnabled(false);
            ui->dataTypesList->setEnabled(false);
            ui->clearBeforeCopyCheckBox->setEnabled(false);
            m_statisticsDialog->show();
        } else {
            QMessageBox::critical(this, "Transfer Error", "Could not start the transfer.");
            delete m_statisticsDialog;
            m_statisticsDialog = nullptr;
        }
    }
}

void MainWindow::on_dataTypesList_itemChanged(QListWidgetItem *item)
{
    if (!item) return;

    // Solo actualizar cuando el elemento está habilitado (evita responder a actualizaciones programáticas)
    if (item->flags() & Qt::ItemIsEnabled) {
        bool isChecked = (item->checkState() == Qt::Checked);
        QString dataType = item->data(Qt::UserRole).toString();
        qDebug() << "User changed data type:" << dataType << "Checked:" << isChecked;
        updateStartButtonState();
    }
}

void MainWindow::onSourceDeviceChanged(int index)
{
    QString newSourceId = ui->sourceDeviceComboBox->itemData(index).toString();
    if (newSourceId != sourceDeviceId) {
        sourceDeviceId = newSourceId;
        qDebug() << "Source device changed to ID:" << sourceDeviceId;
        updateDeviceUI();
    }
}

void MainWindow::onDestDeviceChanged(int index)
{
    QString newDestId = ui->destDeviceComboBox->itemData(index).toString();
    if (newDestId != destDeviceId) {
        destDeviceId = newDestId;
        qDebug() << "Destination device changed to ID:" << destDeviceId;
        updateDeviceUI();
    }
}

void MainWindow::on_actionAcerca_de_triggered()
{
    QMessageBox::about(this, "Acerca de Mobile Data Bridge",
                       "Mobile Data Bridge v1.0\n"
                       "© 2023 Your Company\n"
                       "Todos los derechos reservados.\n\n"
                       "Esta aplicación permite transferir datos entre dispositivos móviles.");
}

void MainWindow::onDeviceConnected(const DeviceInfo &device)
{
    qDebug() << "Dispositivo conectado:" << device.id << device.name << "Tipo:" << device.type;

    // Crear un mensaje en la barra de estado
    QString statusMsg = QString("Dispositivo %1 conectado").arg(device.name);
    ui->statusbar->showMessage(statusMsg, 5000);

    // Si no hay dispositivo origen seleccionado, asignarlo como origen
    if (sourceDeviceId.isEmpty()) {
        sourceDeviceId = device.id;

        // Actualizar también StateManager
        StateManager::instance().setSourceDevice(device.id, device.authorized);

        qDebug() << "Dispositivo asignado como origen:" << device.id;

        // Iniciar análisis inmediatamente si está autorizado
        if (device.authorized) {
            qDebug() << "Iniciando análisis para dispositivo origen:" << device.id;
            // El análisis ahora se maneja a través de StateManager y updateUIForState
        }
    }
    // Si ya hay una fuente pero no destino, asignarlo como destino
    else if (destDeviceId.isEmpty() && device.id != sourceDeviceId) {
        destDeviceId = device.id;

        // Actualizar también StateManager
        StateManager::instance().setDestDevice(device.id, device.authorized);

        qDebug() << "Dispositivo asignado como destino:" << device.id;
    }

    updateDeviceUI();
}

void MainWindow::onDeviceDisconnected(const QString &deviceId)
{
    qDebug() << "Dispositivo desconectado:" << deviceId;

    QString deviceName = "Dispositivo";

    if (deviceId == sourceDeviceId) {
        deviceName = deviceManager->getDeviceInfo(deviceId).name;
        if (deviceName.isEmpty()) deviceName = "Dispositivo origen";
        qDebug() << "Dispositivo origen desconectado";
        sourceDeviceId.clear();

        // Actualizar también StateManager
        StateManager::instance().clearSourceDevice();

        // Notificar al usuario
        ui->statusbar->showMessage("Dispositivo origen desconectado", 5000);
        QMessageBox::warning(this, "Dispositivo Desconectado",
                             QString("El dispositivo origen '%1' ha sido desconectado.\n\nSi estaba en medio de una transferencia, ésta se ha cancelado.").arg(deviceName));
    }

    if (deviceId == destDeviceId) {
        deviceName = deviceManager->getDeviceInfo(deviceId).name;
        if (deviceName.isEmpty()) deviceName = "Dispositivo destino";
        qDebug() << "Dispositivo destino desconectado";
        destDeviceId.clear();

        // Actualizar también StateManager
        StateManager::instance().clearDestDevice();

        // Cancelar cualquier transferencia en curso
        if (dataTransferManager->isTransferInProgress()) {
            dataTransferManager->cancelTransfer();
        }

        // Notificar al usuario
        ui->statusbar->showMessage("Dispositivo destino desconectado", 5000);
        QMessageBox::warning(this, "Dispositivo Desconectado",
                             QString("El dispositivo destino '%1' ha sido desconectado.\n\nSi estaba en medio de una transferencia, ésta se ha cancelado.").arg(deviceName));
    }

    updateDeviceUI();
}

void MainWindow::onDeviceAuthorizationChanged(const QString &deviceId, bool authorized)
{
    qDebug() << "Estado de autorización del dispositivo cambiado:" << deviceId << "Autorizado:" << authorized;

    DeviceInfo device = deviceManager->getDeviceInfo(deviceId);
    QString deviceName = device.name.isEmpty() ? "Dispositivo" : device.name;

    if (authorized) {
        ui->statusbar->showMessage(QString("Dispositivo '%1' autorizado").arg(deviceName), 5000);
    } else {
        ui->statusbar->showMessage(QString("Dispositivo '%1' requiere autorización").arg(deviceName), 5000);
    }

    // Actualizar en StateManager
    if (deviceId == sourceDeviceId) {
        StateManager::instance().setSourceDevice(deviceId, authorized);
    } else if (deviceId == destDeviceId) {
        StateManager::instance().setDestDevice(deviceId, authorized);
    }

    updateDeviceUI();
}

void MainWindow::onDeviceManagerError(const QString &message)
{
    qWarning() << "DeviceManager error:" << message;
    QMessageBox::warning(this, "Device Manager Error", message);
}

void MainWindow::onDeviceListUpdated()
{
    qDebug() << "Device list updated.";
    updateDeviceUI();
}

void MainWindow::onAnalysisStarted(const QString &deviceId)
{
    qDebug() << "Análisis iniciado para dispositivo:" << deviceId;

    // Si tenemos diálogo de progreso, actualizarlo
    if (m_analysisProgressDialog) {
        m_analysisProgressDialog->setValue(0);
        m_analysisProgressDialog->setLabelText("Iniciando análisis del dispositivo...");
    }

    ui->statusbar->showMessage("Analizando datos del dispositivo...", 0);
}

void MainWindow::onAnalysisProgress(const QString &deviceId, const QString &dataType, int progress)
{
    qDebug() << "Progreso del análisis:" << deviceId << dataType << progress << "%";

    // Actualizar diálogo de progreso
    if (m_analysisProgressDialog) {
        m_analysisProgressDialog->setValue(progress);
        m_analysisProgressDialog->setLabelText(QString("Analizando %1: %2%")
                                                   .arg(translateDataTypeForUI(dataType))
                                                   .arg(progress));
    }
}

void MainWindow::onAnalysisComplete(const QString &deviceId)
{
    qDebug() << "Análisis completado para dispositivo:" << deviceId;

    // Cerrar diálogo de progreso si existe
    if (m_analysisProgressDialog) {
        m_analysisProgressDialog->setValue(100);
        m_analysisProgressDialog->setLabelText("¡Análisis completado!");
        QTimer::singleShot(1000, m_analysisProgressDialog, &QProgressDialog::close);
    }

    m_analysisSuccessful = true;

    // Preparar interfaz para transferencia
    StateManager::instance().setAppState(StateManager::ReadyForTransfer);

    // Mostrar información sobre los datos encontrados
    QStringList dataTypes = {"photos", "videos", "contacts", "messages", "calls", "calendar", "music"};
    int totalItems = 0;
    qint64 totalSize = 0;
    QStringList foundTypes;

    for (const QString& type : dataTypes) {
        DataSet ds = dataAnalyzer->getDataSet(deviceId, type);
        if (ds.items.size() > 0) {
            totalItems += ds.items.size();
            totalSize += ds.totalSize;
            foundTypes << translateDataTypeForUI(type);

            qDebug() << "Encontrado tipo de dato:" << type << "Elementos:" << ds.items.size()
                     << "Tamaño:" << ds.totalSize
                     << "Soportado:" << ds.isSupported;
        }
    }

    // Actualizar la lista de tipos de datos
    updateDataTypesList();

    // Mostrar resumen al usuario
    if (totalItems > 0) {
        QString message = QString("Análisis completado. Se encontraron %1 elementos (%2) "
                                  "en las siguientes categorías:\n\n%3\n\n"
                                  "Seleccione los tipos de datos que desea transferir.")
                              .arg(totalItems)
                              .arg(TransferStatisticsDialog::formatSize(totalSize))
                              .arg(foundTypes.join(", "));

        QMessageBox::information(this, "Análisis Completado", message);
    } else {
        QMessageBox::warning(this, "Análisis Completado",
                             "No se encontraron datos para transferir en el dispositivo.");
    }

    // Mostrar mensaje en barra de estado
    ui->statusbar->showMessage("Análisis completado. Seleccione datos para transferir.", 5000);
}

void MainWindow::onAnalysisError(const QString &deviceId, const QString &dataType, const QString &errorMessage)
{
    qDebug() << "Error de análisis:" << deviceId << dataType << errorMessage;

    // Cerrar diálogo de progreso si existe
    if (m_analysisProgressDialog) {
        m_analysisProgressDialog->close();
    }

    // Mostrar error al usuario
    QMessageBox::critical(this, "Error de Análisis",
                          QString("Error al analizar %1: %2")
                              .arg(dataType == "all" ? "el dispositivo" : translateDataTypeForUI(dataType))
                              .arg(errorMessage));

    // Volver al estado anterior
    StateManager::instance().setAppState(StateManager::BothDevicesConnected);
}

void MainWindow::onDataSetUpdated(const QString &deviceId, const QString &dataType)
{
    qDebug() << "Data set updated:" << deviceId << dataType;
    updateDataTypesList();
}

void MainWindow::onTransferStarted(qint64 totalSizeEstimate)
{
    qDebug() << "Transfer started. Total size estimate:" << totalSizeEstimate;
    ui->progressFrame->setVisible(true);
    ui->statusbar->showMessage("Transfer started...", 5000);
}

void MainWindow::onTransferProgress(int overallProgress)
{
    qDebug() << "Overall transfer progress:" << overallProgress << "%";
    ui->progressBar->setValue(overallProgress);
}

void MainWindow::onTransferTaskStarted(const QString &dataType, int totalItems)
{
    qDebug() << "Transfer task started for data type:" << dataType << "Total items:" << totalItems;
}

void MainWindow::onTransferTaskProgress(const QString &dataType, int taskProgressPercent,
                                        int processedItems, int totalItems,
                                        qint64 processedSize, qint64 totalSize,
                                        const QString &currentItemName)
{
    qDebug() << "Task progress:" << dataType << taskProgressPercent << "%"
             << "Processed:" << processedItems << "/" << totalItems
             << "Size:" << processedSize << "/" << totalSize
             << "Current item:" << currentItemName;
}

void MainWindow::onTransferTaskCompleted(const QString &dataType, int successCount)
{
    qDebug() << "Transfer task completed for data type:" << dataType << "Success count:" << successCount;
}

void MainWindow::onTransferTaskFailed(const QString &dataType, const QString &errorMessage)
{
    qWarning() << "Transfer task failed:" << dataType << errorMessage;
}

void MainWindow::onTransferCompleted()
{
    qDebug() << "Transfer completed.";
    ui->statusbar->showMessage("Transfer completed.", 5000);
}

void MainWindow::onTransferCancelled()
{
    qDebug() << "Transfer cancelled.";
    ui->statusbar->showMessage("Transfer cancelled.", 5000);
}

void MainWindow::onTransferFailed(const QString &errorMessage)
{
    qWarning() << "Transfer failed:" << errorMessage;
    QMessageBox::critical(this, "Transfer Error", errorMessage);
}

void MainWindow::onStatisticsDialogClosed()
{
    qDebug() << "Statistics dialog closed.";
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    // Ajustar elementos visuales al cambiar el tamaño de la ventana
    if (ui->sourceImageLabel && ui->destImageLabel) {
        QPixmap androidPlaceholder(":/android_icon.svg");
        QPixmap iosPlaceholder(":/ios_icon.svg");

        if (!androidPlaceholder.isNull()) {
            ui->sourceImageLabel->setPixmap(androidPlaceholder.scaled(
                ui->sourceImageLabel->width() - 40,
                ui->sourceImageLabel->height() - 40,
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation));
        }

        if (!iosPlaceholder.isNull()) {
            ui->destImageLabel->setPixmap(iosPlaceholder.scaled(
                ui->destImageLabel->width() - 40,
                ui->destImageLabel->height() - 40,
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation));
        }
    }
}


void MainWindow::setupIcons() {
    // Verificar disponibilidad de iconos
    bool resourcesAvailable = IconProvider::instance().checkResourceAvailability();

    if (!resourcesAvailable) {
        qDebug() << "Advertencia: Algunos recursos de íconos no están disponibles. Usando alternativas.";
    }
}

void MainWindow::updateUIForState(StateManager::AppState state) {
    // Actualizar barra de estado
    ui->statusbar->showMessage(StateManager::instance().getStateDescription(), 0);

    // Configurar controles según el estado
    switch (state) {
    case StateManager::NoDevices:
        ui->dataTypesList->setEnabled(false);
        ui->startTransferButton->setEnabled(false);
        ui->flipButton->setEnabled(false);
        ui->clearBeforeCopyCheckBox->setEnabled(false);
        break;

    case StateManager::SourceConnected:
        ui->dataTypesList->setEnabled(false);
        ui->startTransferButton->setEnabled(false);
        ui->flipButton->setEnabled(true);
        ui->clearBeforeCopyCheckBox->setEnabled(false);
        break;

    case StateManager::SourceConnectedNotAuth:
        ui->dataTypesList->setEnabled(false);
        ui->startTransferButton->setEnabled(false);
        ui->flipButton->setEnabled(true);
        ui->clearBeforeCopyCheckBox->setEnabled(false);

        // Mostrar mensaje de autorización al usuario
        QTimer::singleShot(500, this, [this]() {
            deviceManager->authorizeAndroidDevice(StateManager::instance().getSourceDeviceId());
        });
        break;

    case StateManager::BothDevicesConnected:
        // Iniciar análisis automáticamente si no se ha hecho
        if (!m_analysisSuccessful) {
            QTimer::singleShot(500, this, [this]() {
                onAnalyzeSourceDevice();
            });
        } else {
            ui->dataTypesList->setEnabled(true);
            ui->clearBeforeCopyCheckBox->setEnabled(true);
            updateStartButtonState();
        }
        ui->flipButton->setEnabled(true);
        break;

    case StateManager::AnalysisInProgress:
        ui->dataTypesList->setEnabled(false);
        ui->startTransferButton->setEnabled(false);
        ui->flipButton->setEnabled(false);
        ui->clearBeforeCopyCheckBox->setEnabled(false);
        break;

    case StateManager::ReadyForTransfer:
        ui->dataTypesList->setEnabled(true);
        ui->clearBeforeCopyCheckBox->setEnabled(true);
        ui->flipButton->setEnabled(true);
        updateStartButtonState();
        break;

    case StateManager::TransferInProgress:
        ui->dataTypesList->setEnabled(false);
        ui->startTransferButton->setEnabled(false);
        ui->flipButton->setEnabled(false);
        ui->clearBeforeCopyCheckBox->setEnabled(false);
        break;
    }

    // Actualizar visualización de dispositivos
    updateDeviceUI();
}

void MainWindow::updateDeviceDisplays() {
    // Este método será integrado con updateDeviceUI()
    // Por ahora solo actualiza el estado global
    updateDeviceUI();
}

void MainWindow::configureForCurrentState() {
    // Actualizar la UI basado en el estado actual
    updateUIForState(StateManager::instance().getAppState());
}

void MainWindow::onAnalyzeSourceDevice() {
    QString sourceId = StateManager::instance().getSourceDeviceId();
    if (sourceId.isEmpty() || !StateManager::instance().isSourceAuthorized()) {
        return;
    }

    // Cambiar estado
    StateManager::instance().setAppState(StateManager::AnalysisInProgress);

    // Crear y mostrar diálogo de progreso
    if (m_analysisProgressDialog) {
        delete m_analysisProgressDialog;
    }

    m_analysisProgressDialog = new QProgressDialog("Analizando dispositivo...", "Cancelar", 0, 100, this);
    m_analysisProgressDialog->setWindowTitle("Análisis en progreso");
    m_analysisProgressDialog->setMinimumDuration(500);
    m_analysisProgressDialog->setAutoClose(false);
    m_analysisProgressDialog->setAutoReset(false);

    // Configurar como modal pero permitir interacción con la ventana principal
    m_analysisProgressDialog->setWindowModality(Qt::WindowModal);

    // Conectar señal de cancelación
    connect(m_analysisProgressDialog, &QProgressDialog::canceled, this, [this]() {
        // Implementar cancelación de análisis aquí
        // Por ahora simplemente cerramos el diálogo
        m_analysisProgressDialog->close();
        StateManager::instance().setAppState(StateManager::BothDevicesConnected);
    });

    // Iniciar análisis
    m_analysisSuccessful = false;
    dataAnalyzer->analyzeDevice(sourceId);
}

