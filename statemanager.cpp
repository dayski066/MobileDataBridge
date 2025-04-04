#include "statemanager.h"
#include <QDebug>

StateManager::StateManager(QObject* parent)
    : QObject(parent),
    m_currentState(NoDevices),
    m_sourceDeviceId(""),
    m_destDeviceId(""),
    m_sourceAuthorized(false),
    m_destAuthorized(false)
{
    qDebug() << "StateManager inicializado. Estado inicial:" << getStateDescription();
}

void StateManager::setAppState(AppState newState) {
    if (m_currentState != newState) {
        AppState oldState = m_currentState;
        m_currentState = newState;

        qDebug() << "Estado de la aplicación cambiado de"
                 << oldState << "a" << newState
                 << "(" << getStateDescription() << ")";

        emit stateChanged(newState, oldState);
    }
}

void StateManager::setSourceDevice(const QString& deviceId, bool authorized) {
    if (m_sourceDeviceId != deviceId || m_sourceAuthorized != authorized) {
        QString oldId = m_sourceDeviceId;
        bool oldAuth = m_sourceAuthorized;

        m_sourceDeviceId = deviceId;
        m_sourceAuthorized = authorized;

        qDebug() << "Dispositivo origen cambiado de"
                 << (oldId.isEmpty() ? "ninguno" : oldId) << "(" << (oldAuth ? "autorizado" : "no autorizado") << ") a"
                 << (deviceId.isEmpty() ? "ninguno" : deviceId) << "(" << (authorized ? "autorizado" : "no autorizado") << ")";

        emit sourceDeviceChanged(deviceId, authorized);
        updateAppState();
    }
}

void StateManager::setDestDevice(const QString& deviceId, bool authorized) {
    if (m_destDeviceId != deviceId || m_destAuthorized != authorized) {
        QString oldId = m_destDeviceId;
        bool oldAuth = m_destAuthorized;

        m_destDeviceId = deviceId;
        m_destAuthorized = authorized;

        qDebug() << "Dispositivo destino cambiado de"
                 << (oldId.isEmpty() ? "ninguno" : oldId) << "(" << (oldAuth ? "autorizado" : "no autorizado") << ") a"
                 << (deviceId.isEmpty() ? "ninguno" : deviceId) << "(" << (authorized ? "autorizado" : "no autorizado") << ")";

        emit destDeviceChanged(deviceId, authorized);
        updateAppState();
    }
}

void StateManager::clearSourceDevice() {
    if (!m_sourceDeviceId.isEmpty()) {
        QString oldId = m_sourceDeviceId;

        m_sourceDeviceId.clear();
        m_sourceAuthorized = false;

        qDebug() << "Dispositivo origen desconectado:" << oldId;

        emit sourceDeviceChanged("", false);
        updateAppState();
    }
}

void StateManager::clearDestDevice() {
    if (!m_destDeviceId.isEmpty()) {
        QString oldId = m_destDeviceId;

        m_destDeviceId.clear();
        m_destAuthorized = false;

        qDebug() << "Dispositivo destino desconectado:" << oldId;

        emit destDeviceChanged("", false);
        updateAppState();
    }
}

void StateManager::updateAppState() {
    // No cambiar el estado automáticamente si estamos en medio de una operación
    if (m_currentState == AnalysisInProgress || m_currentState == TransferInProgress) {
        qDebug() << "No se actualiza el estado automáticamente porque estamos en medio de una operación:"
                 << getStateDescription();
        return;
    }

    AppState newState;

    // Determinar el nuevo estado basado en los dispositivos conectados
    if (m_sourceDeviceId.isEmpty() && m_destDeviceId.isEmpty()) {
        // No hay dispositivos conectados
        newState = NoDevices;
    } else if (!m_sourceDeviceId.isEmpty() && m_destDeviceId.isEmpty()) {
        // Solo hay dispositivo origen
        newState = m_sourceAuthorized ? SourceConnected : SourceConnectedNotAuth;
    } else if (!m_sourceDeviceId.isEmpty() && !m_destDeviceId.isEmpty()) {
        // Ambos dispositivos conectados
        if (m_sourceAuthorized && m_destAuthorized) {
            // Mantener el estado ReadyForTransfer si ya estábamos en él
            newState = m_currentState == ReadyForTransfer ? ReadyForTransfer : BothDevicesConnected;
        } else {
            // Si alguno no está autorizado, tratarlo como un estado menos avanzado
            newState = !m_sourceAuthorized ? SourceConnectedNotAuth : SourceConnected;
        }
    } else {
        // Solo hay destino sin origen (caso poco común)
        newState = NoDevices; // Tratar como si no hubiera dispositivos
    }

    // Actualizar el estado solo si ha cambiado
    setAppState(newState);
}

QString StateManager::getStateDescription() const {
    switch (m_currentState) {
    case NoDevices:
        return "Conecte un dispositivo para comenzar";
    case SourceConnected:
        return "Dispositivo de origen conectado y autorizado";
    case SourceConnectedNotAuth:
        return "Dispositivo de origen requiere autorización";
    case BothDevicesConnected:
        return "Ambos dispositivos conectados - Listo para analizar";
    case AnalysisInProgress:
        return "Analizando contenido del dispositivo...";
    case ReadyForTransfer:
        return "Análisis completado - Seleccione contenido a transferir";
    case TransferInProgress:
        return "Transferencia en progreso...";
    default:
        return "Estado desconocido";
    }
}
