#ifndef CMARK_GFM_EXPORT_H
#define CMARK_GFM_EXPORT_H

/* We build cmark-gfm sources directly into the mdparser PHP extension
 * shared object. All symbols are internal to that .so and need no
 * explicit visibility attributes. Define CMARK_GFM_STATIC_DEFINE to
 * disable cmark-gfm's own import/export logic. */

#define CMARK_GFM_EXPORT
#define CMARK_GFM_NO_EXPORT
#define CMARK_GFM_DEPRECATED
#define CMARK_GFM_DEPRECATED_EXPORT
#define CMARK_GFM_DEPRECATED_NO_EXPORT

#endif
