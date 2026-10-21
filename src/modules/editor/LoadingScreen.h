#pragma once
#include <QSplashScreen>
#include <QPainter>

class LoadingScreen : public QSplashScreen
{
public:
    LoadingScreen() : QSplashScreen(makeImage()) {}
    void progress(const QString& message)
    {
        showMessage(message, Qt::AlignBottom | Qt::AlignHCenter, Qt::white);
        repaint();
    }
private:
    static QPixmap makeImage()
    {
        QPixmap image(QString(ENGINE_ASSET_DIRECTORY) + "/loading-placeholder.png");
        if (image.isNull()) image.load(QString(ENGINE_ASSET_DIRECTORY) + "/loading-placeholder.svg");
        if (!image.isNull()) return image.scaled(720, 360, Qt::KeepAspectRatioByExpanding);
        image = QPixmap(720, 360);
        image.fill(QColor("#101926"));
        QPainter painter(&image);
        painter.setPen(Qt::white);
        painter.drawText(image.rect(), Qt::AlignCenter, "ELDRIDGE ENGINE\nLoading image placeholder");
        return image;
    }
};
