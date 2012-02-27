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

#ifndef __GST_SANDBOXEDDECODEBIN_H__
#define __GST_SANDBOXEDDECODEBIN_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_SANDBOXED_DECODEBIN_TYPE (gst_sandboxed_decodebin_get_type ())
#define GST_SANDBOXED_DECODEBIN(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_SANDBOXED_DECODEBIN_TYPE, GstSandboxedDecodebin))
#define GST_SANDBOXED_DECODEBIN_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GST_SANDBOXED_DECODEBIN_TYPE, GstSandboxedDecodebinClass))
#define IS_GST_SANDBOXED_DECODEBIN(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_SANDBOXED_DECODEBIN_TYPE))
#define IS_GST_SANDBOXED_DECODEBIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_SANDBOXED_DECODEBIN_TYPE))
#define GST_SANDBOXED_DECODEBIN_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_SANDBOXED_DECODEBIN_TYPE, GstSandboxedDecodebinClass))

typedef struct _GstSandboxedDecodeBin GstSandboxedDecodebin;
typedef struct _GstSandboxedDecodeBinClass GstSandboxedDecodebinClass;

typedef struct _GstSandboxedDecodebinPrivate GstSandboxedDecodebinPrivate;

struct _GstSandboxedDecodeBin {
  GstBin parent;

  GstSandboxedDecodebinPrivate *priv;
};

struct _GstSandboxedDecodeBinClass {
  GstBinClass parent;
};

GType gst_sandboxed_decodebin_get_type (void);

G_END_DECLS

#endif /* __GST_SANDBOXEDDECODEBIN_H__ */
