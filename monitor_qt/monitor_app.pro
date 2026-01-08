QT       += core gui widgets concurrent
TARGET = monitor_app_qt
TEMPLATE = app

# --- QUAN TRỌNG: Dùng C++17 để fix lỗi make_unique và template-id của TFLite ---
CONFIG += c++17

# Link thư viện (Edge Impulse cần libcurl và tflite)
LIBS += -lcurl -ltensorflow-lite -ldl -latomic

SOURCES += main.cpp \
           mainwindow.cpp

HEADERS += mainwindow.h
