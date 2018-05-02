/*
 * adder.c
 * Copyright 2011-2016 John Lindgren
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

#include "playlist-internal.h"
#include "internal.h"

#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "audstrings.h"
#include "hook.h"
#include "i18n.h"
#include "list.h"
#include "mainloop.h"
#include "plugins-internal.h"
#include "probe.h"
#include "runtime.h"
#include "tuple.h"
#include "interface.h"
#include "vfs.h"

// regrettably, strcmp_nocase can't be used directly as a
// callback for Index::sort due to taking a third argument;
// strcmp also triggers -Wnoexcept-type with GCC 7
static int filename_compare (const char * a, const char * b)
#ifdef _WIN32
    { return strcmp_nocase (a, b); }
#else
    { return strcmp (a, b); }
#endif

struct AddTask : public ListNode
{
    int playlist_id, at;
    bool play;
    Index<PlaylistAddItem> items;
    PlaylistFilterFunc filter;
    void * user;
};

struct AddResult : public ListNode
{
    int playlist_id, at;
    bool play;
    String title;
    Index<PlaylistAddItem> items;
    bool saw_folder, filtered;
};

static void * add_worker (void * unused);

static List<AddTask> add_tasks;
static List<AddResult> add_results;
static int current_playlist_id = -1;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static bool add_thread_started = false;
static bool add_thread_exited = false;
static pthread_t add_thread;
static QueuedFunc queued_add;
static QueuedFunc status_timer;

static char status_path[512];
static int status_count;
static bool status_shown = false;

static void status_cb (void * unused)
{
    pthread_mutex_lock (& mutex);

    char scratch[128];
    snprintf (scratch, sizeof scratch, dngettext (PACKAGE, "%d file found",
     "%d files found", status_count), status_count);

    if (aud_get_headless_mode ())
    {
        printf ("Searching, %s ...\r", scratch);
        fflush (stdout);
    }
    else
    {
        hook_call ("ui show progress", status_path);
        hook_call ("ui show progress 2", scratch);
    }

    status_shown = true;

    pthread_mutex_unlock (& mutex);
}

static void status_update (const char * filename, int found)
{
    pthread_mutex_lock (& mutex);

    snprintf (status_path, sizeof status_path, "%s", filename);
    status_count = found;

    if (! status_timer.running ())
        status_timer.start (250, status_cb, nullptr);

    pthread_mutex_unlock (& mutex);
}

static void status_done_locked ()
{
    status_timer.stop ();

    if (status_shown)
    {
        if (aud_get_headless_mode ())
            printf ("\n");
        else
            hook_call ("ui hide progress", nullptr);

        status_shown = false;
    }
}

static void add_file (PlaylistAddItem && item, PlaylistFilterFunc filter,
 void * user, AddResult * result, bool skip_invalid)
{
    AUDINFO ("Adding file: %s\n", (const char *) item.filename);
    status_update (item.filename, result->items.len ());

    /*
     * If possible, we'll wait until the file is added to the playlist to probe
     * it.  There are a couple of reasons why we might need to probe it now:
     *
     * 1. We're adding a folder, and need to skip over non-audio files (the
     *    "skip invalid" flag indicates this case).
     * 2. The file might have subtunes, which we need to expand in order to add
     *    them to the playlist correctly.
     *
     * If we already have metadata, or the file is itself a subtune, then
     * neither of these reasons apply.
     */
    if (! item.tuple.valid () && ! is_subtune (item.filename))
    {
        /* JWT:ADDED TO HANDLE SPECIAL YOUTUBE-DL CASE: */
        if (strstr(item.filename, "ytdl://"))
        {
    	       String metadata_helper = aud_get_str("youtubedl", "metadata_helper");
    	       if (metadata_helper[0])
            {
                Tuple file_tuple = Tuple ();
                if (! aud_read_tag_from_tagfile (item.filename, "tmp_tag_data", file_tuple))
                {
                    StringBuf tagdata_filename = filename_build ({aud_get_path (AudPath::UserDir), "tmp_tag_data"});
                    AUDDBG ("i:invoking metadata helper=%s=\n", (const char *) str_concat ({metadata_helper, " ", item.filename, " ", tagdata_filename}));
                    system ((const char *) str_concat ({metadata_helper, " ", item.filename, " ", tagdata_filename}));
                }
            }
            else
                aud_set_bool (nullptr, "youtubedl_tag_data", false);  /* NO YOUTUBE-DL TAG DATA FILE W/O HELPER! */
        }
        /* If we open the file to identify the decoder, we can re-use the same
         * handle to read metadata. */
        VFSFile file;

        if (! item.decoder)
        {
            if (aud_get_bool (nullptr, "slow_probe"))
            {
                /* The slow path.  User settings dictate that we should try to
                 * find a decoder even if we don't recognize the file extension. */
                item.decoder = aud_file_find_decoder (item.filename, false, file);
                if (skip_invalid && ! item.decoder)
                    return;
            }
            else
            {
                /* The fast path.  First see whether any plugins recognize the
                 * file extension.  Note that it's possible for multiple plugins
                 * to recognize the same extension (.ogg, for example). */
                int flags = probe_by_filename (item.filename);
                if (skip_invalid && ! (flags & PROBE_FLAG_HAS_DECODER))
                    return;

                if ((flags & PROBE_FLAG_MIGHT_HAVE_SUBTUNES))
                {
                    /* At least one plugin recognized the file extension and
                     * indicated that there might be subtunes.  Figure out for
                     * sure which decoder we need to use for this file. */
                    item.decoder = aud_file_find_decoder (item.filename, true, file);
                    if (skip_invalid && ! item.decoder)
                        return;
                }
            }
        }

        /* At this point we've either identified the decoder or determined that
         * the file doesn't have any subtunes.  If the former, read the tag so
         * so we can expand any subtunes we find. */
        if (item.decoder && input_plugin_has_subtunes (item.decoder))
            aud_file_read_tag (item.filename, item.decoder, file, item.tuple);
    }

    int n_subtunes = item.tuple.get_n_subtunes ();

    if (n_subtunes)
    {
        for (int sub = 0; sub < n_subtunes; sub ++)
        {
            StringBuf subname = str_printf ("%s?%d",
             (const char *) item.filename, item.tuple.get_nth_subtune (sub));

            if (! filter || filter (subname, user))
                add_file ({String (subname), Tuple (), item.decoder}, filter, user, result, false);
            else
                result->filtered = true;
        }
    }
    else
        result->items.append (std::move (item));
}

/* To prevent infinite recursion, we currently allow adding a folder from within
 * a playlist, but not a playlist from within a folder, nor a second playlist
 * from within a playlist (this last rule is enforced by setting
 * <from_playlist> to true from within add_playlist()). */
static void add_generic (PlaylistAddItem && item, PlaylistFilterFunc filter,
 void * user, AddResult * result, bool save_title, bool from_playlist);

static void add_playlist (const char * filename, PlaylistFilterFunc filter,
 void * user, AddResult * result, bool save_title)
{
    AUDINFO ("Adding playlist: %s\n", filename);
    status_update (filename, result->items.len ());

    String title;
    Index<PlaylistAddItem> items;

    if (! playlist_load (filename, title, items))
        return;

    if (save_title)
        result->title = title;

    for (auto & item : items)
        add_generic (std::move (item), filter, user, result, false, true);
}

static void add_cuesheets (Index<String> & files, PlaylistFilterFunc filter,
 void * user, AddResult * result)
{
    Index<String> cuesheets;

    for (int i = 0; i < files.len ();)
    {
        if (str_has_suffix_nocase (files[i], ".cue"))
            cuesheets.move_from (files, i, -1, 1, true, true);
        else
            i ++;
    }

    if (! cuesheets.len ())
        return;

    // sort cuesheet list in natural order
    cuesheets.sort (str_compare_encoded);

    // sort file list in system-dependent order for duplicate removal
    files.sort (filename_compare);

    for (String & cuesheet : cuesheets)
    {
        AUDINFO ("Adding cuesheet: %s\n", (const char *) cuesheet);
        status_update (cuesheet, result->items.len ());

        String title; // ignored
        Index<PlaylistAddItem> items;

        if (! playlist_load (cuesheet, title, items))
            continue;

        String prev_filename;
        for (auto & item : items)
        {
            String filename = item.tuple.get_str (Tuple::AudioFile);
            if (! filename)
                continue; // shouldn't happen

            if (! filter || filter (item.filename, user))
                add_file (std::move (item), filter, user, result, false);
            else
                result->filtered = true;

            // remove duplicates from file list
            if (prev_filename && ! filename_compare (filename, prev_filename))
                continue;

            int idx = files.bsearch ((const char *) filename, filename_compare);
            if (idx >= 0)
                files.remove (idx, 1);

            prev_filename = std::move (filename);
        }
    }
}

static void add_folder (const char * filename, PlaylistFilterFunc filter,
 void * user, AddResult * result, bool save_title)
{
    AUDINFO ("Adding folder: %s\n", filename);
    status_update (filename, result->items.len ());

    String error;
    Index<String> files = VFSFile::read_folder (filename, error);

    if (error)
        aud_ui_show_error (str_printf (_("Error reading %s:\n%s"), filename, (const char *) error));

    if (! files.len ())
        return;

    if (save_title)
    {
        const char * slash = strrchr (filename, '/');
        if (slash)
            result->title = String (str_decode_percent (slash + 1));
    }

    add_cuesheets (files, filter, user, result);

    // sort file list in natural order (must come after add_cuesheets)
    files.sort (str_compare_encoded);

    bool recurse_folders = aud_get_bool (nullptr, "recurse_folders");

    for (const char * file : files)
    {
        if (filter && ! filter (file, user))
        {
            result->filtered = true;
            continue;
        }

        String error;
        VFSFileTest mode = VFSFile::test_file (file,
         VFSFileTest (VFS_IS_REGULAR | VFS_IS_SYMLINK | VFS_IS_DIR), error);

        if (error)
            AUDERR ("%s: %s\n", file, (const char *) error);

        if (mode & VFS_IS_SYMLINK)
            continue;

        if (mode & VFS_IS_REGULAR)
            add_file ({String (file)}, filter, user, result, true);
        else if (recurse_folders && (mode & VFS_IS_DIR))
            add_folder (file, filter, user, result, false);
    }
}

static void add_generic (PlaylistAddItem && item, PlaylistFilterFunc filter,
 void * user, AddResult * result, bool save_title, bool from_playlist)
{
    if (! strstr (item.filename, "://"))
    {
        /* Let's not add random junk to the playlist. */
        AUDERR ("Invalid URI: %s\n", (const char *) item.filename);
        return;
    }

    if (filter && ! filter (item.filename, user))
    {
        result->filtered = true;
        return;
    }

    /* If the item has a valid tuple or known decoder, or it's a subtune, then
     * assume it's a playable file and skip some checks. */
    if (item.tuple.valid () || item.decoder || is_subtune (item.filename))
        add_file (std::move (item), filter, user, result, false);
    else
    {
        int tests = 0;
        if (! from_playlist)
            tests |= VFS_NO_ACCESS;
        tests |= VFS_IS_DIR;

        String error;
        VFSFileTest mode = VFSFile::test_file (item.filename, (VFSFileTest) tests, error);
        if ((mode & VFS_NO_ACCESS))
            aud_ui_show_error (str_printf (_("Error reading %s:\n%s"),
             (const char *) item.filename, (const char *) error));
        else if (mode & VFS_IS_DIR)
        {
            if (! from_playlist || aud_get_bool (nullptr, "folders_in_playlist"))
                add_folder (item.filename, filter, user, result, save_title);
            result->saw_folder = true;
        }
        else if ((! from_playlist) && aud_filename_is_playlist (item.filename))
            add_playlist (item.filename, filter, user, result, save_title);
        else
            add_file (std::move (item), filter, user, result, false);
    }
}

static void start_thread_locked ()
{
    if (add_thread_exited)
    {
        pthread_mutex_unlock (& mutex);
        pthread_join (add_thread, nullptr);
        pthread_mutex_lock (& mutex);
    }

    if (! add_thread_started || add_thread_exited)
    {
        pthread_create (& add_thread, nullptr, add_worker, nullptr);
        add_thread_started = true;
        add_thread_exited = false;
    }
}

static void stop_thread_locked ()
{
    if (add_thread_started)
    {
        pthread_mutex_unlock (& mutex);
        pthread_join (add_thread, nullptr);
        pthread_mutex_lock (& mutex);
        add_thread_started = false;
        add_thread_exited = false;
    }
}

static void add_finish (void * unused)
{
    pthread_mutex_lock (& mutex);

    AddResult * result;
    while ((result = add_results.head ()))
    {
        add_results.remove (result);

        int playlist, count;

        if (! result->items.len ())
        {
            if (result->saw_folder && ! result->filtered)
                aud_ui_show_error (_("No files found."));
            goto FREE;
        }

        playlist = aud_playlist_by_unique_id (result->playlist_id);
        if (playlist < 0) /* playlist deleted */
            goto FREE;

        if (result->play)
        {
            if (aud_get_bool (nullptr, "clear_playlist"))
                aud_playlist_entry_delete (playlist, 0, aud_playlist_entry_count (playlist));
            else
                aud_playlist_queue_delete (playlist, 0, aud_playlist_queue_count (playlist));
        }

        count = aud_playlist_entry_count (playlist);
        if (result->at < 0 || result->at > count)
            result->at = count;

        if (result->title && ! count)
        {
            String old_title = aud_playlist_get_title (playlist);

/* JWT:REMOVED TO PLAYLIST FILES LOADED BY THEMSELVES WILL NAME THE PLAYLIST TAB!
            if (! strcmp (old_title, N_("New Playlist")))
*/
                aud_playlist_set_title (playlist, result->title);
        }

        /* temporarily disable scanning this playlist; the intent is to avoid
         * scanning until the currently playing entry is known, at which time it
         * can be scanned more efficiently (album art read in the same pass). */
        playlist_enable_scan (false);
        playlist_entry_insert_batch_raw (playlist, result->at, std::move (result->items));

        if (result->play)
        {
            if (! aud_get_bool (0, "shuffle"))
                aud_playlist_set_position (playlist, result->at);

            aud_playlist_play (playlist);
        }

        playlist_enable_scan (true);

    FREE:
        delete result;
    }

    if (add_thread_exited)
    {
        stop_thread_locked ();
        status_done_locked ();
    }

    pthread_mutex_unlock (& mutex);

    hook_call ("playlist add complete", nullptr);
}

static void * add_worker (void * unused)
{
    pthread_mutex_lock (& mutex);

    AddTask * task;
    while ((task = add_tasks.head ()))
    {
        add_tasks.remove (task);

        current_playlist_id = task->playlist_id;
        pthread_mutex_unlock (& mutex);

        playlist_cache_load (task->items);

        AddResult * result = new AddResult ();

        result->playlist_id = task->playlist_id;
        result->at = task->at;
        result->play = task->play;

        bool save_title = (task->items.len () == 1);

        for (auto & item : task->items)
            add_generic (std::move (item), task->filter, task->user, result, save_title, false);

        delete task;

        pthread_mutex_lock (& mutex);
        current_playlist_id = -1;

        if (! add_results.head ())
            queued_add.queue (add_finish, nullptr);

        add_results.append (result);
    }

    add_thread_exited = true;
    pthread_mutex_unlock (& mutex);
    return nullptr;
}

void adder_cleanup ()
{
    pthread_mutex_lock (& mutex);

    add_tasks.clear ();

    stop_thread_locked ();
    status_done_locked ();

    add_results.clear ();

    queued_add.stop ();

    pthread_mutex_unlock (& mutex);
}

EXPORT void aud_playlist_entry_insert (int playlist, int at,
 const char * filename, Tuple && tuple, bool play)
{
    Index<PlaylistAddItem> items;
    items.append (String (filename), std::move (tuple));

    aud_playlist_entry_insert_batch (playlist, at, std::move (items), play);
}

EXPORT void aud_playlist_entry_insert_batch (int playlist, int at,
 Index<PlaylistAddItem> && items, bool play)
{
    aud_playlist_entry_insert_filtered (playlist, at, std::move (items), nullptr, nullptr, play);
}

EXPORT void aud_playlist_entry_insert_filtered (int playlist, int at,
 Index<PlaylistAddItem> && items, PlaylistFilterFunc filter, void * user,
 bool play)
{
    int playlist_id = aud_playlist_get_unique_id (playlist);

    pthread_mutex_lock (& mutex);

    AddTask * task = new AddTask ();

    task->playlist_id = playlist_id;
    task->at = at;
    task->play = play;
    task->items = std::move (items);
    task->filter = filter;
    task->user = user;

    add_tasks.append (task);
    start_thread_locked ();

    pthread_mutex_unlock (& mutex);
}

EXPORT bool aud_playlist_add_in_progress (int playlist)
{
    pthread_mutex_lock (& mutex);

    if (playlist >= 0)
    {
        int playlist_id = aud_playlist_get_unique_id (playlist);

        for (AddTask * task = add_tasks.head (); task; task = add_tasks.next (task))
        {
            if (task->playlist_id == playlist_id)
                goto YES;
        }

        if (current_playlist_id == playlist_id)
            goto YES;

        for (AddResult * result = add_results.head (); result; result = add_results.next (result))
        {
            if (result->playlist_id == playlist_id)
                goto YES;
        }
    }
    else
    {
        if (add_tasks.head () || current_playlist_id >= 0 || add_results.head ())
            goto YES;
    }

    pthread_mutex_unlock (& mutex);
    return false;

YES:
    pthread_mutex_unlock (& mutex);
    return true;
}
