#ifndef SPLASHSCREEN_H
#define SPLASHSCREEN_H

#include <QSplashScreen>

/** class for the splashscreen with information of the running client
 */
class SplashScreen : public QSplashScreen
{
    Q_OBJECT

public:
    explicit SplashScreen(const QPixmap &pixmap = QPixmap(), Qt::WindowFlags f = 0);
    ~SplashScreen();

    public slots:
        /** Slot to call finish() method as it's not defined as slot */
        void slotFinish(QWidget *mainWin);

    private:
        /** Connect core signals to splash screen */
        void subscribeToCoreSignals();
        /** Disconnect core signals to splash screen */
        void unsubscribeFromCoreSignals();
};

#endif // SPLASHSCREEN_H
