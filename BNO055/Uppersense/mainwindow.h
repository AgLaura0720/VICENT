#pragma once

#include <QMainWindow>
#include <QVector>
#include <QStringList>

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

    // Página de cédula
    void handleIdContinue();

    // Serial / adquisición
    void onSerialReadyRead();
    void onAcquisitionTimeout();

    // Otros
    void updateClock();
    void toggleFullscreen();
    void exitFullscreen();

private:
    enum ExamType {
        ExamNone,
        ExamWrist,
        ExamElbow,
        ExamFull
    };

    // Helpers generales
    void setStatusText(const QString &text);
    QWidget* createCentralCard();
    QWidget* createIdPage();
    QWidget* createMenuPage();
    QWidget* createExamPage();
    void updateExamUI();
    QString exerciseLabel(int exNum) const;

    // Adquisición
    void startAcquisitionForCurrentExercise(int durationSeconds);
    void stopAcquisition(bool fromTimeout);
    QString saveCurrentExerciseToCsv();
    void runExcelBuilderForCurrentSession();

    // ----- UI general -----
    HeaderWidget   *m_header;
    QLabel         *m_statusLeft;
    QLabel         *m_statusRight;
    QStackedWidget *m_pages;
    QTimer         *m_clockTimer;
    bool            m_isFullscreen;

    // ----- Estado del examen -----
    ExamType       m_currentExam;
    QVector<int>   m_examExercises;      // p.ej. {1,2,4} ó {1,3,4}
    int            m_currentExerciseIdx; // índice
    bool           m_isAcquiring;        // si se está tomando datos

    // ----- Widgets de la página de examen -----
    QLabel        *m_examTitleLabel;      // título examen + ejercicio
    QLabel        *m_examStatusLabel;     // texto de estado
    QPushButton   *m_examStartStopButton; // "Iniciar"/"Detener"
    QPushButton   *m_examNextButton;      // "Siguiente"/"Finalizar"

    // ----- Página de cédula -----
    QLineEdit     *m_idLineEdit;
    QString        m_patientId;

    // ----- Serial / buffers -----
    QSerialPort   *m_serial;
    QTimer        *m_acqTimer;
    QElapsedTimer *m_elapsed;
    int            m_acqDurationMs;      // <<< AÑADIR ESTA LÍNEA

    QByteArray      m_serialBuffer;
    QVector<double> m_timeSamples;   // timestamp_s
    QVector<double> m_valueSamples;  // ROM o Fuerza
    QVector<double> m_emgSamples;    // EMG del ejercicio
    QStringList     m_sessionCsvFiles;
};

