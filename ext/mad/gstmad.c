/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
 * SECTION:element-mad
 * @see_also: lame
 *
 * MP3 audio decoder.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch filesrc location=music.mp3 ! mad ! audioconvert ! audioresample ! autoaudiosink
 * ]| Decode the mp3 file and play
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include "gstmad.h"
#include <gst/audio/audio.h>

enum
{
  ARG_0,
  ARG_HALF,
  ARG_IGNORE_CRC
};

GST_DEBUG_CATEGORY_STATIC (mad_debug);
#define GST_CAT_DEFAULT mad_debug

static GstStaticPadTemplate mad_src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
        "signed = (boolean) true, "
        "width = (int) 32, "
        "depth = (int) 32, "
        "rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ]")
    );

/* FIXME: make three caps, for mpegversion 1, 2 and 2.5 */
static GstStaticPadTemplate mad_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) [ 1, 3 ], "
        "rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ]")
    );


static gboolean gst_mad_start (GstAudioDecoder * dec);
static gboolean gst_mad_stop (GstAudioDecoder * dec);
static gboolean gst_mad_parse (GstAudioDecoder * dec, GstAdapter * adapter,
    gint * offset, gint * length);
static GstFlowReturn gst_mad_handle_frame (GstAudioDecoder * dec,
    GstBuffer * buffer);
static void gst_mad_flush (GstAudioDecoder * dec, gboolean hard);

static void gst_mad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


GST_BOILERPLATE (GstMad, gst_mad, GstAudioDecoder, GST_TYPE_AUDIO_DECODER);

static void
gst_mad_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &mad_sink_template_factory);
  gst_element_class_add_static_pad_template (element_class,
      &mad_src_template_factory);
  gst_element_class_set_details_simple (element_class, "mad mp3 decoder",
      "Codec/Decoder/Audio",
      "Uses mad code to decode mp3 streams", "Wim Taymans <wim@fluendo.com>");
}

static void
gst_mad_class_init (GstMadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAudioDecoderClass *base_class = (GstAudioDecoderClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  base_class->start = GST_DEBUG_FUNCPTR (gst_mad_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_mad_stop);
  base_class->parse = GST_DEBUG_FUNCPTR (gst_mad_parse);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_mad_handle_frame);
  base_class->flush = GST_DEBUG_FUNCPTR (gst_mad_flush);

  gobject_class->set_property = gst_mad_set_property;
  gobject_class->get_property = gst_mad_get_property;

  /* init properties */
  /* currently, string representations are used, we might want to change that */
  /* FIXME: descriptions need to be more technical,
   * default values and ranges need to be selected right */
  g_object_class_install_property (gobject_class, ARG_HALF,
      g_param_spec_boolean ("half", "Half", "Generate PCM at 1/2 sample rate",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_IGNORE_CRC,
      g_param_spec_boolean ("ignore-crc", "Ignore CRC", "Ignore CRC errors",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_mad_init (GstMad * mad, GstMadClass * klass)
{
  GstAudioDecoder *dec;

  dec = GST_AUDIO_DECODER (mad);
  gst_audio_decoder_set_tolerance (dec, 20 * GST_MSECOND);

  mad->half = FALSE;
  mad->ignore_crc = TRUE;
}

static gboolean
gst_mad_start (GstAudioDecoder * dec)
{
  GstMad *mad = GST_MAD (dec);
  guint options = 0;

  GST_DEBUG_OBJECT (dec, "start");
  mad_stream_init (&mad->stream);
  mad_frame_init (&mad->frame);
  mad_synth_init (&mad->synth);
  mad->rate = 0;
  mad->channels = 0;
  mad->caps_set = FALSE;
  mad->frame.header.samplerate = 0;
  if (mad->ignore_crc)
    options |= MAD_OPTION_IGNORECRC;
  if (mad->half)
    options |= MAD_OPTION_HALFSAMPLERATE;
  mad_stream_options (&mad->stream, options);
  mad->header.mode = -1;
  mad->header.emphasis = -1;
  mad->eos = FALSE;

  /* call upon legacy upstream byte support (e.g. seeking) */
  gst_audio_decoder_set_byte_time (dec, TRUE);

  return TRUE;
}

static gboolean
gst_mad_stop (GstAudioDecoder * dec)
{
  GstMad *mad = GST_MAD (dec);

  GST_DEBUG_OBJECT (dec, "stop");
  mad_synth_finish (&mad->synth);
  mad_frame_finish (&mad->frame);
  mad_stream_finish (&mad->stream);

  return TRUE;
}

static inline gint32
scale (mad_fixed_t sample)
{
#if MAD_F_FRACBITS < 28
  /* round */
  sample += (1L << (28 - MAD_F_FRACBITS - 1));
#endif

  /* clip */
  if (sample >= MAD_F_ONE)
    sample = MAD_F_ONE - 1;
  else if (sample < -MAD_F_ONE)
    sample = -MAD_F_ONE;

#if MAD_F_FRACBITS < 28
  /* quantize */
  sample >>= (28 - MAD_F_FRACBITS);
#endif

  /* convert from 29 bits to 32 bits */
  return (gint32) (sample << 3);
}

/* internal function to check if the header has changed and thus the
 * caps need to be reset.  Only call during normal mode, not resyncing */
static void
gst_mad_check_caps_reset (GstMad * mad)
{
  guint nchannels;
  guint rate;

  nchannels = MAD_NCHANNELS (&mad->frame.header);

#if MAD_VERSION_MINOR <= 12
  rate = mad->header.sfreq;
#else
  rate = mad->frame.header.samplerate;
#endif

  /* rate and channels are not supposed to change in a continuous stream,
   * so check this first before doing anything */

  /* only set caps if they weren't already set for this continuous stream */
  if (mad->channels != nchannels || mad->rate != rate) {
    GstCaps *caps;

    if (mad->caps_set) {
      GST_DEBUG_OBJECT (mad, "Header changed from %d Hz/%d ch to %d Hz/%d ch, "
          "failed sync after seek ?", mad->rate, mad->channels, rate,
          nchannels);
      /* we're conservative on stream changes. However, our *initial* caps
       * might have been wrong as well - mad ain't perfect in syncing. So,
       * we count caps changes and change if we pass a limit treshold (3). */
      if (nchannels != mad->pending_channels || rate != mad->pending_rate) {
        mad->times_pending = 0;
        mad->pending_channels = nchannels;
        mad->pending_rate = rate;
      }
      if (++mad->times_pending < 3)
        return;
    }

    if (mad->stream.options & MAD_OPTION_HALFSAMPLERATE)
      rate >>= 1;

    /* we set the caps even when the pad is not connected so they
     * can be gotten for streaminfo */
    caps = gst_caps_new_simple ("audio/x-raw-int",
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "width", G_TYPE_INT, 32,
        "depth", G_TYPE_INT, 32,
        "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, nchannels, NULL);

    gst_pad_set_caps (GST_AUDIO_DECODER_SRC_PAD (mad), caps);
    gst_caps_unref (caps);

    mad->caps_set = TRUE;
    mad->channels = nchannels;
    mad->rate = rate;
  }
}

static GstFlowReturn
gst_mad_parse (GstAudioDecoder * dec, GstAdapter * adapter,
    gint * _offset, gint * len)
{
  GstMad *mad;
  GstFlowReturn ret = GST_FLOW_UNEXPECTED;
  gint av, size, offset;
  const guint8 *data;
  gboolean eos, sync;
  GstBuffer *guard = NULL;

  mad = GST_MAD (dec);

  av = gst_adapter_available (adapter);
  data = gst_adapter_peek (adapter, av);

  gst_audio_decoder_get_parse_state (dec, &sync, &eos);
  GST_LOG_OBJECT (mad, "parse state sync %d, eos %d", sync, eos);

  if (eos) {
    /* This is one streaming hack right there.
     * mad will not decode the last frame if it is not followed by
     * a number of 0 bytes, due to some buffer overflow, which can
     * not be fixed for reasons I did not inquire into, see
     * http://www.mars.org/mailman/public/mad-dev/2001-May/000262.html
     */
    guard = gst_buffer_new_and_alloc (av + MAD_BUFFER_GUARD);
    /* let's be nice and not mess with baseclass state and keep hacks local */
    memset (GST_BUFFER_DATA (guard), 0, GST_BUFFER_SIZE (guard));
    memcpy (GST_BUFFER_DATA (guard), data, av);
    GST_DEBUG_OBJECT (mad, "added %u zero guard bytes in the adapter; "
        "using fallback buffer of size %u",
        GST_BUFFER_SIZE (guard) - av, GST_BUFFER_SIZE (guard));
    data = GST_BUFFER_DATA (guard);
    av = GST_BUFFER_SIZE (guard);
  }

  /* we basically let mad library do parsing,
   * and translate that back to baseclass.
   * if a frame is found (and also decoded), subsequent handle_frame
   * only needs to synthesize it */

  offset = 0;
  size = 0;

resume:
  if (G_UNLIKELY (offset + MAD_BUFFER_GUARD > av))
    goto exit;

  GST_LOG_OBJECT (mad, "setup mad stream at offset %d (of av %d)", offset, av);
  mad_stream_buffer (&mad->stream, data + offset, av - offset);
  /* convey sync idea to mad */
  mad->stream.sync = sync;
  /* if we get back here, lost sync anyway */
  sync = FALSE;

  while (TRUE) {
    GST_LOG_OBJECT (mad, "decoding the header now");
    if (mad_header_decode (&mad->frame.header, &mad->stream) == -1) {
      /* HACK it seems mad reports wrong error when it is trying to determine
       * free bitrate and scanning for next header */
      if (mad->stream.error == MAD_ERROR_LOSTSYNC) {
        const guint8 *ptr = mad->stream.this_frame;
        guint32 header;

        if (ptr >= data && ptr < data + av) {
          header = GST_READ_UINT32_BE (ptr);
          /* looks like possible freeform header with not much data */
          if (((header & 0xFFE00000) == 0xFFE00000) &&
              (((header >> 12) & 0xF) == 0x0) && (av < 4096)) {
            GST_DEBUG_OBJECT (mad, "overriding freeform LOST_SYNC to BUFLEN");
            mad->stream.error = MAD_ERROR_BUFLEN;
          }
        }
      }
      if (mad->stream.error == MAD_ERROR_BUFLEN) {
        GST_LOG_OBJECT (mad, "not enough data, getting more");
        offset = mad->stream.next_frame - data;
        break;
      } else if (mad->stream.error == MAD_ERROR_LOSTSYNC) {
        GST_LOG_OBJECT (mad, "lost sync");
        continue;
      } else {
        /* probably some bogus header, basically also lost sync */
        GST_DEBUG_OBJECT (mad, "mad_header_decode had an error: %s",
            mad_stream_errorstr (&mad->stream));
        continue;
      }
    }

    /* could have a frame now, subsequent will confirm */
    offset = mad->stream.this_frame - data;
    size = mad->stream.next_frame - mad->stream.this_frame;
    g_assert (size);

    GST_LOG_OBJECT (mad, "parsing and decoding one frame now "
        "(offset %d, size %d)", offset, size);
    if (mad_frame_decode (&mad->frame, &mad->stream) == -1) {
      GST_LOG_OBJECT (mad, "got error %d", mad->stream.error);

      /* not enough data, need to wait for next buffer? */
      if (mad->stream.error == MAD_ERROR_BUFLEN) {
        /* not really expect this error at this stage anymore
         * assume bogus frame and bad sync and move along a bit */
        GST_WARNING_OBJECT (mad, "not enough data (unexpected), moving along");
        offset++;
        goto resume;
      } else if (mad->stream.error == MAD_ERROR_BADDATAPTR) {
        GST_DEBUG_OBJECT (mad, "bad data ptr, skipping presumed frame");
        /* flush past presumed frame */
        offset += size;
        goto resume;
      } else {
        GST_WARNING_OBJECT (mad, "mad_frame_decode had an error: %s",
            mad_stream_errorstr (&mad->stream));
        if (!MAD_RECOVERABLE (mad->stream.error)) {
          /* well, all may be well enough bytes later on ... */
          GST_AUDIO_DECODER_ERROR (mad, 1, STREAM, DECODE, (NULL),
              ("mad error: %s", mad_stream_errorstr (&mad->stream)), ret);
        }
        /* move along and try again */
        offset++;
        goto resume;
      }
      g_assert_not_reached ();
    }

    /* so decoded ok, got a frame now */
    ret = GST_FLOW_OK;
    break;
  }

exit:
  *_offset = offset;
  *len = size;

  /* ensure that if we added some dummy guard bytes above, we don't claim
     to have used them as they're unknown to the caller. */
  if (eos) {
    g_assert (av >= MAD_BUFFER_GUARD);
    av -= MAD_BUFFER_GUARD;
    if (*_offset > av)
      *_offset = av;
    if (*len > av)
      *len = av;
    g_assert (guard);
    gst_buffer_unref (guard);
  }

  return ret;
}

static GstFlowReturn
gst_mad_handle_frame (GstAudioDecoder * dec, GstBuffer * buffer)
{
  GstMad *mad;
  GstFlowReturn ret = GST_FLOW_UNEXPECTED;
  GstBuffer *outbuffer;
  guint nsamples;
  gint32 *outdata;
  mad_fixed_t const *left_ch, *right_ch;

  mad = GST_MAD (dec);

  /* no fancy draining */
  if (G_UNLIKELY (!buffer))
    return GST_FLOW_OK;

  /* _parse prepared a frame */
  nsamples = MAD_NSBSAMPLES (&mad->frame.header) *
      (mad->stream.options & MAD_OPTION_HALFSAMPLERATE ? 16 : 32);
  GST_LOG_OBJECT (mad, "mad frame with %d samples", nsamples);

  /* arrange for initial caps before pushing data,
   * and update later on if needed */
  gst_mad_check_caps_reset (mad);

  ret = gst_pad_alloc_buffer_and_set_caps (GST_AUDIO_DECODER_SRC_PAD (dec),
      0, nsamples * mad->channels * 4,
      GST_PAD_CAPS (GST_AUDIO_DECODER_SRC_PAD (dec)), &outbuffer);
  if (ret != GST_FLOW_OK) {
    /* Head for the exit, dropping samples as we go */
    GST_LOG_OBJECT (dec,
        "Skipping frame synthesis due to pad_alloc return value");
    gst_audio_decoder_finish_frame (dec, NULL, 1);
    goto exit;
  }

  /* TODO would be nice if core or some helper handled this surprise ... */
  if (GST_BUFFER_SIZE (outbuffer) != nsamples * mad->channels * 4) {
    gst_buffer_unref (outbuffer);
    outbuffer = gst_buffer_new_and_alloc (nsamples * mad->channels * 4);
  }

  mad_synth_frame (&mad->synth, &mad->frame);
  left_ch = mad->synth.pcm.samples[0];
  right_ch = mad->synth.pcm.samples[1];

  outdata = (gint32 *) GST_BUFFER_DATA (outbuffer);

  /* output sample(s) in 16-bit signed native-endian PCM */
  if (mad->channels == 1) {
    gint count = nsamples;

    while (count--) {
      *outdata++ = scale (*left_ch++) & 0xffffffff;
    }
  } else {
    gint count = nsamples;

    while (count--) {
      *outdata++ = scale (*left_ch++) & 0xffffffff;
      *outdata++ = scale (*right_ch++) & 0xffffffff;
    }
  }

  ret = gst_audio_decoder_finish_frame (dec, outbuffer, 1);
exit:
  return ret;
}

static void
gst_mad_flush (GstAudioDecoder * dec, gboolean hard)
{
  GstMad *mad;

  mad = GST_MAD (dec);
  if (hard) {
    mad_frame_mute (&mad->frame);
    mad_synth_mute (&mad->synth);
  }
}

static void
gst_mad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMad *mad;

  mad = GST_MAD (object);

  switch (prop_id) {
    case ARG_HALF:
      mad->half = g_value_get_boolean (value);
      break;
    case ARG_IGNORE_CRC:
      mad->ignore_crc = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMad *mad;

  mad = GST_MAD (object);

  switch (prop_id) {
    case ARG_HALF:
      g_value_set_boolean (value, mad->half);
      break;
    case ARG_IGNORE_CRC:
      g_value_set_boolean (value, mad->ignore_crc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* plugin initialisation */

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (mad_debug, "mad", 0, "mad mp3 decoding");

  return gst_element_register (plugin, "mad", GST_RANK_SECONDARY,
      gst_mad_get_type ());
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mad",
    "mp3 decoding based on the mad library",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
