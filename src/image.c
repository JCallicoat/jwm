/**
 * @file image.c
 * @author Joe Wingbermuehle
 * @date 2005-2007
 *
 * @brief Functions to load images.
 *
 */

#include "jwm.h"

#ifndef MAKE_DEPEND

   /* We should include png.h here. See jwm.h for an explanation. */

#  ifdef USE_XPM
#     include <X11/xpm.h>
#  endif
#  ifdef USE_JPEG
#     include <jpeglib.h>
#  endif
#  ifdef USE_CAIRO
#     include <cairo.h>
#     include <cairo-svg.h>
#  endif
#  ifdef USE_RSVG
#     include <librsvg/rsvg.h>
#     ifdef USE_CAIRO
#        include <librsvg/rsvg-cairo.h>
#     endif
#  endif
#endif /* MAKE_DEPEND */

#include "image.h"
#include "main.h"
#include "error.h"
#include "color.h"

#ifdef USE_CAIRO
#ifdef USE_RSVG
static ImageNode *LoadSVGImage(const char *fileName);
#endif
#endif
#ifdef USE_JPEG
static ImageNode *LoadJPEGImage(const char *fileName);
#endif
#ifdef USE_PNG
static ImageNode *LoadPNGImage(const char *fileName);
#endif
#ifdef USE_XPM
static ImageNode *LoadXPMImage(const char *fileName);
#endif
#ifdef USE_ICONS
#ifdef USE_XPM
static ImageNode *CreateImageFromXImages(XImage *image, XImage *shape);
#endif
#endif

#ifdef USE_XPM
static int AllocateColor(Display *d, Colormap cmap, char *name,
                         XColor *c, void *closure);
static int FreeColors(Display *d, Colormap cmap, Pixel *pixels, int n,
                      void *closure);
#endif

/** Load an image from the specified file. */
ImageNode *LoadImage(const char *fileName)
{

   ImageNode *result = NULL;

   if(!fileName) {
      return result;
   }

   /* Attempt to load the file as a PNG image. */
#ifdef USE_PNG
   result = LoadPNGImage(fileName);
   if(result) {
      return result;
   }
#endif

   /* Attempt to load the file as a JPEG image. */
#ifdef USE_JPEG
   result = LoadJPEGImage(fileName);
   if(result) {
      return result;
   }
#endif

   /* Attempt to load the file as an SVG image. */
#ifdef USE_CAIRO
#ifdef USE_RSVG
   result = LoadSVGImage(fileName);
   if(result) {
      return result;
   }
#endif
#endif

   /* Attempt to load the file as an XPM image. */
#ifdef USE_XPM
   result = LoadXPMImage(fileName);
   if(result) {
      return result;
   }
#endif

   return result;

}

/** Load an image from the specified XPM data. */
ImageNode *LoadImageFromData(char **data)
{

   ImageNode *result = NULL;

#ifdef USE_XPM

   XpmAttributes attr;
   XImage *image;
   XImage *shape;
   int rc;

   Assert(data);

   attr.valuemask = XpmAllocColor | XpmFreeColors | XpmColorClosure;
   attr.alloc_color = AllocateColor;
   attr.free_colors = FreeColors;
   attr.color_closure = NULL;
   rc = XpmCreateImageFromData(display, data, &image, &shape, &attr);
   if(rc == XpmSuccess) {
      result = CreateImageFromXImages(image, shape);
      JXDestroyImage(image);
      if(shape) {
         JXDestroyImage(shape);
      }
   }

#endif

   return result;

}

/** Load a PNG image from the given file name.
 * Since libpng uses longjmp, this function is not reentrant to simplify
 * the issues surrounding longjmp and local variables.
 */
#ifdef USE_PNG
ImageNode *LoadPNGImage(const char *fileName)
{

   static ImageNode *result;
   static FILE *fd;
   static unsigned char **rows;
   static png_structp pngData;
   static png_infop pngInfo;
   static png_infop pngEndInfo;

   unsigned char header[8];
   unsigned long rowBytes;
   int bitDepth, colorType;
   unsigned int x, y;
   png_uint_32 width;
   png_uint_32 height;

   Assert(fileName);

   result = NULL;
   fd = NULL;
   rows = NULL;
   pngData = NULL;
   pngInfo = NULL;
   pngEndInfo = NULL;

   fd = fopen(fileName, "rb");
   if(!fd) {
      return NULL;
   }

   x = fread(header, 1, sizeof(header), fd);
   if(x != sizeof(header) || png_sig_cmp(header, 0, sizeof(header))) {
      fclose(fd);
      return NULL;
   }

   pngData = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
   if(JUNLIKELY(!pngData)) {
      fclose(fd);
      Warning(_("could not create read struct for PNG image: %s"), fileName);
      return NULL;
   }

   if(JUNLIKELY(setjmp(png_jmpbuf(pngData)))) {
      png_destroy_read_struct(&pngData, &pngInfo, &pngEndInfo);
      if(fd) {
         fclose(fd);
      }
      if(rows) {
         ReleaseStack(rows);
      }
      DestroyImage(result);
      Warning(_("error reading PNG image: %s"), fileName);
      return NULL;
   }

   pngInfo = png_create_info_struct(pngData);
   if(JUNLIKELY(!pngInfo)) {
      png_destroy_read_struct(&pngData, NULL, NULL);
      fclose(fd);
      Warning(_("could not create info struct for PNG image: %s"), fileName);
      return NULL;
   }

   pngEndInfo = png_create_info_struct(pngData);
   if(JUNLIKELY(!pngEndInfo)) {
      png_destroy_read_struct(&pngData, &pngInfo, NULL);
      fclose(fd);
      Warning("could not create end info struct for PNG image: %s", fileName);
      return NULL;
   }

   png_init_io(pngData, fd);
   png_set_sig_bytes(pngData, sizeof(header));

   png_read_info(pngData, pngInfo);

   result = Allocate(sizeof(ImageNode));
   result->data = NULL;

   png_get_IHDR(pngData, pngInfo, &width, &height,
                &bitDepth, &colorType, NULL, NULL, NULL);
   result->width = (int)width;
   result->height = (int)height;

   png_set_expand(pngData);

   if(bitDepth == 16) {
      png_set_strip_16(pngData);
   } else if(bitDepth < 8) {
      png_set_packing(pngData);
   }

   png_set_swap_alpha(pngData);
   png_set_filler(pngData, 0xFF, PNG_FILLER_BEFORE);

   if(colorType == PNG_COLOR_TYPE_GRAY
      || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
      png_set_gray_to_rgb(pngData);
   }

   png_read_update_info(pngData, pngInfo);

   rowBytes = png_get_rowbytes(pngData, pngInfo);
   result->data = Allocate(rowBytes * result->height);

   rows = AllocateStack(result->height * sizeof(result->data));
   y = 0;
   for(x = 0; x < result->height; x++) {
      rows[x] = &result->data[y];
      y += rowBytes;
   }

   png_read_image(pngData, rows);

   png_read_end(pngData, pngInfo);
   png_destroy_read_struct(&pngData, &pngInfo, &pngEndInfo);

   fclose(fd);

   ReleaseStack(rows);
   rows = NULL;

   return result;

}
#endif /* USE_PNG */

/** Load a JPEG image from the specified file. */
#ifdef USE_JPEG

typedef struct {
   struct jpeg_error_mgr pub;
   jmp_buf jbuffer;
} JPEGErrorStruct;

static void JPEGErrorHandler(j_common_ptr cinfo) {
   JPEGErrorStruct *es = (JPEGErrorStruct*)cinfo->err;
   longjmp(es->jbuffer, 1);
}

ImageNode *LoadJPEGImage(const char *fileName)
{

   static ImageNode *result;
   static struct jpeg_decompress_struct cinfo;
   static FILE *fd;
   static JSAMPARRAY buffer;
   static JPEGErrorStruct jerr;

   int rowStride;
   int x;
   int inIndex, outIndex;

   /* Open the file. */
   fd = fopen(fileName, "rb");
   if(fd == NULL) {
      return NULL;
   }

   /* Make sure everything is initialized so we can recover from errors. */
   result = NULL;
   buffer = NULL;

   /* Setup the error handler. */
   cinfo.err = jpeg_std_error(&jerr.pub);
   jerr.pub.error_exit = JPEGErrorHandler;

   /* Control will return here if an error was encountered. */
   if(setjmp(jerr.jbuffer)) {
      DestroyImage(result);
      jpeg_destroy_decompress(&cinfo);
      fclose(fd);
      return NULL;
   }

   /* Prepare to load the file. */
   jpeg_create_decompress(&cinfo);
   jpeg_stdio_src(&cinfo, fd);

   /* Check the header. */
   jpeg_read_header(&cinfo, TRUE);

   /* Start decompression. */
   jpeg_start_decompress(&cinfo);
   rowStride = cinfo.output_width * cinfo.output_components;
   buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo,
                                       JPOOL_IMAGE, rowStride, 1);

   result = Allocate(sizeof(ImageNode));
   result->width = cinfo.image_width;
   result->height = cinfo.image_height;
   result->data = Allocate(4 * result->width * result->height);

   /* Read lines. */
   outIndex = 0;
   while(cinfo.output_scanline < cinfo.output_height) {
      jpeg_read_scanlines(&cinfo, buffer, 1);
      inIndex = 0;
      for(x = 0; x < result->width; x++) {
         switch(cinfo.output_components) {
         case 1:  /* Grayscale. */
            result->data[outIndex + 1] = GETJSAMPLE(buffer[0][inIndex]);
            result->data[outIndex + 2] = GETJSAMPLE(buffer[0][inIndex]);
            result->data[outIndex + 3] = GETJSAMPLE(buffer[0][inIndex]);
            inIndex += 1;
            break;
         default: /* RGB */
            result->data[outIndex + 1] = GETJSAMPLE(buffer[0][inIndex + 0]);
            result->data[outIndex + 2] = GETJSAMPLE(buffer[0][inIndex + 1]);
            result->data[outIndex + 3] = GETJSAMPLE(buffer[0][inIndex + 2]);
            inIndex += 3;
            break;
         }
         result->data[outIndex + 0] = 0xFF;
         outIndex += 4;
      }
   }

   /* Clean up. */
   jpeg_destroy_decompress(&cinfo);
   fclose(fd);

   return result;

}
#endif /* USE_JPEG */

#ifdef USE_CAIRO
#ifdef USE_RSVG
ImageNode *LoadSVGImage(const char *fileName)
{

#if !GLIB_CHECK_VERSION(2, 35, 0)
   static char initialized = 0;
#endif
   ImageNode *result = NULL;
   RsvgHandle *rh;
   RsvgDimensionData dim;
   GError *e;
   cairo_surface_t *target;
   cairo_t *context;
   int stride;
   int i;

   Assert(fileName);

#if !GLIB_CHECK_VERSION(2, 35, 0)
   if(!initialized) {
      initialized = 1;
      g_type_init();
   }
#endif

   /* Load the image from the file. */
   e = NULL;
   rh = rsvg_handle_new_from_file(fileName, &e);
   if(!rh) {
      return NULL;
   }

   rsvg_handle_get_dimensions(rh, &dim);

   result = (ImageNode*)Allocate(sizeof(ImageNode));
   result->width = dim.width;
   result->height = dim.height;
   result->data = Allocate(4 * dim.width * dim.height);
   memset(result->data, 0, 4 * dim.width * dim.height);

   /* Create the target surface. */
   stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, dim.width);
   target = cairo_image_surface_create_for_data(result->data,
                                                CAIRO_FORMAT_ARGB32,
                                                dim.width, dim.height, stride);
   context = cairo_create(target);
   cairo_paint_with_alpha(context, 0.0);
   rsvg_handle_render_cairo(rh, context);
   cairo_destroy(context);
   cairo_surface_destroy(target);
   g_object_unref(rh);

   for(i = 0; i < 4 * dim.width * dim.height; i += 4) {
      const unsigned long temp = *(unsigned long*)&result->data[i];
      const unsigned long alpha  = (temp >> 24) & 0xFF;
      const unsigned long red    = (temp >> 16) & 0xFF;
      const unsigned long green  = (temp >>  8) & 0xFF;
      const unsigned long blue   = (temp >>  0) & 0xFF;
      result->data[i + 0] = alpha;
      if(alpha > 0) {
         result->data[i + 1] = (red * 255) / alpha;
         result->data[i + 2] = (green * 255) / alpha;
         result->data[i + 3] = (blue * 255) / alpha;
      }
   }

   return result;

}
#endif /* USE_RSVG */
#endif /* USE_CAIRO */

/** Load an XPM image from the specified file. */
#ifdef USE_XPM
ImageNode *LoadXPMImage(const char *fileName)
{

   ImageNode *result = NULL;

   XpmAttributes attr;
   XImage *image;
   XImage *shape;
   int rc;

   Assert(fileName);

   attr.valuemask = XpmAllocColor | XpmFreeColors | XpmColorClosure;
   attr.alloc_color = AllocateColor;
   attr.free_colors = FreeColors;
   attr.color_closure = NULL;
   rc = XpmReadFileToImage(display, (char*)fileName, &image, &shape, &attr);
   if(rc == XpmSuccess) {
      result = CreateImageFromXImages(image, shape);
      JXDestroyImage(image);
      if(shape) {
         JXDestroyImage(shape);
      }
   }

   return result;

}
#endif /* USE_XPM */

/** Create an image from XImages giving color and shape information. */
#ifdef USE_ICONS
#ifdef USE_XPM
ImageNode *CreateImageFromXImages(XImage *image, XImage *shape)
{

   ImageNode *result;
   XColor color;
   unsigned char red, green, blue, alpha;
   int index;
   int x, y;

   result = Allocate(sizeof(ImageNode));
   result->data = Allocate(4 * image->width * image->height);
   result->width = image->width;
   result->height = image->height;

   index = 0;
   for(y = 0; y < image->height; y++) {
      for(x = 0; x < image->width; x++) {

         color.pixel = XGetPixel(image, x, y);
         GetColorFromIndex(&color);

         red = (unsigned char)(color.red >> 8);
         green = (unsigned char)(color.green >> 8);
         blue = (unsigned char)(color.blue >> 8);

         alpha = 0;
         if(!shape || XGetPixel(shape, x, y)) {
            alpha = 255;
         }

         result->data[index++] = alpha;
         result->data[index++] = red;
         result->data[index++] = green;
         result->data[index++] = blue;

      }
   }

   return result;

}
#endif /* USE_XPM */
#endif /* USE_ICONS */

/** Destroy an image node. */
void DestroyImage(ImageNode *image) {
   if(image) {
      if(image->data) {
         Release(image->data);
      }
      Release(image);
   }
}

/** Callback to allocate a color for libxpm. */
#ifdef USE_XPM
int AllocateColor(Display *d, Colormap cmap, char *name,
                  XColor *c, void *closure)
{

   if(name) {
      if(!JXParseColor(d, cmap, name, c)) {
         return -1;
      }
   }

   GetColorIndex(c);
   return 1;

}
#endif /* USE_XPM */

/** Callback to free colors allocated by libxpm.
 * We don't need to do anything here since color.c takes care of this.
 */
#ifdef USE_XPM
int FreeColors(Display *d, Colormap cmap, Pixel *pixels, int n,
               void *closure)
{
   return 1;
}
#endif /* USE_XPM */

