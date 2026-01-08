#include "mainwindow.h"
#include <QApplication>
#include <QWidget>
#include <QGridLayout>
#include <QDebug>
#include <QDialog>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QtConcurrent/QtConcurrent>
#include <QMessageBox>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QEvent>
#include <QFile>
#include <QTextStream>
#include <QSpinBox>
#include <QDir>
#include <QStringList>

// C System Headers
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <float.h>
#include <string.h>

// --- CONFIG ---
#define DHT11_DEV       "/dev/dht11"
#define BH1750_DEV      "/dev/bh1750"
#define DATA_DIR        "/mnt/data"
#define UPLOAD_MARKER   "/mnt/data/.last_upload_date"
#define MODEL_FILE      "/mnt/data/model.tflite"
#define ZIP_FILE        "/mnt/data/model_download.zip"
#define EXTRACT_DIR     "/mnt/data/model_temp_extract"
#define WIFI_CONF_FILE  "/etc/wpa_supplicant.conf"
#define WIFI_IFACE      "wlan0"
#define EI_API_KEY      "ei_938352ab999f8f68e87a537d008fc05e944ef77b9589338f8f525fcd74f3c47d"
#define PROJECT_ID      "855133"

#define UPLOAD_URL      "https://ingestion.edgeimpulse.com/api/training/files"
#define RETRAIN_URL     "https://studio.edgeimpulse.com/v1/api/" PROJECT_ID "/jobs/retrain?impulseId=3"
#define BUILD_URL       "https://studio.edgeimpulse.com/v1/api/" PROJECT_ID "/jobs/build-ondevice-model?type=custom&impulseId=3"
#define JOB_STATUS_URL  "https://studio.edgeimpulse.com/v1/api/" PROJECT_ID "/jobs/%d/status"
#define BASE_DOWNLOAD_URL "https://studio.edgeimpulse.com/v1/api/" PROJECT_ID "/deployment/download"
#define DOWNLOAD_QUERY  "?type=custom&modelType=int8&engine=tflite&impulseId=3"

const char* LABELS_TEXT[] = {"Normal", "Temp Inc, Humid Dec", "Temp Inc, Humid Inc"};

// --- WIFI DIALOG CLASS ---
class WifiDialog : public QDialog {
public:
    QLineEdit *txtSSID;
    QLineEdit *txtPass;
    QLineEdit *currentFocus;
    bool isShift;
    QList<QPushButton*> charBtns;

    WifiDialog(QWidget *parent = nullptr) : QDialog(parent), isShift(false) {
        setWindowTitle("Wifi Settings");
        setStyleSheet(
            "QDialog { background-color: #222; color: white; border: 2px solid #555; }"
            "QLabel { font-size: 16px; font-weight: bold; color: #AAA; }"
            "QLineEdit { height: 40px; font-size: 20px; background-color: #444; color: white; border: 1px solid #777; padding: 5px; selection-background-color: #009688; }"
            "QLineEdit:focus { border: 2px solid #009688; }"
            "QPushButton { height: 45px; font-size: 18px; background-color: #555; color: white; border-radius: 4px; min-width: 40px; }"
            "QPushButton:pressed { background-color: #009688; }"
            "QPushButton#SpecialBtn { background-color: #444; border: 1px solid #666; }"
            "QPushButton#ActionBtn { background-color: #3F51B5; font-weight: bold; }"
        );

        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->addWidget(new QLabel("Wifi Name (SSID):"));
        txtSSID = new QLineEdit(this); txtSSID->installEventFilter(this); mainLayout->addWidget(txtSSID);
        mainLayout->addWidget(new QLabel("Password:"));
        txtPass = new QLineEdit(this); txtPass->setEchoMode(QLineEdit::Password); txtPass->installEventFilter(this); mainLayout->addWidget(txtPass);
        currentFocus = txtSSID;
        mainLayout->addSpacing(10);

        QGridLayout *kbLayout = new QGridLayout(); kbLayout->setSpacing(5);
        const char* row1[] = {"1","2","3","4","5","6","7","8","9","0"}; for(int i=0; i<10; i++) addButton(kbLayout, row1[i], 0, i);
        const char* row2[] = {"q","w","e","r","t","y","u","i","o","p"}; for(int i=0; i<10; i++) addButton(kbLayout, row2[i], 1, i);
        const char* row3[] = {"a","s","d","f","g","h","j","k","l"}; for(int i=0; i<9; i++) addButton(kbLayout, row3[i], 2, i);
        QPushButton *btnShift = new QPushButton("Shift", this); btnShift->setObjectName("SpecialBtn"); connect(btnShift, &QPushButton::clicked, this, &WifiDialog::toggleShift); kbLayout->addWidget(btnShift, 3, 0, 1, 2);
        const char* row4[] = {"z","x","c","v","b","n","m"}; for(int i=0; i<7; i++) addButton(kbLayout, row4[i], 3, i+2);
        QPushButton *btnBack = new QPushButton("⌫", this); btnBack->setObjectName("SpecialBtn"); connect(btnBack, &QPushButton::clicked, this, &WifiDialog::onBackspace); kbLayout->addWidget(btnBack, 3, 9, 1, 1);
        const char* row5[] = {"@", "_", "-", ".", "/", "!", "#", "$", "?"}; for(int i=0; i<9; i++) addButton(kbLayout, row5[i], 4, i);
        QPushButton *btnSpace = new QPushButton("Space", this); kbLayout->addWidget(btnSpace, 4, 9, 1, 1); connect(btnSpace, &QPushButton::clicked, [=](){ if(currentFocus) currentFocus->insert(" "); });

        mainLayout->addLayout(kbLayout); mainLayout->addSpacing(10);
        QHBoxLayout *btnLayout = new QHBoxLayout();
        QPushButton *btnCancel = new QPushButton("Cancel", this); btnCancel->setStyleSheet("background-color: #D32F2F;");
        QPushButton *btnConnect = new QPushButton("Connect", this); btnConnect->setObjectName("ActionBtn"); btnConnect->setStyleSheet("background-color: #4CAF50;");
        btnLayout->addWidget(btnCancel); btnLayout->addWidget(btnConnect);
        mainLayout->addLayout(btnLayout);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(btnConnect, &QPushButton::clicked, this, &QDialog::accept);
        setMinimumWidth(600);
    }
protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::FocusIn) { if (obj == txtSSID) currentFocus = txtSSID; else if (obj == txtPass) currentFocus = txtPass; }
        return QDialog::eventFilter(obj, event);
    }
private:
    void addButton(QGridLayout *layout, QString text, int row, int col) {
        QPushButton *btn = new QPushButton(text, this); layout->addWidget(btn, row, col);
        if (text.length() == 1 && text[0].isLetter()) { charBtns.append(btn); }
        connect(btn, &QPushButton::clicked, [=](){ if(currentFocus) currentFocus->insert(btn->text()); });
    }
    void onBackspace() { if(currentFocus) currentFocus->backspace(); }
    void toggleShift() { isShift = !isShift; for(QPushButton *btn : charBtns) { QString t = btn->text(); btn->setText(isShift ? t.toUpper() : t.toLower()); } }
};

// --- HELPERS ---
struct MemoryStruct { char *memory; size_t size; };
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb; struct MemoryStruct *mem = (struct MemoryStruct *)userp;
  char *ptr = (char*)realloc(mem->memory, mem->size + realsize + 1); if(!ptr) return 0;
  mem->memory = ptr; memcpy(&(mem->memory[mem->size]), contents, realsize); mem->size += realsize; mem->memory[mem->size] = 0; return realsize;
}
static size_t WriteFileCallback(void *ptr, size_t size, size_t nmemb, void *stream) { return fwrite(ptr, size, nmemb, (FILE *)stream); }
static int parse_job_id(const char* json_str) {
    const char *ptr = strstr(json_str, "\"id\""); if (!ptr) return -1; ptr += 4; while (*ptr == ':' || *ptr == ' ' || *ptr == '"') ptr++;
    int id = -1; if (sscanf(ptr, "%d", &id) == 1) return id; return -1;
}

// ============================================================================
//  MAIN WINDOW
// ============================================================================

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    curl_global_init(CURL_GLOBAL_ALL);
    (void)system("modprobe dht11_driver"); (void)system("modprobe bh1750_driver");
    struct stat st = {0}; if (stat(DATA_DIR, &st) == -1) mkdir(DATA_DIR, 0700);

    setupUI();

    // Reset Buffers
    buffer_head = 0; samples_collected = 0;
    for(int i=0; i<WINDOW_LEN; i++) for(int j=0; j<RAW_FEATURE_COUNT; j++) input_buffer[i][j] = 0.0f;
    dataBuffer.clear(); cooldownTimer = 0;
    
    lastPredictionIdx = 0; // Reset last prediction to Normal

    isSystemReady = false; start_time = 0;
    loadModel();

    timer = new QTimer(this); connect(timer, &QTimer::timeout, this, &MainWindow::onTimerTick); timer->start(INTERVAL_S * 1000);
    wifiTimer = new QTimer(this); connect(wifiTimer, &QTimer::timeout, this, &MainWindow::checkWifiState); wifiTimer->start(3000);
    lastWifiState = "UNKNOWN"; onTimerTick();
}

MainWindow::~MainWindow() { curl_global_cleanup(); }

void MainWindow::setupUI() {
    QWidget *centralWidget = new QWidget(this); setCentralWidget(centralWidget);
    this->setStyleSheet("QMainWindow { background-color: #222; color: white; } QLabel { color: white; }");
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    lblTime = new QLabel("Waiting for Time Sync...", this); lblTime->setAlignment(Qt::AlignCenter);
    lblTime->setStyleSheet("font-size: 24px; font-weight: bold; color: #FF9800; margin-bottom: 10px;");
    mainLayout->addWidget(lblTime);

    QGridLayout *sensorLayout = new QGridLayout();
    QLabel *iconTemp = new QLabel("TEMP", this); iconTemp->setAlignment(Qt::AlignCenter);
    lblTemp = new QLabel("-- °C", this); lblTemp->setAlignment(Qt::AlignCenter);
    lblTemp->setStyleSheet("font-size: 36px; font-weight: bold; color: #FF5722;");
    QLabel *iconHum = new QLabel("HUMID", this); iconHum->setAlignment(Qt::AlignCenter);
    lblHum = new QLabel("-- %", this); lblHum->setAlignment(Qt::AlignCenter);
    lblHum->setStyleSheet("font-size: 36px; font-weight: bold; color: #2196F3;");
    QLabel *iconLux = new QLabel("LIGHT", this); iconLux->setAlignment(Qt::AlignCenter);
    lblLux = new QLabel("-- Lux", this); lblLux->setAlignment(Qt::AlignCenter);
    lblLux->setStyleSheet("font-size: 36px; font-weight: bold; color: #FFC107;");
    sensorLayout->addWidget(iconTemp, 0, 0); sensorLayout->addWidget(lblTemp, 1, 0);
    sensorLayout->addWidget(iconHum, 0, 1);  sensorLayout->addWidget(lblHum, 1, 1);
    sensorLayout->addWidget(iconLux, 0, 2);  sensorLayout->addWidget(lblLux, 1, 2);
    mainLayout->addLayout(sensorLayout);

    mainLayout->addSpacing(20);
    QLabel *lblPredTitle = new QLabel("AI PREDICTION:", this); lblPredTitle->setStyleSheet("font-size: 18px; color: #AAA;"); lblPredTitle->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(lblPredTitle);
    lblPrediction = new QLabel("System Paused (Set Time First)", this);
    lblPrediction->setStyleSheet("font-size: 28px; font-weight: bold; color: #777; border: 2px solid #555; padding: 10px; border-radius: 5px; background-color: #333;");
    lblPrediction->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(lblPrediction);

    // --- AC CONTROL WIDGET ---
    acWidget = new QWidget(this);
    acWidget->setStyleSheet("background-color: #37474F; border-radius: 8px; margin-top: 10px;");
    acWidget->setVisible(false);
    QHBoxLayout *acLayout = new QHBoxLayout(acWidget);
    acLabel = new QLabel("Event Detected. Turn on AC?", acWidget);
    acLabel->setStyleSheet("font-size: 18px; color: #FFEB3B; font-weight: bold; border: none;"); acLabel->setAlignment(Qt::AlignCenter);
    QPushButton *btnYes = new QPushButton("YES", acWidget); btnYes->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold; min-width: 80px; height: 40px; border-radius: 4px;");
    QPushButton *btnNo = new QPushButton("NO", acWidget); btnNo->setStyleSheet("background-color: #F44336; color: white; font-weight: bold; min-width: 80px; height: 40px; border-radius: 4px;");
    acLayout->addWidget(acLabel); acLayout->addWidget(btnYes); acLayout->addWidget(btnNo);
    mainLayout->addWidget(acWidget);

    connect(btnYes, &QPushButton::clicked, [=](){ acWidget->setVisible(false); lblStatus->setText("AC Turned ON (Simulated)"); qDebug() << "User selected YES: Turning on AC..."; });
    connect(btnNo, &QPushButton::clicked, [=](){ acWidget->setVisible(false); lblStatus->setText("AC Request Ignored."); qDebug() << "User selected NO."; });

    lblStatus = new QLabel("Please Connect Wifi or Set Time", this); lblStatus->setStyleSheet("color: #888; font-style: italic; margin-top: 5px;"); lblStatus->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(lblStatus);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *btnSettings = new QPushButton("Time", this); btnSettings->setMinimumHeight(50); btnSettings->setStyleSheet("font-size: 16px; background-color: #555; color: white; border: none; border-radius: 4px;"); connect(btnSettings, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    QPushButton *btnWifi = new QPushButton("Wifi", this); btnWifi->setMinimumHeight(50); btnWifi->setStyleSheet("font-size: 16px; background-color: #3F51B5; color: white; border: none; border-radius: 4px;"); connect(btnWifi, &QPushButton::clicked, this, &MainWindow::onWifiSettingsClicked);
    QPushButton *btnUpdate = new QPushButton("Update", this); btnUpdate->setMinimumHeight(50); btnUpdate->setStyleSheet("font-size: 16px; background-color: #009688; color: white; border: none; border-radius: 4px;"); connect(btnUpdate, &QPushButton::clicked, this, &MainWindow::onUpdateModelClicked);
    btnLayout->addWidget(btnSettings); btnLayout->addWidget(btnWifi); btnLayout->addWidget(btnUpdate);
    mainLayout->addLayout(btnLayout);
}

void MainWindow::initializeLoggingSession() {
    start_time = time(NULL); loop_count = 0; samples_collected = 0; buffer_head = 0;
    for(int i=0; i<WINDOW_LEN; i++) for(int j=0; j<RAW_FEATURE_COUNT; j++) input_buffer[i][j] = 0.0f;
    dataBuffer.clear(); cooldownTimer = 0; isSystemReady = true;
    lblTime->setStyleSheet("font-size: 24px; font-weight: bold; color: #4CAF50; margin-bottom: 10px;");
    lblPrediction->setText("Buffering Data...");
    lblPrediction->setStyleSheet("font-size: 28px; font-weight: bold; color: #E91E63; border: 2px solid #555; padding: 10px; border-radius: 5px; background-color: #333;");
    lblStatus->setText("System Ready. Smart Logging Active.");
}

QString MainWindow::getLastUploadDate() { QFile file(UPLOAD_MARKER); if (file.open(QIODevice::ReadOnly | QIODevice::Text)) { QTextStream in(&file); return in.readAll().trimmed(); } return "1970-01-01"; }
void MainWindow::setLastUploadDate(QString dateStr) { QFile file(UPLOAD_MARKER); if (file.open(QIODevice::WriteOnly | QIODevice::Text)) { QTextStream out(&file); out << dateStr; file.close(); } }

void MainWindow::onWifiSettingsClicked() {
    WifiDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) { QString ssid = dialog.txtSSID->text(); QString pass = dialog.txtPass->text(); if(!ssid.isEmpty()) updateWifiConfig(ssid, pass); else QMessageBox::warning(this, "Error", "Wifi name cannot be empty!"); }
}

void MainWindow::updateWifiConfig(QString ssid, QString password) {
    lblStatus->setText("Wifi: Configuring..."); QApplication::processEvents();
    QString config = "ctrl_interface=/var/run/wpa_supplicant\nap_scan=1\nupdate_config=1\n\nnetwork={\n\tssid=\"" + ssid + "\"\n\tpsk=\"" + password + "\"\n}\n";
    QFile file(WIFI_CONF_FILE); if (file.open(QIODevice::WriteOnly | QIODevice::Text)) { QTextStream out(&file); out << config; file.close(); QString cmd = QString("wpa_cli -i %1 reconfigure").arg(WIFI_IFACE); int ret = system(cmd.toStdString().c_str()); if (ret == 0) lblStatus->setText("Wifi: Apply Success. Connecting..."); else lblStatus->setText("Wifi: Apply Failed (Check Permission)"); } else lblStatus->setText("Error: Cannot write Wifi config!");
}

void MainWindow::checkWifiState() {
    if (lblStatus->text().contains("Uploading") || lblStatus->text().contains("Training") || lblStatus->text().contains("Downloading") || lblStatus->text().contains("Building")) return;
    QProcess process; process.start("wpa_cli", QStringList() << "-i" << WIFI_IFACE << "status"); process.waitForFinished(); QString output = process.readAllStandardOutput();
    QString currentState = "DISCONNECTED"; QString ssid = "";
    if (output.contains("wpa_state=COMPLETED")) { currentState = "CONNECTED"; int idx = output.indexOf("ssid="); if (idx != -1) { int end = output.indexOf("\n", idx); ssid = output.mid(idx + 5, end - (idx + 5)); } }
    else if (output.contains("wpa_state=SCANNING")) currentState = "SCANNING"; else if (output.contains("wpa_state=ASSOCIATING") || output.contains("wpa_state=4WAY_HANDSHAKE")) currentState = "CONNECTING";
    if (currentState != lastWifiState) {
        if (currentState == "CONNECTED") { lblStatus->setText(QString("Wifi Connected: %1").arg(ssid)); lblStatus->setStyleSheet("color: #4CAF50; font-style: italic;"); if (!isSystemReady) QtConcurrent::run([=](){ syncTimeFromInternet(); }); if (!this->model) { lblStatus->setText("Wifi Found. Retrying Model Download..."); QtConcurrent::run([=](){ downloadAndInstallModel(); }); } }
        else if (currentState == "DISCONNECTED") { lblStatus->setText("Wifi Disconnected!"); lblStatus->setStyleSheet("color: #F44336; font-style: italic;"); }
        else if (currentState == "CONNECTING") { lblStatus->setText("Wifi Connecting..."); lblStatus->setStyleSheet("color: #FFC107; font-style: italic;"); }
        lastWifiState = currentState;
    }
}

// [BUILD/TRAIN/DOWNLOAD HELPERS]
int MainWindow::triggerBuildJob() {
    CURL *curl; CURLcode res; long http_code = 0; int job_id = -1;
    struct MemoryStruct chunk; chunk.memory = (char*)malloc(1); chunk.size = 0;
    const char* json_payload = "{\"engine\": \"tflite\", \"modelType\": \"int8\"}";
    struct curl_slist *headers = NULL; char api_header[128]; sprintf(api_header, "x-api-key: %s", EI_API_KEY);
    headers = curl_slist_append(headers, api_header); headers = curl_slist_append(headers, "Content-Type: application/json");
    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, BUILD_URL); curl_easy_setopt(curl, CURLOPT_POST, 1L); curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload); curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback); curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk); curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
        res = curl_easy_perform(curl); curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if(res == CURLE_OK && http_code == 200) job_id = parse_job_id(chunk.memory);
        curl_easy_cleanup(curl); curl_slist_free_all(headers); free(chunk.memory);
    } return job_id;
}
bool MainWindow::waitForJob(int job_id, const QString &jobName) {
    int job_finished = 0; int seconds_waited = 0; int TIMEOUT_LIMIT = 1800;
    char job_url[256]; sprintf(job_url, JOB_STATUS_URL, job_id); char api_header[128]; sprintf(api_header, "x-api-key: %s", EI_API_KEY);
    while(job_finished == 0 && seconds_waited < TIMEOUT_LIMIT) {
        sleep(5); seconds_waited += 5; if(seconds_waited % 10 == 0) QMetaObject::invokeMethod(this, [=](){ lblStatus->setText(QString("%1 running (%2s)...").arg(jobName).arg(seconds_waited)); }, Qt::QueuedConnection);
        CURL *curl = curl_easy_init();
        if(curl) {
            struct MemoryStruct chunk; chunk.memory = (char*)malloc(1); chunk.size = 0; struct curl_slist *headers = NULL; headers = curl_slist_append(headers, api_header);
            curl_easy_setopt(curl, CURLOPT_URL, job_url); curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback); curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L); curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
            CURLcode res = curl_easy_perform(curl); long http_code = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (res == CURLE_OK && http_code == 200) {
                QByteArray jsonBytes(chunk.memory); QJsonDocument doc = QJsonDocument::fromJson(jsonBytes); QJsonObject rootObj = doc.object();
                if (rootObj.contains("job")) { QJsonObject jobObj = rootObj["job"].toObject(); if (jobObj.contains("finished")) { if (jobObj["finishedSuccessful"].toBool() == true) job_finished = 1; else job_finished = -1; } }
            } free(chunk.memory); curl_easy_cleanup(curl); curl_slist_free_all(headers);
        } if (job_finished != 0) break;
    } return (job_finished == 1);
}
bool MainWindow::attemptDownload(int retries) {
    char clean_cmd[256]; sprintf(clean_cmd, "rm -rf %s %s", ZIP_FILE, EXTRACT_DIR); (void)system(clean_cmd);
    int attempt = 0; bool success = false; char api_header[128]; sprintf(api_header, "x-api-key: %s", EI_API_KEY);
    while (attempt < retries && !success) {
        attempt++; QString statusMsg = QString("Downloading (Attempt %1/%2)...").arg(attempt).arg(retries); QMetaObject::invokeMethod(this, [=](){ lblStatus->setText(statusMsg); }, Qt::QueuedConnection);
        CURL *curl = curl_easy_init(); long http_code = 0;
        if(curl) {
            FILE *fp = fopen(ZIP_FILE, "wb");
            if(fp) {
                struct curl_slist *headers = NULL; headers = curl_slist_append(headers, api_header); char full_url[512]; sprintf(full_url, "%s%s", BASE_DOWNLOAD_URL, DOWNLOAD_QUERY);
                curl_easy_setopt(curl, CURLOPT_URL, full_url); curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback); curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L); curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L); curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
                CURLcode res = curl_easy_perform(curl); curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code); fclose(fp); curl_slist_free_all(headers); if (res == CURLE_OK && http_code == 200) success = true;
            } curl_easy_cleanup(curl);
        } if (!success) sleep(2);
    } return success;
}

void MainWindow::performUpdateSequence() {
    if (!isSystemReady) { QMetaObject::invokeMethod(this, [=](){ QMessageBox::warning(this, "Error", "Time not synced yet."); }, Qt::QueuedConnection); return; }
    QString currentDateStr = QDateTime::currentDateTime().toString("yyyy-MM-dd"); QString lastUploadDateStr = getLastUploadDate();
    QDir dir(DATA_DIR); QStringList filters; filters << "*.csv"; dir.setNameFilters(filters); dir.setSorting(QDir::Name);
    QStringList filesToUpload; QStringList entryList = dir.entryList();
    foreach (QString filename, entryList) { QString fileDateStr = filename.section('.', 0, 0); if (fileDateStr > lastUploadDateStr && fileDateStr < currentDateStr) filesToUpload.append(filename); }
    if (filesToUpload.isEmpty()) { QMetaObject::invokeMethod(this, [=](){ lblStatus->setText("No past files to upload."); QMessageBox::information(this, "Info", "All past data is already uploaded."); }, Qt::QueuedConnection); return; }

    char api_header[128]; sprintf(api_header, "x-api-key: %s", EI_API_KEY); int max_retries = 5;
    int fileIdx = 0;
    for (const QString &filename : filesToUpload) {
        fileIdx++; QString fullPath = QString("%1/%2").arg(DATA_DIR).arg(filename); QString fileDateStr = filename.section('.', 0, 0); bool upload_ok = false;
        for (int attempt = 1; attempt <= max_retries; attempt++) {
            QString msg = QString("Uploading %1 (%2/%3) - Try %4").arg(filename).arg(fileIdx).arg(filesToUpload.size()).arg(attempt); QMetaObject::invokeMethod(this, [=](){ lblStatus->setText(msg); }, Qt::QueuedConnection);
            CURL *curl = curl_easy_init(); long http_code = 0; CURLcode res = CURLE_FAILED_INIT;
            if(curl) {
                curl_mime *form = curl_mime_init(curl); curl_mimepart *field = curl_mime_addpart(form); curl_mime_name(field, "data"); curl_mime_filedata(field, fullPath.toStdString().c_str()); curl_mime_type(field, "text/csv");
                struct curl_slist *headers = NULL; headers = curl_slist_append(headers, api_header); headers = curl_slist_append(headers, "x-disallow-duplicates: 1"); headers = curl_slist_append(headers, "x-label: normal");
                curl_easy_setopt(curl, CURLOPT_URL, UPLOAD_URL); curl_easy_setopt(curl, CURLOPT_MIMEPOST, form); curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L); curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
                res = curl_easy_perform(curl); curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code); curl_easy_cleanup(curl); curl_mime_free(form); curl_slist_free_all(headers);
            }
            if(res == CURLE_OK && (http_code == 200 || http_code == 201)) { upload_ok = true; break; } else sleep(2);
        }
        if (upload_ok) setLastUploadDate(fileDateStr); else { QMetaObject::invokeMethod(this, [=](){ lblStatus->setText("Upload Error at " + filename); QMessageBox::warning(this, "Error", "Failed to upload " + filename); }, Qt::QueuedConnection); return; }
    }

    int job_id = -1;
    for (int attempt = 1; attempt <= max_retries; attempt++) {
        QMetaObject::invokeMethod(this, [=](){ lblStatus->setText(QString("Triggering Retrain (%1/%2)...").arg(attempt).arg(max_retries)); }, Qt::QueuedConnection);
        CURL *curl = curl_easy_init(); long http_code = 0; CURLcode res = CURLE_FAILED_INIT;
        if(curl) {
            struct MemoryStruct chunk; chunk.memory = (char*)malloc(1); chunk.size = 0; struct curl_slist *headers = NULL; headers = curl_slist_append(headers, api_header); headers = curl_slist_append(headers, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_URL, RETRAIN_URL); curl_easy_setopt(curl, CURLOPT_POST, 1L); curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers); curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback); curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk); curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
            res = curl_easy_perform(curl); curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code); if(res == CURLE_OK && http_code == 200) job_id = parse_job_id(chunk.memory); free(chunk.memory); curl_easy_cleanup(curl); curl_slist_free_all(headers);
        } if (job_id != -1) break; else sleep(2);
    }
    if (job_id == -1) { QMetaObject::invokeMethod(this, [=](){ lblStatus->setText("Trigger Failed!"); }, Qt::QueuedConnection); return; }
    if (!waitForJob(job_id, "Training")) { QMetaObject::invokeMethod(this, [=](){ lblStatus->setText("Training Failed!"); }, Qt::QueuedConnection); return; }
    int build_id = -1;
    for (int attempt = 1; attempt <= max_retries; attempt++) { QMetaObject::invokeMethod(this, [=](){ lblStatus->setText(QString("Triggering Build (%1/%2)...").arg(attempt).arg(max_retries)); }, Qt::QueuedConnection); build_id = triggerBuildJob(); if (build_id != -1) break; else sleep(2); }
    if (build_id == -1) { QMetaObject::invokeMethod(this, [=](){ lblStatus->setText("Build Trigger Failed!"); }, Qt::QueuedConnection); return; }
    if (!waitForJob(build_id, "Building")) { QMetaObject::invokeMethod(this, [=](){ lblStatus->setText("Build Failed!"); }, Qt::QueuedConnection); return; }
    if (attemptDownload(5)) this->installDownloadedModel(); else QMetaObject::invokeMethod(this, [=](){ lblStatus->setText("Download Failed!"); }, Qt::QueuedConnection);
}

void MainWindow::downloadAndInstallModel() {
    bool success = attemptDownload(5);
    if (!success) {
        QMetaObject::invokeMethod(this, [=](){ lblStatus->setText("Cache missing. Re-building..."); }, Qt::QueuedConnection);
        int build_id = triggerBuildJob(); bool build_ok = (build_id != -1) && waitForJob(build_id, "Emergency Build");
        if (build_ok) { QMetaObject::invokeMethod(this, [=](){ lblStatus->setText("Build OK. Downloading..."); }, Qt::QueuedConnection); success = attemptDownload(5); }
        else { QMetaObject::invokeMethod(this, [=](){ lblStatus->setText("Recovery Build Failed."); }, Qt::QueuedConnection); return; }
    } if (success) this->installDownloadedModel(); else QMetaObject::invokeMethod(this, [=](){ lblStatus->setText("Recovery Failed (Network)."); }, Qt::QueuedConnection);
}

void MainWindow::installDownloadedModel() {
    QMetaObject::invokeMethod(this, [=](){ lblStatus->setText("Installing Model..."); }, Qt::QueuedConnection);
    char cmd[512]; sprintf(cmd, "mkdir -p %s", EXTRACT_DIR); (void)system(cmd); sprintf(cmd, "unzip -o %s -d %s > /dev/null", ZIP_FILE, EXTRACT_DIR);
    if (system(cmd) != 0) { QMetaObject::invokeMethod(this, [=](){ lblStatus->setText("Unzip Failed!"); QMessageBox::warning(this, "Error", "Downloaded file is corrupted."); }, Qt::QueuedConnection); return; }
    sprintf(cmd, "mv %s/trained.tflite %s", EXTRACT_DIR, MODEL_FILE); if(system(cmd) != 0) { sprintf(cmd, "find %s -name '*.tflite' -exec mv {} %s \\; -quit", EXTRACT_DIR, MODEL_FILE); (void)system(cmd); }
    sprintf(cmd, "rm -rf %s %s", ZIP_FILE, EXTRACT_DIR); (void)system(cmd);
    QMetaObject::invokeMethod(this, [=]() { this->loadModel(); if (this->model) { lblStatus->setText("Model Updated!"); QMessageBox::information(this, "Success", "Model updated successfully!"); } }, Qt::QueuedConnection);
}

void MainWindow::syncTimeFromInternet() {
    int max_retries = 5; int attempt = 0; bool success = false;
    while (attempt < max_retries && !success) {
        attempt++; if (!lblStatus->text().contains("Uploading")) lblStatus->setText(QString("Syncing Time (%1/%2)...").arg(attempt).arg(max_retries));
        QApplication::processEvents(); CURL *curl = curl_easy_init();
        if (curl) {
            struct MemoryStruct chunk; chunk.memory = (char*)malloc(1); chunk.size = 0;
            curl_easy_setopt(curl, CURLOPT_URL, "http://worldtimeapi.org/api/timezone/Asia/Ho_Chi_Minh"); curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback); curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk); curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L); curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
            if (curl_easy_perform(curl) == CURLE_OK) {
                QByteArray jsonBytes(chunk.memory); QJsonDocument doc = QJsonDocument::fromJson(jsonBytes); QJsonObject obj = doc.object();
                if (obj.contains("unixtime")) { qint64 unixtime = obj["unixtime"].toVariant().toLongLong(); QString cmd = QString("date -s @%1").arg(unixtime); if (system(cmd.toStdString().c_str()) == 0) { QMetaObject::invokeMethod(this, [=](){ initializeLoggingSession(); lblStatus->setText("Time Synced & System Started!"); }, Qt::QueuedConnection); success = true; } }
            } curl_easy_cleanup(curl); free(chunk.memory);
        } if (!success && attempt < max_retries) sleep(2);
    } if (!success) lblStatus->setText("Net Sync Failed. Please Set Time Manually.");
}

void MainWindow::onSettingsClicked() {
    QDialog dialog(this); dialog.setWindowTitle("System Time Settings");
    dialog.setStyleSheet("QDialog { background-color: #333; color: white; } QLabel { font-size: 20px; font-weight: bold; color: #AAA; } QSpinBox { height: 60px; font-size: 28px; background-color: #555; color: white; border: 2px solid #777; } QSpinBox::up-button, QSpinBox::down-button { width: 60px; } QPushButton { height: 60px; font-size: 24px; background-color: #009688; color: white; border-radius: 5px; min-width: 120px; } QPushButton:pressed { background-color: #00796B; }");
    QVBoxLayout *mainLayout = new QVBoxLayout(&dialog); QDateTime current = QDateTime::currentDateTime();
    mainLayout->addWidget(new QLabel("Date (D-M-Y):", &dialog)); QHBoxLayout *dateLayout = new QHBoxLayout();
    QSpinBox *sbDay = new QSpinBox(&dialog); sbDay->setRange(1, 31); sbDay->setValue(current.date().day()); QSpinBox *sbMonth = new QSpinBox(&dialog); sbMonth->setRange(1, 12); sbMonth->setValue(current.date().month()); QSpinBox *sbYear = new QSpinBox(&dialog); sbYear->setRange(2024, 2035); sbYear->setValue(current.date().year());
    dateLayout->addWidget(sbDay); dateLayout->addWidget(sbMonth); dateLayout->addWidget(sbYear); mainLayout->addLayout(dateLayout); mainLayout->addSpacing(20);
    mainLayout->addWidget(new QLabel("Time (H:M):", &dialog)); QHBoxLayout *timeLayout = new QHBoxLayout();
    QSpinBox *sbHour = new QSpinBox(&dialog); sbHour->setRange(0, 23); sbHour->setValue(current.time().hour()); QSpinBox *sbMinute = new QSpinBox(&dialog); sbMinute->setRange(0, 59); sbMinute->setValue(current.time().minute());
    timeLayout->addWidget(sbHour); timeLayout->addWidget(sbMinute); mainLayout->addLayout(timeLayout); mainLayout->addSpacing(30);
    QHBoxLayout *btnLayout = new QHBoxLayout(); QPushButton *btnCancel = new QPushButton("Cancel", &dialog); btnCancel->setStyleSheet("background-color: #D32F2F;"); QPushButton *btnOk = new QPushButton("Save Time", &dialog);
    btnLayout->addWidget(btnCancel); btnLayout->addWidget(btnOk); mainLayout->addLayout(btnLayout);
    connect(btnOk, &QPushButton::clicked, &dialog, &QDialog::accept); connect(btnCancel, &QPushButton::clicked, &dialog, &QDialog::reject); dialog.setMinimumWidth(400);
    if (dialog.exec() == QDialog::Accepted) {
        QDate newDate(sbYear->value(), sbMonth->value(), sbDay->value()); QTime newTime(sbHour->value(), sbMinute->value(), 0);
        if (newDate.isValid()) { QDateTime newDateTime(newDate, newTime); QString cmd = QString("date -s \"%1\"").arg(newDateTime.toString("yyyy-MM-dd HH:mm:ss")); if (system(cmd.toStdString().c_str()) == 0) { initializeLoggingSession(); QMessageBox::information(this, "Success", "Time updated & Data logging started!"); onTimerTick(); } else QMessageBox::critical(this, "Error", "Cannot set time (Root required)!"); }
    }
}

void MainWindow::onUpdateModelClicked() {
    if (!isSystemReady) { QMessageBox::warning(this, "Not Ready", "Please set system time first!"); return; }
    lblStatus->setText("Starting Update Process..."); lastWifiState = "UPDATING"; QtConcurrent::run([=]() { this->performUpdateSequence(); });
}

// ============================================================================
//  MAIN LOOP LOGIC - MODIFIED
// ============================================================================
void MainWindow::onTimerTick() {
    float temp = 0, hum = 0, lux = 0; readDHT11(&temp, &hum); readBH1750(&lux);
    lblTemp->setText(QString::number(temp, 'f', 1) + " °C"); lblHum->setText(QString::number(hum, 'f', 1) + " %"); lblLux->setText(QString::number(lux, 'f', 0) + " Lux");
    time_t now = time(NULL); if (isSystemReady) lblTime->setText(QDateTime::fromTime_t(now).toString("dddd, dd MMM yyyy - HH:mm")); else lblTime->setText("Waiting for Time Sync...");
    if (!isSystemReady) return;

    // 2. PREPARE DATA
    BufferedSample sample; sample.timestamp = (long)now; sample.temp = temp; sample.humid = hum; sample.lux = lux; sample.label = "normal";
    float all_feats[9]; calcTimeFeatures(now, all_feats); all_feats[6] = temp; all_feats[7] = hum; all_feats[8] = lux; memcpy(sample.features, all_feats, sizeof(float)*9);
    dataBuffer.append(sample);

    // 3. DETECT EVENTS (Curve Logic for AUTO LABELING ONLY)
    if (dataBuffer.size() > DETECTION_WINDOW && cooldownTimer == 0) {
        int currentIdx = dataBuffer.size() - 1; int pastIdx = currentIdx - DETECTION_WINDOW;
        BufferedSample current = dataBuffer[currentIdx]; BufferedSample past = dataBuffer[pastIdx];
        float deltaTemp = current.temp - past.temp; float deltaHumid = current.humid - past.humid;
        QString detectedEvent = "";
        if (deltaTemp >= THRESHOLD_TEMP_RISE) {
            if (deltaHumid >= THRESHOLD_HUMID_RISE) detectedEvent = "temp_inc, humid_inc";
            else if (deltaHumid <= THRESHOLD_HUMID_DROP) detectedEvent = "temp_inc, humid_dec";
        }
        if (!detectedEvent.isEmpty()) {
            // Relabel Backwards
            int labelEndIdx = pastIdx; int labelStartIdx = labelEndIdx - PREDICTION_OFFSET; if (labelStartIdx < 0) labelStartIdx = 0;
            for (int i = labelStartIdx; i <= labelEndIdx; i++) if (dataBuffer[i].label == "normal") dataBuffer[i].label = detectedEvent;
            
            // NOTE: REMOVED AC UI TRIGGER FROM HERE
            cooldownTimer = 90; 
        }
    } else { if (cooldownTimer > 0) cooldownTimer--; }

    // 4. FLUSH TO DISK
    while (dataBuffer.size() > BUFFER_MAX_SIZE) {
        BufferedSample toWrite = dataBuffer.takeFirst();
        QDateTime currentDT = QDateTime::fromTime_t(toWrite.timestamp); QString todayFileName = currentDT.toString("yyyy-MM-dd") + ".csv"; QString todayFullLink = QString("%1/%2").arg(DATA_DIR).arg(todayFileName);
        QByteArray fileNameBytes = todayFullLink.toLocal8Bit(); FILE *fp = fopen(fileNameBytes.constData(), "a");
        if (fp) {
             if (ftell(fp) == 0) fprintf(fp, "timestamp,min_sin,min_cos,temp,humid,lux,label\n");
             fprintf(fp, "%ld,%.5f,%.5f,%.1f,%.1f,%.1f,%s\n", toWrite.timestamp * 1000, toWrite.features[0], toWrite.features[1], toWrite.features[6], toWrite.features[7], toWrite.features[8], toWrite.label.toStdString().c_str());
             fclose(fp);
        }
    }

    // 5. INFERENCE (Trigger AC Logic Here)
    float raw_input[RAW_FEATURE_COUNT]; raw_input[0] = all_feats[0]; raw_input[1] = all_feats[1]; raw_input[2] = all_feats[6]; raw_input[3] = all_feats[7]; raw_input[4] = all_feats[8];
    buffer_head = (buffer_head + 1) % WINDOW_LEN; for(int k=0; k<RAW_FEATURE_COUNT; k++) input_buffer[buffer_head][k] = raw_input[k];
    samples_collected++;
    if (samples_collected >= WINDOW_LEN) runInference(); else lblPrediction->setText(QString("Buffering... %1/%2").arg(samples_collected).arg(WINDOW_LEN));
    loop_count++;
}

int MainWindow::readDHT11(float *temp, float *hum) { int fd = open(DHT11_DEV, O_RDONLY); if (fd < 0) return -1; char tmp[64] = {0}; int ret = -1; if (read(fd, tmp, 63) > 0) { if (sscanf(tmp, "Temp: %f C, Hum: %f %%", temp, hum) == 2) ret = 0; else if(sscanf(tmp, "%f %f", temp, hum) == 2) ret = 0; } ::close(fd); return ret; }
int MainWindow::readBH1750(float *lux) { int fd = open(BH1750_DEV, O_RDONLY); if (fd < 0) return -1; char tmp[32] = {0}; int ret = -1; if (read(fd, tmp, 31) > 0) { *lux = atof(tmp); ret = 0; } ::close(fd); return ret; }
void MainWindow::calcTimeFeatures(time_t t, float *features) { struct tm *tm_info = localtime(&t); float min_of_day = tm_info->tm_hour * 60.0 + tm_info->tm_min; features[0] = sin(2 * M_PI * min_of_day / 1440.0); features[1] = cos(2 * M_PI * min_of_day / 1440.0); features[2] = sin(2 * M_PI * tm_info->tm_wday / 7.0); features[3] = cos(2 * M_PI * tm_info->tm_wday / 7.0); features[4] = sin(2 * M_PI * tm_info->tm_yday / 366.0); features[5] = cos(2 * M_PI * tm_info->tm_yday / 366.0); }

void MainWindow::loadModel() {
    model.reset(); interpreter.reset(); model = tflite::FlatBufferModel::BuildFromFile(MODEL_FILE);
    if (!model) { qDebug() << "ERROR: Model missing..."; lblStatus->setText("Model Error! Recovering..."); static bool is_recovering = false; if (!is_recovering) { is_recovering = true; QtConcurrent::run([=](){ downloadAndInstallModel(); is_recovering = false; }); } return; }
    tflite::ops::builtin::BuiltinOpResolver resolver; tflite::InterpreterBuilder builder(*model, resolver); builder(&interpreter);
    if (!interpreter) { lblStatus->setText("Failed to construct interpreter!"); return; } if (interpreter->AllocateTensors() != kTfLiteOk) { lblStatus->setText("Tensor Alloc Failed!"); return; }
    qDebug() << "Model Loaded Successfully"; lblStatus->setText("Model Loaded.");
}

void MainWindow::runInference() {
    if (!interpreter) return;
    float processed_input[MODEL_INPUT_COUNT];
    for (int col = 0; col < RAW_FEATURE_COUNT; col++) {
        float sum = 0.0f; float min_val = FLT_MAX; float max_val = -FLT_MAX; float ma6_sum = 0.0f;
        for (int i = 0; i < WINDOW_LEN; i++) { int buf_idx = (buffer_head + 1 + i) % WINDOW_LEN; float val = input_buffer[buf_idx][col]; sum += val; if (val < min_val) min_val = val; if (val > max_val) max_val = val; if (i >= WINDOW_LEN - MA_WINDOW) ma6_sum += val; }
        float avg = sum / WINDOW_LEN; float ma6 = ma6_sum / MA_WINDOW; processed_input[col * 4 + 0] = avg; processed_input[col * 4 + 1] = min_val; processed_input[col * 4 + 2] = max_val; processed_input[col * 4 + 3] = ma6;
    }
    int input_idx = interpreter->inputs()[0]; TfLiteTensor* input_tensor = interpreter->tensor(input_idx);
    if (input_tensor->type == kTfLiteInt8) { float scale = input_tensor->params.scale; int32_t zero_point = input_tensor->params.zero_point; int8_t* input_data = interpreter->typed_input_tensor<int8_t>(0); for (int i = 0; i < MODEL_INPUT_COUNT; i++) { float quant_val = (processed_input[i] / scale) + zero_point; if (quant_val > 127) quant_val = 127; if (quant_val < -128) quant_val = -128; input_data[i] = (int8_t)round(quant_val); } }
    else { float* input_data = interpreter->typed_input_tensor<float>(0); for(int i=0; i<MODEL_INPUT_COUNT; i++) input_data[i] = processed_input[i]; }
    if (interpreter->Invoke() != kTfLiteOk) { lblStatus->setText("Inference Failed!"); return; }

    int output_idx = interpreter->outputs()[0]; TfLiteTensor* output_tensor = interpreter->tensor(output_idx); float probs[NUM_LABELS];
    if (output_tensor->type == kTfLiteInt8) { float scale = output_tensor->params.scale; int32_t zero_point = output_tensor->params.zero_point; int8_t* out_data = interpreter->typed_output_tensor<int8_t>(0); for(int i=0; i<NUM_LABELS; i++) probs[i] = (out_data[i] - zero_point) * scale; }
    else { float* out_data = interpreter->typed_output_tensor<float>(0); for(int i=0; i<NUM_LABELS; i++) probs[i] = out_data[i]; }
    int max_idx = 0; for(int i=1; i<NUM_LABELS; i++) if(probs[i] > probs[max_idx]) max_idx = i;
    lblPrediction->setText(QString("%1 (%2%)").arg(LABELS_TEXT[max_idx]).arg(probs[max_idx] * 100, 0, 'f', 1));

    // --- AC TRIGGER LOGIC ---
    // Chỉ kích hoạt nếu trạng thái thay đổi từ Normal sang Anomaly (khác 0)
    // Và độ tin cậy > 60%
    if (max_idx != 0 && lastPredictionIdx == 0) {
        if (probs[max_idx] > 0.6) {
             if (acWidget && acLabel) {
                 acLabel->setText(QString(LABELS_TEXT[max_idx]) + "\nDetected. Turn on AC?");
                 acWidget->setVisible(true);
             }
        }
    }
    // Update trạng thái cũ
    lastPredictionIdx = max_idx;
}
