#pragma once

#include <QMainWindow>
#include <QVector>

class HeaderWidget;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;

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

private:
    enum ExamType {
        ExamNone,
        ExamWrist,
        ExamElbow,
        ExamFull
    };

    void setStatusText(const QString &text);
    QWidget* createCentralCard();
    QWidget* createMenuPage();
    QWidget* createExamPage();
    void updateExamUI();  // actualiza textos/botones según ejercicio actual

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
    int            m_currentExerciseIdx; // índice sobre m_examExercises
    bool           m_isAcquiring;        // si se está tomando datos

    // ----- Widgets de la página de examen -----
    QLabel        *m_examTitleLabel;      // "Examen de muñeca — Ejercicio 1"
    QLabel        *m_examStatusLabel;     // "Esperando", "Realizando...", etc.
    QPushButton   *m_examStartStopButton; // "Iniciar" / "Detener"
    QPushButton   *m_examNextButton;      // "Siguiente" / "Finalizar"
};
