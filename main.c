/**
 * based on http://zarb.org/~gc/html/libpng.html
 *
 * Compile instructions:
 * gcc main.c -o main -lpng
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>

#include <png.h>

#define USAGE 	fprintf (stdout, "Usage: ./stegpng [-e -d] <filename> -m <message>"); exit(0)

#define FREE_ALL	if(!filename)free(filename);\
					if(!message)free(message);\
					if(!dst)free(dst)

#define CLEAR_BIT(obj, x) 		(obj&=~(1<<x))
#define SET_BIT(obj, x) 		(obj|=1<<x)
#define GET_BIT(obj, x) 		((obj>>x)&1)

typedef struct png_d png_data_t;
struct png_d {
	int width, height;
	png_byte color_type;
	png_byte bit_depth;
	png_bytep * row_pointers;
};

png_data_t * 
read_png_file (char *filename) {
	FILE *fp = fopen (filename, "rb");
	png_data_t * data;
	png_structp png_p = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_p) abort ();

	png_infop info = png_create_info_struct (png_p);
	if (!info) abort ();

	if (setjmp (png_jmpbuf (png_p))) abort ();

	png_init_io (png_p, fp);

	png_read_info (png_p, info);
	
	data = (png_data_t *) calloc (1, sizeof (png_data_t));
	data->width      = png_get_image_width (png_p, info);
	data->height     = png_get_image_height (png_p, info);
	data->color_type = png_get_color_type (png_p, info);
	data->bit_depth  = png_get_bit_depth (png_p, info);

	// Read any color_type into 8bit depth, RGBA format.
	// See http://www.libpng.org/pub/png/libpng-manual.txt

	if (data->bit_depth == 16)
		png_set_strip_16 (png_p);

	if (data->color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb (png_p);

	// PNG_COLOR_TYPE_GRAY_ALPHA is always 8 or 16bit depth.
	if (data->color_type == PNG_COLOR_TYPE_GRAY && data->bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8 (png_p);

	if (png_get_valid (png_p, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha (png_p);

	// fill alpha with 0xff
	if ( data->color_type == PNG_COLOR_TYPE_RGB ||
		data->color_type == PNG_COLOR_TYPE_GRAY ||
	 	data->color_type == PNG_COLOR_TYPE_PALETTE) {

		png_set_filler (png_p, 0xFF, PNG_FILLER_AFTER);
	}

	if (data->color_type == PNG_COLOR_TYPE_GRAY ||
	   data->color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb (png_p);

	png_read_update_info (png_p, info);

	data->row_pointers = (png_bytep*)malloc (sizeof (png_bytep) * data->height);
	for (int y = 0; y < data->height; y++) {
		data->row_pointers[y] = (png_byte*)malloc (png_get_rowbytes (png_p,info));
	}

	png_read_image (png_p, data->row_pointers);

	fclose (fp);

	return data;
}

void 
write_png_file (char *filename, png_data_t * data) {
	int y;

	FILE *fp = fopen (filename, "wb");
	if (!fp) abort ();

	png_structp png_p = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_p) abort ();

	png_infop info = png_create_info_struct (png_p);
	if (!info) abort ();

	if (setjmp (png_jmpbuf (png_p))) abort ();

	png_init_io (png_p, fp);

	// Output is 8bit depth, RGBA format.
	png_set_IHDR (png_p,
				 info,
				 data->width, data->height,
				 data->bit_depth,
				 PNG_COLOR_TYPE_RGBA,
				 PNG_INTERLACE_NONE,
				 PNG_COMPRESSION_TYPE_DEFAULT,
				 PNG_FILTER_TYPE_DEFAULT
	);
	png_write_info (png_p, info);

	png_write_image (png_p, data->row_pointers);
	png_write_end (png_p, NULL);

	for (int y = 0; y < data->height; y++) {
		free (data->row_pointers[y]);
	}
	free (data->row_pointers);

	fclose (fp);
}

void 
process_png_file (png_data_t * data, char * message) {
	int k = 0;
	int y = 0;
	for (int y = 0; y < data->height; y++) {
		png_bytep row = data->row_pointers[y];
		for (int x = 0; x < data->width*4; x=x+4) {
			for (int w = 0; w < 3; w++) {
				if (GET_BIT (message [y], k) && !GET_BIT (row [x+w], 0)) {
					SET_BIT (row [x+w], 0);
					
				} else if (!GET_BIT (message [y], k) && GET_BIT (row [x+w], 0)) {
					CLEAR_BIT (row [x+w], 0);
				}

				k++;
				if (k == 8) {
					k = 0;
					y++;
					if (y == strlen (message))
						return;
		
				}
				
			}
		}
	}
}

void 
get_message (png_data_t * data) {
	char message [1024];
	int k = 0;
	int y = 0;

	memset (message, 0, 1024);

	for (int y = 0; y < data->height; y++) {
		png_bytep row = data->row_pointers[y];
		for (int x = 0; x < data->width*4; x=x+4) {
			for (int w = 0; w < 3; w++) {
				if (GET_BIT (row [x+w], 0)) {
					SET_BIT (message [y], k);
					
				} else if (!GET_BIT (row [x+w], 0)) {
					CLEAR_BIT (message [y], k);
				}

				k++;
				if (k == 8) {
					y++;
					k = 0;
					if (message[y-1] == '\b' || y == 1024){
						message[y-1] = 0x00;
						printf ("%s\n", message);
						return;
					}
				}
				
			}
			
		}
	}
}

int main (int argc, char *argv[]) {
	

	png_data_t * png_data;

	char * filename = NULL;
	char * dst = NULL;
	char * message = NULL;
	bool d = false;
	int c;


	if (argc < 3)
		USAGE;
	while ((c = getopt (argc, argv, "e:d:m:o:h")) != -1) {
		switch (c) {
			case 'e':
				filename = strdup (optarg);
				break;
			case 'd':
				d = true;
				filename =	strdup (optarg); 
				break;
			case 'm':
				message = (char *) calloc (1, strlen (optarg) + 1);
				sprintf (message, "%s%c", optarg, 0x08);
				break;
			case 'o':
				dst = strdup (optarg); 
				break;
			case 'h':
				USAGE;
				break;
			default:
				USAGE;
				break;
		}
	}

	if (!d) {
		if (!dst || !message) {
			FREE_ALL;
			USAGE;
		}
		png_data = read_png_file (filename);
		process_png_file (png_data, message);
		write_png_file (dst, png_data);
	} else {
		if (!filename) {
			FREE_ALL;		
			USAGE;
		}
		png_data = read_png_file (filename);
		get_message (png_data);
	}

	FREE_ALL;

	return 0;
}

