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
#include <QDir>
#include <QLineEdit>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDebug>

#include <QSerialPort>
#include <QSerialPortInfo>
#include <QElapsedTimer>

#include <limits>

// Paleta visual
static const char *CLR_PRIMARY        = "#1565c0";
static const char *CLR_PRIMARY_HOVER  = "#0d47a1";

static const char *CLR_BG             = "#eaeaea";
static const char *CLR_SURFACE        = "#f5f7fb";
static const char *CLR_DIVIDER        = "#d7dbe3";
static const char *CLR_FOOTER         = "#eef1f6";

static const char *CLR_TEXT           = "#1f2937";
static const char *CLR_TEXT_MUTED     = "#374151";

// ---------------------------------------------------------------------------
//              CONSTRUCTOR
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
    m_examNextButton(nullptr),
    m_idLineEdit(nullptr),
    m_patientId(),
    m_serial(nullptr),
    m_acqTimer(nullptr),
    m_elapsed(nullptr)
{
    setWindowTitle(QString::fromUtf8("UpperSense — Panel de Control"));
    resize(1280, 720);
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

    // ===== Zona central =====
    QWidget *centerWrapper = new QWidget(this);
    QVBoxLayout *centerLayout = new QVBoxLayout(centerWrapper);
    centerLayout->setContentsMargins(40, 20, 40, 20);
    centerLayout->setSpacing(0);
    rootLayout->addWidget(centerWrapper, 1);

    QWidget *card = createCentralCard();
    centerLayout->addWidget(card, 0, Qt::AlignHCenter);

    // ===== Footer =====
    QFrame *footerSep = new QFrame(this);
    footerSep->setFixedHeight(2);
    footerSep->setStyleSheet(QString("background-color:%1;").arg(CLR_DIVIDER));
    rootLayout->addWidget(footerSep);

    QFrame *footer = new QFrame(this);
    footer->setObjectName("footerFrame");
    footer->setStyleSheet(QString("QFrame#footerFrame { background-color:%1; }").arg(CLR_FOOTER));
    footer->setFixedHeight(40);

    QHBoxLayout *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 6, 16, 6);

    m_statusLeft = new QLabel(QString::fromUtf8("Estado: Inicio"), footer);
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

    // ===== Atajos F11 / ESC =====
    connect(new QShortcut(QKeySequence(Qt::Key_F11), this),
            &QShortcut::activated, this, &MainWindow::toggleFullscreen);
    connect(new QShortcut(QKeySequence(Qt::Key_Escape), this),
            &QShortcut::activated, this, &MainWindow::exitFullscreen);

    // ===== Serial / captura =====
    m_serial   = new QSerialPort(this);
    m_acqTimer = new QTimer(this);
    m_elapsed  = new QElapsedTimer();

    connect(m_serial, &QSerialPort::readyRead,
            this, &MainWindow::onSerialReadyRead);
    connect(m_acqTimer, &QTimer::timeout,
            this, &MainWindow::onAcquisitionTimeout);

    // Mostrar página de cédula al inicio
    if (m_pages)
        m_pages->setCurrentIndex(0);

    setStatusText(QString::fromUtf8("Ingrese la cédula del paciente"));
    showMaximized();
}

// ---------------------------------------------------------------------------
//                  CARD CENTRAL + PÁGINAS
// ---------------------------------------------------------------------------

QWidget* MainWindow::createCentralCard()
{
    QFrame *card = new QFrame(this);
    card->setObjectName("cardFrame");
    card->setStyleSheet(QString(
                            "QFrame#cardFrame {"
                            " background-color:%1;"
                            " border-radius:20px;"
                            "}").arg(CLR_SURFACE));

    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(50, 40, 50, 40);
    cardLayout->setSpacing(20);

    QLabel *subtitle = new QLabel(QString::fromUtf8("Pruebas Funcionales"), card);
    subtitle->setFont(QFont("Segoe UI", 16, QFont::Bold));
    subtitle->setStyleSheet(QString("color:%1;").arg(CLR_TEXT));
    subtitle->setAlignment(Qt::AlignHCenter);
    cardLayout->addWidget(subtitle);

    m_pages = new QStackedWidget(card);
    m_pages->addWidget(createIdPage());    // 0
    m_pages->addWidget(createMenuPage());  // 1
    m_pages->addWidget(createExamPage());  // 2

    cardLayout->addWidget(m_pages);

    return card;
}

// ------------------ Página de cédula ------------------

QWidget* MainWindow::createIdPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *v = new QVBoxLayout(page);
    v->setAlignment(Qt::AlignCenter);
    v->setSpacing(16);

    QLabel *title = new QLabel(QString::fromUtf8("Identificación del paciente"), page);
    title->setFont(QFont("Segoe UI", 16, QFont::Bold));
    title->setStyleSheet(QString("color:%1;").arg(CLR_TEXT));
    v->addWidget(title);

    QLabel *subtitle = new QLabel(QString::fromUtf8("Ingrese la cédula (solo números)"), page);
    subtitle->setFont(QFont("Segoe UI", 11));
    subtitle->setStyleSheet(QString("color:%1;").arg(CLR_TEXT_MUTED));
    v->addWidget(subtitle);

    m_idLineEdit = new QLineEdit(page);
    m_idLineEdit->setMaxLength(12);
    m_idLineEdit->setPlaceholderText(QString::fromUtf8("Cédula del paciente"));
    m_idLineEdit->setFixedWidth(260);
    m_idLineEdit->setAlignment(Qt::AlignCenter);
    m_idLineEdit->setStyleSheet(
        "QLineEdit {"
        " background:white;"
        " border-radius:10px;"
        " padding:8px;"
        " font-size:14px;"
        "}"
        );
    v->addWidget(m_idLineEdit, 0, Qt::AlignHCenter);

    QPushButton *btnContinue = new QPushButton(QString::fromUtf8("Continuar"), page);
    btnContinue->setStyleSheet(
        QString(
            "QPushButton {"
            " background-color:%1;"
            " color:white;"
            " border:none;"
            " border-radius:16px;"
            " padding:8px 26px;"
            " font-size:14px;"
            " font-weight:bold;"
            "}"
            "QPushButton:hover {"
            " background-color:%2;"
            "}"
            ).arg(CLR_PRIMARY, CLR_PRIMARY_HOVER)
        );
    v->addWidget(btnContinue, 0, Qt::AlignHCenter);

    connect(btnContinue, &QPushButton::clicked,
            this, &MainWindow::handleIdContinue);

    return page;
}

void MainWindow::handleIdContinue()
{
    QString ced = m_idLineEdit->text().trimmed();
    ced.remove('.');
    ced.remove('-');
    ced.remove(' ');

    if (ced.length() < 7 || ced.length() > 12) {
        QMessageBox::warning(this, tr("Cédula no válida"),
                             tr("La cédula debe tener entre 7 y 12 dígitos."));
        return;
    }

    for (QChar c : ced) {
        if (!c.isDigit()) {
            QMessageBox::warning(this, tr("Cédula no válida"),
                                 tr("La cédula debe contener solo números."));
            return;
        }
    }

    m_patientId = ced;
    setStatusText(QString::fromUtf8("Paciente: ") + m_patientId);

    m_sessionCsvFiles.clear();
    if (m_pages)
        m_pages->setCurrentIndex(1); // menú de exámenes
}

// ------------------ Página de menú ------------------

QWidget* MainWindow::createMenuPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *v = new QVBoxLayout(page);
    v->setAlignment(Qt::AlignCenter);

    QWidget *btnRow = new QWidget(page);
    QHBoxLayout *rowLayout = new QHBoxLayout(btnRow);
    rowLayout->setSpacing(30);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    auto makeButton = [&](const QString &text) {
        QPushButton *b = new QPushButton(text, btnRow);
        b->setStyleSheet(
            QString(
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
                ).arg(CLR_PRIMARY, CLR_PRIMARY_HOVER)
            );
        return b;
    };

    QPushButton *btnWrist = makeButton(QString::fromUtf8("Examen de muñeca"));
    QPushButton *btnElbow = makeButton(QString::fromUtf8("Examen de codo"));
    QPushButton *btnFull  = makeButton(QString::fromUtf8("Examen completo"));

    rowLayout->addWidget(btnWrist);
    rowLayout->addWidget(btnElbow);
    rowLayout->addWidget(btnFull);

    v->addWidget(btnRow, 0, Qt::AlignCenter);

    // Botón Salir (volver a cédula)
    QWidget *bottomBar = new QWidget(page);
    QHBoxLayout *bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(0, 20, 0, 0);

    QPushButton *btnExit = new QPushButton(QString::fromUtf8("Salir"), bottomBar);
    QString exitBtnStyle = QString(
        "QPushButton {"
        " background-color:#b91c1c;"
        " color:white;"
        " border:none;"
        " border-radius:14px;"
        " padding:6px 24px;"
        " font-family:'Segoe UI';"
        " font-size:13px;"
        " font-weight:bold;"
        "}"
        "QPushButton:hover {"
        " background-color:#991b1b;"
        "}"
        );
    btnExit->setStyleSheet(exitBtnStyle);

    bottomLayout->addWidget(btnExit, 0, Qt::AlignLeft);
    bottomLayout->addStretch();

    v->addWidget(bottomBar);

    connect(btnWrist, &QPushButton::clicked, this, &MainWindow::startWristExam);
    connect(btnElbow, &QPushButton::clicked, this, &MainWindow::startElbowExam);
    connect(btnFull,  &QPushButton::clicked, this, &MainWindow::startFullExam);

    connect(btnExit, &QPushButton::clicked, this, [this]() {
        m_currentExam = ExamNone;
        m_examExercises.clear();
        m_currentExerciseIdx = -1;
        m_isAcquiring = false;
        m_sessionCsvFiles.clear();
        if (m_pages)
            m_pages->setCurrentIndex(0);
        setStatusText(QString::fromUtf8("Ingrese la cédula del paciente"));
    });

    return page;
}

// ------------------ Página de examen ------------------

QWidget* MainWindow::createExamPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *v = new QVBoxLayout(page);
    v->setSpacing(16);

    m_examTitleLabel = new QLabel("Examen — Ejercicio", page);
    m_examTitleLabel->setFont(QFont("Segoe UI", 16, QFont::Bold));
    m_examTitleLabel->setStyleSheet(QString("color:%1;").arg(CLR_TEXT));
    m_examTitleLabel->setAlignment(Qt::AlignHCenter);
    v->addWidget(m_examTitleLabel);

    m_examStatusLabel = new QLabel("Esperando", page);
    m_examStatusLabel->setFont(QFont("Segoe UI", 18, QFont::Bold));
    m_examStatusLabel->setStyleSheet(QString("color:%1;").arg(CLR_TEXT));
    m_examStatusLabel->setAlignment(Qt::AlignCenter);
    m_examStatusLabel->setMinimumHeight(80);
    v->addWidget(m_examStatusLabel, 1);

    m_examStartStopButton = new QPushButton("Iniciar", page);
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

    QWidget *bottomBar = new QWidget(page);
    QHBoxLayout *bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(0, 20, 0, 0);

    m_examNextButton = new QPushButton("Siguiente", bottomBar);
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

    bottomLayout->addStretch();
    bottomLayout->addWidget(m_examNextButton, 0, Qt::AlignRight);

    v->addWidget(bottomBar);

    connect(m_examNextButton, &QPushButton::clicked,
            this, &MainWindow::handleExamNext);

    return page;
}

// ---------------------------------------------------------------------------
//                      LÓGICA DE EXÁMENES
// ---------------------------------------------------------------------------

void MainWindow::setStatusText(const QString &text)
{
    if (m_statusLeft)
        m_statusLeft->setText(QString::fromUtf8("Estado: ") + text);
}

void MainWindow::showMenuPage()
{
    if (m_pages)
        m_pages->setCurrentIndex(1);

    m_currentExam = ExamNone;
    m_examExercises.clear();
    m_currentExerciseIdx = -1;
    m_isAcquiring = false;

    setStatusText(QString::fromUtf8("Menú principal"));
}

void MainWindow::startWristExam()
{
    m_currentExam = ExamWrist;
    m_examExercises = {1, 2, 4};      // Flex/Ext, Ulnar/Radial, Fuerza
    m_currentExerciseIdx = 0;
    m_isAcquiring = false;
    m_sessionCsvFiles.clear();

    updateExamUI();
    if (m_pages)
        m_pages->setCurrentIndex(2);

    setStatusText(QString::fromUtf8("Examen de muñeca"));
}

void MainWindow::startElbowExam()
{
    m_currentExam = ExamElbow;
    m_examExercises = {1, 3, 4};      // Flex/Ext, Prono/Sup, Fuerza
    m_currentExerciseIdx = 0;
    m_isAcquiring = false;
    m_sessionCsvFiles.clear();

    updateExamUI();
    if (m_pages)
        m_pages->setCurrentIndex(2);

    setStatusText(QString::fromUtf8("Examen de codo"));
}

void MainWindow::startFullExam()
{
    m_currentExam = ExamFull;
    m_examExercises = {1, 2, 3, 4};   // Flex/Ext, Ulnar/Radial, Prono/Sup, Fuerza
    m_currentExerciseIdx = 0;
    m_isAcquiring = false;
    m_sessionCsvFiles.clear();

    updateExamUI();
    if (m_pages)
        m_pages->setCurrentIndex(2);

    setStatusText(QString::fromUtf8("Examen completo"));
}

QString MainWindow::exerciseLabel(int exNum) const
{
    switch (exNum) {
    case 1:
        return QString::fromUtf8("Flexión / Extensión");
    case 2:
        return QString::fromUtf8("Desviación cubital/radial");
    case 3:
        return QString::fromUtf8("Pronosupinación");
    case 4:
        return QString::fromUtf8("Fuerza de prensión");
    default:
        return QString::fromUtf8("Ejercicio");
    }
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
        title += QString::fromUtf8(" — %1").arg(exerciseLabel(exNum));

    m_examTitleLabel->setText(title);
    m_examStatusLabel->setText(QString::fromUtf8("Esperando"));

    m_examStartStopButton->setText(QString::fromUtf8("Iniciar"));
    m_examStartStopButton->setEnabled(true);

    m_examNextButton->setEnabled(false);
    m_examNextButton->setText(QString::fromUtf8("Siguiente"));

    m_isAcquiring = false;
}

// Iniciar / Detener
void MainWindow::handleExamStartStop()
{
    if (m_currentExam == ExamNone || m_examExercises.isEmpty())
        return;

    if (!m_isAcquiring) {
        bool ok = false;
        int dur = QInputDialog::getInt(this,
                                       tr("Duración del ejercicio"),
                                       tr("Duración en segundos:"),
                                       5, 1, 600, 1, &ok);
        if (!ok)
            return;

        startAcquisitionForCurrentExercise(dur);
    }
    else {
        stopAcquisition(false);
    }
}

void MainWindow::handleExamNext()
{
    if (m_currentExam == ExamNone || m_examExercises.isEmpty())
        return;

    bool isLast = (m_currentExerciseIdx == m_examExercises.size() - 1);
    if (isLast) {
        // Fin del examen: construir Lecturas.xlsx con TODOS los CSV de la sesión
        runExcelBuilderForCurrentSession();
        showMenuPage();
        return;
    }

    ++m_currentExerciseIdx;
    updateExamUI();
}

// ---------------------------------------------------------------------------
//                  SERIAL: INICIO / FIN / LECTURA
// ---------------------------------------------------------------------------

void MainWindow::startAcquisitionForCurrentExercise(int durationSeconds)
{
    if (m_currentExerciseIdx < 0 || m_currentExerciseIdx >= m_examExercises.size())
        return;

    int exNum = m_examExercises[m_currentExerciseIdx];

    char cmd = 0;
    switch (exNum) {
    case 1: cmd = '1'; break; // Flex/Ext
    case 2: cmd = '2'; break; // Ulnar/Radial
    case 3: cmd = '3'; break; // Prono/Sup
    case 4: cmd = '4'; break; // Fuerza de prensión
    default:
        QMessageBox::warning(this, tr("Ejercicio no soportado"),
                             tr("El ejercicio %1 no está soportado.").arg(exNum));
        return;
    }

    if (m_serial->isOpen())
        m_serial->close();

    m_serial->setPortName("COM4");       // ajusta si usas otro puerto
    m_serial->setBaudRate(115200);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        QMessageBox::warning(this, tr("Error de puerto serie"),
                             tr("No se pudo abrir el puerto COM4.\n%1")
                                 .arg(m_serial->errorString()));
        return;
    }

    m_serialBuffer.clear();
    m_timeSamples.clear();
    m_valueSamples.clear();
    m_emgSamples.clear();

    m_elapsed->restart();                      // timestamp = 0 al iniciar
    m_acqDurationMs = durationSeconds * 1000;
    m_acqTimer->start(m_acqDurationMs);

    QByteArray out;
    out.append(cmd);
    m_serial->write(out);
    m_serial->write(" ");  // fijar cero ROM en modos 1–3 (el firmware lo ignora en 4)
    m_serial->flush();

    m_isAcquiring = true;
    m_examStatusLabel->setText(QString::fromUtf8("Tomando datos…"));
    m_examStartStopButton->setText(QString::fromUtf8("Detener"));
    m_examNextButton->setEnabled(false);

    qDebug() << "Inicio adquisición ejercicio" << exNum << "duración" << durationSeconds << "s";
}

void MainWindow::stopAcquisition(bool fromTimeout)
{
    if (!m_isAcquiring)
        return;

    m_acqTimer->stop();

    if (m_serial->isOpen()) {
        m_serial->write("e");
        m_serial->flush();
        m_serial->close();
    }

    m_isAcquiring = false;

    if (fromTimeout)
        m_examStatusLabel->setText(QString::fromUtf8("Tiempo cumplido"));
    else
        m_examStatusLabel->setText(QString::fromUtf8("Toma detenida"));

    m_examStartStopButton->setText(QString::fromUtf8("Iniciar"));

    bool last = (m_currentExerciseIdx == m_examExercises.size() - 1);
    m_examNextButton->setText(last ? QString::fromUtf8("Finalizar")
                                   : QString::fromUtf8("Siguiente"));
    m_examNextButton->setEnabled(true);

    qDebug() << "Fin adquisición. Muestras:" << m_timeSamples.size();

    // Guardar el ejercicio actual a CSV y acumular ruta para la sesión
    QString csvPath = saveCurrentExerciseToCsv();
    if (!csvPath.isEmpty())
        m_sessionCsvFiles.append(csvPath);
}

void MainWindow::onSerialReadyRead()
{
    m_serialBuffer.append(m_serial->readAll());

    int idx;
    while ((idx = m_serialBuffer.indexOf('\n')) != -1) {
        QByteArray lineBA = m_serialBuffer.left(idx);
        m_serialBuffer.remove(0, idx + 1);

        QString line = QString::fromLatin1(lineBA).trimmed();
        if (line.isEmpty())
            continue;

        // 1) Ignorar líneas tipo "ZERO_OK" o mensajes de texto
        if (!line.contains(',')) {
            // Puedes hacer aquí un qDebug() si quieres verlos
            continue;
        }

        // 2) Partir por comas
        QStringList parts = line.split(',', Qt::KeepEmptyParts);
        if (parts.size() < 4)
            continue;

        auto parseField = [](const QString &s) -> double {
            QString tok = s.trimmed();
            if (tok.compare("NaN", Qt::CaseInsensitive) == 0)
                return std::numeric_limits<double>::quiet_NaN();
            bool ok = false;
            double v = tok.toDouble(&ok);
            return ok ? v : std::numeric_limits<double>::quiet_NaN();
        };

        double t_arduino = parseField(parts[0]);
        double angle_deg = parseField(parts[1]);
        double force_kg  = (parts.size() > 2) ? parseField(parts[2]) : std::numeric_limits<double>::quiet_NaN();
        double emg_env   = (parts.size() > 3) ? parseField(parts[3]) : std::numeric_limits<double>::quiet_NaN();
        // threshold y activation están en parts[4], parts[5] si los quieres luego

        // 3) Saber qué ejercicio es
        int exNum = 0;
        if (m_currentExerciseIdx >= 0 && m_currentExerciseIdx < m_examExercises.size())
            exNum = m_examExercises[m_currentExerciseIdx];

        double romOrForce = std::numeric_limits<double>::quiet_NaN();
        if (exNum == 4) {
            romOrForce = force_kg;     // Fuerza de prensión
        } else {
            romOrForce = angle_deg;    // ROM
        }

        double emgVal = emg_env;

        // 4) Tiempo local de Qt
        double t = m_elapsed->elapsed() / 1000.0;

        m_timeSamples.append(t);
        m_valueSamples.append(romOrForce);
        m_emgSamples.append(emgVal);

        // 5) Actualizar UI
        QString valText;
        if (exNum == 4)
            valText = QString::fromUtf8("Fuerza = %1 kg").arg(romOrForce, 0, 'f', 3);
        else
            valText = QString::fromUtf8("ROM = %1 °").arg(romOrForce, 0, 'f', 2);

        m_examStatusLabel->setText(
            QString::fromUtf8("t = %1 s   ·   %2   ·   EMG = %3")
                .arg(QString::number(t, 'f', 2))
                .arg(valText)
                .arg(std::isnan(emgVal)
                         ? QStringLiteral("NaN")
                         : QString::number(emgVal, 'f', 2))
            );

        qDebug() << "LINE:" << line
                 << "-> value:" << romOrForce
                 << "EMG:" << emgVal;
    }
}


void MainWindow::onAcquisitionTimeout()
{
    stopAcquisition(true);
}

// ---------------------------------------------------------------------------
//                  GUARDADO A CSV POR EJERCICIO
// ---------------------------------------------------------------------------

QString MainWindow::saveCurrentExerciseToCsv()
{
    if (m_timeSamples.isEmpty() || m_valueSamples.isEmpty())
        return QString();
    if (m_timeSamples.size() != m_valueSamples.size())
        return QString();

    // EMG puede o no tener mismo tamaño, pero lo intentamos
    bool hasEmg = (m_emgSamples.size() == m_timeSamples.size());

    QString examName;
    switch (m_currentExam) {
    case ExamWrist: examName = QString::fromUtf8("Muñeca"); break;
    case ExamElbow: examName = QString::fromUtf8("Codo"); break;
    case ExamFull:  examName = QString::fromUtf8("Codo_y_Muñeca"); break;
    default:        examName = QString::fromUtf8("Examen"); break;
    }

    int exNum = 0;
    if (m_currentExerciseIdx >= 0 && m_currentExerciseIdx < m_examExercises.size())
        exNum = m_examExercises[m_currentExerciseIdx];

    QString colName;
    QString emgColName;

    switch (exNum) {
    case 1:
        colName    = QString::fromUtf8("ROM Flexión/Extensión_°");
        emgColName = QString::fromUtf8("EMG(F/E)_mv");
        break;
    case 2:
        colName    = QString::fromUtf8("ROM Desviación Ulnar/Radial_°");
        emgColName = QString::fromUtf8("EMG(D)_mv");
        break;
    case 3:
        colName    = QString::fromUtf8("ROM Pronosupinación_°");
        emgColName = QString::fromUtf8("EMG(PS)_mv");
        break;
    case 4:
        colName    = QString::fromUtf8("Fuerza de Prensión_Kg");
        emgColName = QString::fromUtf8("EMG(FP)_mv");
        break;
    default:
        colName    = QString::fromUtf8("Valor");
        emgColName = QString::fromUtf8("EMG");
        break;
    }

    QString basePath = QCoreApplication::applicationDirPath()
                       + "/PacienteData/"
                       + (m_patientId.isEmpty() ? "sin_id" : m_patientId);
    QDir baseDir(basePath);
    if (!baseDir.exists()) {
        if (!baseDir.mkpath(".")) {
            qWarning() << "No se pudo crear directorio" << basePath;
            QMessageBox::warning(this,
                                 tr("Error al guardar"),
                                 tr("No se pudo crear la carpeta para el paciente:\n%1")
                                     .arg(basePath));
            return QString();
        }
    }

    QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    QString fileName = QString("%1_%2_Ej%3.csv")
                           .arg(ts,
                                examName)
                           .arg(exNum);
    QString filePath = baseDir.filePath(fileName);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "No se pudo abrir archivo CSV para escribir:" << filePath
                   << file.errorString();
        QMessageBox::warning(this,
                             tr("Error al guardar"),
                             tr("No se pudo crear el archivo:\n%1\n\n%2")
                                 .arg(filePath,
                                      file.errorString()));
        return QString();
    }

    // BOM UTF-8 para Excel
    QByteArray bom("\xEF\xBB\xBF");
    file.write(bom);

    QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#endif

    // Cabecera: timestamp, valor principal, EMG
    out << "timestamp_s," << colName << "," << emgColName << "\n";

    for (int i = 0; i < m_timeSamples.size(); ++i) {
        double t   = m_timeSamples[i];
        double val = m_valueSamples[i];  // ROM ° o fuerza Kg, ya listo
        double emg = hasEmg ? m_emgSamples[i]
                            : std::numeric_limits<double>::quiet_NaN();

        out << QString::number(t,   'f', 4) << ","
            << QString::number(val, 'f', 4) << ",";

        if (std::isnan(emg)) {
            out << "";
        } else {
            out << QString::number(emg, 'f', 4);
        }

        out << "\n";
    }


    file.close();

    qDebug() << "CSV guardado en:" << filePath;
    setStatusText(QString::fromUtf8("Datos del ejercicio guardados"));

    return filePath;
}

// ---------------------------------------------------------------------------
//           LLAMAR AL SCRIPT PYTHON PARA CREAR Lecturas.xlsx
// ---------------------------------------------------------------------------

void MainWindow::runExcelBuilderForCurrentSession()
{
    if (m_sessionCsvFiles.isEmpty()) {
        qDebug() << "No hay CSV de sesión para construir Excel";
        return;
    }

    QString scriptPath = QCoreApplication::applicationDirPath()
                         + "/build_excel_from_csvs.py";

    QFileInfo fi(scriptPath);
    if (!fi.exists()) {
        QMessageBox::warning(this,
                             tr("Script faltante"),
                             tr("No se encontró el script Python:\n%1\n\n"
                                "Cópialo en la carpeta del ejecutable.")
                                 .arg(scriptPath));
        return;
    }

    QString examName;
    switch (m_currentExam) {
    case ExamWrist: examName = "Muñeca"; break;
    case ExamElbow: examName = "Codo"; break;
    case ExamFull:  examName = "Codo_y_Muñeca"; break;
    default:        examName = "Examen"; break;
    }

    QString pythonExe = "python";   // ajusta ruta si hace falta

    QStringList args;
    args << m_patientId << examName;
    for (const QString &p : m_sessionCsvFiles)
        args << p;

    qDebug() << "Ejecutando" << pythonExe << scriptPath << args;

    int exitCode = QProcess::execute(pythonExe, QStringList() << scriptPath << args);

    if (exitCode == 0) {
        setStatusText(QString::fromUtf8("Lecturas.xlsx actualizado correctamente"));
        QMessageBox::information(this,
                                 tr("Excel actualizado"),
                                 tr("Se generó/actualizó el archivo Lecturas.xlsx "
                                    "para el paciente %1.")
                                     .arg(m_patientId));

        // ======== BORRAR LOS CSV TEMPORALES =========
        for (const QString &path : m_sessionCsvFiles) {
            QFile f(path);
            if (f.exists()) {
                if (!f.remove()) {
                    qWarning() << "No se pudo borrar CSV temporal:" << path
                               << f.errorString();
                }
            }
        }
        m_sessionCsvFiles.clear();
        // ============================================
    } else {
        QMessageBox::warning(this,
                             tr("Error al crear Excel"),
                             tr("El script Python terminó con código %1.\n"
                                "Revisa la consola o ejecuta el script a mano.")
                                 .arg(exitCode));
    }
}

// ---------------------------------------------------------------------------
//                 RELOJ Y PANTALLA COMPLETA
// ---------------------------------------------------------------------------

void MainWindow::updateClock()
{
    QString hora = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_statusRight->setText(
        hora + QString::fromUtf8("   ·   F11: Pantalla completa   |   Esc: Salir"));
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
