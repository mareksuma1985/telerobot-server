#include <gst/gst.h>
#include <gst/interfaces/xoverlay.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <glib.h>

char nadajnik_IP_string[128];
int v4l_device_number = 0;
gboolean video_running = FALSE;

static void on_pad_added(GstElement *element, GstPad *pad, gpointer data) {
	GstPad *sinkpad;
	GstElement *encoder = (GstElement *) data;
	g_print("Dynamic pad created, linking\n");
	sinkpad = gst_element_get_static_pad(encoder, "sink");
	gst_pad_link(pad, sinkpad);
	gst_object_unref(sinkpad);
}

GstStateChangeReturn ret;
GstElement *pipeline;
GstElement *v4l2src, *encoder, *udp_sink;
GMainLoop *loop;
gboolean virgin_video = TRUE;
gboolean virgin_audio = TRUE;
/* polecenie bash do nadawania obrazu i dźwięku:
gst-launch v4l2src ! queue ! video/x-raw-yuv,width=320,height=240,framerate=15/1 ! videorate ! videoscale ! ffmpegcolorspace ! queue ! smokeenc ! queue ! udpsink host=127.0.0.1 port=5000
alsasrc ! queue ! audio/x-raw-int,rate=8000,channels=1,depth=8 ! audioconvert ! speexenc ! queue ! tcpserversink host=127.0.0.1 port=5001 */

int video_start() {
	char v4l_device_path[11];
	/* "/dev/videoX" is 11 characters long */

	if (virgin_video = TRUE) {
		/* ta część jest wykonywana tylko przy pierwszym uruchomieniu nadawania obrazu */
		v4l2src = gst_element_factory_make("v4l2src", "video4linux-source");
		/* tworzy źródło obrazu */
		sprintf(v4l_device_path, "/dev/video%d", v4l_device_number);
		/* tworzy sciezke deskryptora na podstawie przeslanego numeru urzadzenia */
		g_object_set(G_OBJECT(v4l2src), "device", v4l_device_path, NULL);
		/* ustawia odpowiednie urzadzenie */

		/* wyświetlanie faktycznej ścieżki i modelu kamery	*/
		gchar *path;
		gchar *model;
		g_object_get(v4l2src, "device", &path, NULL);
		g_object_get(v4l2src, "device-name", &model, NULL);
		printf("urządzenie %s to %s\n", path, model);
		g_free(path);
		g_free(model);
		/* koniec wyświetlania ścieżki i modelu				*/

		/* tworzenie dalszej części potoku przetwarzania obrazu */
		pipeline = gst_pipeline_new("pipeline");
		udp_sink = gst_element_factory_make("udpsink", "UDP-sink");

		if (!udp_sink) {
			g_print("output could not be found - check your install\n");
		}
		encoder = gst_element_factory_make("smokeenc", "smoke-encoder");

		g_assert(pipeline != NULL);
		g_assert(v4l2src != NULL);
		g_assert(encoder != NULL);
		g_assert(udp_sink != NULL);

		gst_bin_add(GST_BIN(pipeline), v4l2src);
		gst_bin_add(GST_BIN(pipeline), encoder);
		gst_bin_add(GST_BIN(pipeline), udp_sink);

		if (!gst_element_link(v4l2src, encoder)) {
			printf("NOGO: Failed to link %s with encoder!\n", v4l_device_path);
			return -1;
		} else {
			printf("GO: Linked %s with encoder.\n", v4l_device_path);
		}

		if (!gst_element_link(encoder, udp_sink)) {
			g_print("NOGO: Failed to link encoder with udp_sink!\n");
			return -1;
		} else {
			g_print("GO: Linked encoder with udp_sink.\n");
		}
		virgin_video = FALSE;
	}

	g_object_set(G_OBJECT(udp_sink), "port", 5000, NULL);
	/* ustawia numer portu na jaki ma być wysyłany obraz */
	g_object_set(G_OBJECT(udp_sink), "host", nadajnik_IP_string, NULL);
	/* ustawia adres na jaki ma być wysyłany obraz */

	if (gst_element_set_state(GST_ELEMENT(pipeline), GST_STATE_PLAYING)) {
		g_print("GO: Video pipeline state set to playing.\n");
	} else {
		g_print("NOGO: Failed to start up video pipeline!\n");
		return 1;
	}
	video_running = TRUE;
}

/* GstStateChangeReturn ret_audio; */
GstElement *pipeline_audio;
GstElement *alsasrc, *converter, *speexenc, *tcpserversink;

int audio_start() {
	if (virgin_audio = TRUE) {
		/* ta część jest wykonywana tylko przy pierwszym uruchomieniu nadawania dźwięku */
		pipeline_audio = gst_pipeline_new("pipeline-audio");
		alsasrc = gst_element_factory_make("alsasrc", "alsa-source");
		converter = gst_element_factory_make("audioconvert", "audio-convert");
		speexenc = gst_element_factory_make("speexenc", "audio-encoder");
		tcpserversink = gst_element_factory_make("tcpserversink", "tcp-server-sink");

		if (!tcpserversink) {
			g_print("Output could not be found - check your install!\n");
		}

		/* FIXME: assertion failed */
		/*  g_assert (pipeline_audio != NULL);
		 g_assert (alsasrc != NULL);
		 g_assert (converter != NULL);
		 g_assert (speexenc != NULL);
		 g_assert (tcpserversink != NULL);
		 */
		gst_bin_add(GST_BIN(pipeline_audio), alsasrc);
		gst_bin_add(GST_BIN(pipeline_audio), converter);
		gst_bin_add(GST_BIN(pipeline_audio), speexenc);
		gst_bin_add(GST_BIN(pipeline_audio), tcpserversink);

		if (!gst_element_link(alsasrc, converter)) {
			g_print("NOGO: Failed to link audio source with converter!\n");
			return -1;
		} else {
			g_print("GO: Linked audio source with converter.\n");
		}

		if (!gst_element_link(converter, speexenc)) {
			g_print("NOGO: Failed to link converter with speexenc!\n");
			return -1;
		} else {
			g_print("GO: Linked converter with speexenc.\n");
		}

		if (!gst_element_link(speexenc, tcpserversink)) {
			g_print("NOGO: Failed to link speexenc with tcpserversink!\n");
			return -1;
		} else {
			g_print("GO: Linked speexenc with tcpserversink.\n");
		}
		virgin_video = FALSE;
	}

	g_object_set(G_OBJECT(tcpserversink), "host", nadajnik_IP_string, NULL);
	g_object_set(G_OBJECT(tcpserversink), "port", 5001, NULL);

	if (gst_element_set_state(GST_ELEMENT(pipeline_audio), GST_STATE_PLAYING)) {
		g_print("GO: Audio pipeline state set to playing.\n");
	} else {
		g_print("NOGO: Failed to start up audio pipeline!\n");
		return 1;
	}
}

video_stop() {
	gst_element_set_state(pipeline, GST_STATE_PAUSED);
	g_print("Video pipeline: paused\n");
	gst_element_set_state(pipeline, GST_STATE_NULL);
	g_print("Video pipeline: null\n");
	/* gst_object_unref (GST_OBJECT (pipeline));
	 g_print ("Deleting video pipeline\n"); */
	video_running = FALSE;
	return 1;
}

audio_stop() {
	gst_element_set_state(pipeline_audio, GST_STATE_PAUSED);
	g_print("Audio pipeline: paused\n");
	gst_element_set_state(pipeline_audio, GST_STATE_NULL);
	g_print("Audio pipeline: null\n");
	/* gst_object_unref (GST_OBJECT (pipeline_audio));
	 g_print ("Deleting audio pipeline\n"); */
	return 1;
}
