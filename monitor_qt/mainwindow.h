#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QTimer>
#include <QProcess>
#include <curl/curl.h>
#include <memory> 
#include <QList>  

#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/model.h"
#include "tensorflow/lite/kernels/register.h"

// --- CẤU HÌNH ---
#define RAW_FEATURE_COUNT 5
#define MODEL_INPUT_COUNT 20
#define WINDOW_LEN 90
#define MA_WINDOW 6
#define INTERVAL_S 10
#define NUM_LABELS 3

#define BUFFER_MAX_SIZE 180      
#define PREDICTION_OFFSET 90     
#define DETECTION_WINDOW 10      
#define THRESHOLD_TEMP_RISE 0.4  
#define THRESHOLD_HUMID_RISE 2.0 
#define THRESHOLD_HUMID_DROP -2.0

struct BufferedSample {
    long timestamp;
    float features[9];
    float temp;
    float humid;
    float lux;
    QString label;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onSettingsClicked();
    void onWifiSettingsClicked();
    void onUpdateModelClicked();
    void onTimerTick();
    void checkWifiState();

private:
    // UI
    QLabel *lblTime;
    QLabel *lblTemp;
    QLabel *lblHum;
    QLabel *lblLux;
    QLabel *lblPrediction;
    QLabel *lblStatus;
    
    // AC Control
    QWidget *acWidget;     
    QLabel *acLabel;       

    QString getLastUploadDate();
    
    // System
    QTimer *timer;
    QTimer *wifiTimer;
    QString lastWifiState;
    time_t start_time;
    int loop_count = 0;
    bool isSystemReady = false;

    // AI Model
    std::unique_ptr<tflite::FlatBufferModel> model;
    std::unique_ptr<tflite::Interpreter> interpreter;

    // AI Logic
    float input_buffer[WINDOW_LEN][RAW_FEATURE_COUNT];
    int buffer_head = 0;
    int samples_collected = 0;
    
    int lastPredictionIdx = 0; // 0 = Normal

    // Data Buffer (Curve Detection)
    QList<BufferedSample> dataBuffer;
    int cooldownTimer = 0;

    // Functions
    void setupUI();
    void updateWifiConfig(QString ssid, QString password);
    int readDHT11(float *temp, float *hum);
    int readBH1750(float *lux);
    void calcTimeFeatures(time_t t, float *features);
    void syncTimeFromInternet();
    void initializeLoggingSession();
    void setLastUploadDate(QString dateStr);
    
    void loadModel();
    void runInference();
    void performUpdateSequence();
    void downloadAndInstallModel();
    void installDownloadedModel();

    int triggerBuildJob();
    bool waitForJob(int job_id, const QString &jobName);
    bool attemptDownload(int retries);
};

#endif // MAINWINDOW_H
