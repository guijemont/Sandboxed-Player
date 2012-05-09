/*
 * Copyright (C) 2012 Igalia S.L.
*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SCETION:element-sandboxeddecodebin
 *
 * DOCME
 */

#include <gst/gst.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include <sys/mman.h>

#include "gstsandboxeddecodebin.h"
#include "../config.h"

GST_DEBUG_CATEGORY_STATIC (gst_debug_sandboxed_decodebin);
#define GST_CAT_DEFAULT gst_debug_sandboxed_decodebin

G_DEFINE_TYPE (GstSandboxedDecodebin, gst_sandboxed_decodebin, GST_TYPE_BIN);

#define GST_SANDBOXED_DECODEBIN_GET_PRIVATE(o)\
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_SANDBOXED_DECODEBIN_TYPE, GstSandboxedDecodebinPrivate))

#define DECODER_PATH "gst-decoder"

#define AUDIO_SOCKET 0
#define VIDEO_SOCKET 1
#define LAST_SOCKET 1

struct _GstSandboxedDecodebinPrivate {
  GstElement *fdsink;
  GstElement *audiosrc;
  GstElement *audiodepay;
  GstElement *videosrc;
  GstElement *videodepay;

  GstPad *sink_pad;
  GstPad *video_src_pad;
  GstPad *audio_src_pad;

  int subprocess_stdin;
  gchar *shm_video_socket_path;
  gchar *shm_audio_socket_path;
  gchar *audio_shm_area_name;
  gchar *video_shm_area_name;

  GFileMonitor *audio_monitor;
  GFileMonitor *video_monitor;
  GCancellable *monitor_cancellable;
  gboolean subprocess_ready;
  gint uninitialised_socket_paths;
};

static GstStateChangeReturn
gst_sandboxed_decodebin_change_state (GstElement *element,
                                      GstStateChange state_change);

static GstBinClass *parent_class;

/* internal helpers */

/* Returns the fd to write in the subprocess stdin or -1 on error */
static gint
start_decoder (gchar *audio_socket_path,
               gchar *video_socket_path,
               GError **error)
{
  gint subprocess_stdin;
  char **env;
  char *args[] = {
    SANDBOXME_PATH,
    "-P",
    "-u1",
    "--",
    DECODER_PATH,
    video_socket_path,
    audio_socket_path,
    NULL
  };

  env = g_get_environ ();
  if (!g_spawn_async_with_pipes (NULL, /* working_directory */
                                args,
                                env,
                                0, /* flags */
                                NULL, /* child_setup */
                                NULL, /* user_data */
                                NULL, /* child pid */
                                &subprocess_stdin,
                                NULL, /* standard_output */
                                NULL, /* standard_error */
                                error)) {
    subprocess_stdin = -1;
  }
  g_strfreev (env);

  return subprocess_stdin;
}

static void
subprocess_ready (GstSandboxedDecodebin *self)
{
  self->priv->subprocess_ready = TRUE;
  GST_DEBUG_OBJECT (self, "subprocess looks ready");
}


static void
on_file_changed (GFileMonitor     *monitor,
                 GFile            *file,
                 GFile            *other_file,
                 GFileMonitorEvent event_type,
                 GstSandboxedDecodebin *self)
{
  if (event_type == G_FILE_MONITOR_EVENT_CREATED
      && g_atomic_int_dec_and_test (&self->priv->uninitialised_socket_paths)) {
    g_cancellable_cancel (self->priv->monitor_cancellable);
    subprocess_ready (self);
  }
}

/* FIXME: reference the monitors in self->priv so that we can clean them up later */
static void
monitor_subprocess_creation (GstSandboxedDecodebin *self)
{
  GFile *audio_file, *video_file;

  GST_DEBUG_OBJECT (self, "Putting monitors on %s and %s",
                    self->priv->shm_audio_socket_path,
                    self->priv->shm_video_socket_path);

  self->priv->monitor_cancellable = g_cancellable_new ();

  audio_file = g_file_new_for_path (self->priv->shm_audio_socket_path);
  self->priv->audio_monitor =
      g_file_monitor_file (audio_file, G_FILE_MONITOR_NONE,
                           self->priv->monitor_cancellable, NULL);

  video_file = g_file_new_for_path (self->priv->shm_video_socket_path);
  self->priv->video_monitor =
      g_file_monitor_file (video_file, G_FILE_MONITOR_NONE,
                           self->priv->monitor_cancellable, NULL);
  g_signal_connect (self->priv->audio_monitor, "changed",
                    G_CALLBACK (on_file_changed), self);
  g_signal_connect (self->priv->video_monitor, "changed",
                    G_CALLBACK (on_file_changed), self);

  if (g_file_query_exists (audio_file, self->priv->monitor_cancellable)
      || g_file_query_exists (video_file, self->priv->monitor_cancellable)) {
    GST_WARNING_OBJECT (self,
        "Some of the files we are monitoring for creation already exist!");
  }

  g_object_unref (audio_file);
  g_object_unref (video_file);
}


/* GObject vmethod implementations */

static void
gst_sandboxed_decodebin_dispose (GstSandboxedDecodebin *self)
{
}

static void
gst_sandboxed_decodebin_finalize (GstSandboxedDecodebin *self)
{
}

static void
gst_sandboxed_decodebin_init (GstSandboxedDecodebin *self)
{
  GstSandboxedDecodebinPrivate *priv;
  //GError *error = NULL;
  GstPad *fdsinkpad,
         *gdpaudiosrcpad,
         *gdpvideosrcpad;

  self->priv = priv = GST_SANDBOXED_DECODEBIN_GET_PRIVATE (self);

  priv->subprocess_stdin = -1;

  priv->shm_video_socket_path = g_strdup (tmpnam (NULL));
  priv->shm_audio_socket_path = g_strdup (tmpnam (NULL));
  priv->subprocess_ready = FALSE;
  priv->uninitialised_socket_paths = 2;
  monitor_subprocess_creation (self);

  priv->fdsink = gst_element_factory_make ("fdsink", "fdsink0");
  g_object_set (priv->fdsink,
                "async", FALSE,
                NULL);
  priv->audiosrc = gst_element_factory_make ("shmsrc", "audiosrc");
  g_object_set (priv->audiosrc,
                "socket-path", priv->shm_audio_socket_path, NULL);
  priv->videosrc = gst_element_factory_make ("shmsrc", "videosrc");
  g_object_set (priv->videosrc,
                "socket-path", priv->shm_video_socket_path, NULL);

  priv->audiodepay = gst_element_factory_make ("gdpdepay", "audiodepay");
  priv->videodepay = gst_element_factory_make ("gdpdepay", "videodepay");

  gst_bin_add_many (GST_BIN (self),
                    priv->fdsink, priv->audiosrc, priv->videosrc,
                    priv->audiodepay, priv->videodepay,
                    NULL);
  gst_element_link (priv->audiosrc, priv->audiodepay);
  gst_element_link (priv->videosrc, priv->videodepay);

  fdsinkpad = gst_element_get_static_pad (priv->fdsink, "sink");
  priv->sink_pad = gst_ghost_pad_new ("sink", fdsinkpad);
  g_object_unref (fdsinkpad);
  gst_element_add_pad (GST_ELEMENT (self), priv->sink_pad);

  gdpaudiosrcpad = gst_element_get_static_pad (priv->audiodepay, "src");
  priv->audio_src_pad = gst_ghost_pad_new ("audiosrc", gdpaudiosrcpad);
  g_object_unref (gdpaudiosrcpad);
  gst_element_add_pad (GST_ELEMENT (self), priv->audio_src_pad);

  gdpvideosrcpad = gst_element_get_static_pad (priv->videodepay, "src");
  priv->video_src_pad = gst_ghost_pad_new ("videosrc", gdpvideosrcpad);
  g_object_unref (gdpvideosrcpad);
  gst_element_add_pad (GST_ELEMENT (self), priv->video_src_pad);

}

static void
gst_sandboxed_decodebin_class_init (GstSandboxedDecodebinClass *self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (self_class);

  GST_DEBUG_CATEGORY_INIT (gst_debug_sandboxed_decodebin, "sandboxeddecodebin", 0,
      "sandboxed decoder bin");

  parent_class = g_type_class_peek_parent (self_class);

  g_type_class_add_private (self_class, sizeof (GstSandboxedDecodebinPrivate));
  object_class->dispose = (void (*) (GObject *object)) gst_sandboxed_decodebin_dispose;
  object_class->finalize = (void (*) (GObject *object)) gst_sandboxed_decodebin_finalize;

  element_class->change_state = gst_sandboxed_decodebin_change_state;
}

#if 0
static gboolean
do_async_done (gpointer user_data) {
  GstBin *bin = GST_BIN (user_data);
  GstMessage *message;

  fprintf (stderr, "do_async_done\n");
  message = gst_message_new_async_done (GST_OBJECT_CAST (bin));
  parent_class->handle_message (bin, message);

  return FALSE;
}
#endif

GstStateChangeReturn
gst_sandboxed_decodebin_change_state (GstElement *element,
                                      GstStateChange state_change)
{
  GError *error = NULL;
  GstSandboxedDecodebinPrivate *priv;
  GstSandboxedDecodebin *self = GST_SANDBOXED_DECODEBIN (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  priv = GST_SANDBOXED_DECODEBIN_GET_PRIVATE (self);

  switch (state_change) {
  case GST_STATE_CHANGE_NULL_TO_READY:
    /* TODO: set up file monitoring */
    /* spawn subprocess */
    priv->subprocess_stdin = start_decoder (priv->shm_audio_socket_path,
                                            priv->shm_video_socket_path,
                                            &error);
    if (priv->subprocess_stdin == -1) {
      GST_WARNING_OBJECT (element,
                          "Could not spawn subprocess: %s", error->message);
      ret = GST_STATE_CHANGE_FAILURE;
    }

    /* set the right fd to fdsink */
    g_object_set (priv->fdsink, "fd", priv->subprocess_stdin, NULL);
    GST_DEBUG_OBJECT (element, "Waiting for shm sockets to be available\n");
    while (!self->priv->subprocess_ready) {
      /* does that count as acceptable code? The alternatives are another
       * thread to monitor file creation or sleep() */
      g_main_context_iteration (NULL, TRUE);
    }
    GST_DEBUG_OBJECT (element, "Done waiting\n");

    break;
  case GST_STATE_CHANGE_READY_TO_PAUSED:
    GST_DEBUG_OBJECT (element, "Going to PAUSED");
    break;
#if 0
  case GST_STATE_CHANGE_READY_TO_PAUSED:
    /* TODO: check subprocess is ready with the help of file monitoring, if
     * not, do the change asynchronously */
      {
        GstMessage *message;
        ret = GST_STATE_CHANGE_ASYNC;

        message = gst_message_new_async_start (GST_OBJECT_CAST (element), FALSE);
        parent_class->handle_message (GST_BIN (element), message);

        /* Yeah, this is a hack */
        g_timeout_add (2000, do_async_done, self);
      }

    break;
#endif
  case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    GST_DEBUG_OBJECT (element, "Going to PLAYING");
    break;
  default:
    break;
  }
  if (ret != GST_STATE_CHANGE_FAILURE) {
    GstStateChangeReturn bret;
    bret = GST_ELEMENT_CLASS (parent_class)->change_state (element, state_change);
    if (bret == GST_STATE_CHANGE_FAILURE) {
      GST_WARNING_OBJECT (element, "parent change_state failed!");
      ret = bret;
    }
  }

  if (ret != GST_STATE_CHANGE_FAILURE) {
    GstStateChangeReturn fdret;
    gint fd;
    switch (state_change) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG_OBJECT (element, "Trying to set fdsink to PLAYING");
      fdret = gst_element_set_state (priv->fdsink, GST_STATE_PLAYING);
      GST_DEBUG_OBJECT (element, "Returned: %s",
                        gst_element_state_change_return_get_name (fdret));
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      g_object_get (priv->audiosrc,
                    "shm-area-name", &priv->audio_shm_area_name, NULL);
      g_object_get (priv->videosrc,
                    "shm-area-name", &priv->video_shm_area_name, NULL);
      GST_DEBUG_OBJECT (element, "Name of audio/video shm areas: "
                        "\"%s\" and \"%s\"\n",
                        priv->audio_shm_area_name,
                        priv->video_shm_area_name);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* Closing the fd sounds like a polite thing to do now*/
      g_object_get (priv->fdsink, "fd", &fd, NULL);
      close (fd);

      /* Unlinking the stuff the decoder could not unlink because it doesn't
       * have the necessary privileges */
      GST_DEBUG_OBJECT (element, "Trying to unlink %s and %s",
                        priv->shm_video_socket_path,
                        priv->shm_audio_socket_path);
      if (-1 == g_unlink (priv->shm_video_socket_path))
        GST_WARNING_OBJECT (element, "Could not unlink %s: %m", priv->shm_video_socket_path);
      if (-1 == g_unlink (priv->shm_audio_socket_path))
        GST_WARNING_OBJECT (element, "Could not unlink %s: %m", priv->shm_audio_socket_path);

      GST_DEBUG_OBJECT (element,
                        "Trying to unlink audio/video shm areas: \"%s\" and \"%s\"\n",
                        priv->audio_shm_area_name,
                        priv->video_shm_area_name);
      if (-1 == shm_unlink (priv->audio_shm_area_name))
        GST_WARNING_OBJECT (element, "Could not unlink shm area %s: %m",
                            priv->audio_shm_area_name);
      if (-1 == shm_unlink (priv->video_shm_area_name))
        GST_WARNING_OBJECT (element, "Could not unlink shm area %s: %m",
                            priv->video_shm_area_name);
      /* FIXME: where do we free all these strings? */
      break;
    default:
      break;
    }
  }

  return ret;
}

