#ifndef ICONPROVIDER_H
#define ICONPROVIDER_H

#include <QIcon>
#include <QColor>
#include <QMap>
#include <QString>
#include <QStyle>
#include <QPixmap>
#include <QPixmapCache>

/**
 * @brief Clase para gestionar todos los iconos de la aplicación de manera centralizada
 *
 * Esta clase implementa el patrón Singleton y proporciona métodos para obtener iconos
 * basados en tipos de datos y dispositivos, con fallbacks en caso de que los recursos
 * no estén disponibles.
 */
class IconProvider {
public:
    /**
     * @brief Obtener la instancia única del proveedor de iconos (Singleton)
     * @return Referencia a la instancia única
     */
    static IconProvider& instance() {
        static IconProvider instance;
        return instance;
    }

    /**
     * @brief Obtener icono para un tipo de dato específico
     * @param dataType Tipo de dato ("photos", "videos", etc.)
     * @return QIcon correspondiente al tipo de dato
     */
    QIcon getDataTypeIcon(const QString& dataType);

    /**
     * @brief Obtener icono para un tipo de dispositivo
     * @param deviceType Tipo de dispositivo ("android", "ios")
     * @return QIcon correspondiente al tipo de dispositivo
     */
    QIcon getDeviceIcon(const QString& deviceType);

    /**
     * @brief Verificar si los recursos de iconos están disponibles
     * @return true si al menos la mitad de los recursos están disponibles
     */
    bool checkResourceAvailability();

    /**
     * @brief Crear un icono con texto sobre un fondo de color
     * @param text Texto a mostrar en el icono
     * @param bgColor Color de fondo del icono
     * @return QIcon generado con el texto y color especificados
     */
    QIcon createTextIcon(const QString& text, const QColor& bgColor);

private:
    /**
     * @brief Constructor privado (patrón Singleton)
     */
    IconProvider();

    /**
     * @brief Destructor
     */
    ~IconProvider() = default;

    /**
     * @brief Constructor de copia (eliminado para Singleton)
     */
    IconProvider(const IconProvider&) = delete;

    /**
     * @brief Operador de asignación (eliminado para Singleton)
     */
    IconProvider& operator=(const IconProvider&) = delete;

    /**
     * @brief Mapa de colores por tipo de dato
     */
    QMap<QString, QColor> m_dataTypeColors;

    /**
     * @brief Mapa de íconos de sistema por tipo de dato para fallback
     */
    QMap<QString, QStyle::StandardPixmap> m_fallbackIcons;

    /**
     * @brief Método para cargar o generar ícono
     * @param resourcePath Ruta al recurso SVG
     * @param text Texto alternativo si el recurso no está disponible
     * @param color Color para el icono alternativo
     * @return QIcon cargado o generado
     */
    QIcon loadIcon(const QString& resourcePath, const QString& text, const QColor& color);

    /**
     * @brief Crear un icono de color liso (sin texto)
     * @param color Color del icono
     * @param size Tamaño del icono en píxeles
     * @return QIcon con el color especificado
     */
    QIcon createColorIcon(const QColor& color, int size = 16);
};

#endif // ICONPROVIDER_H
