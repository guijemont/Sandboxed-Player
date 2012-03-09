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

#include "gstsandboxeddecodebin.h"

GST_DEBUG_CATEGORY_STATIC (gst_debug_sandboxed_decodebin);
#define GST_CAT_DEFAULT gst_debug_sandboxed_decodebin

G_DEFINE_TYPE (GstSandboxedDecodebin, gst_sandboxed_decodebin, GST_TYPE_BIN);

#define GST_SANDBOXED_DECODEBIN_GET_PRIVATE(o)\
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_SANDBOXED_DECODEBIN_TYPE, GstSandboxedDecodebinPrivate))

#define DECODER_PATH "./decoder"
#define SANDBOXME_PATH "../setuid-sandbox/sandboxme"

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

  GFileMonitor *monitors[LAST_SOCKET+1];
  gboolean file_ready[LAST_SOCKET+1];
  GCancellable *monitor_cancellable;
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

  priv = GST_SANDBOXED_DECODEBIN_GET_PRIVATE (self);

  priv->subprocess_stdin = -1;

  priv->shm_video_socket_path = g_strdup (tmpnam (NULL));
  priv->shm_audio_socket_path = g_strdup (tmpnam (NULL));

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
    sleep (2);
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
  case GST_STATE_CHANGE_READY_TO_NULL:
    /* TODO: clean up some stuff, including unlinking the stuff the subprocess
     * cannot unlink */
    break;
  default:
    break;
  }
  if (ret != GST_STATE_CHANGE_FAILURE) {
    GstStateChangeReturn bret;
    bret = GST_ELEMENT_CLASS (parent_class)->change_state (element, state_change);
    if (bret == GST_STATE_CHANGE_FAILURE)
      ret = bret;
  }

  if (state_change == GST_STATE_CHANGE_READY_TO_PAUSED
      && ret != GST_STATE_CHANGE_FAILURE) {
    GstStateChangeReturn fdret;
    GST_DEBUG_OBJECT (element, "Trying to set fdsink to PLAYING");
    fdret = gst_element_set_state (priv->fdsink, GST_STATE_PLAYING);
    GST_DEBUG_OBJECT (element, "Returned: %s",
                      gst_element_state_change_return_get_name (fdret));
  }

  return ret;
}

