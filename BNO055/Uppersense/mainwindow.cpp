#include "mainwindow.h"
#include "headerwidget.h"

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QDateTime>
#include <QMessageBox>
#include <QShortcut>
#include <QIcon>
#include <QPixmap>
#include <QInputDialog>
#include <QProcess>
#include <QDir>

// Paleta similar a la de Python
static const char *CLR_PRIMARY        = "#1565c0";
static const char *CLR_PRIMARY_HOVER  = "#0d47a1";
static const char *CLR_PRIMARY_MUTED  = "#90caf9";

static const char *CLR_BG             = "#eaeaea";
static const char *CLR_SURFACE        = "#f5f7fb";
static const char *CLR_SURFACE_SHADOW = "#d0d4dc";
static const char *CLR_DIVIDER        = "#d7dbe3";
static const char *CLR_FOOTER         = "#eef1f6";

static const char *CLR_TEXT           = "#1f2937";
static const char *CLR_TEXT_MUTED     = "#374151";

// ---------------- variables de fichero para el bridge Python ----------------
// no cambiamos el header: guardamos el proceso y buffer a nivel de cpp
static QProcess *g_pythonProc = nullptr;
static QByteArray g_pythonBuffer;
static QString g_patientId_global;

// Helper para enviar comando al script Python
static void sendToPython(const QString &line) {
    if (!g_pythonProc) return;
    if (g_pythonProc->state() == QProcess::NotRunning) return;
    QByteArray out = (line + "\n").toUtf8();
    g_pythonProc->write(out);
    g_pythonProc->waitForBytesWritten(200);
}

// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    m_header(nullptr),
    m_statusLeft(nullptr),
    m_statusRight(nullptr),
    m_pages(nullptr),
    m_clockTimer(nullptr),
    m_isFullscreen(false),
    m_currentExam(ExamNone),
    m_currentExerciseIdx(-1),
    m_isAcquiring(false),
    m_examTitleLabel(nullptr),
    m_examStatusLabel(nullptr),
    m_examStartStopButton(nullptr),
    m_examNextButton(nullptr)
{
    setWindowTitle(QString::fromUtf8("UpperSense — Panel de Control"));
    resize(1280, 720);

    // Ícono desde recursos
    setWindowIcon(QIcon(":/img/loguito.ico"));

    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    central->setStyleSheet(QString("background-color:%1;").arg(CLR_BG));

    QVBoxLayout *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ===== Header =====
    m_header = new HeaderWidget(this);
    QPixmap logo(":/img/loguito.png");
    m_header->setLogo(logo);
    rootLayout->addWidget(m_header);

    QFrame *divider = new QFrame(this);
    divider->setFixedHeight(3);
    divider->setStyleSheet(QString("background-color:%1;").arg(CLR_DIVIDER));
    rootLayout->addWidget(divider);

    // ===== Zona central (card + sombra) =====
    QWidget *centerWrapper = new QWidget(this);
    QVBoxLayout *centerLayout = new QVBoxLayout(centerWrapper);
    centerLayout->setContentsMargins(80, 40, 80, 40);
    centerLayout->setSpacing(0);
    rootLayout->addWidget(centerWrapper, 1);

    // shadowFrame eliminado intencionalmente (bloque gris detrás del card)
    centerLayout->setContentsMargins(40, 20, 40, 20);

    QWidget *card = createCentralCard();
    centerLayout->addWidget(card, 0, Qt::AlignHCenter);

    // ===== Footer =====
    QFrame *footerSep = new QFrame(this);
    footerSep->setFixedHeight(2);
    footerSep->setStyleSheet(QString("background-color:%1;").arg(CLR_DIVIDER));
    rootLayout->addWidget(footerSep);

    QFrame *footer = new QFrame(this);
    footer->setObjectName("footerFrame");
    footer->setStyleSheet(
        QString("QFrame#footerFrame {"
                " background-color:%1;"
                "}").arg(CLR_FOOTER));
    footer->setFixedHeight(40);

    QHBoxLayout *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 6, 16, 6);

    m_statusLeft = new QLabel(QString::fromUtf8("Estado: Menú principal"), footer);
    QFont fstatus("Segoe UI", 10);
    m_statusLeft->setFont(fstatus);
    m_statusLeft->setStyleSheet(QString("color:%1;").arg(CLR_TEXT_MUTED));

    m_statusRight = new QLabel(footer);
    m_statusRight->setFont(fstatus);
    m_statusRight->setStyleSheet(QString("color:%1;").arg(CLR_TEXT_MUTED));

    footerLayout->addWidget(m_statusLeft);
    footerLayout->addStretch();
    footerLayout->addWidget(m_statusRight);

    rootLayout->addWidget(footer);

    // ===== Reloj =====
    m_clockTimer = new QTimer(this);
    connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::updateClock);
    m_clockTimer->start(1000);
    updateClock();

    // ===== Atajos F11 / Esc =====
    QShortcut *shortcutFull = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(shortcutFull, &QShortcut::activated, this, &MainWindow::toggleFullscreen);

    QShortcut *shortcutEsc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(shortcutEsc, &QShortcut::activated, this, &MainWindow::exitFullscreen);

    // Iniciar el bridge Python (si existe)
    // Ruta por defecto: ./scripts/arduino_controller.py dentro del directorio de la app
    QString scriptPath = QCoreApplication::applicationDirPath() + "/principal.py";
    QString pythonExe = "python"; // si necesitas ruta absoluta, cámbiala aquí

    // Intentamos arrancar el proceso de Python; si no existe, avisamos pero la app sigue.
    if (!g_pythonProc) {
        g_pythonProc = new QProcess(this);
        g_pythonProc->setProcessChannelMode(QProcess::MergedChannels);
        connect(g_pythonProc, &QProcess::readyReadStandardOutput, this, [this]() {
            // lambda que procesa líneas desde stdout del script Python
            g_pythonBuffer.append(g_pythonProc->readAllStandardOutput());
            int idx;
            while ((idx = g_pythonBuffer.indexOf('\n')) != -1) {
                QByteArray line = g_pythonBuffer.left(idx);
                g_pythonBuffer.remove(0, idx + 1);
                QString s = QString::fromUtf8(line).trimmed();
                qDebug() << "PY_OUT:" << s;

                // Manejo de mensajes conocidos
                if (s.startsWith("STATUS:")) {
                    QString rest = s.section(':', 1);
                    if (rest.startsWith("READY")) {
                        m_statusLeft->setText("Estado: Backend Python listo");
                    } else if (rest.startsWith("CAPTURE_STARTED")) {
                        QString col = rest.section(':', 1);
                        m_examStatusLabel->setText(QString::fromUtf8("Capturando: %1").arg(col));
                    } else if (rest.startsWith("CAPTURE_END")) {
                        QString col = rest.section(':', 1);
                        m_examStatusLabel->setText(QString::fromUtf8("Toma finalizada: %1").arg(col));
                        // habilitar botón Siguiente
                        m_isAcquiring = false;
                        m_examStartStopButton->setText(QString::fromUtf8("Iniciar"));
                        bool isLast = (m_currentExerciseIdx == m_examExercises.size() - 1);
                        m_examNextButton->setText(isLast ? QString::fromUtf8("Finalizar") : QString::fromUtf8("Siguiente"));
                        m_examNextButton->setEnabled(true);
                    }
                } else if (s.startsWith("DATA:")) {
                    // DATA:<colname>,<timestamp_s>,<value>
                    QString payload = s.mid(QString("DATA:").length());
                    QStringList parts = payload.split(',');
                    if (parts.size() >= 3) {
                        QString col = parts[0];
                        double ts = parts[1].toDouble();
                        double val = parts[2].toDouble();
                        // muestra el último valor recibido
                        m_examStatusLabel->setText(QString::fromUtf8("%1  —  %2 s  —  %3").arg(col).arg(QString::number(ts,'f',2)).arg(QString::number(val,'f',2)));
                    }
                } else if (s.startsWith("SAVED:")) {
                    QString path = s.section(':',1);
                    QMessageBox::information(this, "Guardado", QString("Archivo guardado:\n%1").arg(path));
                } else if (s.startsWith("ERROR:")) {
                    qDebug() << "PY ERROR:" << s;
                    // opcional: mostrar al usuario
                } else if (s.startsWith("HWMSG:")) {
                    // mensajes del firmware Arduino reenviados por el Python
                    QString hw = s.mid(QString("HWMSG:").length());
                    qDebug() << "HWMSG:" << hw;
                } else {
                    // otros mensajes, logs, etc.
                    qDebug() << "PY MSG:" << s;
                }
            }
        });
        connect(g_pythonProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [](int exitCode, QProcess::ExitStatus) {
                    qDebug() << "Python bridge finalizado con código" << exitCode;
                });

        g_pythonProc->start(pythonExe, QStringList() << scriptPath);
        if (!g_pythonProc->waitForStarted(1500)) {
            qWarning() << "No se pudo iniciar el script Python en:" << scriptPath;
            // no abortamos la app; el usuario puede usar QSerialPort directo o añadir el script
            QMessageBox::warning(this, "Aviso", "No se pudo iniciar el script Python (arduino_controller.py).\nRevisa que exista en la carpeta ./scripts y que 'python' esté en PATH.");
            // limpiamos el proceso para que no quede en estado inesperado
            g_pythonProc->kill();
            delete g_pythonProc;
            g_pythonProc = nullptr;
        } else {
            qDebug() << "Python bridge iniciado.";
        }
    }

    // Arranca mostrando el menú
    showMaximized();
    showMenuPage();
}


// ================= CARD CENTRAL =================

QWidget* MainWindow::createCentralCard()
{
    QFrame *card = new QFrame(this);
    card->setObjectName("cardFrame");
    card->setStyleSheet(
        QString("QFrame#cardFrame {"
                " background-color:%1;"
                " border-radius:20px;"
                "}").arg(CLR_SURFACE));

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(50, 40, 50, 40);
    cardLayout->setSpacing(20);

    // Subtítulo fijo
    QLabel *subtitle = new QLabel(QString::fromUtf8("Opciones de prueba"), card);
    QFont fsub("Segoe UI", 16, QFont::Bold);
    subtitle->setFont(fsub);
    subtitle->setStyleSheet(QString("color:%1;").arg(CLR_TEXT));
    subtitle->setAlignment(Qt::AlignHCenter);
    cardLayout->addWidget(subtitle);

    // Páginas (menú / examen)
    m_pages = new QStackedWidget(card);
    m_pages->addWidget(createMenuPage());  // índice 0
    m_pages->addWidget(createExamPage());  // índice 1
    cardLayout->addWidget(m_pages);

    return card;
}

QWidget* MainWindow::createMenuPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *v = new QVBoxLayout(page);
    v->setAlignment(Qt::AlignCenter);

    QWidget *btnRow = new QWidget(page);
    QHBoxLayout *rowLayout = new QHBoxLayout(btnRow);
    rowLayout->setSpacing(30);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    auto makeButton = [&](const QString &text) -> QPushButton* {
        QPushButton *b = new QPushButton(text, btnRow);
        QString style = QString(
                            "QPushButton {"
                            " background-color:%1;"
                            " color:white;"
                            " border:none;"
                            " border-radius:18px;"
                            " padding:14px 32px;"
                            " font-family:'Segoe UI';"
                            " font-size:18px;"
                            " font-weight:bold;"
                            "}"
                            "QPushButton:hover {"
                            " background-color:%2;"
                            "}"
                            ).arg(CLR_PRIMARY, CLR_PRIMARY_HOVER);
        b->setStyleSheet(style);
        return b;
    };

    QPushButton *btnWrist = makeButton(QString::fromUtf8("Examen de muñeca"));
    QPushButton *btnElbow = makeButton(QString::fromUtf8("Examen de codo"));
    QPushButton *btnFull  = makeButton(QString::fromUtf8("Examen completo"));

    rowLayout->addWidget(btnWrist);
    rowLayout->addWidget(btnElbow);
    rowLayout->addWidget(btnFull);

    v->addWidget(btnRow, 0, Qt::AlignCenter);

    connect(btnWrist, &QPushButton::clicked, this, &MainWindow::startWristExam);
    connect(btnElbow, &QPushButton::clicked, this, &MainWindow::startElbowExam);
    connect(btnFull,  &QPushButton::clicked, this, &MainWindow::startFullExam);

    return page;
}

QWidget* MainWindow::createExamPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *v = new QVBoxLayout(page);
    v->setSpacing(16);

    // Título del examen + ejercicio
    m_examTitleLabel = new QLabel(QString::fromUtf8("Examen — Ejercicio"), page);
    QFont ftitle("Segoe UI", 16, QFont::Bold);
    m_examTitleLabel->setFont(ftitle);
    m_examTitleLabel->setStyleSheet(QString("color:%1;").arg(CLR_TEXT));
    m_examTitleLabel->setAlignment(Qt::AlignHCenter);
    v->addWidget(m_examTitleLabel);

    // Texto de estado centrado
    m_examStatusLabel = new QLabel(QString::fromUtf8("Esperando"), page);
    QFont fstatus("Segoe UI", 18, QFont::Bold);
    m_examStatusLabel->setFont(fstatus);
    m_examStatusLabel->setStyleSheet(QString("color:%1;").arg(CLR_TEXT));
    m_examStatusLabel->setAlignment(Qt::AlignCenter);
    m_examStatusLabel->setMinimumHeight(80);
    v->addWidget(m_examStatusLabel, 1);

    // Botón Iniciar/Detener centrado
    m_examStartStopButton = new QPushButton(QString::fromUtf8("Iniciar"), page);
    QString mainBtnStyle = QString(
                               "QPushButton {"
                               " background-color:%1;"
                               " color:white;"
                               " border:none;"
                               " border-radius:16px;"
                               " padding:10px 28px;"
                               " font-family:'Segoe UI';"
                               " font-size:16px;"
                               " font-weight:bold;"
                               "}"
                               "QPushButton:hover {"
                               " background-color:%2;"
                               "}"
                               ).arg(CLR_PRIMARY, CLR_PRIMARY_HOVER);
    m_examStartStopButton->setStyleSheet(mainBtnStyle);
    v->addWidget(m_examStartStopButton, 0, Qt::AlignHCenter);

    connect(m_examStartStopButton, &QPushButton::clicked,
            this, &MainWindow::handleExamStartStop);

    // Barra inferior con botón Siguiente/Finalizar
    QWidget *bottomBar = new QWidget(page);
    QHBoxLayout *bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(0, 20, 0, 0);

    m_examNextButton = new QPushButton(QString::fromUtf8("Siguiente"), bottomBar);
    QString nextBtnStyle = QString(
                               "QPushButton {"
                               " background-color:%1;"
                               " color:white;"
                               " border:none;"
                               " border-radius:14px;"
                               " padding:6px 20px;"
                               " font-family:'Segoe UI';"
                               " font-size:13px;"
                               " font-weight:bold;"
                               "}"
                               "QPushButton:disabled {"
                               " background-color:#b0b8c5;"
                               "}"
                               "QPushButton:hover:!disabled {"
                               " background-color:%2;"
                               "}"
                               ).arg(CLR_PRIMARY, CLR_PRIMARY_HOVER);
    m_examNextButton->setStyleSheet(nextBtnStyle);
    m_examNextButton->setEnabled(false);

    bottomLayout->addWidget(m_examNextButton, 0, Qt::AlignLeft);
    bottomLayout->addStretch();

    v->addWidget(bottomBar);

    connect(m_examNextButton, &QPushButton::clicked,
            this, &MainWindow::handleExamNext);

    return page;
}


// ================= LÓGICA DE EXÁMENES =================

void MainWindow::setStatusText(const QString &text)
{
    m_statusLeft->setText(QString::fromUtf8("Estado: ") + text);
}

void MainWindow::showMenuPage()
{
    if (m_pages)
        m_pages->setCurrentIndex(0);

    m_currentExam = ExamNone;
    m_examExercises.clear();
    m_currentExerciseIdx = -1;
    m_isAcquiring = false;

    setStatusText(QString::fromUtf8("Menú principal"));
}

void MainWindow::startWristExam()
{
    m_currentExam = ExamWrist;
    m_examExercises = QVector<int>{1, 3, 4};
    m_currentExerciseIdx = 0;
    m_isAcquiring = false;

    updateExamUI();
    if (m_pages)
        m_pages->setCurrentIndex(1);

    setStatusText(QString::fromUtf8("Examen de muñeca"));
}

void MainWindow::startElbowExam()
{
    m_currentExam = ExamElbow;
    m_examExercises = QVector<int>{1, 2, 4};
    m_currentExerciseIdx = 0;
    m_isAcquiring = false;

    updateExamUI();
    if (m_pages)
        m_pages->setCurrentIndex(1);

    setStatusText(QString::fromUtf8("Examen de codo"));
}

void MainWindow::startFullExam()
{
    m_currentExam = ExamFull;
    m_examExercises = QVector<int>{1, 2, 3, 4};
    m_currentExerciseIdx = 0;
    m_isAcquiring = false;

    updateExamUI();
    if (m_pages)
        m_pages->setCurrentIndex(1);

    setStatusText(QString::fromUtf8("Examen completo"));
}

void MainWindow::updateExamUI()
{
    if (!m_examTitleLabel || !m_examStatusLabel || !m_examStartStopButton || !m_examNextButton)
        return;

    QString examName;
    switch (m_currentExam) {
    case ExamWrist: examName = QString::fromUtf8("Examen de muñeca"); break;
    case ExamElbow: examName = QString::fromUtf8("Examen de codo"); break;
    case ExamFull:  examName = QString::fromUtf8("Examen completo"); break;
    default:        examName = QString::fromUtf8("Examen"); break;
    }

    int exNum = 0;
    if (m_currentExerciseIdx >= 0 && m_currentExerciseIdx < m_examExercises.size())
        exNum = m_examExercises[m_currentExerciseIdx];

    QString title = examName;
    if (exNum > 0)
        title += QString::fromUtf8(" — Ejercicio %1").arg(exNum);

    m_examTitleLabel->setText(title);
    m_examStatusLabel->setText(QString::fromUtf8("Esperando"));

    m_examStartStopButton->setText(QString::fromUtf8("Iniciar"));
    m_examStartStopButton->setEnabled(true);

    m_examNextButton->setEnabled(false);
    m_examNextButton->setText(QString::fromUtf8("Siguiente"));

    m_isAcquiring = false;
}

void MainWindow::handleExamStartStop()
{
    if (m_currentExam == ExamNone || m_examExercises.isEmpty())
        return;

    int exNum = 0;
    if (m_currentExerciseIdx >= 0 && m_currentExerciseIdx < m_examExercises.size())
        exNum = m_examExercises[m_currentExerciseIdx];

    // Mapeo simple de número de ejercicio a nombre de columna (coincide con tu script Python)
    auto colNameForEx = [](int ex)->QString {
        switch (ex) {
        case 1: return QString::fromUtf8("ROM Flexión/Extensión_°");
        case 2: return QString::fromUtf8("ROM Desviación Ulnar/Radial_°");
        case 3: return QString::fromUtf8("ROM Pronosupinación_°");
        case 4: return QString::fromUtf8("Fuerza de Prensión_Kg");
        default: return QString::fromUtf8("Valor");
        }
    };

    if (!m_isAcquiring) {
        // Pedimos cédula si no está definida
        if (g_patientId_global.isEmpty()) {
            bool ok;
            QString ced = QInputDialog::getText(this, tr("Paciente"),
                                                tr("Cédula (solo números):"), QLineEdit::Normal,
                                                QString(), &ok);
            if (!ok || ced.isEmpty()) {
                QMessageBox::warning(this, tr("Error"), tr("Cédula no válida. Cancelando."));
                return;
            }
            // enviar PATIENT al backend Python
            g_patientId_global = ced;
            sendToPython(QString("PATIENT:%1").arg(g_patientId_global));
        }

        // Pedimos duración
        bool okDur;
        int dur = QInputDialog::getInt(this, tr("Duración"),
                                       tr("Duración en segundos:"), 5, 1, 3600, 1, &okDur);
        if (!okDur) return;

        // Preparar y enviar comando START al Python bridge
        QString cmd = QString::number(exNum);
        QString colname = colNameForEx(exNum);
        QString startLine = QString("START:%1:%2:%3").arg(cmd).arg(colname).arg(dur);
        sendToPython(startLine);

        // Actualizar UI
        m_isAcquiring = true;
        m_examStatusLabel->setText(QString::fromUtf8("Realizando toma de datos"));
        m_examStartStopButton->setText(QString::fromUtf8("Detener"));
        m_examNextButton->setEnabled(false);
    } else {
        // Detener: enviar 'e' al backend Python (que reenviará al Arduino)
        sendToPython("e");
        // Ajustamos UI; el Python enviará STATUS:CAPTURE_END cuando termine realmente
        m_isAcquiring = false;
        m_examStatusLabel->setText(QString::fromUtf8("Toma de datos detenida"));
        m_examStartStopButton->setText(QString::fromUtf8("Iniciar"));
        bool isLast = (m_currentExerciseIdx == m_examExercises.size() - 1);
        m_examNextButton->setText(isLast ? QString::fromUtf8("Finalizar") : QString::fromUtf8("Siguiente"));
        m_examNextButton->setEnabled(true);
    }
}

void MainWindow::handleExamNext()
{
    if (m_currentExam == ExamNone || m_examExercises.isEmpty())
        return;

    bool isLast = (m_currentExerciseIdx == m_examExercises.size() - 1);
    if (isLast) {
        // Pedimos al backend que guarde la sesión acumulada en Excel
        sendToPython("SAVE");
        // Limpiamos paciente (opcional)
        g_patientId_global.clear();
        // Termina el examen, volvemos al menú principal
        showMenuPage();
        return;
    }

    // Pasar al siguiente ejercicio
    ++m_currentExerciseIdx;
    updateExamUI();
}


// ================= RELOJ Y PANTALLA COMPLETA =================

void MainWindow::updateClock()
{
    QString hora = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_statusRight->setText(
        hora + QString::fromUtf8("   ·   F11: Pantalla completa   |   Esc: salir"));
}

void MainWindow::toggleFullscreen()
{
    m_isFullscreen = !m_isFullscreen;
    if (m_isFullscreen)
        showFullScreen();
    else
        showMaximized();
}

void MainWindow::exitFullscreen()
{
    m_isFullscreen = false;
    showMaximized();
}
