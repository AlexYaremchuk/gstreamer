/* GStreamer Editing Services
 *
 * Copyright (C) <2015> Thibault Saunier <tsaunier@gnome.org>
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-structured-interface.h"
#include "ges-internal.h"

#include <string.h>


#define LAST_CONTAINER_QDATA g_quark_from_string("ges-structured-last-container")
#define LAST_CHILD_QDATA g_quark_from_string("ges-structured-last-child")

#define GET_AND_CHECK(name,type,var,label) G_STMT_START {\
  gboolean found = FALSE; \
\
  if (type == GST_TYPE_CLOCK_TIME) {\
    found = ges_util_structure_get_clocktime (structure,name, (GstClockTime*)var,NULL);\
  }\
  else { \
    found = gst_structure_get (structure, name, type, var, NULL); \
  }\
  if (!found) {\
    gchar *struct_str = gst_structure_to_string (structure); \
    *error = g_error_new (GES_ERROR, 0, \
        "Could not get the mandatory field '%s'" \
        " of type %s - fields in %s", name, g_type_name (type), struct_str); \
    g_free (struct_str); \
    goto label;\
  } \
} G_STMT_END

#define TRY_GET_STRING(name,var,def) G_STMT_START {\
  *var = gst_structure_get_string (structure, name); \
  if (*var == NULL) \
    *var = def; \
} G_STMT_END

#define TRY_GET_TIME(name, var, var_frames, def) G_STMT_START  {       \
  if (!ges_util_structure_get_clocktime (structure, name, var, var_frames)) { \
      *var = def;                                          \
      *var_frames = GES_FRAME_NUMBER_NONE;                            \
  }                                                        \
} G_STMT_END

#define TRY_GET(name, type, var, def) G_STMT_START {\
  g_assert (type != GST_TYPE_CLOCK_TIME);                      \
  if (!gst_structure_get (structure, name, type, var, NULL))\
    *var = def;                                             \
} G_STMT_END

typedef struct
{
  const gchar **fields;
  GList *invalid_fields;
} FieldsError;

static gboolean
_check_field (GQuark field_id, const GValue * value, FieldsError * fields_error)
{
  guint i;
  const gchar *field = g_quark_to_string (field_id);

  for (i = 0; fields_error->fields[i]; i++) {
    if (g_strcmp0 (fields_error->fields[i], field) == 0) {

      return TRUE;
    }
  }

  fields_error->invalid_fields =
      g_list_append (fields_error->invalid_fields, (gpointer) field);

  return TRUE;
}

static gboolean
_check_fields (GstStructure * structure, FieldsError fields_error,
    GError ** error)
{
  gst_structure_foreach (structure,
      (GstStructureForeachFunc) _check_field, &fields_error);

  if (fields_error.invalid_fields) {
    GList *tmp;
    const gchar *struct_name = gst_structure_get_name (structure);
    GString *msg = g_string_new (NULL);

    g_string_append_printf (msg, "Unknown propert%s in %s%s:",
        g_list_length (fields_error.invalid_fields) > 1 ? "ies" : "y",
        strlen (struct_name) > 1 ? "--" : "-",
        gst_structure_get_name (structure));

    for (tmp = fields_error.invalid_fields; tmp; tmp = tmp->next)
      g_string_append_printf (msg, " %s", (gchar *) tmp->data);

    if (error)
      *error = g_error_new_literal (GES_ERROR, 0, msg->str);

    g_string_free (msg, TRUE);

    return FALSE;
  }

  return TRUE;
}

gboolean
_ges_save_timeline_if_needed (GESTimeline * timeline, GstStructure * structure,
    GError ** error)
{
  gboolean res = TRUE;
  const gchar *nested_timeline_id =
      gst_structure_get_string (structure, "project-uri");

  if (nested_timeline_id) {
    res = ges_timeline_save_to_uri (timeline, nested_timeline_id, NULL, TRUE,
        error);
  }

  return res;
}

gboolean
_ges_add_remove_keyframe_from_struct (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  GESTrackElement *element;

  gboolean absolute;
  gdouble value;
  GstClockTime timestamp;
  GstControlBinding *binding = NULL;
  GstTimedValueControlSource *source = NULL;
  gchar *element_name = NULL, *property_name = NULL;

  gboolean ret = FALSE;

  const gchar *valid_fields[] =
      { "element-name", "property-name", "value", "timestamp", "project-uri",
    NULL
  };

  FieldsError fields_error = { valid_fields, NULL };

  if (!_check_fields (structure, fields_error, error))
    return FALSE;

  GET_AND_CHECK ("element-name", G_TYPE_STRING, &element_name, done);
  GET_AND_CHECK ("property-name", G_TYPE_STRING, &property_name, done);
  GET_AND_CHECK ("timestamp", GST_TYPE_CLOCK_TIME, &timestamp, done);

  element =
      GES_TRACK_ELEMENT (ges_timeline_get_element (timeline, element_name));

  if (!GES_IS_TRACK_ELEMENT (element)) {
    *error =
        g_error_new (GES_ERROR, 0, "Could not find TrackElement %s",
        element_name);

    goto done;
  }

  binding = ges_track_element_get_control_binding (element, property_name);
  if (binding == NULL) {
    *error = g_error_new (GES_ERROR, 0, "No control binding found for %s:%s"
        " you should first set-control-binding on it",
        element_name, property_name);

    goto done;
  }

  g_object_get (binding, "control-source", &source, NULL);

  if (source == NULL) {
    *error = g_error_new (GES_ERROR, 0, "No control source found for %s:%s"
        " you should first set-control-binding on it",
        element_name, property_name);

    goto done;
  }

  if (!GST_IS_TIMED_VALUE_CONTROL_SOURCE (source)) {
    *error = g_error_new (GES_ERROR, 0, "You can use add-keyframe"
        " only on GstTimedValueControlSource not %s",
        G_OBJECT_TYPE_NAME (source));

    goto done;
  }

  g_object_get (binding, "absolute", &absolute, NULL);
  if (absolute) {
    GParamSpec *pspec;
    const GValue *v;
    GValue v2 = G_VALUE_INIT;

    if (!ges_timeline_element_lookup_child (GES_TIMELINE_ELEMENT (element),
            property_name, NULL, &pspec)) {
      *error =
          g_error_new (GES_ERROR, 0, "Could not get property %s for %s",
          property_name, GES_TIMELINE_ELEMENT_NAME (element));
      goto done;
    }

    v = gst_structure_get_value (structure, "value");
    if (!v) {
      gchar *struct_str = gst_structure_to_string (structure);

      *error = g_error_new (GES_ERROR, 0,
          "Could not get the mandatory field 'value'"
          " of type %s - fields in %s", g_type_name (pspec->value_type),
          struct_str);
      g_free (struct_str);
      goto done;
    }

    g_value_init (&v2, G_TYPE_DOUBLE);
    if (!g_value_transform (v, &v2)) {
      gchar *struct_str = gst_structure_to_string (structure);

      *error = g_error_new (GES_ERROR, 0,
          "Could not get the mandatory field 'value'"
          " of type %s - fields in %s", g_type_name (pspec->value_type),
          struct_str);
      g_free (struct_str);
      goto done;
    }
    value = g_value_get_double (&v2);
    g_value_reset (&v2);
  } else
    GET_AND_CHECK ("value", G_TYPE_DOUBLE, &value, done);

  if (!g_strcmp0 (gst_structure_get_name (structure), "add-keyframe"))
    ret = gst_timed_value_control_source_set (source, timestamp, value);
  else {
    ret = gst_timed_value_control_source_unset (source, timestamp);

    if (!ret) {
      *error =
          g_error_new (GES_ERROR, 0,
          "Could not unset value for timestamp: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timestamp));
    }
  }
  ret = _ges_save_timeline_if_needed (timeline, structure, error);

done:
  if (source)
    gst_object_unref (source);
  g_free (element_name);
  g_free (property_name);

  return ret;

}

GESAsset *
_ges_get_asset_from_timeline (GESTimeline * timeline, GType type,
    const gchar * id, GError ** error)
{
  GESAsset *asset;
  GESProject *project = ges_timeline_get_project (timeline);
  GError *err = NULL;

  asset = ges_project_create_asset_sync (project, id, type, &err);

  if (err)
    g_propagate_error (error, err);
  if (!asset || (error && *error)) {

    if (error && !*error) {
      *error = g_error_new (GES_ERROR, 0,
          "There was an error requesting the asset with id %s and type %s (%s)",
          id, g_type_name (type), "unknown");
    }

    GST_ERROR
        ("There was an error requesting the asset with id %s and type %s (%s)",
        id, g_type_name (type), error ? (*error)->message : "unknown");

    return NULL;
  }

  return asset;
}

/* Unref after usage */
GESLayer *
_ges_get_layer_by_priority (GESTimeline * timeline, gint priority)
{
  GList *layers;
  gint nlayers;
  GESLayer *layer = NULL;

  priority = MAX (priority, 0);
  layers = ges_timeline_get_layers (timeline);
  nlayers = (gint) g_list_length (layers);
  if (priority >= nlayers) {
    gint i = nlayers;

    while (i <= priority) {
      layer = ges_timeline_append_layer (timeline);

      i++;
    }

    layer = gst_object_ref (layer);

    goto done;
  }

  layer = ges_timeline_get_layer (timeline, priority);

done:
  g_list_free_full (layers, gst_object_unref);

  return layer;
}

static gchar *
ensure_uri (gchar * location)
{
  if (gst_uri_is_valid (location))
    return g_strdup (location);
  else
    return gst_filename_to_uri (location, NULL);
}

static gboolean
get_flags_from_string (GType type, const gchar * str_flags, guint * flags)
{
  GValue value = G_VALUE_INIT;
  g_value_init (&value, type);

  if (!gst_value_deserialize (&value, str_flags)) {
    g_value_unset (&value);

    return FALSE;
  }

  *flags = g_value_get_flags (&value);
  g_value_unset (&value);

  return TRUE;
}

gboolean
_ges_add_clip_from_struct (GESTimeline * timeline, GstStructure * structure,
    GError ** error)
{
  GESAsset *asset = NULL;
  GESLayer *layer = NULL;
  GESClip *clip;
  gint layer_priority;
  const gchar *name;
  const gchar *text;
  const gchar *pattern;
  const gchar *track_types_str;
  const gchar *nested_timeline_id;
  gchar *asset_id = NULL;
  gchar *check_asset_id = NULL;
  const gchar *type_string;
  GType type;
  gboolean res = FALSE;
  GESTrackType track_types = GES_TRACK_TYPE_UNKNOWN;

  GESFrameNumber start_frame = GES_FRAME_NUMBER_NONE, inpoint_frame =
      GES_FRAME_NUMBER_NONE, duration_frame = GES_FRAME_NUMBER_NONE;
  GstClockTime duration = 1 * GST_SECOND, inpoint = 0, start =
      GST_CLOCK_TIME_NONE;

  const gchar *valid_fields[] =
      { "asset-id", "pattern", "name", "layer-priority", "layer", "type",
    "start", "inpoint", "duration", "text", "track-types", "project-uri",
    NULL
  };

  FieldsError fields_error = { valid_fields, NULL };

  if (!_check_fields (structure, fields_error, error))
    return FALSE;

  GET_AND_CHECK ("asset-id", G_TYPE_STRING, &check_asset_id, beach);

  TRY_GET_STRING ("pattern", &pattern, NULL);
  TRY_GET_STRING ("text", &text, NULL);
  TRY_GET_STRING ("name", &name, NULL);
  TRY_GET ("layer-priority", G_TYPE_INT, &layer_priority, -1);
  if (layer_priority == -1)
    TRY_GET ("layer", G_TYPE_INT, &layer_priority, -1);
  TRY_GET_STRING ("type", &type_string, "GESUriClip");
  TRY_GET_TIME ("start", &start, &start_frame, GST_CLOCK_TIME_NONE);
  TRY_GET_TIME ("inpoint", &inpoint, &inpoint_frame, 0);
  TRY_GET_TIME ("duration", &duration, &duration_frame, GST_CLOCK_TIME_NONE);
  TRY_GET_STRING ("track-types", &track_types_str, NULL);
  TRY_GET_STRING ("project-uri", &nested_timeline_id, NULL);

  if (track_types_str) {
    if (!get_flags_from_string (GES_TYPE_TRACK_TYPE, track_types_str,
            &track_types)) {
      *error =
          g_error_new (GES_ERROR, 0, "Invalid track types: %s",
          track_types_str);
    }

  }

  if (!(type = g_type_from_name (type_string))) {
    *error = g_error_new (GES_ERROR, 0, "This type doesn't exist : %s",
        type_string);

    goto beach;
  }

  if (type == GES_TYPE_URI_CLIP) {
    asset_id = ensure_uri (check_asset_id);
  } else {
    asset_id = g_strdup (check_asset_id);
  }

  gst_structure_set (structure, "asset-id", G_TYPE_STRING, asset_id, NULL);
  asset = _ges_get_asset_from_timeline (timeline, type, asset_id, error);
  if (!asset) {
    res = FALSE;

    goto beach;
  }

  if (layer_priority == -1) {
    GESContainer *container;

    container = g_object_get_qdata (G_OBJECT (timeline), LAST_CONTAINER_QDATA);
    if (!container || !GES_IS_CLIP (container))
      layer = _ges_get_layer_by_priority (timeline, 0);
    else
      layer = ges_clip_get_layer (GES_CLIP (container));

    if (!layer)
      layer = _ges_get_layer_by_priority (timeline, 0);
  } else {
    layer = _ges_get_layer_by_priority (timeline, layer_priority);
  }

  if (!layer) {
    *error =
        g_error_new (GES_ERROR, 0, "No layer with priority %d", layer_priority);
    goto beach;
  }

  if (GES_FRAME_NUMBER_IS_VALID (start_frame))
    start = ges_timeline_get_frame_time (timeline, start_frame);

  if (GES_FRAME_NUMBER_IS_VALID (inpoint_frame)) {
    inpoint =
        ges_clip_asset_get_frame_time (GES_CLIP_ASSET (asset), inpoint_frame);
    if (!GST_CLOCK_TIME_IS_VALID (inpoint)) {
      *error =
          g_error_new (GES_ERROR, 0, "Could not get inpoint from frame %"
          G_GINT64_FORMAT, inpoint_frame);
      goto beach;
    }
  }

  if (GES_FRAME_NUMBER_IS_VALID (duration_frame)) {
    duration = ges_timeline_get_frame_time (timeline, duration_frame);
  }

  if (GES_IS_URI_CLIP_ASSET (asset) && !GST_CLOCK_TIME_IS_VALID (duration)) {
    duration = GST_CLOCK_DIFF (inpoint,
        ges_uri_clip_asset_get_duration (GES_URI_CLIP_ASSET (asset)));
  }

  clip = ges_layer_add_asset (layer, asset, start, inpoint, duration,
      track_types);

  if (clip) {
    res = TRUE;

    if (GES_TIMELINE_ELEMENT_DURATION (clip) == 0) {
      *error = g_error_new (GES_ERROR, 0,
          "Clip %s has 0 as duration, please provide a proper duration",
          asset_id);
      res = FALSE;
      goto beach;
    }


    if (GES_IS_TEST_CLIP (clip)) {
      if (pattern) {
        GEnumClass *enum_class =
            G_ENUM_CLASS (g_type_class_ref (GES_VIDEO_TEST_PATTERN_TYPE));
        GEnumValue *value = g_enum_get_value_by_nick (enum_class, pattern);

        if (!value) {
          res = FALSE;
          goto beach;
        }

        ges_test_clip_set_vpattern (GES_TEST_CLIP (clip), value->value);
        g_type_class_unref (enum_class);
      }
    }

    if (GES_IS_TITLE_CLIP (clip) && text)
      ges_timeline_element_set_child_properties (GES_TIMELINE_ELEMENT (clip),
          "text", text, NULL);

    if (name
        && !ges_timeline_element_set_name (GES_TIMELINE_ELEMENT (clip), name)) {
      res = FALSE;
      *error =
          g_error_new (GES_ERROR, 0, "couldn't set name %s on clip with id %s",
          name, asset_id);
    }
  } else {
    *error =
        g_error_new (GES_ERROR, 0,
        "Couldn't add clip with id %s to layer with priority %d", asset_id,
        layer_priority);
    res = FALSE;
    goto beach;
  }

  if (res) {
    g_object_set_qdata (G_OBJECT (timeline), LAST_CONTAINER_QDATA, clip);
    g_object_set_qdata (G_OBJECT (timeline), LAST_CHILD_QDATA, NULL);
  }

  res = _ges_save_timeline_if_needed (timeline, structure, error);

beach:
  gst_clear_object (&layer);
  gst_clear_object (&asset);
  g_free (asset_id);
  g_free (check_asset_id);
  return res;
}

gboolean
_ges_container_add_child_from_struct (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  GESAsset *asset = NULL;
  GESContainer *container;
  GESTimelineElement *child = NULL;
  const gchar *container_name, *child_name, *child_type, *id;

  gboolean res = TRUE;
  const gchar *valid_fields[] = { "container-name", "asset-id",
    "child-type", "child-name", "project-uri", NULL
  };

  FieldsError fields_error = { valid_fields, NULL };

  if (!_check_fields (structure, fields_error, error))
    return FALSE;

  container_name = gst_structure_get_string (structure, "container-name");

  if (container_name == NULL) {
    container = g_object_get_qdata (G_OBJECT (timeline), LAST_CONTAINER_QDATA);
  } else {
    container =
        GES_CONTAINER (ges_timeline_get_element (timeline, container_name));
  }

  if (!GES_IS_CONTAINER (container)) {
    *error =
        g_error_new (GES_ERROR, 0, "Could not find container: %s (%p)",
        container_name, container);

    res = FALSE;
    goto beach;
  }

  id = gst_structure_get_string (structure, "asset-id");
  child_type = gst_structure_get_string (structure, "child-type");

  if (id && child_type) {
    asset =
        _ges_get_asset_from_timeline (timeline, g_type_from_name (child_type),
        id, error);

    if (asset == NULL) {
      res = FALSE;
      goto beach;
    }

    child = GES_TIMELINE_ELEMENT (ges_asset_extract (asset, NULL));
    if (!GES_IS_TIMELINE_ELEMENT (child)) {
      *error = g_error_new (GES_ERROR, 0, "Could not extract child element");

      goto beach;
    }
  }

  child_name = gst_structure_get_string (structure, "child-name");
  if (!child && child_name) {
    child = ges_timeline_get_element (timeline, child_name);
    if (!GES_IS_TIMELINE_ELEMENT (child)) {
      *error = g_error_new (GES_ERROR, 0, "Could not find child element");

      goto beach;
    }
  }

  if (!child) {
    *error =
        g_error_new (GES_ERROR, 0, "Wrong parameters, could not get a child");

    return FALSE;
  }

  if (child_name)
    ges_timeline_element_set_name (child, child_name);
  else
    child_name = GES_TIMELINE_ELEMENT_NAME (child);

  res = ges_container_add (container, child);
  if (res == FALSE) {
    g_error_new (GES_ERROR, 0, "Could not add child to container");
  } else {
    g_object_set_qdata (G_OBJECT (timeline), LAST_CHILD_QDATA, child);
  }
  res = _ges_save_timeline_if_needed (timeline, structure, error);

beach:
  gst_clear_object (&asset);
  return res;
}

gboolean
_ges_set_child_property_from_struct (GESTimeline * timeline,
    GstStructure * structure, GError ** error)
{
  const GValue *value;
  GESTimelineElement *element;
  const gchar *property_name, *element_name;

  const gchar *valid_fields[] =
      { "element-name", "property", "value", "project-uri", NULL };

  FieldsError fields_error = { valid_fields, NULL };

  if (!_check_fields (structure, fields_error, error))
    return FALSE;

  element_name = gst_structure_get_string (structure, "element-name");
  if (element_name == NULL)
    element = g_object_get_qdata (G_OBJECT (timeline), LAST_CHILD_QDATA);
  else
    element = ges_timeline_get_element (timeline, element_name);

  property_name = gst_structure_get_string (structure, "property");
  if (property_name == NULL) {
    const gchar *name = gst_structure_get_name (structure);

    if (g_str_has_prefix (name, "set-"))
      property_name = &name[4];
    else {
      gchar *struct_str = gst_structure_to_string (structure);

      *error =
          g_error_new (GES_ERROR, 0, "Could not find any property name in %s",
          struct_str);
      g_free (struct_str);

      return FALSE;
    }
  }

  if (element) {
    if (!ges_track_element_lookup_child (GES_TRACK_ELEMENT (element),
            property_name, NULL, NULL))
      element = NULL;
  }

  if (!element) {
    element = g_object_get_qdata (G_OBJECT (timeline), LAST_CONTAINER_QDATA);

    if (element == NULL) {
      *error =
          g_error_new (GES_ERROR, 0,
          "Could not find anywhere to set property: %s", property_name);

      return FALSE;
    }
  }

  if (!GES_IS_TIMELINE_ELEMENT (element)) {
    *error =
        g_error_new (GES_ERROR, 0, "Could not find child %s", element_name);

    return FALSE;
  }

  value = gst_structure_get_value (structure, "value");

  g_print ("%s Setting %s property to %s\n", element->name, property_name,
      gst_value_serialize (value));

  if (!ges_timeline_element_set_child_property (element, property_name,
          (GValue *) value)) {
    guint n_specs, i;
    GParamSpec **specs =
        ges_timeline_element_list_children_properties (element, &n_specs);
    GString *errstr = g_string_new (NULL);

    g_string_append_printf (errstr,
        "\n  Could not set property `%s` on `%s`, valid properties:\n",
        property_name, GES_TIMELINE_ELEMENT_NAME (element));

    for (i = 0; i < n_specs; i++)
      g_string_append_printf (errstr, "    - %s\n", specs[i]->name);
    g_free (specs);

    *error = g_error_new_literal (GES_ERROR, 0, errstr->str);
    g_string_free (errstr, TRUE);

    return FALSE;
  }
  return _ges_save_timeline_if_needed (timeline, structure, error);
}

#undef GET_AND_CHECK
#undef TRY_GET