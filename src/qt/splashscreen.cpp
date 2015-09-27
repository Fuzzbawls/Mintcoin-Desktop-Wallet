#include "splashscreen.h"
#include "clientversion.h"
#include "util.h"
#include

#include <QPainter>
#include <QApplication>

SplashScreen::SplashScreen(const QPixmap &pixmap, Qt::WindowFlags f) :
    QSplashScreen(pixmap, f)
{
    // set reference point, paddings
    int paddingRight            = 120;
    int paddingTop              = 25;
    int line1 = 30;
    int line2 = 40;

    float fontFactor            = 1.0;
    float devicePixelRatio      = 1.0;
#if QT_VERSION > 0x050100
    devicePixelRatio = ((QGuiApplication*)QCoreApplication::instance())->devicePixelRatio();
#endif

    // define text to place
    QString titleText       = QString(QApplication::applicationName()).replace(QString("-testnet"), QString(""), Qt::CaseSensitive); // cut of testnet, place it as single object further down
    QString versionText     = QString("Version %1 ").arg(QString::fromStdString(FormatFullVersion()));
    QString copyrightText1   = QChar(0xA9)+QString(" 2009-%1 ").arg(COPYRIGHT_YEAR) + QString(tr("The Bitcoin developers"));
    QString copyrightText2   = QChar(0xA9)+QString(" 2011-%1 ").arg(COPYRIGHT_YEAR) + QString(tr("The MintCoin developers"));

    QString font            = "Arial";

    // load the bitmap for writing some text over it
    QPixmap newPixmap;
    newPixmap     = QPixmap(":/images/splash");

    QPainter pixPaint(&newPixmap);
    pixPaint.setPen(QColor(70,70,70));
    
	// check font size and drawing with
    QFontMetrics fm = pixPaint.fontMetrics();
    pixPaint.setFont(QFont(font, 14*fontFactor));
    int versionWidth = fm.width(versionText);
    if(versionWidth > 160) {
        fontFactor = 0.75;
    }

    pixPaint.setFont(QFont(font, 14*fontFactor));
    fm = pixPaint.fontMetrics();
    versionWidth = fm.width(versionText);
    pixPaint.drawText(newPixmap.width()-paddingRight,paddingTop,versionText);

    // draw copyright stuff
    pixPaint.setFont(QFont(font, 10*fontFactor));
    int copy1width = fm.width(copyrightText1);
    int copy2width = fm.width(copyrightText2);
#ifdef Q_OS_MAC
    pixPaint.drawText(newPixmap.width()-(copy1width*2)+(copy1width/2),newPixmap.height()-line2,copyrightText1);
    pixPaint.drawText(newPixmap.width()-(copy2width*2)+(copy2width/2)+15,newPixmap.height()-line1,copyrightText2);
#else
    pixPaint.drawText((532/2)-(copy1width/2),newPixmap.height()-line2,copyrightText1);
    pixPaint.drawText((532/2)-(copy2width/2),newPixmap.height()-line1,copyrightText2);
#endif

    pixPaint.end();

    this->setPixmap(newPixmap);

    subscribeToCoreSignals();
}

SplashScreen::~SplashScreen()
{
    unsubscribeFromCoreSignals();
}

void SplashScreen::slotFinish(QWidget *mainWin)
{
    finish(mainWin);
}

static void InitMessage(SplashScreen *splash, const std::string &message)
{
    QMetaObject::invokeMethod(splash, "showMessage",
        Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(int, Qt::AlignBottom|Qt::AlignHCenter),
        Q_ARG(QColor, QColor(55,55,55)));
}

void SplashScreen::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.InitMessage.connect(boost::bind(InitMessage, this, _1));
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.InitMessage.disconnect(boost::bind(InitMessage, this, _1));
}