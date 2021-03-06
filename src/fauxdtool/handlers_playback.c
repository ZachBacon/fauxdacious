/*
 * handlers_playback.c
 * Copyright 2005-2013 George Averill, William Pitcock, Matti Hämäläinen, and
 *                     John Lindgren
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

#include <stdlib.h>

#include "fauxdtool.h"

void playback_play (int argc, char * * argv)
{
    obj_fauxdacious_call_play_sync (dbus_proxy, NULL, NULL);
}

void playback_pause (int argc, char * * argv)
{
    gboolean paused = FALSE;  /* JWT:NEXT 3 ADDED TO ADDRESS AUDACIOUS-REJECTED BUG#681. */
    obj_fauxdacious_call_paused_sync (dbus_proxy, & paused, NULL, NULL);
    if (! paused)
        obj_fauxdacious_call_pause_sync (dbus_proxy, NULL, NULL);
}

void playback_playpause (int argc, char * * argv)
{
    obj_fauxdacious_call_play_pause_sync (dbus_proxy, NULL, NULL);
}

void playback_stop (int argc, char * * argv)
{
    obj_fauxdacious_call_stop_sync (dbus_proxy, NULL, NULL);
}

void playback_playing (int argc, char * * argv)
{
    gboolean playing = FALSE;
    obj_fauxdacious_call_playing_sync (dbus_proxy, & playing, NULL, NULL);

    exit (! playing);
}

void playback_paused (int argc, char * * argv)
{
    gboolean paused = FALSE;
    obj_fauxdacious_call_paused_sync (dbus_proxy, & paused, NULL, NULL);

    exit (! paused);
}

void playback_stopped (int argc, char * * argv)
{
    gboolean stopped = FALSE;
    obj_fauxdacious_call_stopped_sync (dbus_proxy, & stopped, NULL, NULL);

    exit (! stopped);
}

void playback_status (int argc, char * * argv)
{
    char * status = NULL;
    obj_fauxdacious_call_status_sync (dbus_proxy, & status, NULL, NULL);

    if (! status)
        exit (1);

    fauxdtool_report ("%s", status);
    g_free (status);
}

void playback_seek (int argc, char * * argv)
{
    if (argc < 2)
    {
        fauxdtool_whine_args (argv[0], "<position>");
        exit (1);
    }

    obj_fauxdacious_call_seek_sync (dbus_proxy, MAX (0, atof (argv[1]) * 1000), NULL, NULL);
}

void playback_seek_relative (int argc, char * * argv)
{
    if (argc < 2)
    {
        fauxdtool_whine_args (argv[0], "<position>");
        exit (1);
    }

    unsigned oldtime = 0;
    obj_fauxdacious_call_time_sync (dbus_proxy, & oldtime, NULL, NULL);
    obj_fauxdacious_call_seek_sync (dbus_proxy, MAX (0, oldtime + atof (argv[1]) * 1000), NULL, NULL);
}

void playback_record (int argc, char * * argv)
{
    obj_fauxdacious_call_record_sync (dbus_proxy, NULL, NULL);
}

void playback_recording (int argc, char * * argv)
{
    gboolean recording = FALSE;
    obj_fauxdacious_call_recording_sync (dbus_proxy, & recording, NULL, NULL);

    exit (! recording);
}

void playback_setpausemute (int argc, char * * argv)
{
    gboolean pausemute = (argc < 2 || argv[1][0] == '1' || argv[1][0] == 't' ||
            argv[1][0] == 'T' || argv[1][0] == 'y' || argv[1][0] == 'Y') ? TRUE : FALSE;
    obj_fauxdacious_call_set_pausemute_sync (dbus_proxy, pausemute, NULL, NULL);
}

void playback_getpausemute (int argc, char * * argv)
{
    gboolean pausemute = FALSE;
    obj_fauxdacious_call_get_pausemute_sync (dbus_proxy, & pausemute, NULL, NULL);

    exit (pausemute);
}

