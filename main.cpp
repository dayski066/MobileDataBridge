#include "mainwindow.h"
#include <QApplication>
#include <QDebug>

void myMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    FILE *output = stderr;
    switch (type) {
    case QtDebugMsg:
        fprintf(output, "Debug: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        break;
    case QtInfoMsg:
        fprintf(output, "Info: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        break;
    case QtWarningMsg:
        fprintf(output, "Warning: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        break;
    case QtCriticalMsg:
        fprintf(output, "Critical: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        break;
    case QtFatalMsg:
        fprintf(output, "Fatal: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        fflush(output);
        abort();
    }
    fflush(output);
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(myMessageHandler);
    qDebug() << "Iniciando la aplicación con" << argc << "argumentos.";
    QApplication a(argc, argv);
    if (!a.arguments().isEmpty()) {
        qDebug() << "Argumentos recibidos:" << a.arguments();
    } else {
        qDebug() << "No se recibieron argumentos.";
    }
    qDebug() << "Creando la ventana principal...";
    MainWindow w;
    if (w.isVisible()) {
        qWarning() << "La ventana principal ya está visible antes de show(), esto no debería ocurrir.";
    }
    qDebug() << "Mostrando la ventana principal...";
    w.show();
    qDebug() << "Iniciando el bucle de eventos...";
    int result = a.exec();
    qDebug() << "Aplicación finalizada con código de salida:" << result;
    return result;
}
