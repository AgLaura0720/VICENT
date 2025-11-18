#pragma once

#include <QMainWindow>
#include <QVector>
#include <QString>

class HeaderWidget;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;
class QLineEdit;
class QSerialPort;
class QElapsedTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    // Navegación principal
    void showMenuPage();

    // Inicio de exámenes
    void startWristExam();     // muñeca
    void startElbowExam();     // codo
    void startFullExam();      // examen completo

    // Flujo dentro de un examen
    void handleExamStartStop();   // Iniciar / Detener
    void handleExamNext();        // Siguiente / Finalizar

    // Otros
    void updateClock();
    void toggleFullscreen();
    void exitFullscreen();

    // Página de cédula
    void handleIdContinue();

    // Serial / captura
    void onSerialReadyRead();
    void onAcquisitionTimeout();

private:
    enum ExamType {
        ExamNone,
        ExamWrist,
        ExamElbow,
        ExamFull
    };

    void setStatusText(const QString &text);
    QWidget* createCentralCard();
    QWidget* createIdPage();
    QWidget* createMenuPage();
    QWidget* createExamPage();
    void updateExamUI();  // actualiza textos/botones según ejercicio actual

    QString exerciseLabel(int exNum) const;


    void startAcquisitionForCurrentExercise(int durationSeconds);
    void stopAcquisition(bool fromTimeout);

    // Guardado
    QString saveCurrentExerciseToCsv();           // guarda UN ejercicio -> CSV, devuelve ruta
    void runExcelBuilderForCurrentSession();      // llama al script Python para hacer Lecturas.xlsx

    // ----- UI general -----
    HeaderWidget   *m_header;
    QLabel         *m_statusLeft;
    QLabel         *m_statusRight;
    QStackedWidget *m_pages;
    QTimer         *m_clockTimer;
    bool            m_isFullscreen;

    // ----- Estado del examen -----
    ExamType       m_currentExam;
    QVector<int>   m_examExercises;      // p.ej. {1,2} ó {1,3}
    int            m_currentExerciseIdx; // índice sobre m_examExercises
    bool           m_isAcquiring;        // si se está tomando datos

    // ----- Widgets de la página de examen -----
    QLabel        *m_examTitleLabel;      // "Examen de muñeca — Ejercicio 1"
    QLabel        *m_examStatusLabel;     // "Esperando", "Realizando...", etc.
    QPushButton   *m_examStartStopButton; // "Iniciar" / "Detener"
    QPushButton   *m_examNextButton;      // "Siguiente" / "Finalizar"

    // ----- Página de cédula -----
    QLineEdit     *m_idLineEdit;
    QString        m_patientId;

    // ----- Comunicación con Arduino -----
    QSerialPort   *m_serial;        // puerto serie
    QTimer        *m_acqTimer;      // duración de la toma
    qint64         m_acqDurationMs;
    QElapsedTimer *m_elapsed;       // para timestamps relativos
    QByteArray     m_serialBuffer;  // buffer de líneas parciales

    QVector<double> m_timeSamples;   // timestamps (s)
    QVector<double> m_valueSamples;  // valores de ángulo

    // CSV generados en ESTA sesión (para construir el Excel al final)
    QVector<QString> m_sessionCsvFiles;
};
