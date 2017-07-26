#include "ledvideo.h"
#include "gif_lib.h"

void rgb2ppm(const char *name, const void *data, int width, int height);
void rgba2ppm(const char *name, const void *data, int width, int height);
void raster2rgba(const unsigned char *RasterBits, const ColorMapObject *colormap,
	int transparent_index, unsigned char *buf, int width, int height);

/*
GifFileType
    int SWidth, SHeight,                // Screen dimensions.
        SColorResolution,               // How many colors can we generate?
        SBackGroundColor;               // I hope you understand this one...
    ColorMapObject *SColorMap;          // NULL if not exists.
    int            ImageCount;          // Number of current image
    GifImageDesc   Image;               // Block describing current image
    SavedImage     *SavedImages;	// Use this to accumulate file state

ColorMapObject
    int ColorCount;
    int BitsPerPixel;
    GifColorType *Colors;

GifColorType
    unsigned char  Red, Green, Blue;

GifImageDesc
    int Left, Top, Width, Height,       // Current image dimensions.
        Interlace;                      // Sequential/Interlaced lines.
    ColorMapObject *ColorMap;           // The local color map

SavedImage
    GifImageDesc        ImageDesc;
    unsigned char       *RasterBits;
    int                 Function; 	// DEPRECATED: Use ExtensionBlocks[x].Function
    int                 ExtensionBlockCount;
    ExtensionBlock      *ExtensionBlocks;

ExtensionBlock
    int         ByteCount;
    char        *Bytes;
    int		Function;		// type of the Extension block
*/


// load a GIF file into a list of animation frames
//
animation_frame * gifload(const char *filename)
{
	GifFileType *gif;
	ExtensionBlock *ext;
	unsigned char *buf;
	ColorMapObject *colormap;
	SavedImage *image;
	const unsigned char *src;
	unsigned char *dest;
	animation_frame *anim, *first=NULL, *last=NULL;
	int width, height, offset;
	int disposal_method, transparent_flag, animation_delay, transparent_index;
	int i, j, x, y, index;
	//char name[64];

	//gif = DGifOpenFileName("../test.gif");
	//gif = DGifOpenFileName("../ironingman2.gif");
	gif = DGifOpenFileName(filename);
	if (!gif) return NULL;
	if (DGifSlurp(gif) != GIF_OK) {
		DGifCloseFile(gif);
		return NULL;
	}
	width = gif->SWidth;
	height = gif->SHeight;
	printf("opened and read %s\n", filename);
	printf("size: %d, %d\n", width, height);
	printf("SColorResolution: %d\n", gif->SColorResolution);
	printf("SBackGroundColor: %d\n", gif->SBackGroundColor);
	printf("ImageCount: %d\n", gif->ImageCount);
	//printf(": %d\n", gif->);

	buf = malloc(width * height * 4);
	if (!buf) {
		DGifCloseFile(gif);
		return NULL;
	}
	memset(buf, 0, width * height * 4);

	// http://www.matthewflickinger.com/lab/whatsinagif/animation_and_transparency.asp

	for (i=0; i < gif->ImageCount; i++) {
		image = &(gif->SavedImages[i]);
		// first, find the animation and transparency info
		ext = image->ExtensionBlocks;
		disposal_method = 0;
		transparent_flag = 0;
		animation_delay = 150000;
		transparent_index = -1;
		for (j=0; j < image->ExtensionBlockCount; j++) {
			if (ext[j].Function == 0xF9 && ext[j].ByteCount >= 4) {
				disposal_method = (ext[j].Bytes[0] >> 2) & 7;
				transparent_flag = ext[j].Bytes[0] & 1;
				animation_delay = ((ext[j].Bytes[1] & 255) |
				   ((ext[j].Bytes[2] & 255) << 8)) * 10000;
				if (transparent_flag) {
					transparent_index = ext[j].Bytes[3] & 255;
				}
			}
		}
		printf("image %d: disposal=%d, transparent=%d, delay=%d, tindex=%d\n",
			i, disposal_method, transparent_flag,
			animation_delay, transparent_index);
		printf("         position=%d,%d  size=%d,%d  ilace=%d\n",
			image->ImageDesc.Left, image->ImageDesc.Top,
			image->ImageDesc.Width, image->ImageDesc.Height,
			image->ImageDesc.Interlace);

		// check the image size is ok
		if (image->ImageDesc.Left + image->ImageDesc.Width > width) break;
		if (image->ImageDesc.Top + image->ImageDesc.Height > height) break;

		// get the colormap
		colormap = image->ImageDesc.ColorMap;
		if (!colormap) colormap = gif->SColorMap;
		if (!colormap) break;

		// if the disposal method says to use a solid background, fill it
		if (disposal_method == 2) {
			memset(buf, 0, width * height * 4);
#if 0
			int red, green, blue;
			red = colormap->Colors[gif->SBackGroundColor].Red;
			green = colormap->Colors[gif->SBackGroundColor].Green;
			blue = colormap->Colors[gif->SBackGroundColor].Blue;
			dest = buf;
			for (y=0; y < height; y++) {
				for (x=0; x < width; x++) {
					*dest++ = red;
					*dest++ = green;
					*dest++ = blue;
					*dest++ = 255;
				}
			}
#endif
		}

		// convert the indexed raster data to RGBA format
		src = image->RasterBits;
		for (y=0; y < image->ImageDesc.Height; y++) {
			offset = (image->ImageDesc.Top + y) * width;
			offset += image->ImageDesc.Left;
			dest = &buf[offset * 4];
			for (x=0; x < image->ImageDesc.Width; x++) {
				index = *src++;

				if (index == transparent_index) {
					dest += 4;
				} else {
					*dest++ = colormap->Colors[index].Red;
					*dest++ = colormap->Colors[index].Green;
					*dest++ = colormap->Colors[index].Blue;
					*dest++ = 255; // alpha = 100% opaque
				}
			}
		}

		anim = malloc(sizeof(animation_frame) + width * height * 4);
		if (!anim) break;
		anim->width = width;
		anim->height = height;
		anim->delay = animation_delay;
		anim->next = NULL;
		memcpy(anim->data, buf, width * height * 4);
		if (first == NULL) {
			first = anim;
		} else {
			last->next = anim;
		}
		last = anim;

		//snprintf(name, sizeof(name), "image%d.ppm", i);
		//rgba2ppm(name, buf, width, height);

		// disposal methods
		//  0 = no animation
		//  1 = draw on top of prior image
		//  2 = init to background color
		//  3 = restore to previous state?? (not supported?)

		if (disposal_method == 0) break;
	}
	free(buf);
	DGifCloseFile(gif);
	return first;
}

void free_anim_list(animation_frame *first)
{
	animation_frame *next;

	while (first) {
		next = first->next;
		free(first);
		first = next;
	}
}

void raster2rgba(const unsigned char *RasterBits, const ColorMapObject *colormap,
	int transparent_index, unsigned char *buf, int width, int height)
{
	int i, index, numpix;
	const unsigned char *src;
	unsigned char *dest;

	src = RasterBits;
	dest = buf;
	numpix = width * height;
	for (i=0; i < numpix; i++) {
		index = *src++;
		if (index == transparent_index) {
			dest += 4;
		} else {
			*dest++ = colormap->Colors[index].Red;
			*dest++ = colormap->Colors[index].Green;
			*dest++ = colormap->Colors[index].Blue;
			*dest++ = 255; // alpha = 100% opaque
		}
	}
}


void rgb2ppm(const char *name, const void *data, int width, int height)
{
	FILE *f = fopen(name, "wb");
	if (!f) return;
	fprintf(f, "P6\n%d %d 255\n", width, height);
	fwrite(data, width * height * 3, 1, f);
	fclose(f);
}

void rgba2ppm(const char *name, const void *data, int width, int height)
{
	unsigned char *buf, *dest;
	const unsigned char *src;
	FILE *f;
	int i;

	buf = malloc(width * height * 3);
	if (!buf) return;
	f = fopen(name, "wb");
	if (!f) {
		free(buf);
		return;
	}
	src = data;
	dest = buf;
	for (i=0; i < width * height; i++) {
		*dest++ = *src++;
		*dest++ = *src++;
		*dest++ = *src++;
		src++;
	}
	fprintf(f, "P6\n%d %d 255\n", width, height);
	fwrite(buf, width * height * 3, 1, f);
	fclose(f);
	free(buf);
}
