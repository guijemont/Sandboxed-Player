#include <stdio.h>
#include <gst/gst.h>
#include "libsandbox.h"

GstElement *pipeline;
GstBus *bus;
GMainLoop *loop;

static void on_pipeline_ready (void);

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
init_pipeline (const gchar *pipeline_desc)
{
  GError *error = NULL;

  fprintf (stderr, "Loading all plugins\n");
  load_all_plugins ();

  fprintf (stderr, "Creating pipeline\n");
  pipeline = gst_parse_launch (pipeline_desc, &error);
  if (!pipeline)
    g_warning ("Could not create pipeline: %s", error->message);

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

int
main (int argc, char **argv)
{
  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("Syntax: %s <pipeline>\n", argv[0]);
    return EXIT_FAILURE;
  }

  loop = g_main_loop_new (g_main_context_default (), FALSE);

  g_idle_add ((GSourceFunc)init_pipeline, argv[1]);

  g_main_loop_run (loop);

  return EXIT_SUCCESS;
}
