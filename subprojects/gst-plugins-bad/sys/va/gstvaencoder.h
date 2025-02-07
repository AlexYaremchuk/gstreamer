/* GStreamer
 *  Copyright (C) 2021 Intel Corporation
 *     Author: He Junyan <junyan.he@intel.com>
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

#pragma once

#include <gst/va/gstva.h>
#include <gst/video/video.h>
#include <va/va.h>

G_BEGIN_DECLS

#define GST_TYPE_VA_ENCODER (gst_va_encoder_get_type())
G_DECLARE_FINAL_TYPE (GstVaEncoder, gst_va_encoder, GST, VA_ENCODER, GstObject);

typedef struct _GstVaEncodePicture GstVaEncodePicture;
struct _GstVaEncodePicture
{
  GstVaDisplay *display;

  /* picture parameters */
  GArray *params;

  GstBuffer *raw_buffer;
  GstBuffer *reconstruct_buffer;

  VABufferID coded_buffer;
};

#define GST_TYPE_VA_ENCODER_RATE_CONTROL (gst_va_encoder_rate_control_get_type())
GType                 gst_va_encoder_rate_control_get_type (void);

gboolean              gst_va_encoder_is_open              (GstVaEncoder * self);
gboolean              gst_va_encoder_open                 (GstVaEncoder * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint,
                                                           GstVideoFormat video_format,
                                                           guint rt_format,
                                                           gint coded_width,
                                                           gint coded_height,
                                                           gint codedbuf_size,
                                                           guint max_reconstruct_surfaces,
                                                           guint rc_ctrl,
                                                           guint32 packed_headers);
gboolean              gst_va_encoder_close                (GstVaEncoder * self);
gboolean              gst_va_encoder_has_profile_and_entrypoint (GstVaEncoder * self,
                                                                 VAProfile profile,
                                                                 VAEntrypoint entrypoint);
gint                  gst_va_encoder_get_max_slice_num    (GstVaEncoder * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint);
gboolean              gst_va_encoder_get_max_num_reference (GstVaEncoder * self,
                                                            VAProfile profile,
                                                            VAEntrypoint entrypoint,
                                                            guint32 * list0,
                                                            guint32 * list1);
guint32               gst_va_encoder_get_rate_control_mode (GstVaEncoder * self,
                                                            VAProfile profile,
                                                            VAEntrypoint entrypoint);
guint32               gst_va_encoder_get_quality_level    (GstVaEncoder * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint);
gboolean              gst_va_encoder_has_trellis          (GstVaEncoder * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint);
guint32               gst_va_encoder_get_rtformat         (GstVaEncoder * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint);
guint32               gst_va_encoder_get_packed_headers   (GstVaEncoder * self,
                                                           VAProfile profile,
                                                           VAEntrypoint entrypoint);
gboolean              gst_va_encoder_add_param            (GstVaEncoder * self,
                                                           GstVaEncodePicture * pic,
                                                           VABufferType type,
                                                           gpointer data,
                                                           gsize size);
gboolean              gst_va_encoder_add_packed_header    (GstVaEncoder * self,
                                                           GstVaEncodePicture * pic,
                                                           gint type,
                                                           gpointer data,
                                                           gsize size_in_bits,
                                                           gboolean has_emulation_bytes);
GstVaEncoder *        gst_va_encoder_new                  (GstVaDisplay * display,
                                                           guint32 codec);
GArray *              gst_va_encoder_get_surface_formats  (GstVaEncoder * self);
GstCaps *             gst_va_encoder_get_sinkpad_caps     (GstVaEncoder * self);
GstCaps *             gst_va_encoder_get_srcpad_caps      (GstVaEncoder * self);
gboolean              gst_va_encoder_encode               (GstVaEncoder * self,
                                                           GstVaEncodePicture * pic);

GstVaEncodePicture *  gst_va_encode_picture_new           (GstVaEncoder * self,
                                                           GstBuffer * raw_buffer);
void                  gst_va_encode_picture_free          (GstVaEncodePicture * pic);
VASurfaceID           gst_va_encode_picture_get_raw_surface (GstVaEncodePicture * pic);
VASurfaceID           gst_va_encode_picture_get_reconstruct_surface (GstVaEncodePicture * pic);

G_END_DECLS
