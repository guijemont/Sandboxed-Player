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

#include "gstsandboxeddecodebin.h"

GST_DEBUG_CATEGORY_STATIC (gst_debug_sandboxed_decodebin);
#define GST_CAT_DEFAULT gst_debug_sandboxed_decodebin

G_DEFINE_TYPE (GstSandboxedDecodebin, gst_sandboxed_decodebin, <ENTER_TYPE_HERE>);

#define GST_SANDBOXED_DECODEBIN_GET_PRIVATE(o)\
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_SANDBOXED_DECODEBIN_TYPE, GstSandboxedDecodebinPrivate))

struct _GstSandboxedDecodebinPrivate {
};

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

  priv = GST_SANDBOXED_DECODEBIN_GET_PRIVATE (self);
}

static void
gst_sandboxed_decodebin_class_init (GstSandboxedDecodebinClass *self_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (self_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (self_class);

  g_type_class_add_private (self_class, sizeof (GstSandboxedDecodebinPrivate));
  object_class->dispose = (void (*) (GObject *object)) gst_sandboxed_decodebin_dispose;
  object_class->finalize = (void (*) (GObject *object)) gst_sandboxed_decodebin_finalize;
}
