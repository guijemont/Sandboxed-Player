#include <stdio.h>
#include <gst/gst.h>
#include <glib-unix.h>
#include "libsandbox.h"

struct PipelineInfo {
  const gchar *video_shm;
  const gchar *audio_shm;
  GstElement *videosink;
  GstElement *audiosink;
  gint connections;
};

GstElement *pipeline;
GstBus *bus;
GMainLoop *loop;

static void on_pipeline_ready (void);

#define SHM_SIZE 100000000

static gboolean
on_message (GstBus *bus,
            GstMessage *message,
            gpointer data)
{
  switch (message->type) {
  case GST_MESSAGE_STATE_CHANGED:
    if (message->src == (GstObject *)pipeline) {
      GstState new_state;
      gst_message_parse_state_changed (message,
                                       NULL, /* oldstate */
                                       &new_state,
                                       NULL /* pending */);
      if (new_state == GST_STATE_READY)
        on_pipeline_ready ();
      if (new_state == GST_STATE_NULL)
        fprintf (stderr, "decoder: pipeline set to NULL state\n");
    }
    break;
  case GST_MESSAGE_EOS:
    if (message->src == (GstObject *)pipeline) {
      fprintf (stderr, "Got EOS, quitting\n");
      g_main_loop_quit (loop);
    }
  default:
    break;
  }

  return TRUE;
}

static void
load_all_plugins (void)
{
  /* NOTE: we leak plugin references like crazy here. That's cool because we
   * want them to remain loaded. If we want to be clever, we could drop these
   * references once we're in PAUSED to allow for the unloading of plugins we
   * don't need.
   */
  GList *plugin_list = NULL, *elem;
  GstRegistry *registry = gst_registry_get_default ();

  plugin_list = gst_registry_get_plugin_list (registry);

  for (elem = plugin_list; elem; elem = elem->next) {
    GstPlugin *plugin = GST_PLUGIN (elem->data);
    if (!gst_plugin_is_loaded (plugin))
      gst_plugin_load (plugin);
  }
}

static void
on_client_connected (GstElement *shmsink,
                     gint arg0,
                     struct PipelineInfo *pipeline_info)
{
  g_atomic_int_inc (&pipeline_info->connections);
}

static void
on_client_disconnected (GstElement *shmsink,
                     gint arg0,
                     struct PipelineInfo *pipeline_info)
{
  if (g_atomic_int_dec_and_test (&pipeline_info->connections)) {
    fprintf (stderr, "No more connections, quitting!\n");
    gst_element_set_state (pipeline, GST_STATE_NULL);
    g_object_unref (pipeline);
    g_main_loop_quit (loop);
  }
}

static void
monitor_shmsink_connections (struct PipelineInfo *pipeline_info)
{
  g_signal_connect (pipeline_info->videosink, "client-connected",
                    G_CALLBACK (on_client_connected), pipeline_info);
  g_signal_connect (pipeline_info->videosink, "client-disconnected",
                    G_CALLBACK (on_client_disconnected), pipeline_info);
  g_signal_connect (pipeline_info->audiosink, "client-connected",
                    G_CALLBACK (on_client_connected), pipeline_info);
  g_signal_connect (pipeline_info->audiosink, "client-disconnected",
                    G_CALLBACK (on_client_disconnected), pipeline_info);
}

static gboolean
init_pipeline (struct PipelineInfo *pipeline_info)
{
  GError *error = NULL;
  gchar *pipeline_desc;

  fprintf (stderr, "Loading all plugins\n");
  load_all_plugins ();

  fprintf (stderr, "Creating pipeline\n");
  pipeline_desc = g_strdup_printf ("fdsrc ! decodebin2 name=decoder "
      "decoder. ! video/x-raw-yuv;video/x-raw-rgb ! gdppay ! queue ! shmsink name=videosink socket-path=%s shm-size=%d "
      "decoder. ! audio/x-raw-int;audio/x-raw-float ! gdppay ! queue ! shmsink name=audiosink socket-path=%s shm-size=%d",
      pipeline_info->video_shm, SHM_SIZE,
      pipeline_info->audio_shm, SHM_SIZE);

  pipeline = gst_parse_launch (pipeline_desc, &error);
  g_free (pipeline_desc);
  if (!pipeline) {
    fprintf (stderr, "Problem creating pipeline: %s\n", error->message);
    exit(EXIT_FAILURE);
  }

  pipeline_info->videosink = gst_bin_get_by_name (GST_BIN (pipeline), "videosink");
  pipeline_info->audiosink = gst_bin_get_by_name (GST_BIN (pipeline), "audiosink");

  monitor_shmsink_connections (pipeline_info);

  fprintf (stderr, "Setting up bus watch\n");
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  gst_bus_add_watch (bus, on_message, NULL);

  fprintf (stderr, "Going to READY\n");
  gst_element_set_state (pipeline, GST_STATE_READY);

  chrootme ();

  return FALSE;
}

static void
on_pipeline_ready (void)
{
  fprintf (stderr, "pipeline is READY\n");
  if (NULL == g_getenv("GST_DECODER_DEBUG")) {
    /* Unless we're in debug mode, close stdout and stderr which are likely
     * to be fds on a tty, which is a potential "escape" risk, and make them
     * point to /dev/null */
    int devnul = -1;
    fprintf (stderr, "Going into silent mode\n");
    devnul = open ("/dev/null", 0, O_RDWR);
    g_assert (devnul != -1);
    /* make stdout point to /dev/null */
    g_assert (-1 != dup2 (devnul, 1));
    /* make stderr point to /dev/null */
    g_assert (-1 != dup2 (devnul, 2));
  }
  fprintf (stderr, "going to PLAYING\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
}

/* signal handler */
static gboolean
shut_down (gpointer data)
{
  fprintf (stderr, "Decoder: Received a signal telling us to shut down.\n");
  if (pipeline) {
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }

  g_main_loop_quit (loop);

  return TRUE;
}

static void
set_up_signals (void)
{
  g_unix_signal_add (SIGHUP, shut_down, NULL);
  g_unix_signal_add (SIGINT, shut_down, NULL);
  g_unix_signal_add (SIGTERM, shut_down, NULL);
}

int
main (int argc, char **argv)
{
  struct PipelineInfo pipeline_info;

  gst_init (&argc, &argv);

  if (argc != 3) {
    fprintf (stderr, "Syntax: %s <output shm video socket> <output shm audio socket>\n", argv[0]);
    return EXIT_FAILURE;
  }

  pipeline_info.video_shm = argv[1];
  pipeline_info.audio_shm = argv[2];
  pipeline_info.connections = 0;

  loop = g_main_loop_new (g_main_context_default (), FALSE);

  set_up_signals ();

  g_idle_add ((GSourceFunc)init_pipeline, &pipeline_info);

  g_main_loop_run (loop);

  fprintf (stderr, "Decoder: over and out!\n");

  return EXIT_SUCCESS;
}
