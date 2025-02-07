/* GStreamer
 * Copyright (C) 2017 Matthew Waters <matthew@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "icestream.h"
#include "nicetransport.h"

#define GST_CAT_DEFAULT gst_webrtc_ice_stream_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  SIGNAL_0,
  LAST_SIGNAL,
};

enum
{
  PROP_0,
  PROP_ICE,
  PROP_STREAM_ID,
};

//static guint gst_webrtc_ice_stream_signals[LAST_SIGNAL] = { 0 };

struct _GstWebRTCICEStreamPrivate
{
  gboolean gathered;
  GList *transports;
  gboolean gathering_started;
  gulong candidate_gathering_done_id;
};

#define gst_webrtc_ice_stream_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWebRTCICEStream, gst_webrtc_ice_stream,
    GST_TYPE_OBJECT, G_ADD_PRIVATE (GstWebRTCICEStream)
    GST_DEBUG_CATEGORY_INIT (gst_webrtc_ice_stream_debug,
        "webrtcicestream", 0, "webrtcicestream"););

static void
gst_webrtc_ice_stream_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWebRTCICEStream *stream = GST_WEBRTC_ICE_STREAM (object);

  switch (prop_id) {
    case PROP_ICE:
      g_weak_ref_set (&stream->ice_weak, g_value_get_object (value));
      break;
    case PROP_STREAM_ID:
      stream->stream_id = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_ice_stream_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWebRTCICEStream *stream = GST_WEBRTC_ICE_STREAM (object);

  switch (prop_id) {
    case PROP_ICE:
      g_value_set_object (value, g_weak_ref_get (&stream->ice_weak));
      break;
    case PROP_STREAM_ID:
      g_value_set_uint (value, stream->stream_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_webrtc_ice_stream_finalize (GObject * object)
{
  GstWebRTCICEStream *stream = GST_WEBRTC_ICE_STREAM (object);
  GstWebRTCICE *ice = g_weak_ref_get (&stream->ice_weak);

  if (ice) {
    NiceAgent *agent;
    g_object_get (ice, "agent", &agent, NULL);

    if (stream->priv->candidate_gathering_done_id != 0) {
      g_signal_handler_disconnect (agent,
          stream->priv->candidate_gathering_done_id);
    }

    g_object_unref (agent);
    gst_object_unref (ice);
  }

  g_list_free (stream->priv->transports);
  stream->priv->transports = NULL;

  g_weak_ref_clear (&stream->ice_weak);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
_on_candidate_gathering_done (NiceAgent * agent, guint stream_id,
    GWeakRef * ice_weak)
{
  GstWebRTCICEStream *ice = g_weak_ref_get (ice_weak);
  GList *l;

  if (!ice)
    return;

  if (stream_id != ice->stream_id)
    goto cleanup;

  GST_DEBUG_OBJECT (ice, "%u gathering done", stream_id);

  ice->priv->gathered = TRUE;

  for (l = ice->priv->transports; l; l = l->next) {
    GstWebRTCICETransport *ice = l->data;

    gst_webrtc_ice_transport_gathering_state_change (ice,
        GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE);
  }

cleanup:
  gst_object_unref (ice);
}

GstWebRTCICETransport *
gst_webrtc_ice_stream_find_transport (GstWebRTCICEStream * stream,
    GstWebRTCICEComponent component)
{
  GstWebRTCICEComponent trans_comp;
  GstWebRTCICETransport *ret;
  GList *l;

  g_return_val_if_fail (GST_IS_WEBRTC_ICE_STREAM (stream), NULL);

  for (l = stream->priv->transports; l; l = l->next) {
    GstWebRTCICETransport *trans = l->data;
    g_object_get (trans, "component", &trans_comp, NULL);

    if (component == trans_comp)
      return gst_object_ref (trans);
  }

  ret =
      GST_WEBRTC_ICE_TRANSPORT (gst_webrtc_nice_transport_new (stream,
          component));
  stream->priv->transports = g_list_prepend (stream->priv->transports, ret);

  return ret;
}

static GWeakRef *
weak_new (GstWebRTCICEStream * stream)
{
  GWeakRef *weak = g_new0 (GWeakRef, 1);
  g_weak_ref_init (weak, stream);
  return weak;
}

static void
weak_free (GWeakRef * weak)
{
  g_weak_ref_clear (weak);
  g_free (weak);
}

static void
gst_webrtc_ice_stream_constructed (GObject * object)
{
  GstWebRTCICEStream *stream = GST_WEBRTC_ICE_STREAM (object);
  NiceAgent *agent;
  GstWebRTCICE *ice = g_weak_ref_get (&stream->ice_weak);

  g_assert (ice != NULL);
  g_object_get (ice, "agent", &agent, NULL);
  stream->priv->candidate_gathering_done_id = g_signal_connect_data (agent,
      "candidate-gathering-done", G_CALLBACK (_on_candidate_gathering_done),
      weak_new (stream), (GClosureNotify) weak_free, (GConnectFlags) 0);

  g_object_unref (agent);
  gst_object_unref (ice);

  G_OBJECT_CLASS (parent_class)->constructed (object);
}

gboolean
gst_webrtc_ice_stream_gather_candidates (GstWebRTCICEStream * stream)
{
  NiceAgent *agent;
  GList *l;
  GstWebRTCICE *ice;
  gboolean ret = TRUE;

  g_return_val_if_fail (GST_IS_WEBRTC_ICE_STREAM (stream), FALSE);

  GST_DEBUG_OBJECT (stream, "start gathering candidates");

  if (stream->priv->gathered)
    return TRUE;

  for (l = stream->priv->transports; l; l = l->next) {
    GstWebRTCICETransport *trans = l->data;

    gst_webrtc_ice_transport_gathering_state_change (trans,
        GST_WEBRTC_ICE_GATHERING_STATE_GATHERING);
  }

  ice = g_weak_ref_get (&stream->ice_weak);
  g_assert (ice != NULL);

  g_object_get (ice, "agent", &agent, NULL);

  if (!stream->priv->gathering_started) {
    if (ice->min_rtp_port != 0 || ice->max_rtp_port != 65535) {
      if (ice->min_rtp_port > ice->max_rtp_port) {
        GST_ERROR_OBJECT (ice,
            "invalid port range: min-rtp-port %d must be <= max-rtp-port %d",
            ice->min_rtp_port, ice->max_rtp_port);
        ret = FALSE;
        goto cleanup;
      }

      nice_agent_set_port_range (agent, stream->stream_id,
          NICE_COMPONENT_TYPE_RTP, ice->min_rtp_port, ice->max_rtp_port);
    }
    /* mark as gathering started to prevent changing ports again */
    stream->priv->gathering_started = TRUE;
  }

  if (!nice_agent_gather_candidates (agent, stream->stream_id)) {
    ret = FALSE;
    goto cleanup;
  }

  for (l = stream->priv->transports; l; l = l->next) {
    GstWebRTCNiceTransport *trans = l->data;

    gst_webrtc_nice_transport_update_buffer_size (trans);
  }

cleanup:
  if (agent)
    g_object_unref (agent);
  if (ice)
    gst_object_unref (ice);

  return ret;
}

static void
gst_webrtc_ice_stream_class_init (GstWebRTCICEStreamClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->constructed = gst_webrtc_ice_stream_constructed;
  gobject_class->get_property = gst_webrtc_ice_stream_get_property;
  gobject_class->set_property = gst_webrtc_ice_stream_set_property;
  gobject_class->finalize = gst_webrtc_ice_stream_finalize;

  g_object_class_install_property (gobject_class,
      PROP_ICE,
      g_param_spec_object ("ice",
          "ICE", "ICE agent associated with this stream",
          GST_TYPE_WEBRTC_ICE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_STREAM_ID,
      g_param_spec_uint ("stream-id",
          "ICE stream id", "ICE stream id associated with this stream",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
gst_webrtc_ice_stream_init (GstWebRTCICEStream * stream)
{
  stream->priv = gst_webrtc_ice_stream_get_instance_private (stream);

  g_weak_ref_init (&stream->ice_weak, NULL);
}

GstWebRTCICEStream *
gst_webrtc_ice_stream_new (GstWebRTCICE * ice, guint stream_id)
{
  return g_object_new (GST_TYPE_WEBRTC_ICE_STREAM, "ice", ice,
      "stream-id", stream_id, NULL);
}
