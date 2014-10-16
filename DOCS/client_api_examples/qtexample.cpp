// License: pick one of: public domain, WTFPL, ISC, Ms-PL, AGPLv3

// This example can be built with: qmake && make

#include <clocale>
#include <sstream>

#include <QFileDialog>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QGridLayout>
#include <QApplication>
#include <QTextEdit>
#include <QJsonDocument>
#include <QPushButton>
#include <QPainter>
#include <QLabel>

#include <mpv/qthelper.hpp>

#include "qtexample.h"

VideoFrame::VideoFrame(QWidget *parent, mpv_handle *a_mpv) :
    QWidget(parent), mpv(a_mpv), osd_frame(NULL), osd_memory(NULL)
{
    osd_frame = new QFrame(this);
    osd_frame->move(0, 0);
    osd_frame->show();

    QBoxLayout *layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    layout->insertWidget(0, osd_frame);

    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setAttribute(Qt::WA_NativeWindow);
    winId();

    int64_t wid = videoWinId();
    mpv_set_option(mpv, "wid", MPV_FORMAT_INT64, &wid);
}

void VideoFrame::paintEvent(QPaintEvent *event)
{
    (void)event;
    // Here we redraw the _sub_ window. The paint event is merely abused to
    // know when we should update the overlay because the sub window may
    // have changed appearance for whatever reasons.
    redrawOsd();
}

void VideoFrame::redrawOsd()
{
    // Do double buffering, because mpv will use the referenced image memory
    // until the overlay is removed or replaced.
    QImage *img = new QImage(osd_frame->size(), QImage::Format_ARGB32_Premultiplied);
    img->fill(Qt::transparent);
    {
      QPainter painter(img);
      osd_frame->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    }
    QVariantList cmd;
    cmd.append("overlay_add");
    // overlay ID - a low positive number, freely chosen by the application
    // It's possible to add multiple overlays by using other IDs.
    cmd.append(0);
    cmd.append(0); // x (top corner of display within the mpv window)
    cmd.append(0); // y
    // File, file descriptor, or address of OSD memory (address in this case)
    cmd.append("&" + QString::number((uintptr_t)(void *)img->bits()));
    // Offset within the OSD file (unused here, useful for file mappings)
    cmd.append(0);
    // Pixel format; corresponds to QImage::Format_ARGB32_Premultiplied
    cmd.append("bgra");
    // Width/height/stride
    cmd.append(img->width());
    cmd.append(img->height());
    cmd.append(img->bytesPerLine());

    mpv::qt::command_variant(mpv, cmd);

    delete osd_memory;
    osd_memory = img;
}

VideoFrame::~VideoFrame()
{
    // First, absolutely make sure mpv is not using the image data anymore.
    QVariantList cmd;
    cmd.append("overlay_remove");
    cmd.append(0); // overlay ID
    mpv::qt::command_variant(mpv, cmd);

    delete osd_frame;
    delete osd_memory;
}

static void wakeup(void *ctx)
{
    // This callback is invoked from any mpv thread (but possibly also
    // recursively from a thread that is calling the mpv API). Just notify
    // the Qt GUI thread to wake up (so that it can process events with
    // mpv_wait_event()), and return as quickly as possible.
    MainWindow *mainwindow = (MainWindow *)ctx;
    QCoreApplication::postEvent(mainwindow, new QEvent(QEvent::User));
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent), mpv_container(0), mpv(0), log(0), video_frame(0)
{
    setWindowTitle("Qt embedding demo");
    setMinimumSize(640, 480);

    QMenu *menu = menuBar()->addMenu(tr("&File"));
    QAction *on_open = new QAction(tr("&Open"), this);
    on_open->setShortcuts(QKeySequence::Open);
    on_open->setStatusTip(tr("Open a file"));
    connect(on_open, SIGNAL(triggered()), this, SLOT(on_file_open()));
    menu->addAction(on_open);

    statusBar();

    QMainWindow *log_window = new QMainWindow(this);
    log = new QTextEdit(log_window);
    log->setReadOnly(true);
    log_window->setCentralWidget(log);
    log_window->setWindowTitle("mpv log window");
    log_window->setMinimumSize(500, 50);
    log_window->show();

    mpv = mpv_create();
    if (!mpv)
        throw "can't create mpv instance";

    video_frame = new VideoFrame(this, mpv);

    setCentralWidget(video_frame);
    video_frame->setMinimumSize(640, 480);
    video_frame->show();

    QPushButton *butt = new QPushButton(video_frame->osdFrame());
    butt->setText("hi");
    butt->setToolTip("this is a tooltip");
    butt->move(100, 200);
    butt->show();
    connect(butt, SIGNAL(clicked()), this, SLOT(on_click()));

    QLabel *d = new QLabel(video_frame->osdFrame());
    d->move(50, 50);
    d->setMinimumSize(100, 100);
    d->setStyleSheet("background-color: rgba(0,120,0,50);");
    d->setText("hi!");
    d->show();

    /*
    ---- normal code, commented for the sake of VideoFrame
    // Create a video child window. Force Qt to create a native window, and
    // pass the window ID to the mpv wid option. This doesn't work on OSX,
    // because Cocoa doesn't support this form of embedding.
    mpv_container->setAttribute(Qt::WA_DontCreateNativeAncestors);
    mpv_container->setAttribute(Qt::WA_NativeWindow);
    // If you have a HWND, use: int64_t wid = (intptr_t)hwnd;
    //int64_t wid = mpv_container->winId();
    //mpv_set_option(mpv, "wid", MPV_FORMAT_INT64, &wid);
    */

    // Enable default bindings, because we're lazy. Normally, a player using
    // mpv as backend would implement its own key bindings.
    mpv_set_option_string(mpv, "input-default-bindings", "yes");

    // Enable keyboard input on the X11 window. For the messy details, see
    // --input-vo-keyboard on the manpage.
    mpv_set_option_string(mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(mpv, "input-cursor", "no");
    mpv_set_option_string(mpv, "cursor-autohide", "no");

    // Let us receive property change events with MPV_EVENT_PROPERTY_CHANGE if
    // this property changes.
    mpv_observe_property(mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);

    mpv_observe_property(mpv, 0, "track-list", MPV_FORMAT_NODE);
    mpv_observe_property(mpv, 0, "chapter-list", MPV_FORMAT_NODE);

    // Request log messages with level "info" or higher.
    // They are received as MPV_EVENT_LOG_MESSAGE.
    mpv_request_log_messages(mpv, "info");

    // From this point on, the wakeup function will be called. The callback
    // can come from any thread, so we use the Qt QEvent mechanism to relay
    // the wakeup in a thread-safe way.
    mpv_set_wakeup_callback(mpv, wakeup, this);

    if (mpv_initialize(mpv) < 0)
        throw "mpv failed to initialize";
}

void MainWindow::handle_mpv_event(mpv_event *event)
{
    switch (event->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        mpv_event_property *prop = (mpv_event_property *)event->data;
        if (strcmp(prop->name, "time-pos") == 0) {
            if (prop->format == MPV_FORMAT_DOUBLE) {
                double time = *(double *)prop->data;
                std::stringstream ss;
                ss << "At: " << time;
                statusBar()->showMessage(QString::fromStdString(ss.str()));
            } else if (prop->format == MPV_FORMAT_NONE) {
                // The property is unavailable, which probably means playback
                // was stopped.
                statusBar()->showMessage("");
            }
        } else if (strcmp(prop->name, "chapter-list") == 0 ||
                   strcmp(prop->name, "track-list") == 0)
        {
            if (prop->format == MPV_FORMAT_NODE) {
                QVariant v = mpv::qt::node_to_variant((mpv_node *)prop->data);
                // Abuse JSON support for easily printing the mpv_node contents.
                QJsonDocument d = QJsonDocument::fromVariant(v);
                append_log("Change property " + QString(prop->name) + ":\n");
                append_log(d.toJson().data());
            }
        }
        break;
    }
    case MPV_EVENT_VIDEO_RECONFIG: {
        // Retrieve the new video size.
        int64_t w, h;
        if (mpv_get_property(mpv, "dwidth", MPV_FORMAT_INT64, &w) >= 0 &&
            mpv_get_property(mpv, "dheight", MPV_FORMAT_INT64, &h) >= 0 &&
            w > 0 && h > 0)
        {
            // Note that the MPV_EVENT_VIDEO_RECONFIG event doesn't necessarily
            // imply a resize, and you should check yourself if the video
            // dimensions really changed.
            // mpv itself will scale/letter box the video to the container size
            // if the video doesn't fit.
            std::stringstream ss;
            ss << "Reconfig: " << w << " " << h;
            statusBar()->showMessage(QString::fromStdString(ss.str()));
        }
        break;
    }
    case MPV_EVENT_LOG_MESSAGE: {
        struct mpv_event_log_message *msg = (struct mpv_event_log_message *)event->data;
        std::stringstream ss;
        ss << "[" << msg->prefix << "] " << msg->level << ": " << msg->text;
        append_log(QString::fromStdString(ss.str()));
        break;
    }
    case MPV_EVENT_SHUTDOWN: {
        delete video_frame;
        video_frame = NULL;
        mpv_terminate_destroy(mpv);
        mpv = NULL;
        break;
    }
    default: ;
        // Ignore uninteresting or unknown events.
    }
}

bool MainWindow::event(QEvent *event)
{
    // QEvent::User is sent by wakeup().
    if (event->type() == QEvent::User) {
        // Process all events, until the event queue is empty.
        while (mpv) {
            mpv_event *event = mpv_wait_event(mpv, 0);
            if (event->event_id == MPV_EVENT_NONE)
                break;
            handle_mpv_event(event);
        }
        return true;
    }
    return QMainWindow::event(event);
}

void MainWindow::on_file_open()
{
    QString filename = QFileDialog::getOpenFileName(this, "Open file");
    if (mpv) {
        const QByteArray c_filename = filename.toUtf8();
        const char *args[] = {"loadfile", c_filename.data(), NULL};
        mpv_command_async(mpv, 0, args);
    }
}

void MainWindow::on_click()
{
    printf("whoo!\n");
}

void MainWindow::append_log(const QString &text)
{
    QTextCursor cursor = log->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    log->setTextCursor(cursor);
}

MainWindow::~MainWindow()
{
    delete video_frame;
    if (mpv)
        mpv_terminate_destroy(mpv);
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Qt sets the locale in the QApplication constructor, but libmpv requires
    // the LC_NUMERIC category to be set to "C", so change it back.
    std::setlocale(LC_NUMERIC, "C");

    MainWindow w;
    w.show();

    return a.exec();
}
