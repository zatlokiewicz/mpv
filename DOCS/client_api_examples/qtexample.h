#ifndef QTEXAMPLE_H
#define QTEXAMPLE_H

#include <QMainWindow>
#include <QFrame>
#include <QEvent>

#include <mpv/client.h>

class QTextEdit;

class VideoFrame : public QWidget
{
    Q_OBJECT

public:
    explicit VideoFrame(QWidget *parent, mpv_handle *mpv);
    ~VideoFrame();
    QFrame *osdFrame() { return osd_frame; }
    WId videoWinId() { return winId(); }
protected:
    virtual void paintEvent(QPaintEvent *event);
private:
    void redrawOsd();

    mpv_handle *mpv;
    QFrame *osd_frame;
    QImage *osd_memory;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

protected:
    virtual bool event(QEvent *event);

private slots:
    void on_file_open();
    void on_click();

private:
    QWidget *mpv_container;
    mpv_handle *mpv;
    QTextEdit *log;
    VideoFrame *video_frame;

    void append_log(const QString &text);

    void create_player();
    void handle_mpv_event(mpv_event *event);
};

#endif // QTEXAMPLE_H
