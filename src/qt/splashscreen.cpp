#include "splashscreen.h"
#include "clientversion.h"
#include "util.h"
#include

#include <QApplication>
#include <QPainter>
#include <QDesktopWidget>

SplashScreen::SplashScreen(Qt::WindowFlags f) :
    QWidget(0, f), curAlignment(0)
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
    QString titleText       = tr("MintCoin");
    QString versionText     = QString("Version %1 ").arg(QString::fromStdString(FormatFullVersion()));
    QString copyrightText1   = QChar(0xA9)+QString(" 2009-%1 ").arg(COPYRIGHT_YEAR) + QString(tr("The Bitcoin developers"));
    QString copyrightText2   = QChar(0xA9)+QString(" 2011-%1 ").arg(COPYRIGHT_YEAR) + QString(tr("The MintCoin developers"));

    QString font            = "Arial";

    // load the bitmap for writing some text over it
    pixmap     = QPixmap(":/images/splash");

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
    pixPaint.drawText(pixmap.width()-paddingRight,paddingTop,versionText);

    // draw copyright stuff
    pixPaint.setFont(QFont(font, 10*fontFactor));
    int copy1width = fm.width(copyrightText1);
    int copy2width = fm.width(copyrightText2);
#ifdef Q_OS_MAC
    pixPaint.drawText(pixmap.width()-(copy1width*2)+(copy1width/2),pixmap.height()-line2,copyrightText1);
    pixPaint.drawText(pixmap.width()-(copy2width*2)+(copy2width/2)+15,pixmap.height()-line1,copyrightText2);
#else
    pixPaint.drawText((532/2)-(copy1width/2),pixmap.height()-line2,copyrightText1);
    pixPaint.drawText((532/2)-(copy2width/2),pixmap.height()-line1,copyrightText2);
#endif

    pixPaint.end();

    // Set window title
    //if(isTestNet)
    //    setWindowTitle(titleText + " " + testnetAddText);
    //else
        setWindowTitle(titleText);

    // Resize window and move to center of desktop, disallow resizing
    QRect r(QPoint(), pixmap.size());
    resize(r.size());
    setFixedSize(r.size());
    move(QApplication::desktop()->screenGeometry().center() - r.center());

    subscribeToCoreSignals();
}

SplashScreen::~SplashScreen()
{
    unsubscribeFromCoreSignals();
}

void SplashScreen::slotFinish(QWidget *mainWin)
{
    hide();
    deleteLater();
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

void SplashScreen::showMessage(const QString &message, int alignment, const QColor &color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
}

void SplashScreen::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.drawPixmap(0, 0, pixmap);
    QRect r = rect().adjusted(5, 5, -5, -5);
    painter.setPen(curColor);
    painter.drawText(r, curAlignment, curMessage);
}