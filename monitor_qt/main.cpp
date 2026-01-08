#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    qputenv("QT_IM_MODULE", QByteArray("qtvirtualkeyboard"));
    qputenv("QT_VIRTUALKEYBOARD_STYLE", QByteArray("default"));
    setenv("TZ", "ICT-7", 1); 
    tzset();
    QApplication a(argc, argv);
    
    // Đặt font chữ to hơn cho màn hình cảm ứng embedded
    QFont font = a.font();
    font.setPointSize(12);
    a.setFont(font);

    MainWindow w;
    w.showFullScreen(); // Hiển thị toàn màn hình trên Raspberry Pi

    return a.exec();
}
