#include "iconprovider.h"

#include <QApplication>
#include <QPainter>
#include <QDebug>
#include <QSvgRenderer>
#include <QFile>
#include <QFileInfo>
#include <QDir>

IconProvider::IconProvider() {
    // Inicializar colores para los tipos de datos
    m_dataTypeColors["photos"] = QColor("#4CAF50");     // Verde
    m_dataTypeColors["videos"] = QColor("#F44336");     // Rojo
    m_dataTypeColors["contacts"] = QColor("#2196F3");   // Azul
    m_dataTypeColors["messages"] = QColor("#FF9800");   // Naranja
    m_dataTypeColors["calls"] = QColor("#673AB7");      // Violeta
    m_dataTypeColors["calendar"] = QColor("#9C27B0");   // Púrpura
    m_dataTypeColors["music"] = QColor("#3F51B5");      // Índigo
    m_dataTypeColors["notes"] = QColor("#FFC107");      // Ámbar
    m_dataTypeColors["voice_memos"] = QColor("#00BCD4"); // Cian
    m_dataTypeColors["voicemail"] = QColor("#607D8B");  // Azul grisáceo

    // Inicializar íconos fallback del sistema
    m_fallbackIcons["photos"] = QStyle::SP_FileDialogDetailedView;
    m_fallbackIcons["videos"] = QStyle::SP_MediaPlay;
    m_fallbackIcons["contacts"] = QStyle::SP_DialogOkButton;
    m_fallbackIcons["messages"] = QStyle::SP_MessageBoxInformation;
    m_fallbackIcons["calls"] = QStyle::SP_DialogHelpButton;
    m_fallbackIcons["calendar"] = QStyle::SP_FileDialogInfoView;
    m_fallbackIcons["music"] = QStyle::SP_MediaVolume;
    m_fallbackIcons["notes"] = QStyle::SP_FileDialogContentsView;
    m_fallbackIcons["voice_memos"] = QStyle::SP_MediaVolume;
    m_fallbackIcons["voicemail"] = QStyle::SP_MessageBoxQuestion;
}

QIcon IconProvider::getDataTypeIcon(const QString& dataType) {
    // Primero intentamos cargar desde el caché
    QString cacheKey = "datatype_" + dataType;
    QPixmap pixmap;
    if (QPixmapCache::find(cacheKey, &pixmap)) {
        return QIcon(pixmap);
    }

    // Construir la ruta del recurso
    // Convertir '_' a '-' para el caso de voice_memos -> voice-memos-icon.svg
    QString resourcePath = QString(":/") + QString(dataType).replace('_', '-') + "-icon.svg";
    // Texto para alternativa (primera letra en mayúscula)
    QString shortName = dataType.left(1).toUpper();

    // Color por defecto para este tipo de dato (o gris si no está definido)
    QColor color = m_dataTypeColors.value(dataType, QColor("#888888"));

    // Cargar o generar el ícono
    QIcon icon = loadIcon(resourcePath, shortName, color);

    // Guardar en caché si tenemos un ícono válido
    if (!icon.isNull()) {
        pixmap = icon.pixmap(32, 32);
        QPixmapCache::insert(cacheKey, pixmap);
    }

    return icon;
}

QIcon IconProvider::getDeviceIcon(const QString& deviceType) {
    // Primero intentamos cargar desde el caché
    QString cacheKey = "device_" + deviceType;
    QPixmap pixmap;
    if (QPixmapCache::find(cacheKey, &pixmap)) {
        return QIcon(pixmap);
    }

    // Construir la ruta del recurso
    QString resourcePath;
    if (deviceType == "android") {
        resourcePath = ":/android-icon.svg";
    } else if (deviceType == "ios") {
        resourcePath = ":/ios-icon.svg";
    } else {
        resourcePath = ":/";  // Ruta inválida para forzar fallback
    }

    // Texto para alternativa (primera letra en mayúscula)
    QString shortName = deviceType.left(1).toUpper();

    // Color específico para cada plataforma
    QColor color = (deviceType == "android") ? QColor("#3DDC84") : QColor("#007AFF");

    // Cargar o generar el ícono
    QIcon icon = loadIcon(resourcePath, shortName, color);

    // Guardar en caché si tenemos un ícono válido
    if (!icon.isNull()) {
        pixmap = icon.pixmap(64, 64);
        QPixmapCache::insert(cacheKey, pixmap);
    }

    return icon;
}

QIcon IconProvider::loadIcon(const QString& resourcePath, const QString& text, const QColor& color) {
    // Comprobar si el archivo de recurso existe
    QFile file(resourcePath);
    if (file.exists()) {
        // Intentar cargar el SVG
        QSvgRenderer renderer(resourcePath);
        if (renderer.isValid()) {
            // El SVG es válido, crear un pixmap y renderizar el SVG
            QPixmap pixmap(32, 32);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            renderer.render(&painter);
            painter.end();
            return QIcon(pixmap);
        }
    }

    // Si llegamos aquí, el SVG no se pudo cargar o no es válido
    // Intentar usar un ícono del sistema si es un tipo de dato conocido
    QString dataType = resourcePath.section('-', 0, 0).mid(3); // Extraer el tipo de dato de la ruta
    if (m_fallbackIcons.contains(dataType)) {
        QStyle::StandardPixmap standardIcon = m_fallbackIcons[dataType];
        QIcon icon = QApplication::style()->standardIcon(standardIcon);
        if (!icon.isNull()) {
            return icon;
        }
    }

    // Como último recurso, crear un ícono de texto
    return createTextIcon(text, color);
}

QIcon IconProvider::createTextIcon(const QString& text, const QColor& bgColor) {
    // Crear un pixmap para el ícono
    const int size = 32;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Dibujar círculo de fondo
    QRect circleRect(1, 1, size-2, size-2);
    painter.setBrush(bgColor);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(circleRect);

    // Dibujar borde
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(bgColor.darker(120), 1));
    painter.drawEllipse(circleRect);

    // Dibujar texto
    QFont font = painter.font();
    font.setPointSize(size/2);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(circleRect, Qt::AlignCenter, text);

    return QIcon(pixmap);
}

QIcon IconProvider::createColorIcon(const QColor& color, int size) {
    // Crear un pixmap para el ícono
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Dibujar círculo de color
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(2, 2, size-4, size-4);

    // Añadir un borde
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color.darker(120), 1));
    painter.drawEllipse(2, 2, size-4, size-4);

    return QIcon(pixmap);
}

bool IconProvider::checkResourceAvailability() {
    // Lista de recursos a verificar
    QStringList resources = {
        ":/photos-icon.svg",
        ":/videos-icon.svg",
        ":/contacts-icon.svg",
        ":/messages-icon.svg",
        ":/calls-icon.svg",
        ":/calendar-icon.svg",
        ":/music-icon.svg",
        ":/notes-icon.svg",
        ":/voice-memos-icon.svg",
        ":/voicemail-icon.svg",
        ":/android-icon.svg",
        ":/ios-icon.svg"
    };

    // Contar cuántos recursos están disponibles
    int availableCount = 0;
    for (const QString& res : resources) {
        QFile file(res);
        if (file.exists()) {
            // Verificar también si es un SVG válido
            QSvgRenderer renderer(res);
            if (renderer.isValid()) {
                availableCount++;
            } else {
                qDebug() << "El recurso existe pero no es un SVG válido:" << res;
            }
        } else {
            qDebug() << "Recurso no encontrado:" << res;
        }
    }

    // Reportar cuántos recursos están disponibles
    qDebug() << "Recursos de iconos disponibles:" << availableCount << "de" << resources.size();

    // Si al menos el 50% de los recursos están disponibles, consideramos que el sistema funciona
    return (availableCount >= resources.size() / 2);
}
