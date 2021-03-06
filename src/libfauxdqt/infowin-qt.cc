/*
 * infowin.cc
 * Copyright 2006-2019 Ariadne Conill, Tomasz Moń, Eugene Zagidullin,
 *                     John Lindgren, and Thomas Lange
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <math.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QPushButton>
#include <QTextDocument>
#include <QVBoxLayout>

#include <libfauxdcore/audstrings.h>
#include <libfauxdcore/hook.h>
#include <libfauxdcore/i18n.h>
#include <libfauxdcore/interface.h>
#include <libfauxdcore/playlist.h>
#include <libfauxdcore/probe.h>
#include <libfauxdcore/runtime.h>
#include <libfauxdcore/drct.h>
#include <libfauxdcore/vfs.h>

#include "info-widget.h"
#include "libfauxdqt.h"
#include "libfauxdqt-internal.h"

namespace audqt {

/* This class remedies some of the deficiencies of QLabel (such as lack
 * of proper wrapping).  It can be expanded and/or made more visible if
 * it turns out to be useful outside InfoWindow. */
class TextWidget : public QWidget
{
public:
    TextWidget ()
    {
        m_doc.setDefaultFont (font ());
    }

    void setText (const QString & text)
    {
        m_doc.setPlainText (text);
        updateGeometry ();
    }

    void setWidth (int width)
    {
        m_doc.setTextWidth (width);
        updateGeometry ();
    }

protected:
    QSize sizeHint () const override
    {
        qreal width = m_doc.idealWidth ();
        qreal height = m_doc.size ().height ();
        return QSize (ceil (width), ceil (height));
    }

    QSize minimumSizeHint () const override
        { return sizeHint (); }

    void changeEvent (QEvent * event) override
    {
        if (event->type () == QEvent::FontChange)
        {
            m_doc.setDefaultFont (font ());
            updateGeometry ();
        }
    }

    void paintEvent (QPaintEvent * event) override
    {
        QPainter painter (this);
        m_doc.drawContents (& painter);
    }

private:
    QTextDocument m_doc;
};

class InfoWindow : public QDialog
{
public:
    InfoWindow (QWidget * parent = nullptr);

    void fillInfo (int playlist, int entry, const char * filename, const Tuple & tuple,
     PluginHandle * decoder, bool updating_enabled);

private:
    String m_filename;
    QLabel m_image;
    TextWidget m_uri_label;
    InfoWidget m_infowidget;
    QDialogButtonBox * bbox;

    void displayImage (const char * filename);

    const HookReceiver<InfoWindow, const char *>
     art_hook {"art ready", this, & InfoWindow::displayImage};
};

InfoWindow::InfoWindow (QWidget * parent) : QDialog (parent)
{
    setWindowTitle (_("Song Info"));
    setContentsMargins (margins.TwoPt);

    m_image.setAlignment (Qt::AlignCenter);
    m_uri_label.setWidth (2 * audqt::sizes.OneInch);
    m_uri_label.setContextMenuPolicy (Qt::CustomContextMenu);

    connect (& m_uri_label, & QWidget::customContextMenuRequested, [this] (const QPoint & pos) {
        show_copy_context_menu (this, m_uri_label.mapToGlobal (pos), QString (m_filename));
    });

    auto left_vbox = make_vbox (nullptr);
    left_vbox->addWidget (& m_image);
    left_vbox->addWidget (& m_uri_label);
    left_vbox->setStretch (0, 1);
    left_vbox->setStretch (1, 0);

    auto hbox = make_hbox (nullptr);
    hbox->addLayout (left_vbox);
    hbox->addWidget (& m_infowidget);

    auto vbox = make_vbox (this);
    vbox->addLayout (hbox);

    bbox = new QDialogButtonBox (QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    bbox->button (QDialogButtonBox::Save)->setText (translate_str (N_("_Save")));
    bbox->button (QDialogButtonBox::Close)->setText (translate_str (N_("_Close")));
    vbox->addWidget (bbox);

    m_infowidget.linkEnabled (bbox->button (QDialogButtonBox::Save));

    connect (bbox, & QDialogButtonBox::accepted, [this] () {
        m_infowidget.updateFile ();
        deleteLater ();
    });

    connect (bbox, & QDialogButtonBox::rejected, this, & QObject::deleteLater);
}

void InfoWindow::fillInfo (int playlist, int entry, const char * filename, const Tuple & tuple,
 PluginHandle * decoder, bool updating_enabled)
{
    m_filename = String (filename);
    m_uri_label.setText ((QString) uri_to_display (filename));
    displayImage (filename);
    if (! updating_enabled)
        bbox->button (QDialogButtonBox::Save)->setDisabled (true);
    m_infowidget.fillInfo (playlist, entry, filename, tuple, decoder, updating_enabled);
}

void InfoWindow::displayImage (const char * filename)
{
    if (! strcmp_safe (filename, m_filename))
        m_image.setPixmap (art_request (filename, 2 * sizes.OneInch, 2 * sizes.OneInch));
}

static InfoWindow * s_infowin = nullptr;

static void show_infowin (int playlist, int entry, const char * filename,
 const Tuple & tuple, PluginHandle * decoder, bool can_write)
{
    if (! s_infowin)
    {
        s_infowin = new InfoWindow;
        s_infowin->setAttribute (Qt::WA_DeleteOnClose);

        QObject::connect (s_infowin, & QObject::destroyed, [] () {
            s_infowin = nullptr;
        });
    }

    s_infowin->fillInfo (playlist, entry, filename, tuple, decoder, can_write);
    s_infowin->resize (6 * sizes.OneInch, 3 * sizes.OneInch);
    window_bring_to_front (s_infowin);
}

EXPORT void infowin_show (int playlist, int entry)
{
    String filename = aud_playlist_entry_get_filename (playlist, entry);
    if (! filename)
        return;

    String error;
    PluginHandle * decoder = aud_playlist_entry_get_decoder (playlist, entry,
     Playlist::Wait, & error);
    Tuple tuple = decoder ? aud_playlist_entry_get_tuple (playlist, entry,
     Playlist::Wait, & error) : Tuple ();

    if (aud_drct_get_record_enabled ())  //JWT:SWITCH TO RECORDING FILE, IF RECORDING!:
    {
        filename = aud_get_str ("filewriter", "_record_fid");
        AUDDBG ("-infowin_show: RECORDING FID=%s=\n", (const char *) filename);
        VFSFile file (filename, "r");
        decoder = aud_file_find_decoder (filename, true, file, & error);
    }

    if (decoder && tuple.valid () && ! aud_custom_infowin (filename, decoder))
    {
        /* cuesheet entries cannot be updated */
        bool can_write = aud_file_can_write_tuple (filename, decoder) &&
         ! tuple.is_set (Tuple::StartTime);
        /* JWT:LET 'EM SAVE TO USER'S CONFIG FILE IF CAN'T SAVE TO FILE/STREAM: */
        if (aud_get_bool (nullptr, "user_tag_data") && ! tuple.is_set (Tuple::StartTime))
            can_write = true;

        tuple.delete_fallbacks ();
        show_infowin (playlist, entry, filename, tuple, decoder, can_write);
    }
    else
        infowin_hide ();

    if (error)
        aud_ui_show_error (str_printf (_("Error opening %s:\n%s"),
         (const char *) filename, (const char *) error));
}

EXPORT void infowin_show_current ()
{
    int playlist = aud_playlist_get_playing ();
    int position;

    if (playlist == -1)
        playlist = aud_playlist_get_active ();

    position = aud_playlist_get_position (playlist);

    if (position == -1)
        return;

    infowin_show (playlist, position);
}

EXPORT void infowin_hide ()
{
    delete s_infowin;
}

} // namespace audqt
