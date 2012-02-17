#include <stdio.h>
#include <gst/gst.h>
#include <glib-unix.h>
#include "libsandbox.h"

struct PipelineInfo {
  const gchar *video_shm;
  const gchar *audio_shm;
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

static gboolean
init_pipeline (const struct PipelineInfo *pipeline_info)
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
  fprintf (stderr, "pipeline is READY, going to PLAYING\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
}

/* signal handler */
static gboolean
shut_down (gpointer data)
{
  fprintf (stderr, "Received a signal telling us to shut down.\n");
  if (pipeline) {
    //gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_element_send_event (pipeline, gst_event_new_eos ());
  }

  //g_idle_add ((GSourceFunc)g_main_loop_quit, loop);
  //g_main_loop_quit (loop);

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
    g_print ("Syntax: %s <output shm video socket> <output shm audio socket>\n", argv[0]);
    return EXIT_FAILURE;
  }

  pipeline_info.video_shm = argv[1];
  pipeline_info.audio_shm = argv[2];

  loop = g_main_loop_new (g_main_context_default (), FALSE);

  set_up_signals ();

  g_idle_add ((GSourceFunc)init_pipeline, &pipeline_info);

  g_main_loop_run (loop);

  return EXIT_SUCCESS;
}
