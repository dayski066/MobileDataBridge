#ifndef STATEMANAGER_H
#define STATEMANAGER_H

#include <QObject>
#include <QString>

/**
 * @brief Clase para gestionar el estado global de la aplicación
 *
 * Esta clase implementa el patrón Singleton y maneja el estado de la aplicación,
 * incluyendo los dispositivos conectados, su autorización, y el estado del proceso
 * (análisis, transferencia, etc.)
 */
class StateManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Estados posibles de la aplicación
     */
    enum AppState {
        NoDevices,              // No hay dispositivos conectados
        SourceConnected,        // Solo hay dispositivo origen
        SourceConnectedNotAuth, // Hay dispositivo origen pero no autorizado
        BothDevicesConnected,   // Ambos dispositivos conectados y autorizados
        AnalysisInProgress,     // Análisis en progreso
        ReadyForTransfer,       // Listo para transferir (análisis completado)
        TransferInProgress      // Transferencia en progreso
    };
    Q_ENUM(AppState)

    /**
     * @brief Obtener la instancia única del gestor de estado (Singleton)
     * @return Referencia a la instancia única
     */
    static StateManager& instance() {
        static StateManager instance;
        return instance;
    }

    /**
     * @brief Establecer el estado de la aplicación
     * @param newState Nuevo estado
     */
    void setAppState(AppState newState);

    /**
     * @brief Obtener el estado actual de la aplicación
     * @return Estado actual
     */
    AppState getAppState() const { return m_currentState; }

    /**
     * @brief Obtener descripción textual del estado actual
     * @return Descripción del estado para mostrar al usuario
     */
    QString getStateDescription() const;

    /**
     * @brief Establecer información del dispositivo origen
     * @param deviceId ID del dispositivo
     * @param authorized Si está autorizado
     */
    void setSourceDevice(const QString& deviceId, bool authorized);

    /**
     * @brief Establecer información del dispositivo destino
     * @param deviceId ID del dispositivo
     * @param authorized Si está autorizado
     */
    void setDestDevice(const QString& deviceId, bool authorized);

    /**
     * @brief Limpiar información del dispositivo origen (en desconexión)
     */
    void clearSourceDevice();

    /**
     * @brief Limpiar información del dispositivo destino (en desconexión)
     */
    void clearDestDevice();

    /**
     * @brief Obtener ID del dispositivo origen
     * @return ID del dispositivo origen
     */
    QString getSourceDeviceId() const { return m_sourceDeviceId; }

    /**
     * @brief Obtener ID del dispositivo destino
     * @return ID del dispositivo destino
     */
    QString getDestDeviceId() const { return m_destDeviceId; }

    /**
     * @brief Verificar si el dispositivo origen está autorizado
     * @return true si está autorizado
     */
    bool isSourceAuthorized() const { return m_sourceAuthorized; }

    /**
     * @brief Verificar si el dispositivo destino está autorizado
     * @return true si está autorizado
     */
    bool isDestAuthorized() const { return m_destAuthorized; }

signals:
    /**
     * @brief Señal emitida cuando cambia el estado de la aplicación
     * @param newState Nuevo estado
     * @param oldState Estado anterior
     */
    void stateChanged(AppState newState, AppState oldState);

    /**
     * @brief Señal emitida cuando cambia el dispositivo origen
     * @param deviceId ID del dispositivo (vacío si se desconectó)
     * @param authorized Si está autorizado
     */
    void sourceDeviceChanged(const QString& deviceId, bool authorized);

    /**
     * @brief Señal emitida cuando cambia el dispositivo destino
     * @param deviceId ID del dispositivo (vacío si se desconectó)
     * @param authorized Si está autorizado
     */
    void destDeviceChanged(const QString& deviceId, bool authorized);

private:
    /**
     * @brief Constructor privado (patrón Singleton)
     * @param parent Objeto padre (por defecto nullptr)
     */
    StateManager(QObject* parent = nullptr);

    /**
     * @brief Destructor
     */
    ~StateManager() = default;

    /**
     * @brief Constructor de copia (eliminado para Singleton)
     */
    StateManager(const StateManager&) = delete;

    /**
     * @brief Operador de asignación (eliminado para Singleton)
     */
    StateManager& operator=(const StateManager&) = delete;

    /**
     * @brief Estado actual de la aplicación
     */
    AppState m_currentState;

    /**
     * @brief ID del dispositivo origen
     */
    QString m_sourceDeviceId;

    /**
     * @brief ID del dispositivo destino
     */
    QString m_destDeviceId;

    /**
     * @brief Si el dispositivo origen está autorizado
     */
    bool m_sourceAuthorized;

    /**
     * @brief Si el dispositivo destino está autorizado
     */
    bool m_destAuthorized;

    /**
     * @brief Recalcular el estado automáticamente basado en los dispositivos conectados
     */
    void updateAppState();
};

#endif // STATEMANAGER_H
