#include <unistd.h>
#include <stdlib.h>
#include <glib-unix.h>
#include <gst/gst.h>

#define DECODER_PATH "./decoder"
#define SANDBOXME_PATH "../setuid-sandbox/sandboxme"

struct SafePlayer {
  GMainLoop *loop;
  int subprocess_stdin;
  const gchar *media_uri;
  gchar *shm_video_socket_path;
  gchar *shm_audio_socket_path;
  GstElement *source_pipeline;
  GstElement *sink_pipeline;
  char **environment;
};

static void
safe_player_init (struct SafePlayer *player, const gchar *media_uri, char **envp)
{
  player->loop = g_main_loop_new (g_main_context_default (), FALSE);
  player->subprocess_stdin = -1;
  player->media_uri = media_uri;
  /* FIXME: this is insecure in a way, ideally we should have the name decided
   * securely by shmsink in the decoder process, and communicated back to us */
  player->shm_video_socket_path = g_strdup (tmpnam (NULL));
  player->shm_audio_socket_path = g_strdup (tmpnam (NULL));
  player->source_pipeline = NULL;
  player->sink_pipeline = NULL;
  player->environment = envp;
}

static void
exec_decoder (struct SafePlayer *player)
{
  char * const args[] = {
    SANDBOXME_PATH,
    "-P",
    "-u1",
    "--",
    DECODER_PATH,
    player->shm_video_socket_path,
    player->shm_audio_socket_path,
    NULL
  };
  int ret;

  ret = execve (SANDBOXME_PATH, args, player->environment);
  fprintf (stderr, "execve() of %s returned %d: %m\n", SANDBOXME_PATH, ret);
  exit (EXIT_FAILURE);
}

/* Return an fd in which you can write to the stdin of the decoder (or -1 on
 * error) */
static int
start_decoder (struct SafePlayer *safe_player)
{
  int pipe_fd[2] = {-1, -1};
  int pid;
  int ret = -1;

  if (pipe (pipe_fd)) {
    fprintf (stderr,
             "Could not create pipe to communicate with decoder subprocess: %m\n");
    goto clean;
  }

  pid = fork ();
  if (pid < 0) {
    fprintf (stderr, "fork() failed! %m\n");
    goto clean;
  } else if (pid == 0) { /* child: decoder */
    if (dup2 (pipe_fd[0], STDIN_FILENO)) {
      fprintf (stderr, "Cannot get access to stdin of decoder subprocess: %m\n");
      exit (EXIT_FAILURE);
    }
    close (pipe_fd[0]);
    exec_decoder (safe_player);
  } else { /* parent: controller */
    ret = pipe_fd[1];
    goto beach;
  }

clean:
  if (pipe_fd[0] != -1)
    close (pipe_fd[0]);
  if (pipe_fd[1] != -1)
    close (pipe_fd[1]);

beach:
  return ret;
}

static gboolean
init_source_pipeline (struct SafePlayer *player)
{
  gchar * pipeline_desc;
  GError *error = NULL;

  pipeline_desc = g_strdup_printf ("giosrc location=%s ! fdsink fd=%d",
                                   player->media_uri,
                                   player->subprocess_stdin);
  player->source_pipeline = gst_parse_launch (pipeline_desc, &error);
  g_free (pipeline_desc);

  if (!player->source_pipeline) {
    fprintf (stderr, "Could not create source pipeline: %s\n", error->message);
    /* Don't know what to do; let's commit suicide */
    g_assert_not_reached ();
  }

  gst_element_set_state (player->source_pipeline, GST_STATE_PLAYING);

  return FALSE;
}

static gboolean
init_sink_pipeline (struct SafePlayer *player)
{
  gchar * pipeline_desc;
  GError *error = NULL;

  pipeline_desc =
      g_strdup_printf ("shmsrc socket-path=%s ! gdpdepay ! queue ! autovideosink "
                       "shmsrc socket-path=%s ! gdpdepay ! queue ! autoaudiosink",
                       player->shm_video_socket_path,
                       player->shm_audio_socket_path);

  player->sink_pipeline = gst_parse_launch (pipeline_desc, &error);
  g_free (pipeline_desc);

  if (!player->sink_pipeline) {
    fprintf (stderr, "Could not create sink pipeline: %s\n", error->message);
    /* Don't know what to do; let's commit suicide */
    g_assert_not_reached ();
  }

  gst_element_set_state (player->sink_pipeline, GST_STATE_PLAYING);

  return FALSE;
}

/* signal handler */
static gboolean
shut_down (struct SafePlayer *player)
{
  fprintf (stderr, "Received a signal telling us to shut down.\n");
  if (player->source_pipeline)
    gst_element_set_state (player->source_pipeline, GST_STATE_NULL);
  if (player->sink_pipeline)
    gst_element_set_state (player->sink_pipeline, GST_STATE_NULL);

  g_main_loop_quit (player->loop);

  return TRUE;
}

static void
set_up_signals (struct SafePlayer *player)
{
  g_unix_signal_add (SIGHUP, (GSourceFunc)shut_down, player);
  g_unix_signal_add (SIGINT, (GSourceFunc)shut_down, player);
  g_unix_signal_add (SIGTERM, (GSourceFunc)shut_down, player);
}

/*
TODO:
 - ensure we transfer the environment -> or maybe not, we can't reasonably get
 LD_LIBRARY_PATH through
 - create source pipeline using player->subprocess_stdin for fdsink, make it go to
 PLAYING
 - once the decoder has created the shmsinks, create the sink pipelines, make
 it go to PLAYING (need to get stdout of decoder)
 - find a way to clean up everything at EOF/when CTRL-C'ing safeplayer.
 */

int
main (int argc, char **argv, char **envp)
{
  struct SafePlayer player;

  gst_init (&argc, &argv);


  if (argc != 2) {
    g_print ("Syntax: %s media_url\n", argv[0]);
    return EXIT_FAILURE;
  }

  safe_player_init (&player, argv[1], envp);
  set_up_signals (&player);

  player.subprocess_stdin = start_decoder (&player);
  g_assert (player.subprocess_stdin != -1);

  g_idle_add ((GSourceFunc)init_source_pipeline, &player);
  g_timeout_add (2000, /* HACK: we wait a bit for the decoder subprocess to set
                          up the shm stuff */
                 (GSourceFunc)init_sink_pipeline, &player);

  g_main_loop_run (player.loop);

  return EXIT_SUCCESS;
}
